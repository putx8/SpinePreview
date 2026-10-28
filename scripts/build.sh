#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${SPINE_VERSION:-all}"
case "$VERSION" in
    all) VERSIONS=(2.1 3.8 4.0) ;;
    2.1|3.8|4.0) VERSIONS=("$VERSION") ;;
    *) printf 'SPINE_VERSION must be all, 2.1, 3.8, or 4.0\n' >&2; exit 1 ;;
esac
ANGLE="$ROOT/Vendor/ANGLE"
if [[ ! -f "$ANGLE/lib/libEGL.dylib" || ! -f "$ANGLE/lib/libGLESv2.dylib" ]]; then
    printf 'Missing bundled ANGLE libraries in %s\n' "$ANGLE/lib" >&2
    exit 1
fi
cmake -S "$ROOT" -B "$ROOT/build/cmake" \
    -DSPINE_VERSION="$VERSION" -DSPINE_RELEASE_VERSION="${SPINE_RELEASE_VERSION:-1.0.0}" \
    -DSPINE_BUILD_NUMBER="${SPINE_BUILD_NUMBER:-1}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build "$ROOT/build/cmake" --parallel "${BUILD_JOBS:-8}"
for version in "${VERSIONS[@]}"; do
    APP="$ROOT/build/SpinePreview${version}.app"
    ditto "$ROOT/build/cmake/SpinePreview${version}.app" "$APP"
    for library in libGLESv2.dylib libEGL.dylib; do
        codesign --force --sign - "$APP/Contents/Frameworks/$library"
    done
    codesign --force --sign - "$APP"
    codesign --verify --deep --strict "$APP"
    printf 'Built: %s\n' "$APP"
done
