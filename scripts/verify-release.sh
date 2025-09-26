#!/usr/bin/env bash
set -euo pipefail

echo "== Release Verification Script =="

fail() { echo "[FAIL] $1" >&2; exit 1; }
pass() { echo "[OK] $1"; }

# 1. Extract versions
if [[ ! -f library.json ]]; then fail "library.json missing"; fi
if [[ ! -f library.properties ]]; then fail "library.properties missing"; fi

VERSION_JSON=$(grep '"version"' library.json | sed -E 's/.*"version" *: *"([^"]+)".*/\1/')
VERSION_PROP=$(grep '^version=' library.properties | cut -d'=' -f2)

[[ -n "${VERSION_JSON}" ]] || fail "Could not extract version from library.json"
[[ -n "${VERSION_PROP}" ]] || fail "Could not extract version from library.properties"

if [[ "${VERSION_JSON}" != "${VERSION_PROP}" ]]; then
  fail "Version mismatch: library.json=${VERSION_JSON} vs library.properties=${VERSION_PROP}"
else
  pass "Version files consistent (${VERSION_JSON})"
fi

# 2. Changelog entry (optional)
if [[ -f CHANGELOG.md ]]; then
  if grep -Eq "^## \\[${VERSION_JSON}\\]" CHANGELOG.md; then
    pass "Changelog contains entry for ${VERSION_JSON}"
  else
    echo "(Info) CHANGELOG.md missing heading for [${VERSION_JSON}]; skipping changelog check";
  fi
else
  echo "(Info) No CHANGELOG.md; skipping changelog entry check";
fi

# 3. README badge owner (no placeholder)
if grep -q 'yourusername' README.md; then
  fail "README still contains placeholder 'yourusername'"
else
  pass "README badge owner OK"
fi

# 4. User-Agent strings reflect current version
UA_MISMATCH=0
while IFS= read -r line; do
  if [[ "$line" =~ ESPAsyncWebClient/ && ! "$line" =~ ESPAsyncWebClient/${VERSION_JSON} ]]; then
    echo "Found outdated User-Agent line: $line" >&2
    UA_MISMATCH=1
  fi
done < <(grep -R "ESPAsyncWebClient/" -n src || true)

if [[ $UA_MISMATCH -ne 0 ]]; then
  fail "Outdated User-Agent strings detected"
else
  pass "User-Agent strings up to date (${VERSION_JSON})"
fi

# 5. No stale previous version literal (basic check for immediate prior released version)
# Derive previous semver by decrementing patch if possible.
PREV_PATTERN=""
if [[ "${VERSION_JSON}" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  MAJ="${BASH_REMATCH[1]}"; MIN="${BASH_REMATCH[2]}"; PAT="${BASH_REMATCH[3]}"
  if (( PAT > 0 )); then
    PREV_PATTERN="ESPAsyncWebClient/${MAJ}.${MIN}.$((PAT-1))"
  fi
fi

if [[ -n "${PREV_PATTERN}" ]]; then
  if grep -R "${PREV_PATTERN}" -n src examples test 2>/dev/null | grep -v CHANGELOG.md >/dev/null; then
    fail "Found stale previous version literal ${PREV_PATTERN} outside CHANGELOG"
  else
    pass "No stale previous version literals (except historical changelog)"
  fi
else
  echo "(Info) Could not compute previous version pattern; skipping stale literal check"
fi

# 6. Supported architectures (ensure only esp32 in library.properties)
if ! grep -Eq '^architectures=esp32$' library.properties; then
  fail "library.properties architectures not restricted to esp32"
else
  pass "Architectures restriction OK (esp32)"
fi

# 7. Optional: Tag consistency (if GITHUB_REF provided and is a tag)
if [[ "${GITHUB_REF:-}" == refs/tags/* ]]; then
  TAG="${GITHUB_REF##*/}"
  # Accept vX.Y.Z or X.Y.Z
  CANON_TAG=${TAG#v}
  if [[ "${CANON_TAG}" != "${VERSION_JSON}" ]]; then
    fail "Git tag (${TAG}) does not match version (${VERSION_JSON})"
  else
    pass "Git tag matches version (${TAG})"
  fi
else
  echo "(Info) GITHUB_REF not a tag; skipping tag/version match check"
fi

# 8. Export version for subsequent steps
if [[ -n "${GITHUB_ENV:-}" ]]; then
  echo "VERSION=${VERSION_JSON}" >> "$GITHUB_ENV"
  pass "Exported VERSION=${VERSION_JSON} to GITHUB_ENV"
else
  echo "(Info) GITHUB_ENV not set; skipping export"
fi

echo "All release verification checks passed."
