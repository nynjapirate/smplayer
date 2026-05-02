#!/bin/bash
# Build a portable AppImage for the fork. Forces dark mode at runtime via
# the SMPLAYER_FORCE_DARK=1 env var (handled in src/main.cpp).
#
# Approach: we bundle Qt libs and plugins manually (no linuxdeployqt
# dependency) and downloads appimagetool on demand. Output lands in dist/.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$REPO_DIR/src"
DIST_DIR="$REPO_DIR/dist"
APPDIR="$DIST_DIR/SMPlayerFork.AppDir"
TOOLS_DIR="$DIST_DIR/tools"
APPNAME="smplayer-fork"
VERSION="${VERSION:-23.12.0}"
ARCH="${ARCH:-x86_64}"

mkdir -p "$DIST_DIR" "$TOOLS_DIR"

echo "==> Building smplayer"
cd "$SRC_DIR"
[ -f Makefile ] || qmake -qt=5 PREFIX=/usr
make -j"$(nproc)" >/dev/null
[ -x ./smplayer ] || { echo "build failed: src/smplayer missing"; exit 1; }
cd "$REPO_DIR"

echo "==> Staging $APPDIR"
rm -rf "$APPDIR"
mkdir -p \
	"$APPDIR/usr/bin" \
	"$APPDIR/usr/lib" \
	"$APPDIR/usr/plugins/platforms" \
	"$APPDIR/usr/plugins/imageformats" \
	"$APPDIR/usr/plugins/iconengines" \
	"$APPDIR/usr/plugins/styles" \
	"$APPDIR/usr/plugins/xcbglintegrations" \
	"$APPDIR/usr/share/applications" \
	"$APPDIR/usr/share/icons/hicolor/256x256/apps"

# Binary + the icon it expects next to itself for Phase H logo override.
install -m 755 "$SRC_DIR/smplayer" "$APPDIR/usr/bin/smplayer-fork"
install -m 644 "$SRC_DIR/smplayer.png" "$APPDIR/usr/bin/smplayer.png"

# Bundle ldd-resolved shared libs (excluding the always-on-host ones —
# glibc, kernel-vDSO, X11/dbus libs that must come from the host to remain
# portable across distros).
echo "==> Bundling shared libraries"
SKIP_RE='^(/lib(64)?/ld-linux|linux-vdso|/lib/x86_64-linux-gnu/(libc|libdl|libpthread|libm|librt|libresolv|libgcc_s|libstdc\+\+)\.so|/lib/x86_64-linux-gnu/libX|/lib/x86_64-linux-gnu/libxcb|/lib/x86_64-linux-gnu/libdbus|/lib/x86_64-linux-gnu/libgl|/lib/x86_64-linux-gnu/libfontconfig|/lib/x86_64-linux-gnu/libfreetype)'
ldd "$SRC_DIR/smplayer" | awk '/=>/ { print $3 }' | while read -r lib; do
	[ -n "$lib" ] && [ -f "$lib" ] || continue
	if echo "$lib" | grep -qE "$SKIP_RE"; then continue; fi
	cp -L --no-clobber "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
done

# Qt platform plugin (xcb) and its companions
QT_PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt5/plugins"
if [ -d "$QT_PLUGIN_DIR" ]; then
	cp "$QT_PLUGIN_DIR/platforms/libqxcb.so"                    "$APPDIR/usr/plugins/platforms/" 2>/dev/null || true
	cp -r "$QT_PLUGIN_DIR/imageformats/."                       "$APPDIR/usr/plugins/imageformats/" 2>/dev/null || true
	cp -r "$QT_PLUGIN_DIR/iconengines/."                        "$APPDIR/usr/plugins/iconengines/" 2>/dev/null || true
	cp -r "$QT_PLUGIN_DIR/styles/."                             "$APPDIR/usr/plugins/styles/" 2>/dev/null || true
	cp -r "$QT_PLUGIN_DIR/xcbglintegrations/."                  "$APPDIR/usr/plugins/xcbglintegrations/" 2>/dev/null || true
	# Pull in the platform-plugin's own dependencies.
	for sofile in "$APPDIR"/usr/plugins/*/*.so; do
		[ -f "$sofile" ] || continue
		ldd "$sofile" 2>/dev/null | awk '/=>/ { print $3 }' | while read -r lib; do
			[ -n "$lib" ] && [ -f "$lib" ] || continue
			if echo "$lib" | grep -qE "$SKIP_RE"; then continue; fi
			cp -L --no-clobber "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
		done
	done
fi

# Desktop file (AppImage convention: .desktop at AppDir root, plus the
# usual /usr/share/applications copy)
DESKTOP=$(cat <<EOF
[Desktop Entry]
Name=SMPlayer (fork, dark)
GenericName=Media Player
Comment=Personal SMPlayer fork (gbit00) — AppImage with forced dark mode
Exec=AppRun %U
Icon=smplayer-fork
Terminal=false
Type=Application
Categories=AudioVideo;Player;Video;
MimeType=video/mp4;video/x-matroska;video/webm;video/x-msvideo;application/x-mpegurl;
StartupNotify=true
EOF
)
echo "$DESKTOP" > "$APPDIR/$APPNAME.desktop"
echo "$DESKTOP" > "$APPDIR/usr/share/applications/$APPNAME.desktop"

# Icons (AppImage convention: PNG at AppDir root + symlink)
install -m 644 "$SRC_DIR/smplayer.png" "$APPDIR/$APPNAME.png"
install -m 644 "$SRC_DIR/smplayer.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/$APPNAME.png"
ln -sf "$APPNAME.png" "$APPDIR/.DirIcon"

# AppRun — the entry point the AppImage shell wraps. Forces dark mode by
# exporting SMPLAYER_FORCE_DARK=1 (handled in main.cpp), routes Qt
# library/plugin lookup at the bundled paths, and unsets QT_QPA_PLATFORMTHEME
# so the host KDE/GTK theme can't override our palette.
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"

export SMPLAYER_FORCE_DARK=1
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
# Stop host theme integration from clobbering our forced-dark palette.
unset QT_QPA_PLATFORMTHEME
unset QT_STYLE_OVERRIDE

exec "$HERE/usr/bin/smplayer-fork" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

echo "==> Acquiring appimagetool"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"
if [ ! -x "$APPIMAGETOOL" ]; then
	if command -v curl >/dev/null; then
		curl -fL -o "$APPIMAGETOOL" \
			"https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
		chmod +x "$APPIMAGETOOL"
	elif command -v wget >/dev/null; then
		wget -qO "$APPIMAGETOOL" \
			"https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
		chmod +x "$APPIMAGETOOL"
	else
		echo "Neither curl nor wget present. Install one, or download appimagetool manually to $APPIMAGETOOL"
		echo "AppDir staged at: $APPDIR"
		exit 0
	fi
fi

echo "==> Building AppImage"
ARCH="$ARCH" "$APPIMAGETOOL" --no-appstream "$APPDIR" "$DIST_DIR/SMPlayerFork-${VERSION}-${ARCH}.AppImage" 2>&1 | tail -10

ls -la "$DIST_DIR"/*.AppImage
echo
echo "Run with:"
echo "  $DIST_DIR/SMPlayerFork-${VERSION}-${ARCH}.AppImage"
echo "Forces dark mode automatically via SMPLAYER_FORCE_DARK=1 in AppRun."
