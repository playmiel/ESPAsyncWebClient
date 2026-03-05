#!/usr/bin/env bash
# sync-version.sh — Update version in all 3 source-of-truth files at once.
# Usage: ./scripts/sync-version.sh 2.2.0
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <version>"
  echo "Example: $0 2.2.0"
  exit 1
fi

NEW_VERSION="$1"

# Validate semver format
if [[ ! "${NEW_VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Error: '${NEW_VERSION}' is not a valid semver (expected X.Y.Z)"
  exit 1
fi

echo "Syncing version to ${NEW_VERSION} across all files..."

# 1. library.json
if [[ -f library.json ]]; then
  sed -i -E "s/\"version\" *: *\"[^\"]+\"/\"version\": \"${NEW_VERSION}\"/" library.json
  echo "  ✓ library.json"
else
  echo "  ✗ library.json not found"
fi

# 2. library.properties
if [[ -f library.properties ]]; then
  sed -i -E "s/^version=.*/version=${NEW_VERSION}/" library.properties
  echo "  ✓ library.properties"
else
  echo "  ✗ library.properties not found"
fi

# 3. src/HttpCommon.h
if [[ -f src/HttpCommon.h ]]; then
  sed -i -E "s/#define ESP_ASYNC_WEB_CLIENT_VERSION \"[^\"]+\"/#define ESP_ASYNC_WEB_CLIENT_VERSION \"${NEW_VERSION}\"/" src/HttpCommon.h
  echo "  ✓ src/HttpCommon.h"
else
  echo "  ✗ src/HttpCommon.h not found"
fi

echo ""
echo "Done! Version is now ${NEW_VERSION} everywhere."
echo ""
echo "Next steps:"
echo "  1. Update CHANGELOG.md with your changes under [${NEW_VERSION}]"
echo "  2. Commit and push to main"
echo "  3. The auto-tag workflow will create the tag v${NEW_VERSION} automatically"
echo "  4. The release workflow will generate the release with auto-generated notes"
