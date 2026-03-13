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
  python3 - <<'PY' "${NEW_VERSION}"
import json
import sys

new_version = sys.argv[1]
path = "library.json"

with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)

if not isinstance(data, dict) or "version" not in data:
    raise SystemExit("library.json missing top-level 'version' field")

data["version"] = new_version

with open(path, "w", encoding="utf-8", newline="\n") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write("\n")
PY
  echo "  ✓ library.json"
else
  echo "  ✗ library.json not found"
fi

# 2. library.properties
if [[ -f library.properties ]]; then
  python3 - <<'PY' "${NEW_VERSION}"
import re
import sys

new_version = sys.argv[1]
path = "library.properties"

with open(path, "r", encoding="utf-8") as f:
    text = f.read()

updated, count = re.subn(r"^version=.*$", f"version={new_version}", text, flags=re.MULTILINE)
if count == 0:
    raise SystemExit("library.properties missing 'version=' line")

with open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(updated)
PY
  echo "  ✓ library.properties"
else
  echo "  ✗ library.properties not found"
fi

# 3. src/HttpCommon.h
if [[ -f src/HttpCommon.h ]]; then
  python3 - <<'PY' "${NEW_VERSION}"
import re
import sys

new_version = sys.argv[1]
path = "src/HttpCommon.h"

with open(path, "r", encoding="utf-8") as f:
    text = f.read()

pattern = r'#define ESP_ASYNC_WEB_CLIENT_VERSION "[^"]+"'
replacement = f'#define ESP_ASYNC_WEB_CLIENT_VERSION "{new_version}"'
updated, count = re.subn(pattern, replacement, text)
if count == 0:
    raise SystemExit("src/HttpCommon.h missing ESP_ASYNC_WEB_CLIENT_VERSION define")

with open(path, "w", encoding="utf-8", newline="\n") as f:
    f.write(updated)
PY
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
