# patch_qtermwidget.cmake —— 剥离 qtermwidget 2.4.0 的翻译机器 + SHARED→STATIC + 遮蔽 install
# 用法: cmake -DSOURCE_DIR=<qtermwidget源码目录> -P patch_qtermwidget.cmake
# 背景: vcpkg 静态 Qt6 无 Qt6LinguistTools，而 qtermwidget 顶层 CMakeLists
#       REQUIRED 拉取它并 include(LXQtTranslateTs)。本脚本：
#       1) find_package(Qt6LinguistTools ... REQUIRED) → QUIET
#       2) 删除 include(LXQtTranslateTs)
#       3) lxqt_translate_ts(QTERMWIDGET_QM ...) 调用加 if 守卫 + endif
#       4) add_library(... SHARED ...) → STATIC，并移除 ${QTERMWIDGET_QM}
#       5) 在 cmake_minimum_required 后定义 function(install) endfunction()，
#          把 qtermwidget 所有 install() 静默为 no-op（避免其 /usr/local 绝对路径
#          安装破坏下游 CPack 打包），同时保留子目录空的 cmake_install.cmake 使父
#          add_subdirectory 的 include 成功。

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "patch_qtermwidget.cmake: 需 -DSOURCE_DIR=<dir>")
endif()
set(F "${SOURCE_DIR}/CMakeLists.txt")
file(READ "${F}" S)

# 1) LinguistTools REQUIRED → QUIET
string(REPLACE
    "find_package(Qt6LinguistTools \"\${QT_MINIMUM_VERSION}\" REQUIRED)"
    "find_package(Qt6LinguistTools \"\${QT_MINIMUM_VERSION}\" QUIET)"
    S "${S}")

# 2) 删除 include(LXQtTranslateTs)
string(REPLACE "include(LXQtTranslateTs)\n" "" S "${S}")

# 3) lxqt_translate_ts 调用加守卫。该调用以 "lxqt_translate_ts(QTERMWIDGET_QM"
#    开始，以其后第一个行首 ")" 结束。
string(FIND "${S}" "lxqt_translate_ts(QTERMWIDGET_QM" _pos)
if(_pos GREATER_EQUAL 0)
    string(SUBSTRING "${S}" 0 ${_pos} _head)
    string(SUBSTRING "${S}" ${_pos} -1 _tail)
    # 在 _tail 中找调用结束：第一个孤立的 ")\n"
    string(FIND "${_tail}" ")\n" _endpos)
    if(_endpos GREATER_EQUAL 0)
        math(EXPR _after "${_endpos} + 2")  # 跳过 ")\n"
        string(SUBSTRING "${_tail}" 0 ${_after} _call)
        string(SUBSTRING "${_tail}" ${_after} -1 _rest)
        set(_guarded "if(Qt6LinguistTools_FOUND)\n${_call}endif()  # patched: skip translations\n")
        set(S "${_head}${_guarded}${_rest}")
    endif()
endif()

# 4) add_library SHARED → STATIC，并移除 ${QTERMWIDGET_QM}
string(REPLACE "add_library(\${QTERMWIDGET_LIBRARY_NAME} SHARED"
               "add_library(\${QTERMWIDGET_LIBRARY_NAME} STATIC" S "${S}")
# 原 "${UI_SRCS} ${QTERMWIDGET_QM})" → "${UI_SRCS})"（保留 UI_SRCS 自身的 }，仅去掉 QM 项）
string(REPLACE " \${QTERMWIDGET_QM})" ")" S "${S}")

file(WRITE "${F}" "${S}")
message(STATUS "patched qtermwidget: ${F}")
