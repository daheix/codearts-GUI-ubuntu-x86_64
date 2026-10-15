#!/usr/bin/env bash
# packaging/package.sh —— 基于自含静态二进制 + 打包全部运行期 .so，产出真正自包含的
# tar.gz + .deb（安装后不依赖自身目录以外的任何库，libc/ld 基础库除外）。
#
# 为何收编 .so：codeartsGUI 静态链接 Qt6，但 xcb/GL/X11/xkb/ibus 等平台层仍为系统动态
# 库，且 libdbus 经 RUNPATH 指向 vcpkg 目录——换机即无法启动。故把 ldd 全闭包（除基础
# 库）拷入包内 lib/，patchelf 将 rpath 指到 $ORIGIN/lib，二进制与 lib 同目录分发。
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
BUILD="${1:-$ROOT/build}"

VER=$(grep -oE 'project\(codeartsGUI VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt \
      | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
: "${VER:=0.2.3}"

BIN="$BUILD/codeartsGUI"
[ -x "$BIN" ] || { echo "未找到可执行 $BIN，请先 ./build.sh build" >&2; exit 1; }

# 不与自身目录同包的“基础库”（任何 Linux 必有），其余全部收编。
is_base_lib() {
  case "$1" in
    libc.so*|libm.so*|ld-linux*|linux-vdso*|libdl.so*|libpthread.so*|librt.so*) return 0;;
    *) return 1;;
  esac
}

# bundle_libs <二进制> <lib目录> <二进制的rpath>
#   把 ldd 全闭包中的非基础 .so 拷入 lib目录（解析符号链接取真实内容），
#   并对每个 .so 设 rpath=$ORIGIN（同目录互找），对主二进制设指定 rpath。
bundle_libs() {
  local bin="$1" libdir="$2" binrpath="$3"
  mkdir -p "$libdir"
  # 收集 NEEDED 的已解析路径（ldd 输出形如: name => /abs/path (0x..)，或 /abs/path (..)）
  local resolved; resolved=$(ldd "$bin" 2>/dev/null \
    | awk '{ if ($2=="=>") print $3; else if ($1 ~ /^\//) print $1 }' | sort -u)
  local so base real
  while IFS= read -r so; do
    [ -n "$so" ] && [ -e "$so" ] || continue
    base=$(basename "$so")
    is_base_lib "$base" && continue
    [ -e "$libdir/$base" ] && continue
    real=$(readlink -f "$so")
    cp -L "$real" "$libdir/$base"
    # 版本符号链接：如 libX11.so.6 -> 真实文件，保持可被 soname 命中
    chmod u+w "$libdir/$base"
  done <<< "$resolved"
  # 每个收编 .so 设 rpath=$ORIGIN（lib 目录内互找）
  for f in "$libdir"/*; do
    [ -f "$f" ] && patchelf --set-rpath '$ORIGIN' "$f" >/dev/null 2>&1 || true
  done
  # 主二进制 rpath 指到 lib 目录（按传入）
  patchelf --set-rpath "$binrpath" "$bin"
}

DIST="$ROOT/dist"
rm -rf "$DIST"
APP="$DIST/codeartsGUI-${VER}"
mkdir -p "$APP/lib"

# 二进制放入 app 根，lib 放 app/lib —— 同目录分发，$ORIGIN/lib 命中
cp "$BIN" "$APP/codeartsGUI"
bundle_libs "$APP/codeartsGUI" "$APP/lib" '$ORIGIN/lib'

install -Dm644 resources/codeartsGUI.desktop "$APP/codeartsGUI.desktop"
install -Dm644 LICENSE "$APP/LICENSE"
install -Dm644 README.md "$APP/README.md"
install -Dm644 CHANGELOG.md "$APP/CHANGELOG.md"
echo "codeartsGUI $VER" > "$APP/VERSION"

# 安装脚本：整目录拷到 $PREFIX/lib/codeartsGUI，符号链接到 $PREFIX/bin
cat > "$APP/install.sh" <<EOF
#!/usr/bin/env bash
# codeartsGUI ${VER} 自包含安装（无需系统 Qt6，二进制+库全部在本目录）
set -euo pipefail
cd "\$(dirname "\$0")"
PREFIX="\${PREFIX:-\$HOME/.local}"
install -d "\$PREFIX/lib/codeartsGUI"
cp -a codeartsGUI lib "\$PREFIX/lib/codeartsGUI/"
install -d "\$PREFIX/bin"
ln -sf "\$PREFIX/lib/codeartsGUI/codeartsGUI" "\$PREFIX/bin/codeartsGUI"
install -Dm644 codeartsGUI.desktop "\$PREFIX/share/applications/codeartsGUI.desktop"
command -v update-desktop-database >/dev/null 2>&1 && \\
    update-desktop-database "\$PREFIX/share/applications" || true
echo "已安装 codeartsGUI ${VER} 到 \$PREFIX/lib/codeartsGUI（bin 符号链接 \$PREFIX/bin/codeartsGUI）"
echo "运行：QT_QPA_PLATFORM=xcb codeartsGUI   （前置：已装 CodeArts CLI/TUI）"
EOF
chmod +x "$APP/install.sh"

echo "==> 生成 tar.gz"
tar -C "$DIST" -czf "$DIST/codeartsGUI-${VER}-ubuntu-x86_64.tar.gz" "$(basename "$APP")"
echo "    $DIST/codeartsGUI-${VER}-ubuntu-x86_64.tar.gz"

echo "==> 生成 .deb"
DEBSTAGE="$DIST/deb"
mkdir -p "$DEBSTAGE/DEBIAN" "$DEBSTAGE/usr/bin" \
    "$DEBSTAGE/usr/lib/codeartsGUI" "$DEBSTAGE/usr/share/applications" \
    "$DEBSTAGE/usr/share/doc/codeartsGUI"
install -Dm755 "$APP/codeartsGUI" "$DEBSTAGE/usr/lib/codeartsGUI/codeartsGUI"
cp -a "$APP/lib/." "$DEBSTAGE/usr/lib/codeartsGUI/lib/"
install -Dm644 LICENSE README.md "$DEBSTAGE/usr/share/doc/codeartsGUI/"
gzip -9n -c CHANGELOG.md > "$DEBSTAGE/usr/share/doc/codeartsGUI/changelog.gz"
# bin 用相对符号链接，指向同包内的 lib 目录；rpath=$ORIGIN/lib 仍命中
ln -sf ../lib/codeartsGUI/codeartsGUI "$DEBSTAGE/usr/bin/codeartsGUI"
install -Dm644 resources/codeartsGUI.desktop "$DEBSTAGE/usr/share/applications/codeartsGUI.desktop"
SIZE_KB=$(du -sk "$DEBSTAGE" | cut -f1)
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
Description: self-contained native Qt6 workbench for Huawei CodeArts CLI/TUI
 codeartsGUI 面向华为云码道 CodeArts AI 智能体 CLI/TUI 的原生终端仿真图形工作台。
 自带全部运行期库（Qt6 含内），除 libc 外不依赖系统其它库。进程隔离防卡顿 +
 /goal 自愈驱动 + 双主题 + 权限管理 + 中文输入。
EOF
chmod -R g-w "$DEBSTAGE"
dpkg-deb --root-owner-group --build "$DEBSTAGE" "$DIST/codeartsGUI-${VER}-amd64.deb" >/dev/null
echo "    $DIST/codeartsGUI-${VER}-amd64.deb"

echo ""
echo "✅ 自包含打包完成（版本 $VER）："
ls -la "$DIST"/*.tar.gz "$DIST"/*.deb
echo ""
echo "自包含校验：包内二进制 ldd 是否仍指向 vcpkg/外部路径（应为空）："
bad=$(ldd "$APP/codeartsGUI" 2>/dev/null | grep -i "vcpkg" || true)
[ -z "$bad" ] && echo "  ✓ 无 vcpkg 依赖" || echo "  ⚠ $bad"
