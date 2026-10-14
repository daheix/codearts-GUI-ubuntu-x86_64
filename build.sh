#!/usr/bin/env bash
# codeartsGUI 构建脚本
# 用法: ./build.sh [configure|build|test|run|package|clean]
#
# Qt6 来源（显式，无隐藏 fallback）：
#   1) 环境变量 CODEARTS_QT6_PREFIX 指定（最高优先）
#   2) 否则用 ChinaSimStdio vcpkg 静态 Qt6（作者本机默认）
#   3) CMakeLists 会在两处都没有时回退系统 Qt6（会打印所选来源）
set -euo pipefail
cd "$(dirname "$0")"

BUILD="${BUILD_DIR:-build}"
DEFAULT_QT6_PREFIX="/home/wsc/wsc/ChinaSimStdio/vcpkg/installed/x64-linux"

do_configure() {
    local qt_prefix="${CODEARTS_QT6_PREFIX:-$DEFAULT_QT6_PREFIX}"
    if [ -d "$qt_prefix/lib/cmake/Qt6" ]; then
        cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
            -DCODEARTS_QT6_PREFIX="$qt_prefix"
    else
        echo "提示：$qt_prefix 非 vcpkg 静态 Qt6 前缀，改用系统 Qt6。" >&2
        cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
    fi
}
do_build() {
    cmake --build "$BUILD" -j"$(nproc)"
}
do_test() {
    local fail=0
    for t in codeartsGUI_tests goal_tests systems_tests goalrunner_tests; do
        printf '%-20s ' "$t"
        "$BUILD/$t" 2>&1 | grep -E "test cases|assertions" | tr '\n' ' '
        echo ""
        "$BUILD/$t" 2>&1 | grep -q "Status: SUCCESS" || fail=1
    done
    return $fail
}
do_run() {
    # 静态 Qt6 仅带 xcb 平台插件，走 XWayland
    QT_QPA_PLATFORM=xcb "$BUILD/codeartsGUI"
}
do_package() {
    # 基于自含静态二进制手动产出 tar.gz + .deb（不用 CMake/CPack）
    bash packaging/package.sh "$BUILD"
}
do_clean() {
    rm -rf "$BUILD" dist
}

case "${1:-build}" in
    configure) do_configure ;;
    build)     do_configure; do_build ;;
    test)      do_test ;;
    run)       do_run ;;
    package)   do_package ;;
    clean)     do_clean ;;
    *) echo "用法: $0 [configure|build|test|run|package|clean]"; exit 1 ;;
esac
