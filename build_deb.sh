#!/bin/bash
# Build a .deb for the fork. Package name: smplayer-fork, binary at
# /usr/bin/smplayer-fork — installs side-by-side with apt's smplayer.
# Requires: dpkg-deb (debian-tools), fakeroot, qmake-qt5, build-essentials.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$REPO_DIR/src"
DIST_DIR="$REPO_DIR/dist"
VERSION="${VERSION:-23.12.0}"
ARCH="${ARCH:-amd64}"
PKG="smplayer-fork"
STAGE="$DIST_DIR/${PKG}_${VERSION}_${ARCH}"

echo "==> Building smplayer ($VERSION, $ARCH) for .deb"
cd "$SRC_DIR"
[ -f Makefile ] || qmake -qt=5 PREFIX=/usr
make -j"$(nproc)" >/dev/null
[ -x ./smplayer ] || { echo "build failed: src/smplayer missing"; exit 1; }
cd "$REPO_DIR"

echo "==> Staging files at $STAGE"
rm -rf "$STAGE"
mkdir -p \
	"$STAGE/DEBIAN" \
	"$STAGE/usr/bin" \
	"$STAGE/usr/share/applications" \
	"$STAGE/usr/share/icons/hicolor/scalable/apps" \
	"$STAGE/usr/share/icons/hicolor/256x256/apps" \
	"$STAGE/usr/share/doc/$PKG"

# Binary
install -m 755 "$SRC_DIR/smplayer" "$STAGE/usr/bin/$PKG"

# Icon — fork-specific PNG (256x256) plus upstream's SVG if present
install -m 644 "$SRC_DIR/smplayer.png" "$STAGE/usr/share/icons/hicolor/256x256/apps/$PKG.png"
if [ -f "$REPO_DIR/icons/smplayer.svg" ]; then
	install -m 644 "$REPO_DIR/icons/smplayer.svg" "$STAGE/usr/share/icons/hicolor/scalable/apps/$PKG.svg"
fi

# Desktop file
cat > "$STAGE/usr/share/applications/$PKG.desktop" <<EOF
[Desktop Entry]
Name=SMPlayer (fork)
GenericName=Media Player
Comment=Personal SMPlayer fork (gbit00) with playlist UX, hover thumbnails, custom move shortcuts, toolbar editor
Exec=$PKG %U
Icon=$PKG
Terminal=false
Type=Application
Categories=AudioVideo;Player;Video;
MimeType=video/mp4;video/x-matroska;video/webm;video/x-msvideo;application/x-mpegurl;
StartupNotify=true
EOF

# Copyright (re-uses upstream's GPL2+ — present at repo root)
if [ -f "$REPO_DIR/Copying.txt" ]; then
	install -m 644 "$REPO_DIR/Copying.txt" "$STAGE/usr/share/doc/$PKG/copyright"
fi

# Compute Installed-Size in KB (per Debian policy)
INSTALLED_SIZE="$(du -sk "$STAGE" --exclude=DEBIAN | cut -f1)"

# Control file
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: ${VERSION}-1
Section: video
Priority: optional
Architecture: $ARCH
Installed-Size: $INSTALLED_SIZE
Maintainer: nynjapirate <noreply@github.com>
Depends: libc6, libqt5core5a, libqt5gui5, libqt5widgets5, libqt5network5, libqt5xml5, libqt5dbus5, libqt5concurrent5, mpv | mplayer | mplayer-nogui, ffmpeg
Recommends: ffmpegthumbnailer
Conflicts:
Description: SMPlayer 23.12.0 personal fork (gbit00)
 SMPlayer fork with: folder-load freeze fix (async ffprobe-based metadata),
 VLC-style playlist + thumbnail-row mode, Haruna-style hover-thumbnail above
 the seekbar, configurable keyboard-driven move-to-folder shortcuts (with
 symlink toggle), numpad / main-row keyboard separation, and a graphical
 toolbar editor with per-button icon/text overrides.
 .
 Installs as 'smplayer-fork' so it does not collide with the official
 'smplayer' apt package — both can be installed and used independently.
EOF

echo "==> Building .deb"
fakeroot dpkg-deb --build "$STAGE"
mv "$DIST_DIR/${PKG}_${VERSION}_${ARCH}.deb" "$DIST_DIR/" 2>/dev/null || true

ls -la "$DIST_DIR"/*.deb
echo
echo "Install with:"
echo "  sudo dpkg -i $DIST_DIR/${PKG}_${VERSION}_${ARCH}.deb"
echo "Run with:"
echo "  smplayer-fork"
