#!/usr/bin/env bash
# Fetch Brood's demo loops: the sounds the browser version and the standalone app play when
# nothing is plugged in. They are Jack Herz's own recordings, cut to whole bars and levelled
# (about -18 LUFS, peaks at or under -3 dBFS), and kept as FLAC because it loops without a gap.
#
# They are not part of the source and are not committed (the repo carries no binaries). Each
# download is checked against a pinned SHA-256, so a truncated or substituted file is refused.
#
#   bash scripts/fetch-demos.sh                fetch what is missing from jackherz.com
#   bash scripts/fetch-demos.sh --from DIR     copy them from a local folder instead
#   bash scripts/fetch-demos.sh --check        report only, change nothing
#
# BROOD_DEMO_URL overrides where they are fetched from.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST="$ROOT/assets/demos"
BASE="${BROOD_DEMO_URL:-https://jackherz.com/brood-app/demos}"
FROM=""
CHECK=0
QUIET=0
while [ $# -gt 0 ]; do
  case "$1" in
    --from) FROM="${2:?--from needs a folder}"; shift ;;
    --check) CHECK=1 ;;
    --quiet) QUIET=1 ;;
    -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

# name | sha256. The order is the order of the sound menu (DEMOS in src/audio-engine.js and
# kDemos in native/plugin/PluginProcessor.cpp).
MANIFEST="
ambiotica.flac|5459b701c7912b6c5068204fc0d0799ab297d1451a57a1746d7e27f516b10457
feedback-ambience.flac|f0232c974d7616c37ae4dec4c85dc08a71e4fd8b1ce528e7cd5fc3329431b557
tela-tape-loop.flac|5ccedef6bb091e9ed5ea6f9d7603e855efe48b30c19146ffa878e4f5db16ccb0
habit-and-keys-1.flac|42d166a3ce5dc7bda16e42432f243c51521a35f10441c3976194197de3d4f77f
habit-and-keys-2.flac|022ac2714f6d66d35a8f8456c67266f2a1a95c301803071186d946816a2afab0
chroma-rhodes.flac|4838b6f8a8a781ad6c6573d757387fd99899c4df117db424d91ebb95e10f613c
nylon-tines.flac|21924f3888a43c771c4e0393e3f2f34ee6563f6285fbda7195103f2c28fb53a0
"

say() { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }
sha_of() {
  if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
  else sha256sum "$1" | cut -d' ' -f1; fi
}

mkdir -p "$DEST"
missing=0
failed=0
while IFS='|' read -r name want; do
  [ -n "$name" ] || continue
  target="$DEST/$name"
  if [ -f "$target" ] && [ "$(sha_of "$target")" = "$want" ]; then
    say "  ok      $name"
    continue
  fi
  missing=1
  if [ "$CHECK" = 1 ]; then say "  missing $name"; continue; fi
  say "  getting $name"
  if [ -n "$FROM" ]; then
    cp "$FROM/$name" "$target.part" 2>/dev/null || { say "  FAILED  $name: not in $FROM"; failed=1; continue; }
  elif ! curl -fsSL --retry 2 --max-time 120 "$BASE/$name" -o "$target.part"; then
    say "  FAILED  $name: download failed"; rm -f "$target.part"; failed=1; continue
  fi
  if [ "$(sha_of "$target.part")" != "$want" ]; then
    say "  FAILED  $name: checksum mismatch"; rm -f "$target.part"; failed=1; continue
  fi
  mv "$target.part" "$target"
done <<EOF
$MANIFEST
EOF

if [ "$failed" = 1 ]; then
  cat >&2 <<'MSG'

Some demo loops could not be fetched. Everything still builds without them: the browser
version and the standalone app then have no demo, only your own input and files.
MSG
  exit 1
fi
if [ "$CHECK" = 1 ] && [ "$missing" = 1 ]; then exit 1; fi
say "  demo loops present in assets/demos"
