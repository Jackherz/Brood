// Render a parity scenario through the JS reference core.
//   node tests/parity/render.js <scenario.txt> <out.f32>
// Output: interleaved stereo float32 little-endian.
'use strict';
const fs = require('fs');
const { BroodCore } = require('../../src/brood-worklet.js');
const { parse, input, BLOCK } = require('./scenario.js');

const sc = parse(fs.readFileSync(process.argv[2], 'utf8'));
const core = new BroodCore(sc.rate);
const nBlocks = Math.ceil((sc.seconds * sc.rate) / BLOCK);
const out = new Float32Array(nBlocks * BLOCK * 2);
const inp = new Float32Array(BLOCK), L = new Float32Array(BLOCK), R = new Float32Array(BLOCK);
for (let b = 0; b < nBlocks; b++) {
  for (const ev of sc.events.get(b) || []) {
    if (ev.kind === 'param') core.setParam(ev.name, ev.value);
    else core.footswitch(ev.which, ev.action);
  }
  // A host transport running from the start at the scenario's tempo, for RECORD's bar line.
  // Same arithmetic as native/tests/render.cpp.
  if (core.p.hostPlaying) {
    core.p.ppq = (b * BLOCK) / sc.rate * core.p.bpm / 60;
    core.p.barStartPpq = Math.floor(core.p.ppq / core.p.barQuarters) * core.p.barQuarters;
  }
  for (let i = 0; i < BLOCK; i++) inp[i] = input(b * BLOCK + i, sc.rate);
  core.process(inp, L, R);
  for (let i = 0; i < BLOCK; i++) { out[2 * (b * BLOCK + i)] = L[i]; out[2 * (b * BLOCK + i) + 1] = R[i]; }
}
fs.writeFileSync(process.argv[3], Buffer.from(out.buffer));
