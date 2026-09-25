#!/usr/bin/env bash
set -euo pipefail

# Build the native media bridge and its pinned static dependencies. The Go
# fallback remains usable when this optional native build is unavailable.

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/native/build"}
SRC_DIR="$BUILD_DIR/src"
PREFIX="$BUILD_DIR/prefix"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}

VERSION_FILE="$ROOT_DIR/third_party/versions.lock"
locked_version() {
    awk -F= -v key="$1" '$1 == key {print $2; exit}' "$VERSION_FILE"
}
FFMPEG_VERSION=${FFMPEG_VERSION:-$(locked_version ffmpeg)}
CHAFA_VERSION=${CHAFA_VERSION:-$(locked_version chafa)}
GLIB_VERSION=${GLIB_VERSION:-$(locked_version glib)}

for tool in curl tar make cc pkg-config meson autoreconf; do
    command -v "$tool" >/dev/null || {
        echo "required native build tool missing: $tool" >&2
        exit 1
    }
done

mkdir -p "$SRC_DIR" "$PREFIX" "$BUILD_DIR/lib"

fetch_source() {
    local name=$1
    local url=$2
    local archive="$SRC_DIR/$name.tar.xz"
    local source="$SRC_DIR/$name"
    if [[ ! -d "$source" ]]; then
        [[ -f "$archive" ]] || curl -fL --retry 3 -o "$archive" "$url"
        tar -xJf "$archive" -C "$SRC_DIR"
    fi
    printf '%s\n' "$source"
}

build_ffmpeg() {
    local source
    source=$(fetch_source "ffmpeg-$FFMPEG_VERSION" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz")
    if [[ -f "$PREFIX/lib/libavcodec.a" ]]; then
        return
    fi
    pushd "$source" >/dev/null
    ./configure \
        --prefix="$PREFIX" \
        --enable-static --disable-shared --enable-pic \
        --disable-programs --disable-doc --disable-debug \
        --disable-network --disable-autodetect --disable-everything \
        --disable-x86asm \
        --enable-protocol=file \
        --enable-demuxer=avi,flv,matroska,mov,mpegts \
        --enable-decoder=av1,h264,hevc,mjpeg,mpeg1video,mpeg2video,mpeg4,vp8,vp9 \
        --enable-parser=av1,h264,hevc,mjpeg,mpeg4video,vp3,vp8,vp9
    make -j"$JOBS"
    make install
    popd >/dev/null
}

build_glib() {
    local source
    source=$(fetch_source "glib-$GLIB_VERSION" "https://download.gnome.org/sources/glib/${GLIB_VERSION%.*}/glib-$GLIB_VERSION.tar.xz")
    if [[ -f "$PREFIX/lib/libglib-2.0.a" ]]; then
        return
    fi
    meson setup "$source/build-hitvid" "$source" \
        --prefix="$PREFIX" --libdir=lib --buildtype=release \
        -Ddefault_library=static -Dtests=false -Dglib_debug=disabled \
        -Ddocumentation=false -Dman=false -Dselinux=disabled -Dsysprof=disabled \
        -Dlibmount=disabled -Dxattr=false
    meson compile -C "$source/build-hitvid" -j "$JOBS"
    meson install -C "$source/build-hitvid"
}

build_chafa() {
    local source
    source=$(fetch_source "chafa-$CHAFA_VERSION" "https://github.com/hpjansson/chafa/releases/download/$CHAFA_VERSION/chafa-$CHAFA_VERSION.tar.xz")
    if [[ -f "$PREFIX/lib/libchafa.a" ]]; then
        return
    fi
    pushd "$source" >/dev/null
    [[ -f configure ]] || autoreconf -fi
    PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" ./configure \
        --prefix="$PREFIX" --disable-shared --enable-static --without-tools \
        --without-avif --without-jpeg --without-jxl --without-svg \
        --without-tiff --without-webp
    make -j"$JOBS"
    make install
    popd >/dev/null
}

build_bridge() {
    local cflags libs
    cflags="-I$ROOT_DIR/native/include -I$PREFIX/include/chafa -I$PREFIX/lib/chafa/include $(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --cflags glib-2.0)"
    libs="$(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --static --libs chafa glib-2.0)"
    cc -std=c11 -O2 -fPIC $cflags -c "$ROOT_DIR/native/src/media_bridge.c" \
        -o "$BUILD_DIR/media_bridge.o"
    ar rcs "$BUILD_DIR/lib/libhitvidmedia.a" "$BUILD_DIR/media_bridge.o"
    printf '%s\n' "$libs -L$PREFIX/lib -lavformat -lavcodec -lswscale -lswresample -lavutil" \
        > "$BUILD_DIR/native.ldflags"
}

build_ffmpeg
build_glib
build_chafa
build_bridge
echo "Native dependencies and bridge built under $BUILD_DIR"
