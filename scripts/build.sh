#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build/SpinePreview.app"
ANGLE="$ROOT/Vendor/ANGLE"
if [[ ! -f "$ANGLE/lib/libEGL.dylib" || ! -f "$ANGLE/lib/libGLESv2.dylib" ]]; then
    printf 'Missing bundled ANGLE libraries in %s\n' "$ANGLE/lib" >&2
    exit 1
fi
cmake -S "$ROOT" -B "$ROOT/build/cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64'
cmake --build "$ROOT/build/cmake" --parallel "${BUILD_JOBS:-8}"
ditto "$ROOT/build/cmake/SpinePreview.app" "$APP"
for library in libGLESv2.dylib libEGL.dylib; do
    codesign --force --sign - "$APP/Contents/Frameworks/$library"
done
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
printf 'Built: %s\n' "$APP"
