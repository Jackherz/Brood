#!/bin/bash
# Renders every parity scenario with the C++ renderer and compares it with the baseline
# taken before a change, so a step can show exactly which scenarios moved and by how much.
#
#   bash scripts/diff-baseline.sh --record   # take the baseline (build/baseline/)
#   bash scripts/diff-baseline.sh            # compare against it
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RENDER="${BROOD_RENDER:-$ROOT/native/build/brood_render}"
BASE="$ROOT/build/baseline"
NOW="$ROOT/build/baseline-now"
if [ "${1:-}" = "--record" ]; then
  rm -rf "$BASE" && mkdir -p "$BASE"
  for f in "$ROOT"/tests/parity/scenarios/*.txt; do "$RENDER" "$f" "$BASE/$(basename "$f" .txt).f32"; done
  echo "baseline: $(ls "$BASE" | wc -l | tr -d ' ') scenarios in $BASE"
  exit 0
fi
[ -d "$BASE" ] || { echo "no baseline: run with --record first" >&2; exit 1; }
rm -rf "$NOW" && mkdir -p "$NOW"
for f in "$ROOT"/tests/parity/scenarios/*.txt; do "$RENDER" "$f" "$NOW/$(basename "$f" .txt).f32"; done
node -e '
const fs = require("fs"), path = require("path");
const [base, now] = process.argv.slice(1);
for (const f of fs.readdirSync(now).sort()) {
  const name = f.replace(/\.f32$/, "");
  if (!fs.existsSync(path.join(base, f))) { console.log(`new        ${name}`); continue; }
  const A = new Float32Array(fs.readFileSync(path.join(base, f)).buffer.slice(0));
  const B = new Float32Array(fs.readFileSync(path.join(now, f)).buffer.slice(0));
  if (A.length !== B.length) { console.log(`LENGTH     ${name}: ${A.length} vs ${B.length}`); continue; }
  let max = 0, at = 0;
  for (let i = 0; i < A.length; i++) { const d = Math.abs(A[i] - B[i]); if (d > max) { max = d; at = i; } }
  console.log(`${max === 0 ? "identical " : "CHANGED   "} ${name}${max === 0 ? "" : `: max |diff| ${max.toExponential(2)} at ${((at >> 1) / 48000).toFixed(3)} s`}`);
}' "$BASE" "$NOW"
