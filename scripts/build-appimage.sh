#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 4 ] || [ "$#" -gt 6 ]; then
    echo "usage: $0 <arch> <version> <linuxdeploy> <appimagetool> [output] [runtime]" >&2
    exit 1
fi

ARCH="$1"
VERSION="$2"
LINUXDEPLOY="$3"
APPIMAGETOOL="$4"
OUTPUT="${5:-ani2xcursor-v${VERSION}-linux-${ARCH}.AppImage}"
RUNTIME_FILE="${6:-}"
APPDIR="${PWD}/AppDir"
LIB_ARGS=()

rm -rf "$APPDIR" "$OUTPUT"

install -Dm755 build/ani2xcursor "$APPDIR/usr/bin/ani2xcursor"
install -Dm755 packaging/appimage/AppRun "$APPDIR/AppRun"
install -Dm644 packaging/appimage/ani2xcursor.desktop \
    "$APPDIR/usr/share/applications/ani2xcursor.desktop"
install -Dm644 packaging/appimage/ani2xcursor.svg \
    "$APPDIR/usr/share/icons/hicolor/scalable/apps/ani2xcursor.svg"
install -Dm644 LICENSE "$APPDIR/usr/share/licenses/ani2xcursor/LICENSE"
install -Dm644 README.md "$APPDIR/usr/share/doc/ani2xcursor/README.md"
install -Dm644 completions/bash/ani2xcursor \
    "$APPDIR/usr/share/bash-completion/completions/ani2xcursor"
install -Dm644 completions/zsh/_ani2xcursor \
    "$APPDIR/usr/share/zsh/site-functions/_ani2xcursor"
install -Dm644 completions/fish/ani2xcursor.fish \
    "$APPDIR/usr/share/fish/vendor_completions.d/ani2xcursor.fish"

if [ -d build/locale ]; then
    mkdir -p "$APPDIR/usr/share"
    cp -a build/locale "$APPDIR/usr/share/"
fi

ln -s usr/share/icons/hicolor/scalable/apps/ani2xcursor.svg "$APPDIR/.DirIcon"

chmod +x "$LINUXDEPLOY" "$APPIMAGETOOL"

while read -r lib; do
    [ -n "$lib" ] || continue
    name="$(basename "$lib")"
    case "$name" in
        libc.so.*|libdl.so.*|libm.so.*|libpthread.so.*|libresolv.so.*|librt.so.*|libutil.so.*|ld-linux*.so*)
            continue
            ;;
    esac
    LIB_ARGS+=(--library "$lib")
done < <(ldd build/ani2xcursor | awk '/=> \// { print $3 }' | sort -u)

export ARCH
export VERSION
export NO_APPSTREAM=1
export APPIMAGE_EXTRACT_AND_RUN=1

"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/ani2xcursor.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/ani2xcursor.svg" \
    --executable "$APPDIR/usr/bin/ani2xcursor" \
    "${LIB_ARGS[@]}"

if [ -n "$RUNTIME_FILE" ]; then
    "$APPIMAGETOOL" --runtime-file "$RUNTIME_FILE" "$APPDIR" "$OUTPUT"
else
    "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
fi
