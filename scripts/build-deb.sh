#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DIST_DIR=${HITVID_DIST_DIR:-"$ROOT_DIR/dist"}
VERSION=${HITVID_VERSION:-0.0.0+git.${GITHUB_SHA:-local}}
ARCH=${HITVID_ARCH:-$(dpkg --print-architecture)}
BINARY=${HITVID_BINARY:-"$DIST_DIR/hitvid-linux-amd64"}

if [[ "$ARCH" != "amd64" ]]; then
    echo "v1.2.2 standalone Debian package currently supports amd64 only" >&2
    exit 1
fi
if ! dpkg --compare-versions "$VERSION" ge 0; then
    echo "invalid Debian package version: $VERSION" >&2
    exit 1
fi
command -v dpkg-deb >/dev/null || { echo "required tool missing: dpkg-deb" >&2; exit 1; }

mkdir -p "$DIST_DIR"
if [[ ! -x "$BINARY" ]]; then
    HITVID_OUTPUT="$BINARY" bash "$ROOT_DIR/scripts/build-standalone.sh"
fi
bash "$ROOT_DIR/scripts/verify-standalone.sh" "$BINARY"

build_root=$(mktemp -d "${TMPDIR:-/tmp}/hitvid-deb.XXXXXX")
trap 'rm -rf "$build_root"' EXIT
package_root="$build_root/package"
mkdir -p "$package_root/DEBIAN" "$package_root/usr/bin" "$package_root/usr/share/doc/hitvid"

install -m 0755 "$BINARY" "$package_root/usr/bin/hitvid"
install -m 0644 "$ROOT_DIR/README.md" "$package_root/usr/share/doc/hitvid/README.md"
install -m 0644 "$ROOT_DIR/LICENSE" "$package_root/usr/share/doc/hitvid/copyright"
install -m 0644 "$ROOT_DIR/third_party/NOTICES.md" "$package_root/usr/share/doc/hitvid/THIRD_PARTY_NOTICES.md"
install -m 0644 "$ROOT_DIR/third_party/SOURCE-OFFER.md" "$package_root/usr/share/doc/hitvid/SOURCE-OFFER.md"
if [[ -f "$DIST_DIR/SOURCE-MANIFEST.txt" ]]; then
    install -m 0644 "$DIST_DIR/SOURCE-MANIFEST.txt" "$package_root/usr/share/doc/hitvid/SOURCE-MANIFEST.txt"
fi
install -m 0644 "$ROOT_DIR/third_party/licenses/LGPL-2.1.txt" "$package_root/usr/share/doc/hitvid/LGPL-2.1.txt"
install -m 0644 "$ROOT_DIR/third_party/licenses/LGPL-3.0.txt" "$package_root/usr/share/doc/hitvid/LGPL-3.0.txt"
install -m 0644 "$ROOT_DIR/third_party/licenses/OTHER-LICENSES.md" "$package_root/usr/share/doc/hitvid/OTHER-LICENSES.md"
if [[ -f "$ROOT_DIR/report/report_1.2.2.md" ]]; then
    install -m 0644 "$ROOT_DIR/report/report_1.2.2.md" "$package_root/usr/share/doc/hitvid/report_1.2.2.md"
fi

cat > "$package_root/DEBIAN/control" <<EOF
Package: hitvid
Version: $VERSION
Section: video
Priority: optional
Architecture: $ARCH
Maintainer: Hitmux <support@hitmux.com>
Homepage: https://hitmux.com
Description: standalone high-performance terminal video player
 hitvid plays video files directly in a terminal with FFmpeg and Chafa linked
 into the executable. No ffmpeg, ffprobe, chafa, or shared-library runtime
 dependency is required by the official amd64 build.
EOF

output="$DIST_DIR/hitvid_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$package_root" "$output" >/dev/null
echo "Built $output"
