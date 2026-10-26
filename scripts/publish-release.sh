#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:?Usage: publish-release.sh tag package-directory}"
DIRECTORY="${2:?Usage: publish-release.sh tag package-directory}"
: "${GH_REPO:?GH_REPO must identify the GitHub repository}"
python3 "$ROOT/scripts/release.py" verify --tag "$TAG" --output "$DIRECTORY"

# A rerun may resume a draft, but must never overwrite a published release.
if draft="$(gh release view "$TAG" --json isDraft --jq .isDraft 2>/dev/null)"; then
    if [[ "$draft" != true ]]; then
        printf 'Release %s is already published; leaving it unchanged.\n' "$TAG"
        exit 0
    fi
else
    options=(--draft --verify-tag --title "SpinePreview $TAG" --notes-file "$DIRECTORY/release-notes.md")
    if [[ "$TAG" == *-* ]]; then options+=(--prerelease); fi
    gh release create "$TAG" "${options[@]}"
fi
assets=("$DIRECTORY"/*.zip "$DIRECTORY"/*.tar.gz "$DIRECTORY/SHA256SUMS")
gh release upload "$TAG" "${assets[@]}" --clobber
options=(--draft=false --notes-file "$DIRECTORY/release-notes.md")
if [[ "$TAG" == *-* ]]; then options+=(--prerelease --latest=false); else options+=(--prerelease=false); fi
gh release edit "$TAG" "${options[@]}"
