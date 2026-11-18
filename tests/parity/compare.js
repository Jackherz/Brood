// Render every scenario with the JS reference and the C++ port and compare.
//   node tests/parity/compare.js <path to brood_render executable>
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');

const exe = process.argv[2];
const dir = path.join(__dirname, 'scenarios');
// Renders go in a temp folder: removed when every scenario passes, kept when one fails (so the
// renders can be looked at), and any left over from an earlier run is removed after a day.
for (const old of fs.readdirSync(os.tmpdir()).filter((x) => x.startsWith('brood-parity-'))) {
  const p = path.join(os.tmpdir(), old);
  try { if (Date.now() - fs.statSync(p).mtimeMs > 24 * 3600 * 1000) fs.rmSync(p, { recursive: true, force: true }); } catch {}
}
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'brood-parity-'));
process.on('exit', (code) => {
  if (code === 0) fs.rmSync(tmp, { recursive: true, force: true });
  else console.log(`renders kept in ${tmp} (removed by the next run after a day)`);
});
let failed = 0;
for (const f of fs.readdirSync(dir).filter((x) => x.endsWith('.txt')).sort()) {
  const sc = path.join(dir, f);
  const a = path.join(tmp, f + '.js.f32'), b = path.join(tmp, f + '.cpp.f32');
  const ha = a + '.heads', hb = b + '.heads';
  execFileSync(process.execPath, [path.join(__dirname, 'render.js'), sc, a, ha]);
  execFileSync(exe, [sc, b, hb]);
  const A = new Float32Array(fs.readFileSync(a).buffer.slice(0));
  const B = new Float32Array(fs.readFileSync(b).buffer.slice(0));
  if (A.length !== B.length) { console.log(`FAIL ${f}: length ${A.length} vs ${B.length}`); failed++; continue; }
  let maxDiff = 0, at = 0, sig = 0, err = 0;
  for (let i = 0; i < A.length; i++) {
    const d = Math.abs(A[i] - B[i]);
    if (d > maxDiff) { maxDiff = d; at = i; }
    sig += A[i] * A[i]; err += d * d;
  }
  const snr = 10 * Math.log10(sig / Math.max(err, 1e-30));
  // float32 buffers vs double scalars leave tiny rounding differences; anything
  // structural (a missed event, a wrong mapping) shows up as a gross error
  const ok = maxDiff < 1e-3 && snr > 80;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${f}: max |diff| ${maxDiff.toExponential(2)} at frame ${at >> 1}, SNR ${snr.toFixed(1)} dB`);
  if (!ok) failed++;
  // The playhead: where each core says the loop is heard from, block by block. Positions wrap
  // round the window, so they're compared the short way round.
  const f32 = (file) => { const buf = fs.readFileSync(file); return new Float32Array(buf.buffer, buf.byteOffset, buf.byteLength / 4).slice(); };   // a small file's Buffer can sit in a larger pool
  const HA = f32(ha), HB = f32(hb);
  let worstPos = 0, worstLevel = 0, headsAt = 0;
  if (HA.length !== HB.length) { console.log(`FAIL ${f} heads: length ${HA.length} vs ${HB.length}`); failed++; continue; }
  for (let i = 0; i < HA.length; i += 5) {
    const W = Math.max(1, HA[i]);
    if (HA[i] !== HB[i]) { worstPos = Infinity; headsAt = i / 5; break; }
    for (let k = 0; k < 2; k++) {
      const lv = Math.abs(HA[i + 2 + 2 * k] - HB[i + 2 + 2 * k]);
      if (lv > worstLevel) worstLevel = lv;
      if (HA[i + 2 + 2 * k] < 1e-3 && HB[i + 2 + 2 * k] < 1e-3) continue;   // a head that isn't sounding has no place
      let d = Math.abs(HA[i + 1 + 2 * k] - HB[i + 1 + 2 * k]) % W;
      d = Math.min(d, W - d);
      if (d > worstPos) { worstPos = d; headsAt = i / 5; }
    }
  }
  const headsOk = worstPos < 1 && worstLevel < 1e-3;
  console.log(`${headsOk ? 'ok  ' : 'FAIL'} ${f} heads: worst position ${worstPos.toFixed(4)} samples at block ${headsAt}, worst level ${worstLevel.toExponential(2)}`);
  if (!headsOk) failed++;
}
process.exit(failed ? 1 : 0);
