#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ASSETS="$ROOT/Assets/AppIcon"
mkdir -p "$ROOT/build"
ICON_TMP_DIR="$(mktemp -d "$ROOT/build/app-icon.XXXXXX")"
trap 'rm -rf "$ICON_TMP_DIR"' EXIT
ICONSET="$ICON_TMP_DIR/AppIcon.iconset"
mkdir -p "$ICONSET"
sips -z 1024 1024 "$ASSETS/source.png" --out "$ASSETS/AppIcon.png" >/dev/null
for size in 16 32 128 256 512; do
    sips -z "$size" "$size" "$ASSETS/AppIcon.png" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    retina=$((size * 2))
    sips -z "$retina" "$retina" "$ASSETS/AppIcon.png" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$ASSETS/AppIcon.icns"
printf 'Built: %s\n' "$ASSETS/AppIcon.icns"
