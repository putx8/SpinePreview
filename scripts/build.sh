#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/build/ANGLE Triangle.app"
ANGLE="$ROOT/Vendor/ANGLE"
if [[ ! -f "$ANGLE/lib/libEGL.dylib" ]]; then
    mkdir -p "$ANGLE"
    unzip -q "$HOME/Downloads/angle-mac-universal.zip" -d "$ANGLE"
fi
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$ROOT/build/module-cache"
cp "$ROOT/Sources/Info.plist" "$APP/Contents/Info.plist"
cp "$ANGLE/lib/libEGL.dylib" "$ANGLE/lib/libGLESv2.dylib" "$APP/Contents/Frameworks/"
xcrun clang++ "$ROOT/Sources/main.mm" -o "$APP/Contents/MacOS/ANGLETriangle" \
    -std=c++17 -fobjc-arc -fmodules -fmodules-cache-path="$ROOT/build/module-cache" \
    -Wall -Wextra -Werror -O2 -arch arm64 -arch x86_64 -mmacosx-version-min=12.0 \
    -I"$ANGLE/include" -L"$ANGLE/lib" -lEGL -lGLESv2 \
    -framework Cocoa -framework QuartzCore \
    -Wl,-rpath,@executable_path/../Frameworks
for library in libGLESv2.dylib libEGL.dylib; do
    codesign --force --sign - "$APP/Contents/Frameworks/$library"
done
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"
printf 'Built: %s\n' "$APP"
