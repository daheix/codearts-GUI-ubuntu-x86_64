#!/usr/bin/env bash
# packaging/package.sh —— 基于自含静态二进制手动产出 tar.gz + .deb（开源发布）
#
# 为何不用 CPack：CPack 的 install 打包会遍历含 qtermwidget（FetchContent 子项目）
# 在内的整棵 CMake install 树，其 install 规则指向 /usr/local 绝对路径导致 staging
# 失败；而遮蔽其 install 又会使父 cmake_install.cmake 的 include 悬空。静态二进制的
# 正确打包方式是直接组装文件树，完全可控。
#
# 产出（dist/ 下）：
#   codeartsGUI-<ver>-ubuntu-x86_64.tar.gz  免安装包（含二进制 + desktop + install.sh + 文档）
#   codeartsGUI-<ver>-amd64.deb             Debian/Ubuntu 包
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
BUILD="${1:-$ROOT/build}"

# 版本号：从顶层 CMakeLists project(... VERSION x.y.z) 读取
VER=$(grep -oE 'project\(codeartsGUI VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt \
      | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
: "${VER:=0.2.0}"

BIN="$BUILD/codeartsGUI"
[ -x "$BIN" ] || { echo "未找到可执行 $BIN，请先 ./build.sh build" >&2; exit 1; }
# 自包含性：静态 Qt6 构建应无 Qt6 动态依赖（拷贝即用）。动态 Qt6 构建（如 CI）
# 会依赖 libQt6*.so，此处仅告警不中断——作者正式 Release 用静态 vcpkg Qt6 自含。
if ldd "$BIN" 2>/dev/null | grep -qiE 'libQt6|libqxcb'; then
    echo "提示：$BIN 依赖 Qt6 动态库（非静态构建），.tar.gz 需目标机装有 Qt6 运行时。" >&2
    QT_DYNAMIC=1
else
    QT_DYNAMIC=0
fi

DIST="$ROOT/dist"
rm -rf "$DIST"
STAGE="$DIST/stage-codeartsGUI-${VER}"
mkdir -p "$STAGE/bin" "$STAGE/share/applications" "$STAGE/share/doc/codeartsGUI"

cp "$BIN" "$STAGE/bin/codeartsGUI"
install -Dm644 resources/codeartsGUI.desktop "$STAGE/share/applications/codeartsGUI.desktop"
install -Dm644 LICENSE "$STAGE/share/doc/codeartsGUI/LICENSE"
install -Dm644 README.md "$STAGE/share/doc/codeartsGUI/README.md"
install -Dm644 CHANGELOG.md "$STAGE/share/doc/codeartsGUI/CHANGELOG.md"

# 免安装包内的安装脚本（把二进制与 desktop 装到 ~/.local）
cat > "$STAGE/install.sh" <<EOF
#!/usr/bin/env bash
# codeartsGUI ${VER} 安装脚本（用户态，免 root）
set -euo pipefail
cd "\$(dirname "\$0")"
PREFIX="\${CODEGUI_PREFIX:-\$HOME/.local}"
install -Dm755 bin/codeartsGUI "\$PREFIX/bin/codeartsGUI"
install -Dm644 share/applications/codeartsGUI.desktop \\
    "\$PREFIX/share/applications/codeartsGUI.desktop"
command -v update-desktop-database >/dev/null 2>&1 && \\
    update-desktop-database "\$PREFIX/share/applications" || true
echo "已安装到 \$PREFIX/bin/codeartsGUI（运行：QT_QPA_PLATFORM=xcb codeartsGUI）"
EOF
chmod +x "$STAGE/install.sh"

# .desktop 的 Exec 用 PATH 名即可；确保 Exec 可被找到
echo "==> 生成 tar.gz"
tar -C "$DIST" -czf "$DIST/codeartsGUI-${VER}-ubuntu-x86_64.tar.gz" \
    "$(basename "$STAGE")"
echo "    $DIST/codeartsGUI-${VER}-ubuntu-x86_64.tar.gz"

echo "==> 生成 .deb"
DEBSTAGE="$DIST/deb"
mkdir -p "$DEBSTAGE/DEBIAN" "$DEBSTAGE/usr/bin" \
    "$DEBSTAGE/usr/share/applications" "$DEBSTAGE/usr/share/doc/codeartsGUI"
install -Dm755 "$BIN" "$DEBSTAGE/usr/bin/codeartsGUI"
install -Dm644 resources/codeartsGUI.desktop "$DEBSTAGE/usr/share/applications/codeartsGUI.desktop"
install -Dm644 LICENSE "$DEBSTAGE/usr/share/doc/codeartsGUI/LICENSE"
install -Dm644 README.md "$DEBSTAGE/usr/share/doc/codeartsGUI/README.md"
SIZE_KB=$(du -k "$BIN" | cut -f1)
cat > "$DEBSTAGE/DEBIAN/control" <<EOF
Package: codeartsgui
Version: ${VER}
Section: devel
Priority: optional
Architecture: amd64
Installed-Size: ${SIZE_KB}
Maintainer: daheix <91401694+daheix@users.noreply.github.com>
Homepage: https://github.com/daheix/codearts-GUI-ubuntu-x86_64
Depends: libc6
Description: Native Qt6 workbench for Huawei CodeArts CLI/TUI
 codeartsGUI 面向华为云码道 CodeArts AI 智能体 CLI/TUI 的原生终端仿真图形工作台。
 进程隔离防卡顿（长会话不卡死）+ /goal 自愈目标驱动 + 双主题 + 权限管理
 + 模型错误永久重试。静态构建，拷贝即用。
EOF
gzip -9n "$DEBSTAGE/usr/share/doc/codeartsGUI/CHANGELOG.md" 2>/dev/null && \
  mv "$DEBSTAGE/usr/share/doc/codeartsGUI/CHANGELOG.md.gz" \
     "$DEBSTAGE/usr/share/doc/codeartsGUI/changelog.gz" || \
  install -Dm644 CHANGELOG.md "$DEBSTAGE/usr/share/doc/codeartsGUI/changelog"
chmod -R g-w "$DEBSTAGE"
dpkg-deb --root-owner-group --build "$DEBSTAGE" \
    "$DIST/codeartsGUI-${VER}-amd64.deb" >/dev/null
echo "    $DIST/codeartsGUI-${VER}-amd64.deb"

echo ""
echo "✅ 打包完成（版本 $VER）："
ls -la "$DIST"/*.tar.gz "$DIST"/*.deb
