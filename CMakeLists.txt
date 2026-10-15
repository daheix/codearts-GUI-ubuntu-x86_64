cmake_minimum_required(VERSION 3.22)
project(codeartsGUI VERSION 0.2.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# ── Qt6 来源选择（三态，全程显式，无隐藏 fallback）──
# 1) -DCODEARTS_QT6_PREFIX=<path> 显式指定（最高优先）
# 2) 探测 ChinaSimStdio vcpkg 静态 Qt6（作者本机默认）
# 3) 否则：系统 Qt6（find_package 走默认搜索路径）
# 静态 Qt6 需显式定义其引用的 imported target 并做传递依赖闭包链接，
# 故以 CODEARTS_STATIC_QT6 标记是否走静态路径。CMAKE 将打印所选来源。
set(DEFAULT_VCPKG_QT6 "/home/wsc/wsc/ChinaSimStdio/vcpkg/installed/x64-linux")
if(NOT DEFINED CODEARTS_QT6_PREFIX)
    if(EXISTS "${DEFAULT_VCPKG_QT6}/lib/cmake/Qt6/Qt6Config.cmake")
        set(CODEARTS_QT6_PREFIX "${DEFAULT_VCPKG_QT6}")
    else()
        set(CODEARTS_QT6_PREFIX "")
    endif()
endif()
if(CODEARTS_QT6_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${CODEARTS_QT6_PREFIX}")
    set(CODEARTS_STATIC_QT6 ON)
    message(STATUS "codeartsGUI: Qt6 来源 = 静态候选 ${CODEARTS_QT6_PREFIX}")
else()
    set(CODEARTS_STATIC_QT6 OFF)
    message(STATUS "codeartsGUI: Qt6 来源 = 系统 Qt6（find_package 默认搜索）")
endif()

# vcpkg 静态 Qt6 的 freetype/xcbqpa 等 imported target 引用了这些系统库，需在
# find_package(Qt6) 前显式定义。注意：不要 find_package(Freetype)，vcpkg 自带
# freetype 导出集，重复定义会冲突。仅静态路径需要。
if(CODEARTS_STATIC_QT6)
    find_package(BZip2 REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Test)

if(CODEARTS_STATIC_QT6)
    set(VCPKG_LIB "${CODEARTS_QT6_PREFIX}/lib")
else()
    set(VCPKG_LIB "")
endif()

# 静态 Qt6 传递依赖闭包：Gui→fontconfig→freetype/expat/png/zlib/brotli。
# CMake 把直接库项排在 Qt6 接口库(fontconfig)之前，expat 无法靠位置排到其后，
# 故对 expat 用 --whole-archive 强制纳入符号，使后置 fontconfig 的 XML_* 命中。
# 仅静态 Qt6 需要；系统动态 Qt6 走常规链接（该变量为空）。
if(CODEARTS_STATIC_QT6)
    set(CODEARTS_QT_CLOSURE
        "${VCPKG_LIB}/libfontconfig.a"
        "${VCPKG_LIB}/libfreetype.a"
        "-Wl,--whole-archive" "${VCPKG_LIB}/libexpat.a" "-Wl,--no-whole-archive"
        "${VCPKG_LIB}/libpng16.a"
        "${VCPKG_LIB}/libz.a"
        "${VCPKG_LIB}/libbrotlidec.a"
        "${VCPKG_LIB}/libbrotlicommon.a")
else()
    set(CODEARTS_QT_CLOSURE "")
endif()

# 静态 Qt6：ibus 输入法插件目标（中文输入）。作为 CODEARTS_IBUS_PLUGIN 变量插入
# 主链接列表、置于闭包之前；闭包（freetype/fontconfig/whole-archive expat）保持最后
# 统一解析符号，否则打乱 fontconfig/freetype 顺序会报 FT_Get_BDF_Property undefined。
set(CODEARTS_IBUS_PLUGIN "")
if(CODEARTS_STATIC_QT6)
    find_package(Qt6QIbusPlatformInputContextPlugin QUIET CONFIG)
    if(TARGET Qt6::QIbusPlatformInputContextPlugin)
        set(CODEARTS_IBUS_PLUGIN Qt6::QIbusPlatformInputContextPlugin)
        set(CODEARTS_IBUS_DEF CODEARTS_STATIC_IBUS_PLUGIN=1)
        message(STATUS "codeartsGUI: 静态链接 ibus 输入法插件（中文输入）")
    else()
        message(WARNING "codeartsGUI: 未找到 Qt6 ibus 输入法插件，中文输入将不可用")
    endif()
endif()

# ── 依赖链: lxqt-build-tools 2.4.0 → qtermwidget 2.4.0 ──
# qtermwidget 用 find_package(lxqt2-build-tools)，需先将 lbt 安装到本地前缀
# 再把前缀加入 CMAKE_PREFIX_PATH（FetchContent 子目录方式无法被 find_package 找到）。
set(LBT_PREFIX "${CMAKE_BINARY_DIR}/lbt-prefix")

FetchContent_Declare(lxqt2_build_tools
    GIT_REPOSITORY https://github.com/lxqt/lxqt-build-tools.git
    GIT_TAG 2.4.0
    GIT_SHALLOW TRUE)
FetchContent_GetProperties(lxqt2_build_tools)
if(NOT lxqt2_build_tools_POPULATED)
    FetchContent_Populate(lxqt2_build_tools)
endif()

# configure 期安装 lbt 到本地前缀（幂等：已安装则跳过）
if(NOT EXISTS "${LBT_PREFIX}/share/cmake/lxqt2-build-tools/lxqt2-build-tools-config.cmake")
    execute_process(COMMAND ${CMAKE_COMMAND}
        -S ${lxqt2_build_tools_SOURCE_DIR} -B ${CMAKE_BINARY_DIR}/lbt-build
        -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
        -DCMAKE_INSTALL_PREFIX=${LBT_PREFIX}
        RESULT_VARIABLE _lbt_cfg)
    if(NOT _lbt_cfg EQUAL 0)
        message(FATAL_ERROR "lxqt-build-tools configure 失败 (${_lbt_cfg})")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/lbt-build --target install
        RESULT_VARIABLE _lbt_bld)
    if(NOT _lbt_bld EQUAL 0)
        message(FATAL_ERROR "lxqt-build-tools install 失败 (${_lbt_bld})")
    endif()
endif()
list(APPEND CMAKE_PREFIX_PATH "${LBT_PREFIX}")

# ── qtermwidget 2.4.0 (Qt6 终端仿真器, 静态) ──
# patch：vcpkg 无 Qt6LinguistTools，剥离翻译机器 + SHARED→STATIC。
# 打包不走 CMake install/CPack（CPack 会遍历含本子项目在内的整棵 install 树，
# 其 install 规则指向 /usr/local 绝对路径致 staging 失败），而由
# packaging/package.sh 手动基于自含静态二进制产出 tar.gz/.deb，故不改其 install。
FetchContent_Declare(qtermwidget
    GIT_REPOSITORY https://github.com/lxqt/qtermwidget.git
    GIT_TAG 2.4.0
    GIT_SHALLOW TRUE
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_qtermwidget.cmake)
FetchContent_MakeAvailable(qtermwidget)

# ── 主程序 ──
set(VCPKG_LIB "${VCPKG_QT6}/lib")
add_executable(codeartsGUI
    src/main.cpp
    src/environment.h
    src/environment.cpp
    src/sessionwidget.h
    src/sessionwidget.cpp
    src/sessionmanager.h
    src/sessionmanager.cpp
    src/processmonitor.h
    src/processmonitor.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/goalprotocol.h
    src/goalprotocol.cpp
    src/goalstate.h
    src/goalstate.cpp
    src/retryengine.h
    src/retryengine.cpp
    src/permissionstore.h
    src/permissionstore.cpp
    src/thememanager.h
    src/thememanager.cpp
    src/goalrunner.h
    src/goalrunner.cpp
    src/goaldialog.h
    src/goaldialog.cpp
    src/permissiondialog.h
    src/permissiondialog.cpp
    src/permissionpage.h
    src/permissionpage.cpp)

target_include_directories(codeartsGUI PRIVATE src ${qtermwidget_SOURCE_DIR}/lib)

target_link_libraries(codeartsGUI PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    qtermwidget6
    ${CODEARTS_IBUS_PLUGIN}
    ${CODEARTS_QT_CLOSURE}
)

# ibus 静态插件：导入宏需该编译定义（仅在静态链接时）
if(DEFINED CODEARTS_IBUS_DEF)
    target_compile_definitions(codeartsGUI PRIVATE ${CODEARTS_IBUS_DEF})
endif()

install(TARGETS codeartsGUI RUNTIME DESTINATION bin)
install(FILES resources/codeartsGUI.desktop
        DESTINATION share/applications)
install(FILES LICENSE README.md DESTINATION share/doc/codeartsGUI)

# 注：发布安装包不使用 CMake install/CPack。CPack 的 install 打包会遍历含
# qtermwidget（FetchContent 子项目）在内的整棵 install 树，其 install 规则指向
# /usr/local 绝对路径导致 staging 失败；而遮蔽其 install 又会使父 cmake_install.cmake
# 的 include 悬空。故改用 packaging/package.sh 基于自含静态二进制手动产出
# tar.gz + .deb（完全可控）。上面的 install() 仅保留供人工 cmake --install 参考。

# ── 单元测试（doctest, 仅 Core：Environment + ProcessMonitor）──
enable_testing()
add_executable(codeartsGUI_tests
    tests/test_core.cpp
    src/environment.cpp
    src/processmonitor.cpp)
target_include_directories(codeartsGUI_tests PRIVATE src third_party)
target_link_libraries(codeartsGUI_tests PRIVATE Qt6::Core)
add_test(NAME core COMMAND codeartsGUI_tests)

# ── /goal 核心纯逻辑测试（GoalProtocol/GoalState/RetryEngine）──
add_executable(goal_tests
    tests/test_goal.cpp
    src/goalprotocol.cpp
    src/goalstate.cpp
    src/retryengine.cpp)
target_include_directories(goal_tests PRIVATE src third_party)
target_link_libraries(goal_tests PRIVATE Qt6::Core)
add_test(NAME goal COMMAND goal_tests)

# ── GoalRunner 状态机测试（mock 终端 + 固定时钟）──
add_executable(goalrunner_tests
    tests/test_goalrunner.cpp
    src/goalrunner.cpp
    src/goalprotocol.cpp
    src/goalstate.cpp
    src/retryengine.cpp)
target_include_directories(goalrunner_tests PRIVATE src third_party)
target_link_libraries(goalrunner_tests PRIVATE Qt6::Core Qt6::Test)
add_test(NAME goalrunner COMMAND goalrunner_tests)

# ── 权限库 + 主题测试 ──
add_executable(systems_tests
    tests/test_systems.cpp
    src/permissionstore.cpp
    src/thememanager.cpp)
target_include_directories(systems_tests PRIVATE src third_party)
target_link_libraries(systems_tests PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    ${CODEARTS_QT_CLOSURE}
)
add_test(NAME systems COMMAND systems_tests)


