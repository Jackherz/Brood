#!/usr/bin/env bash
# Download the three OFL fonts the interface renders with.
#
# The fonts are binary files, so they are not committed — a git patch cannot
# carry them reliably. They come from Google Fonts (SIL Open Font License 1.1,
# see assets/fonts/OFL-*.txt) and each download is checked against a pinned
# SHA-256, so a truncated or substituted file is rejected rather than baked into
# the plug-in.
#
#   bash scripts/fetch-fonts.sh            download what is missing
#   bash scripts/fetch-fonts.sh --force    re-download everything
#   bash scripts/fetch-fonts.sh --check    report only, change nothing
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST="$ROOT/assets/fonts"
FORCE=0
CHECK=0
QUIET=0
for arg in "$@"; do
  case "$arg" in
    --force) FORCE=1 ;;
    --check) CHECK=1 ;;
    --quiet) QUIET=1 ;;
    -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# name | sha256 | path inside google/fonts
MANIFEST="
VT323-Regular.ttf|cf4de751ada78ceac033dbe16a687742939995b77bc2a052ae17a4957958594d|ofl/vt323/VT323-Regular.ttf
PressStart2P-Regular.ttf|034c77f1f05ec89421e4a63f0e3a4ca1ecf852cc6d2bf611f126f275728e017d|ofl/pressstart2p/PressStart2P-Regular.ttf
SpaceMono-Regular.ttf|95837e182baeeada83368f7748db28357f0a1b75c6b84ff7065b5edf933c8e18|ofl/spacemono/SpaceMono-Regular.ttf
"

say() { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }

sha_of() {
  if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
  else sha256sum "$1" | cut -d' ' -f1; fi
}

# raw.githubusercontent.com first; the GitHub contents API as a fallback because
# some networks allow api.github.com and not the raw host.
download() {
  local rel="$1" out="$2" tmp="$2.part"
  local raw="https://raw.githubusercontent.com/google/fonts/main/$rel"
  if [ "$QUIET" = 1 ]; then
    curl -fsSL --retry 2 --max-time 60 "$raw" -o "$tmp" 2>/dev/null && return 0
  else
    curl -fsSL --retry 2 --max-time 60 "$raw" -o "$tmp" && return 0
    say "        raw host failed, trying the contents API"
  fi
  curl -fsSL --retry 2 --max-time 60 \
    -H 'Accept: application/vnd.github.raw' \
    "https://api.github.com/repos/google/fonts/contents/$rel" -o "$tmp" || return 1
}

mkdir -p "$DEST"
missing=0
failed=0

while IFS='|' read -r name want rel; do
  [ -n "$name" ] || continue
  target="$DEST/$name"
  if [ -f "$target" ] && [ "$FORCE" = 0 ]; then
    if [ "$(sha_of "$target")" = "$want" ]; then
      say "  ok      $name"
      continue
    fi
    say "  stale   $name (hash differs, re-downloading)"
  elif [ "$FORCE" = 0 ]; then
    missing=1
  fi

  if [ "$CHECK" = 1 ]; then
    say "  missing $name"
    missing=1
    continue
  fi

  say "  getting $name"
  if ! download "$rel" "$target"; then
    say "  FAILED  $name — download failed"
    rm -f "$target.part"
    failed=1
    continue
  fi
  got=$(sha_of "$target.part")
  if [ "$got" != "$want" ]; then
    say "  FAILED  $name — checksum mismatch (got $got)"
    rm -f "$target.part"
    failed=1
    continue
  fi
  mv "$target.part" "$target"
done <<EOF
$MANIFEST
EOF

if [ "$failed" = 1 ]; then
  cat >&2 <<'MSG'

Some fonts could not be fetched. The browser front panel needs all three; the
plug-in builds without them and falls back to a default monospace face.
Download them by hand from https://github.com/google/fonts (ofl/vt323,
ofl/pressstart2p, ofl/spacemono) and put the .ttf files in assets/fonts/.
MSG
  exit 1
fi

if [ "$CHECK" = 1 ] && [ "$missing" = 1 ]; then
  exit 1
fi
say "  fonts present in assets/fonts"
