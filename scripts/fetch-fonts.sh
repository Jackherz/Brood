#!/usr/bin/env bash
# Download the OFL fonts the interface renders with.
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

# name | sha256 | path inside google/fonts, or a full URL pinned to a commit
# B612 Mono, Overpass Medium and Chivo Mono are the plug-in's panel faces (screen, names,
# values); Overpass and Chivo Mono come from their own repositories because google/fonts only
# carries them as variable fonts, and the plug-in wants the weight it was designed at.
MANIFEST="
B612Mono-Regular.ttf|b98cb96cc8a6206dae08c063d60902df7e6d40f86139ebdb97256704253c9c69|ofl/b612mono/B612Mono-Regular.ttf
Overpass-Medium.ttf|6a83ad9b089b049e2f18b977a77590718178b037b064711c19032cb8469bc7b0|https://raw.githubusercontent.com/RedHatOfficial/Overpass/cc00971b35aa2ebf42e147ed81ff81e94763acd1/fonts/ttf/Overpass-Medium.ttf
ChivoMono-Regular.ttf|c495de432d15afbcf3163621e3b9115ee4bcc09d85732da179b2d2f57265ba2a|https://raw.githubusercontent.com/Omnibus-Type/Chivo/dc61c468d79781eb5183426e88e844af16cdc3e5/fonts/Chivo%20Mono/ttf/ChivoMono-Regular.ttf
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
  case "$rel" in
    https://*) curl -fsSL --retry 2 --max-time 60 "$rel" -o "$tmp"; return ;;
  esac
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

Some fonts could not be fetched. The plug-in builds without them and falls back
to the kit's VT323 and the system sans; the browser version falls back to the system's faces. Download them by hand from the sources
listed at the top of scripts/fetch-fonts.sh and put the .ttf files in assets/fonts/.
MSG
  exit 1
fi

if [ "$CHECK" = 1 ] && [ "$missing" = 1 ]; then
  exit 1
fi
say "  fonts present in assets/fonts"
