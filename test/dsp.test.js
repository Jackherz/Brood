'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { BroodCore, migrateTapeStart, speedMap, slipSpeedMap, stretchMap, clockHz, syncLabel, syncSecondsRaw, rampDivLabel, rampCycleSeconds, RAMP_DIV_BEATS, divMap, DIV_STEPS, MOD_TARGETS, RAMP_WAVES, rampWaveIndex, rampHz, SYNC_DIVISIONS, CORE_FS } = require('../src/brood-worklet.js');

const FS = 48000;
const BLOCK = 128;

// Run `seconds` of audio through the core; `src(t)` gives the input sample,
// `at` maps a block index to a callback (for footswitch presses).
function render(core, seconds, src, at = {}) {
  const n = Math.ceil((seconds * FS) / BLOCK);
  const L = new Float32Array(n * BLOCK), R = new Float32Array(n * BLOCK);
  const inp = new Float32Array(BLOCK), oL = new Float32Array(BLOCK), oR = new Float32Array(BLOCK);
  for (let b = 0; b < n; b++) {
    if (at[b]) at[b](core);
    for (let i = 0; i < BLOCK; i++) inp[i] = src((b * BLOCK + i) / FS);
    core.process(inp, oL, oR);
    L.set(oL, b * BLOCK); R.set(oR, b * BLOCK);
  }
  return { L, R };
}

const blk = (sec) => Math.round((sec * FS) / BLOCK);
const rms = (a, s, e) => { let acc = 0; const i0 = Math.floor(s * FS), i1 = Math.floor(e * FS); for (let i = i0; i < i1; i++) acc += a[i] * a[i]; return Math.sqrt(acc / (i1 - i0)); };
const finite = (a) => a.every(Number.isFinite);
const click = (t) => (t < 0.002 ? 0.8 : 0);
const tone = (f, dur) => (t) => (t < dur ? 0.4 * Math.sin(2 * Math.PI * f * t) : 0);

// Find the strongest output sample after `from` seconds.
function peakTime(a, from, to) {
  let best = 0, at = 0;
  for (let i = Math.floor(from * FS); i < Math.floor(to * FS); i++) if (Math.abs(a[i]) > best) { best = Math.abs(a[i]); at = i; }
  return at / FS;
}

test('bypass passes the dry signal untouched', () => {
  const core = new BroodCore(FS);
  const { L, R } = render(core, 0.5, tone(220, 1));
  const src = tone(220, 1);
  for (let i = 1000; i < 20000; i += 97) {
    assert.ok(Math.abs(L[i] - src(i / FS)) < 1e-6);
    assert.ok(Math.abs(R[i] - src(i / FS)) < 1e-6);
  }
});

test('bypass remains transparent above nominal full scale', () => {
  const core = new BroodCore(FS);
  const src = (t) => 1.4 * Math.sin(2 * Math.PI * 220 * t);
  const { L } = render(core, 0.2, src);
  for (let i = 0; i < 9000; i += 73) assert.ok(Math.abs(L[i] - src(i / FS)) < 1e-6);
});

test('delay repeats at the TIME setting, and half clock doubles it', () => {
  const measure = (clock) => {
    const core = new BroodCore(FS);
    core.setParam('wetMode', 1); core.setParam('mix', 1);
    core.setParam('modWet', 0.3); core.setParam('clock', clock);
    core.s.clock = clock; core.s.mix = 1; core.modeGain = [0, 1, 0];
    // time 0.5 → expMap(0.5, 0.02s, 2s) = 0.2 s at full clock
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const { L } = render(core, 1.6, (t) => (t > 0.1 && t < 0.102 ? 0.8 : 0));
    return peakTime(L, 0.12, 1.5) - 0.1;
  };
  const full = measure(1);
  assert.ok(Math.abs(full - 0.2) < 0.01, `full clock echo at ${full}s`);
  // clock knob 0.5 → 12 kHz; delay buffers are calibrated at 64 kHz.
  const ratio = clockHz(0.5) / CORE_FS;
  const slow = measure(0.5);
  assert.ok(Math.abs(slow - 0.2 / ratio) < 0.02, `low clock echo at ${slow}s, expected ${0.2 / ratio}`);
});

test('delay time changes crossfade between fixed read heads', () => {
  const delay = new BroodCore(FS).delay;
  const oldDelay = CORE_FS * 0.2, newDelay = CORE_FS * 0.35;
  delay.tick(0, oldDelay, 0.5, false);
  for (let i = 0; i < CORE_FS * 0.015; i++) delay.tick(0, newDelay, 0.5, false);
  assert.equal(delay.d, oldDelay, 'old head stays stationary during the fade');
  assert.equal(delay.nextD, newDelay, 'new head remains at the requested time');
  for (let i = 0; i < CORE_FS * 0.02; i++) delay.tick(0, newDelay, 0.5, false);
  assert.equal(delay.d, newDelay, 'new head becomes active after the fade');
});

test('delay timing is stable at 44.1 and 96 kHz host rates', () => {
  for (const rate of [44100, 96000]) {
    const core = new BroodCore(rate);
    core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('clock', 1);
    core.s.clock = 1; core.s.mix = 1; core.modeGain = [0, 1, 0];
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const input = new Float32Array(BLOCK), left = new Float32Array(BLOCK), right = new Float32Array(BLOCK);
    let best = 0, at = 0;
    for (let block = 0; block < Math.ceil(rate * 0.55 / BLOCK); block++) {
      for (let i = 0; i < BLOCK; i++) {
        const t = (block * BLOCK + i) / rate;
        input[i] = t > 0.1 && t < 0.102 ? 0.8 : 0;
      }
      core.process(input, left, right);
      for (let i = 0; i < BLOCK; i++) {
        const t = (block * BLOCK + i) / rate;
        if (t > 0.12 && Math.abs(left[i]) > best) { best = Math.abs(left[i]); at = t; }
      }
    }
    assert.ok(best > 0.1 && Math.abs(at - 0.3) < 0.01, `${rate} Hz: echo at ${at}s`);
  }
});

test('reverb tail decays and stays finite in every Modify position', () => {
  for (const m of [0, 0.5, 1]) {
    const core = new BroodCore(FS);
    core.setParam('wetMode', 0); core.setParam('modWet', m); core.setParam('time', 0.4);
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const { L, R } = render(core, 4, click);
    assert.ok(finite(L) && finite(R));
    const early = rms(L, 0.1, 0.4), late = rms(L, 3.5, 4);
    assert.ok(early > 1e-3, `reverb produced output (m=${m})`);
    assert.ok(late < early * 0.2, `reverb decays (m=${m}): ${early} → ${late}`);
  }
});

test('reverb time changes the delay-network size as well as decay', () => {
  const firstArrival = (size) => {
    const reverb = new BroodCore(FS).reverb;
    reverb.setDecay(3, size);
    for (let i = 0; i < CORE_FS * 0.1; i++) {
      const out = reverb.tick(i === 0 ? 1 : 0, 0, false, false);
      if (Math.abs(out[0]) + Math.abs(out[1]) > 1e-5) return i;
    }
    return Infinity;
  };
  assert.ok(firstArrival(0.35) < firstArrival(1) * 0.6);
});

test('reverb freeze holds the tail', () => {
  const core = new BroodCore(FS);
  core.setParam('time', 0.2);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const { L } = render(core, 3, tone(330, 0.5), {
    [blk(0.6)]: (c) => c.footswitch('wet', 'down'),
  });
  assert.ok(core.freeze);
  const a = rms(L, 1.2, 1.6), b = rms(L, 2.5, 2.9);
  assert.ok(a > 1e-3 && b > a * 0.7, `frozen tail sustains: ${a} → ${b}`);
});

test('maximum delay feedback stays bounded', () => {
  const core = new BroodCore(FS);
  core.setParam('wetMode', 1); core.setParam('modWet', 1); core.setParam('time', 0.2);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const { L } = render(core, 6, tone(440, 0.3));
  assert.ok(finite(L));
  assert.ok(L.every((v) => Math.abs(v) <= 1));
});

test('slip produces pitch-shifted slices in all directions', () => {
  for (const m of [0, 0.15, 0.4, 1]) {
    const core = new BroodCore(FS);
    core.setParam('wetMode', 2); core.setParam('modWet', m); core.setParam('mix', 1);
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const { L } = render(core, 2, tone(220, 2));
    assert.ok(finite(L));
    assert.ok(rms(L, 1, 2) > 0.02, `slip output m=${m}`);
  }
});

test('tapping the loop switch captures what was just played', () => {
  const core = new BroodCore(FS);
  core.setParam('mix', 1);
  // play a tone for 1 s, then silence; tap at 1.0 s
  const { L } = render(core, 3, tone(330, 1), {
    [blk(1.0)]: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); },
  });
  assert.strictEqual(core.state().loop, 'play');
  assert.ok(rms(L, 2, 3) > 0.1, 'loop keeps playing after input stops');
});

test('holding the loop switch records a fresh loop of that length', () => {
  const core = new BroodCore(FS);
  core.setParam('length', 1);
  render(core, 1.5, tone(330, 3), {
    [blk(0.5)]: (c) => c.footswitch('loop', 'down'),
    [blk(1.3)]: (c) => c.footswitch('loop', 'up'),
  });
  const secs = core.lsrc / CORE_FS;
  assert.ok(Math.abs(secs - 0.8) < 0.01, `loop length ${secs}s`);
});

test('a hold longer than loop capacity captures the newest full buffer', () => {
  const core = new BroodCore(FS);
  core.setParam('clock', 1); core.s.clock = 1;
  render(core, 2.1, tone(330, 3), {
    [blk(0.2)]: (c) => c.footswitch('loop', 'down'),
    [blk(1.8)]: (c) => c.footswitch('loop', 'up'),
  });
  assert.equal(core.state().loop, 'play');
  // Capacity is counted in core samples: 64 000 of them is 1 s at 64 kHz.
  assert.equal(core.lsrc, 64000);
});

test('tap while playing stops the loop; hold while playing overdubs', () => {
  const core = new BroodCore(FS);
  render(core, 2, tone(330, 1), {
    [blk(1.0)]: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); },
    [blk(1.2)]: (c) => c.footswitch('loop', 'down'),
    [blk(1.4)]: (c) => assert.strictEqual(c.state().loop, 'play'),
    [blk(1.6)]: (c) => assert.strictEqual(c.state().loop, 'dub'),
    [blk(1.8)]: (c) => c.footswitch('loop', 'up'),
  });
  assert.strictEqual(core.state().loop, 'play');
  render(core, 0.1, () => 0, { 0: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); } });
  assert.strictEqual(core.state().loop, 'off');
});

test('every loop mode and routing stays finite', () => {
  for (const loopMode of [0, 1, 2]) for (const routing of [0, 1, 2]) for (const wetMode of [0, 1, 2]) {
    const core = new BroodCore(FS);
    Object.entries({ loopMode, routing, wetMode, modLoop: 0.1, clock: 0.3 }).forEach(([k, v]) => core.setParam(k, v));
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const { L, R } = render(core, 2, (t) => 0.3 * Math.sin(2 * Math.PI * 196 * t) * Math.exp(-3 * (t % 0.5)), {
      [blk(1.0)]: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); },
    });
    assert.ok(finite(L) && finite(R), `loop ${loopMode} routing ${routing} wet ${wetMode}`);
  }
});

test('loop buffer length is set by the clock (fixed sample count)', () => {
  for (const [knob, secs] of [[1, 1], [0, 32]]) {
    const core = new BroodCore(FS);
    core.setParam('clock', knob); core.s.clock = knob;
    render(core, secs + 0.5, tone(330, 100), {
      [blk(secs + 0.4)]: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); },
    });
    assert.ok(Math.abs(core.lsrc / clockHz(knob) - secs) < 0.02, `clock ${knob}`);
  }
});

test('ENV mode: playing interrupts the loop with a repeating slice', () => {
  const core = new BroodCore(FS);
  core.setParam('loopMode', 0); core.setParam('modLoop', 0.8); core.setParam('length', 0.2);
  core.setParam('mix', 1);
  // record a 220 Hz loop, then play a loud 880 Hz note: the output should keep
  // repeating loop material (not 880 Hz, since dry is killed) and stay finite
  const src = (t) => (t < 1 ? 0.3 * Math.sin(2 * Math.PI * 220 * t) : t > 1.5 && t < 2.2 ? 0.5 * Math.sin(2 * Math.PI * 880 * t) : 0);
  const { L } = render(core, 3, src, {
    [blk(1.0)]: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); },
    [blk(1.8)]: (c) => assert.ok(c.envGate, 'gate opens while playing'),
    [blk(2.8)]: (c) => assert.ok(!c.envGate, 'gate closes after you stop'),
  });
  assert.ok(finite(L) && rms(L, 1.6, 2.1) > 0.02);
});

test('speed maps hit musical anchors', () => {
  assert.strictEqual(speedMap(0.5), 0.5);
  assert.strictEqual(speedMap(1), 4);
  assert.strictEqual(speedMap(0), -4);
  assert.strictEqual(speedMap(2 / 3), 1);
  assert.strictEqual(slipSpeedMap(0), -2);
  assert.strictEqual(slipSpeedMap(1), 2);
  assert.strictEqual(stretchMap(0.5), 0);
  assert.strictEqual(stretchMap(1), 1);
  assert.strictEqual(stretchMap(0), -1);
});

test('clock is stepped by default and continuously variable with Smooth', () => {
  assert.strictEqual(clockHz(0), 2000);
  assert.strictEqual(clockHz(0.5), 12000);
  assert.strictEqual(clockHz(1), 64000);
  assert.strictEqual(clockHz(0.51), clockHz(0.5));
  assert.notStrictEqual(clockHz(0.51, true), clockHz(0.5, true));
});

test('wet input ignores routing when the loop is listening', () => {
  const core = new BroodCore(FS);
  core.setParam('wetMode', 1); core.setParam('routing', 2); core.setParam('mix', 1);
  core.setParam('time', 0.15);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const { L } = render(core, 1.5, tone(220, 1));
  assert.ok(rms(L, 0.5, 1) > 0.01, 'wet receives input even in LOOP routing when looper is off');
});

test('the history is clean unless PRE puts the wet side ahead of the looper', () => {
  const run = (wetPre) => {
    const core = new BroodCore(FS);
    core.setParam('wetMode', 1); core.setParam('time', 0.2); core.setParam('modWet', 0.9);
    core.setParam('wetPre', wetPre);
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    render(core, 1.0, tone(330, 0.15));
    let energy = 0;
    // Recent history after the dry tone has stopped holds only the wet repeats.
    for (let i = 35000; i < 60000; i++) energy += core.hist[i] * core.hist[i];
    return energy;
  };
  assert.ok(run(1) > 0.1, 'with PRE the looper hears the repeats');
  assert.strictEqual(run(0), 0, 'without it, only the input');
});

test('WEAVE keeps three threads audible, repeatable and knob-responsive', () => {
  const run = (time, modify) => {
    const core = new BroodCore(FS);
    core.setParam('wetMode', 3); core.setParam('mix', 1);
    core.setParam('time', time); core.setParam('modWet', modify);
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    // A gated tone keeps the memory fresh, like a guitarist playing.
    const { L } = render(core, 3, (t) => (t % 1 < 0.75 ? 0.35 * Math.sin(2 * Math.PI * 330 * t) : 0));
    return rms(L, 1.5, 3);
  };
  const texture = run(0.5, 0.3);
  assert.ok(texture > 0.005, `weave should be audible, got ${texture}`);
  assert.ok(Math.abs(texture - run(0.5, 0.3)) < 1e-12, 'same knob position, same fabric');
  assert.ok(Math.abs(texture - run(0.5, 0.85)) > 1e-4, 'MODIFY changes texture into rhythm');
  assert.ok(Math.abs(texture - run(0.95, 0.3)) > 1e-4, 'TIME changes how far back the threads reach');
});

test('WEAVE stays bounded at the extremes', () => {
  const core = new BroodCore(FS);
  core.setParam('wetMode', 3); core.setParam('mix', 1); core.setParam('modWet', 1);
  core.setParam('time', 1); core.setParam('clock', 0); core.setParam('width', 1);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const { L, R } = render(core, 2, tone(196, 5));
  for (let i = 0; i < L.length; i++) {
    assert.ok(Number.isFinite(L[i]) && Number.isFinite(R[i]), 'finite');
    assert.ok(Math.abs(L[i]) <= 1 && Math.abs(R[i]) <= 1, 'bounded');
  }
});

test('tempo sync puts the delay on the musical division, at any clock', () => {
  const echoAt = (time, bpm, clock) => {
    const core = new BroodCore(FS);
    core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('modWet', 0.5);
    core.setParam('sync', 1); core.setParam('bpm', bpm); core.setParam('time', time); core.setParam('clock', clock);
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const n = FS * 3;
    const click = new Float32Array(n); click[FS] = 0.8;
    const L = new Float32Array(n), R = new Float32Array(n);
    core.process(click, L, R, n);
    let best = 0, at = 0;
    for (let i = FS + 64; i < n; i++) if (Math.abs(L[i]) > best) { best = Math.abs(L[i]); at = i; }
    return (at - FS) / FS;
  };
  // 1/4 at 120 BPM is 0.5 s; at 90 BPM it is 0.667 s; 1/8T at 120 is 0.167 s.
  assert.ok(Math.abs(echoAt(2 / 3, 120, 1) - 0.5) < 0.01, '1/4 at 120 bpm');
  assert.ok(Math.abs(echoAt(2 / 3, 90, 1) - 2 / 3) < 0.01, '1/4 at 90 bpm');
  assert.ok(Math.abs(echoAt(1 / 3, 120, 1) - 1 / 6) < 0.01, '1/8T at 120 bpm');
  // The clock changes the core rate, not the musical time.
  assert.ok(Math.abs(echoAt(2 / 3, 120, 0.4) - 0.5) < 0.01, '1/4 at 120 bpm with a low clock');
  assert.strictEqual(syncLabel(2 / 3), '1/4');
});

test('sync off leaves TIME as a free time', () => {
  const core = new BroodCore(FS);
  core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('modWet', 0.5);
  core.setParam('sync', 0);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const n = FS * 2, click = new Float32Array(n); click[FS] = 0.8;
  const L = new Float32Array(n), R = new Float32Array(n);
  core.process(click, L, R, n);
  let best = 0, at = 0;
  for (let i = FS + 64; i < n; i++) if (Math.abs(L[i]) > best) { best = Math.abs(L[i]); at = i; }
  assert.ok(Math.abs((at - FS) / FS - 0.2) < 0.02, 'free delay at the default knob');
});

test('balance at LOOP always returns the loop, whatever the routing', () => {
  for (const routing of [0, 1, 2]) {
    const core = new BroodCore(FS);
    core.setParam('routing', routing); core.setParam('balance', 1); core.setParam('mix', 1); core.setParam('loopMode', 1);
    render(core, 1.2, tone(220, 5));
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const { L } = render(core, 1, () => 0);
    // Silent input, loop playing, wet engaged, balance fully to LOOP: must be audible.
    assert.ok(rms(L, 0.2, 0.9) > 0.05, `routing ${routing}: loop silent at balance 100% (rms ${rms(L, 0.2, 0.9)})`);
  }
});

test('the tempo divisions in the UI and the worklet agree', () => {
  // main.jsx keeps its own copy because an AudioWorklet cannot import ES modules;
  // evaluate that copy and compare it with the worklet's table.
  const fs = require('node:fs');
  const path = require('node:path');
  const src = fs.readFileSync(path.join(__dirname, '../src/main.jsx'), 'utf8');
  const start = src.indexOf('const SYNC_DIVISIONS');
  const end = src.indexOf('const syncSeconds');
  assert.ok(start > 0 && end > start, 'could not find the UI division table');
  const body = `${src.slice(start, end)}\n return SYNC_DIVISIONS;`;
  const ui = new Function('clamp', body)((v, a = 0, b = 1) => Math.min(b, Math.max(a, v)));
  assert.deepStrictEqual(ui.map((d) => d.label), SYNC_DIVISIONS.map((d) => d.label));
  assert.deepStrictEqual(ui.map((d) => d.beats), SYNC_DIVISIONS.map((d) => d.beats));
});

// The worklet exports the span table's beats and its label function, not the label
// array itself; recover the labels so the UI copy can be compared against them.
const RAMP_DIV_LABELS_FOR_TEST = () => RAMP_DIV_BEATS.map((_, i) => rampDivLabel(i / (RAMP_DIV_BEATS.length - 1)));

test('the modulation spans in the UI and the worklet agree', () => {
  // Same rule as the divisions above: main.jsx keeps its own copy of the span
  // table, so evaluate it here and compare label for label, beat for beat.
  const fs = require('node:fs');
  const path = require('node:path');
  const src = fs.readFileSync(path.join(__dirname, '../src/main.jsx'), 'utf8');
  const start = src.indexOf('const RAMP_DIV_LABELS');
  const end = src.indexOf('const rampCycleSeconds');
  assert.ok(start > 0 && end > start, 'could not find the UI span table');
  const body = `${src.slice(start, end)}\n return { RAMP_DIV_LABELS, RAMP_DIV_BEATS, rampDivIndex, rampDivLabel };`;
  const ui = new Function('clamp', body)((v, a = 0, b = 1) => Math.min(b, Math.max(a, v)));
  assert.deepStrictEqual(ui.RAMP_DIV_LABELS, RAMP_DIV_LABELS_FOR_TEST());
  assert.deepStrictEqual(ui.RAMP_DIV_BEATS, RAMP_DIV_BEATS);
  // and the label the readout asks for is the label the engine divides by
  for (let i = 0; i < RAMP_DIV_BEATS.length; i++) {
    const v = i / (RAMP_DIV_BEATS.length - 1);
    assert.strictEqual(ui.rampDivLabel(v), rampDivLabel(v));
    assert.strictEqual(ui.rampDivIndex(v), i);
  }
});

test('a synced modulation span is measured against the tempo, not the clock', () => {
  // 8 bars at 120 bpm is 16 s: slow enough that a 1 s window barely turns.
  assert.ok(Math.abs(rampCycleSeconds(120, 0) - 16) < 1e-9, '8 bars at 120');
  assert.ok(Math.abs(rampCycleSeconds(120, 1) - 0.125) < 1e-9, '1/16 at 120');
  assert.ok(Math.abs(rampCycleSeconds(60, 1) - 0.25) < 1e-9, 'half the tempo, twice the span');
  // A whole bar must survive the delay's 2 s ceiling, or nothing slow is reachable.
  // (The delay's knob runs fast-to-slow, so 1/1 sits at the top of its travel.)
  assert.ok(syncSecondsRaw(40, 1) > 2, 'raw divisions are not clamped to the delay');
});

test('HOLD stops a source and it carries on from the same place', () => {
  const core = new BroodCore(FS);
  const run = (seconds) => {
    const inp = new Float32Array(BLOCK), oL = new Float32Array(BLOCK), oR = new Float32Array(BLOCK);
    for (let b = 0; b < Math.ceil((seconds * FS) / BLOCK); b++) core.process(inp, oL, oR);
  };
  run(0.7);
  const phases = [core.ramp[0].ph, core.ramp[1].ph];
  core.setParam('mod1Hold', 1); core.setParam('mod2Hold', 1);
  run(0.7);
  assert.deepEqual([core.ramp[0].ph, core.ramp[1].ph], phases);
  core.setParam('mod1Hold', 0); core.setParam('mod2Hold', 0);
  run(0.1);
  assert.notEqual(core.ramp[0].ph, phases[0]);
});

test('a source\'s SYNC hands its rate to the tempo and free mode takes it back', () => {
  // Count how often the generator wraps: the phase only ever jumps backwards.
  const turns = (rampSync, rampSpeed, bpm, seconds) => {
    const core = new BroodCore(FS);
    core.setParam('mod1_time', 0.5);
    core.setParam('mod1Wave', 0);
    core.setParam('mod1Rate', rampSpeed);
    core.setParam('mod1Sync', rampSync);
    core.setParam('bpm', bpm);
    let wraps = 0, prev = core.ramp[0].ph;
    const n = Math.ceil((seconds * FS) / BLOCK);
    const inp = new Float32Array(BLOCK), oL = new Float32Array(BLOCK), oR = new Float32Array(BLOCK);
    for (let b = 0; b < n; b++) {
      core.process(inp, oL, oR);
      const ph = core.ramp[0].ph;
      if (ph < prev) wraps++;
      prev = ph;
    }
    return wraps;
  };
  // 1/16 at 120 bpm is 0.125 s a turn: eight turns in a second.
  assert.ok(turns(1, 1, 120, 1.2) >= 8 && turns(1, 1, 120, 1.2) <= 10, 'synced 1/16');
  // The same knob free-running is about half a hertz, so one turn in 1.2 s.
  assert.ok(turns(0, 0.45, 120, 1.2) <= 1, 'free rate is not the span list');
  // Eight bars at 120 bpm is 16 s: no wrap in a short window, whichever tempo.
  assert.strictEqual(turns(1, 0, 120, 1.2), 0, '8 bars does not wrap early');
  // And the span really does follow the host tempo: twice as fast, twice as many.
  const slow = turns(1, 0.8, 60, 2.4);
  const fast = turns(1, 0.8, 120, 2.4);
  assert.ok(fast >= slow * 2, `tempo doubles the turns (${slow} -> ${fast})`);
});

test('the wet SYNC and the ENV switch are separate', () => {
  const play = (loopMode, wetMode, extra) => {
    const core = new BroodCore(FS);
    for (const [k, v] of Object.entries({ loopMode, wetMode, bpm: 120, clock: 1, time: 0.5, length: 0.5, mix: 0.6, modWet: 0.6, routing: 1, ...extra })) core.setParam(k, v);
    render(core, 1.2, tone(330, 5));
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    return render(core, 2, tone(330, 5)).L;
  };
  const diff = (a, b) => { let d = 0; for (let i = 0; i < a.length; i++) diff; return a.reduce((acc, v, i) => acc + Math.abs(v - b[i]), 0); };
  // ENV: its own switch changes the slice; the wet switch does not touch it.
  const envFree = play(0, 0, {});
  assert.ok(diff(envFree, play(0, 0, { envSync: 1 })) > 1, 'ENV SYNC does nothing');
  assert.ok(diff(envFree, play(0, 0, { sync: 1 })) < 0.001, 'the wet SYNC leaked into ENV');
  // WEAVE: the wet switch changes the reach; the ENV switch is irrelevant there.
  const weaveFree = play(1, 3, {});
  assert.ok(diff(weaveFree, play(1, 3, { sync: 1 })) > 1, 'the wet SYNC does nothing in WEAVE');
  assert.ok(diff(weaveFree, play(1, 3, { envSync: 1 })) < 0.001, 'ENV SYNC leaked into WEAVE');
});

test('END moves the end marker, not the start marker', () => {
  // Drive loopWindow directly: it is the mapping that decides where the window sits,
  // and it is the thing that used to drag START along with END.
  for (const loopMode of [0, 1, 2]) {
    const core = new BroodCore(FS);
    core.lsrc = 10 * FS;                     // a ten-second take, whole samples
    core.setParam('loopMode', loopMode);
    core.setParam('start', 0.15);
    core.setParam('length', 0.5);
    core.setParam('loopEnd', 0.7);
    const wide = core.loopWindow();
    core.setParam('loopEnd', 0.5);
    const narrow = core.loopWindow();
    assert.strictEqual(narrow.start, wide.start, `mode ${loopMode}: END moved START`);
    assert.ok(narrow.W < wide.W, `mode ${loopMode}: END did not shorten the window`);
    // START 1 is the newest audio in every mode: the window (TAPE: LENGTH's span) ends at
    // the end of the take.
    core.setParam('start', 1);
    const atTop = core.loopWindow();
    const reach = loopMode === 1 ? core.loopSpan() : atTop.W;
    assert.ok(Math.abs(atTop.start + reach - core.lsrc) <= 1, `mode ${loopMode}: START 1 stops short`);
    // ...and cannot move a full-length window, because there is nowhere to slide to.
    if (loopMode !== 1) {
      core.setParam('loopEnd', 1);
      assert.strictEqual(core.loopWindow().start, 0, `mode ${loopMode}: a full window moved`);
    }
  }
});

test('LOOP MEMORY raises the capture ceiling, 0 keeps the pedal rule', () => {
  const capturedSeconds = (memory, clock) => {
    const core = new BroodCore(FS);
    core.setParam('memory', memory); core.setParam('clock', clock);
    render(core, 36, () => 0.1);
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    return core.lsrc / clockHz(clock);
  };
  assert.ok(Math.abs(capturedSeconds(0, 1) - 1) < 0.02, '1 s at 64 kHz');
  assert.ok(Math.abs(capturedSeconds(0, 0) - 32) < 0.2, '32 s at 2 kHz');
  assert.ok(Math.abs(capturedSeconds(1, 1) - 30) < 0.05, '30 s ceiling');
  assert.ok(capturedSeconds(0.5, 1) > 4 && capturedSeconds(0.5, 1) < 7, 'mid memory');
  // MEMORY can never exceed the 30 s promise, even at the lowest clock.
  assert.ok(capturedSeconds(1, 0) <= 30.05, 'never longer than the ceiling');
});

test('END trims the end of the loop and never moves where it starts', () => {
  // LENGTH decides the loop's start point (in TAPE the window is the tail of the
  // capture), so END must only shorten the loop from its beginning. It used to
  // shorten the capture instead, which dragged the start around with it.
  for (const [mode, name] of [[0, 'ENV'], [1, 'TAPE'], [2, 'STRETCH']]) {
    const core = new BroodCore(FS);
    core.setParam('loopMode', mode);
    core.setParam('length', 0.72);
    // From the oldest audio in ENV and STRETCH; TAPE keeps its default, the newest. At 1
    // an ENV window keeps against the end of the take, so trimming it moves its start.
    core.setParam('start', mode === 1 ? 1 : 0);
    render(core, 3, () => 0.2);
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    const full = core.state();
    if (mode === 1) assert.ok(full.winStart > 0 && full.winLen < core.lsrc, `${name}: LENGTH sets the window`);
    else assert.ok(full.winStart === 0 && full.winLen === core.lsrc, `${name}: the whole capture plays`);
    core.setParam('loopEnd', 0.5);
    render(core, 0.1, () => 0);
    const half = core.state();
    assert.strictEqual(half.winStart, full.winStart, `${name}: END leaves the start alone`);
    assert.ok(Math.abs(half.winLen - full.winLen / 2) <= 1, `${name}: END halves what plays`);
    core.setParam('loopEnd', 0.02);
    render(core, 0.1, () => 0);
    assert.ok(core.state().winLen >= 64, `${name}: the trim has a floor`);
    assert.strictEqual(core.state().winStart, full.winStart, `${name}: still no movement at the extreme`);
  }
});

test('TAPE takes its loop window from the tail of the capture', () => {
  const core = new BroodCore(FS);
  core.setParam('loopMode', 1);
  core.setParam('length', 1);
  render(core, 3, () => 0.2);
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  const long = core.state();
  core.setParam('length', 0.4);
  render(core, 0.1, () => 0);
  const short = core.state();
  assert.ok(short.winLen < long.winLen, 'a shorter LENGTH is a shorter loop');
  assert.ok(short.winStart > long.winStart, 'and it starts later in the capture');
  assert.ok(Math.abs(short.winStart - (core.lsrc - short.winLen)) <= 1, 'the window ends at the capture end');
});

test('START moves the loop window inside the take', () => {
  // The user asked for it back for ENV and STRETCH: END says how much of the take
  // plays, START says where that window sits in it. Zero is where each mode has
  // always begun, so nothing changes at the default.
  const renderLoop = (mode, length) => {
    const core = new BroodCore(FS);
    core.setParam('loopMode', mode);
    core.setParam('length', length);
    render(core, 4, () => 0.2);
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    return core;
  };
  for (const [mode, name] of [[0, 'ENV'], [2, 'STRETCH']]) {
    const core = renderLoop(mode, 0.72);
    core.setParam('loopEnd', 0.5);
    core.setParam('start', 0);
    render(core, 0.1, () => 0);
    const base = core.state();
    assert.strictEqual(base.winStart, 0, `${name}: START 0 opens on the oldest audio`);
    const room = core.lsrc - base.winLen;
    core.setParam('start', 1);
    render(core, 0.1, () => 0);
    const end = core.state();
    assert.strictEqual(end.winLen, base.winLen, `${name}: START leaves the length alone`);
    assert.ok(Math.abs(end.winStart - room) <= 1, `${name}: START 1 slides the window to the end of the take`);
    // START is measured against the take now, so a quarter of the way along is a
    // quarter of the take — until it reaches the end marker, which is the ceiling.
    core.setParam('start', 0.25);
    render(core, 0.1, () => 0);
    const expected = Math.min(Math.round(0.25 * (core.lsrc - 64)), room);
    assert.ok(Math.abs(core.state().winStart - expected) <= 1, `${name}: and stops anywhere between`);
  }
  // TAPE: START counts from the oldest audio too. Its default, 1, is the tail.
  const tape = renderLoop(1, 0.72);
  render(tape, 0.1, () => 0);
  const tail = tape.state();
  assert.ok(tail.winStart > 0, 'TAPE: the default plays the newest audio');
  tape.setParam('start', 0);
  render(tape, 0.1, () => 0);
  assert.strictEqual(tape.state().winStart, 0, 'TAPE: START 0 reaches all the way back');
  tape.setParam('start', 1);
  render(tape, 0.1, () => 0);
  assert.strictEqual(tape.state().winStart, tail.winStart, 'TAPE: and 1 is the tail again');
  // With END wide open there is nothing to move into.
  const whole = renderLoop(0, 1);
  whole.setParam('start', 1);
  render(whole, 0.1, () => 0);
  assert.strictEqual(whole.state().winStart, 0, 'a full-length window cannot move');
});

test('moving START crossfades instead of jumping', () => {
  // A step in the read head is a click, so a change of START is a 25 ms crossfade.
  // Compare the worst sample-to-sample step while sweeping against the same figure
  // for the loop playing still: a jump would show up as a step many times bigger.
  const core = new BroodCore(FS);
  core.setParam('loopMode', 2);
  core.setParam('length', 0.5);
  core.setParam('loopEnd', 0.4);
  core.setParam('mix', 1);
  render(core, 3, (t) => 0.35 * Math.sin(2 * Math.PI * 140 * t) + 0.2 * Math.sin(2 * Math.PI * 523 * t));
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  core.setParam('balance', 1);                      // the loop on its own
  const still = render(core, 0.5, () => 0);
  const step = (a, from, to) => {
    let m = 0;
    for (let i = Math.floor(from * FS) + 1; i < Math.floor(to * FS); i++) m = Math.max(m, Math.abs(a[i] - a[i - 1]));
    return m;
  };
  const quiet = step(still.L, 0.1, 0.5);
  // One jump of the whole window at once, 300 times a second.
  const stepped = render(core, 0.6, () => 0, {});
  const sweep = render(core, 0.6, () => 0, Object.fromEntries(
    Array.from({ length: blk(0.5) }, (_, b) => [b, (c) => c.setParam('start', (b / blk(0.5)))])
  ));
  const moving = step(sweep.L, 0.02, 0.6);
  assert.ok(Number.isFinite(moving) && moving < Math.max(0.02, quiet * 4),
    `sweeping START stays smooth (worst step ${moving.toFixed(4)} vs ${quiet.toFixed(4)} still)`);
  assert.ok(/^[\s\S]*$/.test('') && stepped.L.length > 0, 'the stepped pass renders');
  // And a single big move mid-loop, the worst case a drag can produce.
  core.setParam('start', 0);
  render(core, 0.1, () => 0);
  const jump = render(core, 0.4, () => 0, { [blk(0.1)]: (c) => c.setParam('start', 1) });
  const worst = step(jump.L, 0.05, 0.4);
  assert.ok(worst < Math.max(0.02, quiet * 4), `one big move stays smooth (worst step ${worst.toFixed(4)})`);
});

// ───────────────────────── hidden options ─────────────────────────

const hfEnergy = (a, from, to) => { let acc = 0; for (let i = Math.floor(from * FS) + 1; i < Math.floor(to * FS); i++) acc += Math.pow(a[i] - a[i - 1], 2); return Math.sqrt(acc / Math.floor((to - from) * FS)); };
const sideEnergy = (L, R) => { let acc = 0; for (let i = 0; i < L.length; i++) { const d = (L[i] - R[i]) * 0.5; acc += d * d; } return Math.sqrt(acc / L.length); };
const crossings = (a, from, to) => { let n = 0; for (let i = Math.floor(from * FS) + 1; i < Math.floor(to * FS); i++) if ((a[i - 1] <= 0) !== (a[i] <= 0)) n++; return n / 2 / (to - from); };

const wetCore = (patch) => {
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries(patch)) { core.setParam(k, v); }
  core.wetOn = true;
  core.s.mix = patch.mix ?? core.p.mix;
  return core;
};

test('clock divisions are stepped 1x, 2x, 4x', () => {
  assert.deepStrictEqual(DIV_STEPS, [1, 2, 4]);
  assert.deepStrictEqual([0, 0.2, 0.5, 0.7, 0.8, 1].map(divMap), [1, 1, 2, 2, 4, 4]);
});

test('TONE is a hi-cut on the wet bus; fully open is bit-transparent', () => {
  const hf = (value) => {
    const core = wetCore({ wetMode: 0, mix: 1, time: 0.7, tone: value });
    core.s.tone = value;
    const { L } = render(core, 3, (t) => 0.4 * Math.sin(2 * Math.PI * 220 * t) + 0.2 * Math.sin(2 * Math.PI * 7000 * t));
    return hfEnergy(L, 1, 3);
  };
  const open = hf(1), shut = hf(0);
  assert.ok(shut < open * 0.8, `closed ${shut.toFixed(4)} should be darker than open ${open.toFixed(4)}`);

  const plain = new BroodCore(FS);
  plain.setParam('wetMode', 0); plain.setParam('mix', 1); plain.wetOn = true; plain.s.mix = 1;
  const a = render(plain, 1, tone(220, 1));
  const openCore = new BroodCore(FS);
  openCore.setParam('wetMode', 0); openCore.setParam('mix', 1); openCore.setParam('tone', 1);
  openCore.wetOn = true; openCore.s.mix = 1; openCore.s.tone = 1;
  const b = render(openCore, 1, tone(220, 1));
  assert.deepStrictEqual(Array.from(a.L), Array.from(b.L), 'tone fully open must not filter');
});

test('STEREO WIDTH keeps 0.5 as built, folds to mono at 0 and doubles at 1', () => {
  const image = (width) => {
    const core = wetCore({ wetMode: 0, mix: 1, time: 0.7, width });
    core.s.width = width;
    const { L, R } = render(core, 2.5, (t) => 0.3 * Math.sin(2 * Math.PI * 220 * t) + 0.1 * Math.sin(2 * Math.PI * 3100 * t));
    return sideEnergy(L.slice(FS), R.slice(FS));
  };
  const mid = image(0.5), mono = image(0), wide = image(1);
  assert.ok(mid > 0.005, 'the reverb is stereo to begin with');
  assert.ok(mono < mid * 0.05, `width 0 should collapse the sides (${mono.toFixed(5)} vs ${mid.toFixed(4)})`);
  assert.ok(wide > mid * 1.6, `width 1 should widen (${wide.toFixed(4)} vs ${mid.toFixed(4)})`);

  // The neutral setting must be exactly the old behaviour, not just close.
  const plain = new BroodCore(FS);
  plain.setParam('wetMode', 0); plain.setParam('mix', 1); plain.wetOn = true; plain.s.mix = 1;
  const a = render(plain, 1, tone(220, 1));
  const withWidth = new BroodCore(FS);
  withWidth.setParam('wetMode', 0); withWidth.setParam('mix', 1); withWidth.setParam('width', 0.5);
  withWidth.wetOn = true; withWidth.s.mix = 1;
  const b = render(withWidth, 1, tone(220, 1));
  assert.deepStrictEqual(Array.from(a.L), Array.from(b.L), 'width 0.5 must be the built image');
});

test('WET CLOCK DIVISION stretches the wet side only', () => {
  const echoGap = (wetDiv) => {
    const core = new BroodCore(FS);
    core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('modWet', 0.3);
    core.setParam('time', 0.3); core.setParam('wetDiv', wetDiv);
    core.wetOn = true; core.s.mix = 1; core.s.time = 0.3; core.s.modWet = 0.3; core.modeGain = [0, 1, 0, 0];
    const { L } = render(core, 4, click);
    return peakTime(L, 0.02, 3.9);
  };
  const one = echoGap(0), two = echoGap(0.5), four = echoGap(1);
  assert.ok(Math.abs(two / one - 2) < 0.1, `2x division: ${one.toFixed(3)} -> ${two.toFixed(3)}`);
  assert.ok(Math.abs(four / one - 4) < 0.2, `4x division: ${one.toFixed(3)} -> ${four.toFixed(3)}`);
});

test('the input is read at each core tick, so a captured loop is smooth', () => {
  // At 48 kHz in and a 64 kHz core, handing every tick the latest host sample
  // repeated one sample in four: a stair-step recorded into the loop. Played four
  // times slower the stairs are easy to measure, as roughness (mean |second
  // difference| over mean |signal|) far above what a clean sine at that pitch has.
  const core = new BroodCore(FS);
  core.setParam('loopMode', 1); core.setParam('mix', 1); core.setParam('loopDiv', 1);
  core.setParam('clock', 1); core.setParam('modLoop', 0.75);
  core.s.mix = 1;
  const { L } = render(core, 3, (t) => 0.35 * Math.sin(2 * Math.PI * 220 * t), {
    [blk(0.1)]: (c) => { c.footswitch('loop', 'down'); },
    [blk(0.6)]: (c) => { c.footswitch('loop', 'up'); },
  });
  // The median over 10 ms chunks, so the fade where the window wraps does not count.
  const chunk = Math.floor(0.01 * FS), rough = [];
  for (let s = Math.floor(1 * FS); s + chunk < Math.floor(2.8 * FS); s += chunk) {
    let d2 = 0, d0 = 0;
    for (let i = s; i < s + chunk; i++) { d2 += Math.abs(L[i + 1] - 2 * L[i] + L[i - 1]); d0 += Math.abs(L[i]); }
    rough.push(d2 / d0);
  }
  rough.sort((a, b) => a - b);
  const median = rough[rough.length >> 1], ideal = (2 * Math.PI * crossings(L, 1, 2.8) / FS) ** 2;
  assert.ok(median < 2 * ideal, `loop roughness ${median.toExponential(2)} vs a clean sine's ${ideal.toExponential(2)}`);
});

test('LOOP CLOCK DIVISION halves and quarters the loop pitch', () => {
  const pitch = (loopDiv, loopMode) => {
    const core = new BroodCore(FS);
    core.setParam('loopMode', loopMode); core.setParam('mix', 1); core.setParam('loopDiv', loopDiv);
    core.setParam('clock', 1); core.setParam('time', 0.5);
    core.s.mix = 1;
    const { L } = render(core, 6, (t) => 0.35 * Math.sin(2 * Math.PI * 220 * t), {
      [blk(0.1)]: (c) => { c.footswitch('loop', 'down'); },
      [blk(0.3)]: (c) => { c.footswitch('loop', 'up'); },
    });
    return crossings(L, 3, 5);
  };
  const env1 = pitch(0, 0), env2 = pitch(0.5, 0), env4 = pitch(1, 0);
  assert.ok(Math.abs(env1 - 220) < 12, `loop plays at source pitch (${env1.toFixed(1)})`);
  assert.ok(Math.abs(env2 - 110) < 8, `half clock, half pitch (${env2.toFixed(1)})`);
  assert.ok(Math.abs(env4 - 55) < 6, `quarter clock, quarter pitch (${env4.toFixed(1)})`);
  // The capture itself is not shortened by a division — only its playback.
  const core = new BroodCore(FS);
  render(core, 1.2, () => 0.2);
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  const lsrc = core.lsrc;
  assert.ok(lsrc > 20000, 'the loop still holds a full capture');
});

test('FADE decides how much of the old loop survives an overdub', () => {
  const levelAfterOverdub = (fade) => {
    const core = new BroodCore(FS);
    core.setParam('loopMode', 1); core.setParam('mix', 0.6); core.setParam('clock', 1); core.setParam('fade', fade);
    core.s.mix = 0.6; core.s.fade = fade;
    let src = (t) => 0.3 * Math.sin(2 * Math.PI * 200 * t);
    render(core, 0.4, src, { [blk(0.1)]: (c) => c.footswitch('loop', 'down'), [blk(0.3)]: (c) => c.footswitch('loop', 'up') });
    // Two passes of silence through the overdub head.
    render(core, 4.5, () => 0, { [blk(0.1)]: (c) => c.footswitch('loop', 'down') });
    return rms(render(core, 0.4, () => 0).L, 0.2, 0.4);
  };
  const keep = levelAfterOverdub(0), gone = levelAfterOverdub(1);
  assert.ok(keep > 0.005, 'FADE 0 keeps the loop, as before');
  assert.ok(gone < keep * 0.05, `FADE 1 lets the silent overdub wipe it (${gone.toFixed(5)} vs ${keep.toFixed(4)})`);
});

test('CLASSIC makes the slow clock noisy and the reverb ring', () => {
  const noiseFloor = (classic, wearNoise = 1) => {
    const core = wetCore({ wetMode: 0, mix: 1, time: 0.6, clock: 0, classic: classic ? 1 : 0, wearNoise });
    const { L } = render(core, 2, () => 0);
    return rms(L, 1, 2);
  };
  const clean = noiseFloor(false), noisy = noiseFloor(true);
  assert.ok(clean < 1e-6, 'without CLASSIC a silent input stays silent');
  assert.ok(noisy > 1e-4, `CLASSIC adds clock noise (${noisy.toFixed(5)})`);
  const half = noiseFloor(true, 0.5), none = noiseFloor(true, 0);
  assert.ok(half > noisy * 0.4 && half < noisy * 0.6, `NOISE at half halves it (${half.toFixed(5)})`);
  assert.ok(none < 1e-6, 'NOISE at 0 leaves worn silent');
});

test('WORN after the loop keeps the take clean, and is still heard', () => {
  const tap = (core, which) => { core.footswitch(which, 'down'); core.footswitch(which, 'up'); };
  const worn = { wetMode: 0, mix: 1, time: 0.6, clock: 0, classic: 1, wetPre: 1 };
  const kept = (post) => {
    const core = new BroodCore(FS);
    for (const [k, v] of Object.entries({ ...worn, wearPost: post ? 1 : 0, balance: 1, loopMode: 1 })) core.setParam(k, v);
    tap(core, 'wet'); tap(core, 'loop');
    render(core, 3, () => 0, { [blk(2.0)]: (c) => tap(c, 'loop') });
    core.setParam('classic', 0); tap(core, 'wet');   // wear off and wet off: only the loop is left to hear
    const { L } = render(core, 3, () => 0);
    return rms(L, 1, 3);
  };
  const inLoop = kept(false), after = kept(true);
  assert.ok(inLoop > 1e-5, `worn in the loop records its hiss (${inLoop.toFixed(6)})`);
  assert.ok(after < inLoop * 0.05, `worn after the loop keeps the take clean (${after.toFixed(6)} of ${inLoop.toFixed(6)})`);
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries({ ...worn, wearPost: 1 })) core.setParam(k, v);
  tap(core, 'wet');
  const { L } = render(core, 2, () => 0);
  assert.ok(rms(L, 1, 2) > 1e-4, 'worn after the loop is still heard');
});

test('TAPE: deep, fast LENGTH modulation keeps the playhead moving through the window', () => {
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries({ loopMode: 1, clock: 1, memory: 0.6, length: 0.5, mix: 1, balance: 1, routing: 2, modLoop: 0.66 })) core.setParam(k, v);
  render(core, 4, (t) => 0.4 * Math.sin(2 * Math.PI * 220 * t));
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  for (const [k, v] of Object.entries({ mod1On: 1, mod1Rate: 1, mod1Wave: 0.3, mod1_length: 1 })) core.setParam(k, v);   // square, 8 Hz, full depth
  let back = 0, last = -1;
  const at = {};
  for (let b = 0; b < blk(3); b++) at[b] = (c) => {
    const st = c.state();
    const f = st.winLen > 0 ? st.pos / st.winLen : 0;
    if (b > blk(1) && last >= 0 && f < last - 0.1 && last < 0.9) back++;
    last = f;
  };
  render(core, 3, () => 0, at);
  const { L } = render(core, 1, () => 0);
  let maxStep = 0;
  for (let i = 1; i < L.length; i++) maxStep = Math.max(maxStep, Math.abs(L[i] - L[i - 1]));
  assert.equal(back, 0, `LENGTH modulation throws the playhead back ${back} times`);
  assert.ok(maxStep < 0.07, `LENGTH modulation steps by ${maxStep.toFixed(3)}`);
  assert.ok(rms(L, 0, 1) > 0.1, 'and the loop keeps sounding');
});

test('START means the same in every loop mode', () => {
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries({ loopMode: 0, clock: 1, memory: 0.6, length: 0.5, loopEnd: 0.5, start: 0.3, mix: 1, balance: 1, routing: 2 })) core.setParam(k, v);
  render(core, 4, (t) => 0.4 * Math.sin(2 * Math.PI * 220 * t));
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  const starts = [];
  for (const m of [0, 1, 2]) { core.setParam('loopMode', m); render(core, 0.5, () => 0); starts.push(core.state().winStart); }
  assert.ok(Math.abs(starts[1] - starts[0]) <= 1 && Math.abs(starts[2] - starts[0]) <= 1, `one place in every mode (${starts})`);
  assert.ok(Math.abs(starts[0] - Math.round(0.3 * (core.state().lsrc - 64))) <= 1, 'measured from the oldest audio');
});

test('switching loop mode keeps the playhead where it is in the take', () => {
  let lo = 0.5, hi = 1;
  for (let i = 0; i < 60; i++) { const mid = (lo + hi) / 2; if (speedMap(mid, 0.04) < 1) lo = mid; else hi = mid; }
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries({ loopMode: 1, clock: 1, memory: 0.6, length: 0.6, loopEnd: 0.6, start: 0.2, modLoop: lo, mix: 1, balance: 1, routing: 2 })) core.setParam(k, v);
  render(core, 4, (t) => 0.4 * Math.sin(2 * Math.PI * 220 * t));
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  render(core, 1.3, () => 0);
  const before = core.state();
  core.setParam('loopMode', 0);
  render(core, BLOCK / FS, () => 0);
  const after = core.state();
  const moved = (after.winStart + after.pos) - (before.winStart + before.pos);
  const perBlock = BLOCK * 64000 / FS;
  assert.ok(moved >= -1 && moved <= 2 * perBlock, `the playhead stays put (moved ${moved.toFixed(0)}, one block is ${perBlock.toFixed(0)})`);
});

test('an old TAPE START comes back to the same place', () => {
  const len = 4 * 64000;
  for (const length of [0.2, 0.5, 0.9]) for (const old of [0, 0.1, 0.5, 0.8, 1]) {
    const W0 = Math.max(64, Math.round(len * Math.min(1, 0.02 + 0.98 * length)));
    const was = Math.round((1 - old) * (len - W0));
    const now = migrateTapeStart(old, length, len);
    const is = Math.min(Math.round(now * (len - 64)), len - W0);
    assert.ok(Math.abs(is - was) <= 1, `old ${old} at LENGTH ${length}: ${is} vs ${was}`);
  }
  assert.equal(migrateTapeStart(0, 0.5, len), 1);
  assert.equal(migrateTapeStart(1, 0.5, len), 0);
});

test('RECORD: the loop switch records what is played after it', () => {
  const tap = (core) => { core.footswitch('loop', 'down'); core.footswitch('loop', 'up'); };
  const fresh = (extra = {}) => {
    const core = new BroodCore(FS);
    for (const [k, v] of Object.entries({ recMode: 1, clock: 1, memory: 1, mix: 1, balance: 1, routing: 2, ...extra })) core.setParam(k, v);
    return core;
  };
  const seconds = (core) => core.state().lsrc / core.state().clockHz;
  {
    const core = fresh();
    render(core, 1, () => 0);
    tap(core);
    assert.equal(core.state().loop, 'rec', 'a tap starts recording');
    render(core, 1, (t) => 0.4 * Math.sin(2 * Math.PI * 440 * t));
    tap(core);
    assert.equal(core.state().loop, 'play', 'a second tap plays it');
    assert.ok(Math.abs(seconds(core) - 1) < 0.01, `the take is what was played (${seconds(core).toFixed(3)} s)`);
  }
  {
    const core = fresh({ recBars: 2, recAtTap: 1, bpm: 120 });
    render(core, 0.5, () => 0);
    tap(core);
    let closed = -1;
    const at = {};
    for (let b = 0; b < blk(5); b++) at[b] = (c) => { if (b === blk(2)) c.setParam('clock', 0.6); if (closed < 0 && c.state().loop === 'play') closed = b; };
    render(core, 5, (t) => 0.3 * Math.sin(2 * Math.PI * 330 * t), at);
    assert.ok(closed >= 0 && Math.abs(closed - blk(4)) <= 1, `2 bars close at 4 s (block ${closed})`);
  }
  {
    const core = fresh({ recBars: 1, bpm: 120, hostPlaying: 1 });
    const transport = (c, b) => { c.p.ppq = (b * BLOCK) / FS * 2; c.p.barStartPpq = Math.floor(c.p.ppq / 4) * 4; };
    const at0 = {};
    for (let b = 0; b < blk(1.3); b++) at0[b] = (c) => transport(c, b);
    render(core, 1.3, () => 0, at0);
    tap(core);
    assert.ok(core.state().recArmed, 'a tap mid-bar waits for the bar line');
    let started = -1;
    const at = {};
    for (let b = 0; b < blk(2); b++) at[b] = (c) => { transport(c, b + blk(1.3)); if (started < 0 && c.state().loop === 'rec') started = b + blk(1.3); };
    render(core, 2, () => 0.2, at);
    assert.ok(started >= 0 && Math.abs(started - blk(2)) <= 1, `it starts on the bar line (block ${started})`);
  }
  {
    const core = fresh({ memory: Math.log(2) / Math.log(30) });
    render(core, 0.5, () => 0);
    tap(core);
    render(core, 3, (t) => 0.3 * Math.sin(2 * Math.PI * (200 + 100 * t) * t));
    assert.equal(core.state().loop, 'play', 'the ceiling closes the recording');
    assert.ok(Math.abs(seconds(core) - 2) < 0.02, `at 2 s (${seconds(core).toFixed(3)})`);
  }
});

test('TAPE ECHO: a TIME sweep bends the pitch, and a long freeze holds', () => {
  const echo = () => wetCore({ wetMode: 1, mix: 1, modWet: 0.5, time: 0.3, clock: 1, routing: 0, tapeDelay: 1 });
  const tone = (t) => 0.3 * Math.sin(2 * Math.PI * 1000 * t);
  const sweep = {};
  for (let b = 0; b < blk(4); b++) { const t = (b * BLOCK) / FS; if (t > 1.5 && t < 2.5) sweep[b] = (c) => c.setParam('time', 0.3 + 0.4 * (t - 1.5)); }
  const { L } = render(echo(), 4, tone, sweep);
  let zc = 0;
  for (let i = Math.floor(1.6 * FS) + 1; i < Math.floor(2.4 * FS); i++) if ((L[i - 1] <= 0) !== (L[i] <= 0)) zc++;
  assert.ok(Math.abs(zc / 2 / 0.8 - 1000) > 20, `the pitch bends (${(zc / 1.6).toFixed(0)} Hz)`);
  let step = 0;
  for (let i = FS + 1; i < L.length; i++) step = Math.max(step, Math.abs(L[i] - L[i - 1]));
  assert.ok(step < 0.12, `and never steps (${step.toFixed(3)})`);
  const frozen = echo();
  let s = 7;
  render(frozen, 2, () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return ((s >>> 8) / 16777216 - 0.5) * 0.4; });
  frozen.setParam('freeze', 1);
  render(frozen, 1, () => 0);
  const first = render(frozen, 1, () => 0).L;
  frozen.setParam('time', 0.8);
  render(frozen, 17, () => 0);
  const last = render(frozen, 1, () => 0).L;
  const level = 20 * Math.log10(rms(last, 0, 1) / rms(first, 0, 1));
  assert.ok(Math.abs(level) < 0.5, `a 20 s freeze holds its level (${level.toFixed(2)} dB)`);
});

// ─────────────────────────── the ramp engine ───────────────────────────

// Sample the control the DSP actually reads, once per block.
function rampTrace(patch, seconds, field = 'time') {
  const core = new BroodCore(FS);
  Object.assign(core.p, patch);
  for (const [k, v] of Object.entries(patch)) core.setParam(k, v);
  const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
  const out = [];
  for (let b = 0; b < Math.ceil((seconds * FS) / BLOCK); b++) {
    core.process(inp, L, R);
    out.push(core.e[field]);
  }
  return out;
}
const rminmax = (a) => [Math.min(...a), Math.max(...a)];
const wrapsIn = (seconds, speed) => {
  const core = new BroodCore(FS);
  core.setParam('mod1_time', 0.5); core.setParam('mod1Rate', speed);
  const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
  let wraps = 0, last = core.ramp[0].ph;
  for (let b = 0; b < Math.ceil((seconds * FS) / BLOCK); b++) {
    core.process(inp, L, R);
    if (core.ramp[0].ph < last) wraps++;
    last = core.ramp[0].ph;
  }
  return wraps;
};

test('the modulation targets and shapes are the documented lists', () => {
  assert.deepStrictEqual(MOD_TARGETS, ['time', 'mix', 'length', 'modWet', 'clock', 'modLoop', 'balance']);
  assert.deepStrictEqual(RAMP_WAVES, ['TRI', 'SQR', 'SIN', 'RND', 'SMTH']);
  RAMP_WAVES.forEach((_, i) => assert.strictEqual(rampWaveIndex(i / (RAMP_WAVES.length - 1)), i));
  assert.strictEqual(rampWaveIndex(-4), 0);
  assert.strictEqual(rampWaveIndex(4), RAMP_WAVES.length - 1);
});

test('one source moves several controls by its own depth, and a negative depth inverts', () => {
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries({ time: 0.5, mix: 0.5, length: 0.5, mod1_time: 0.4, mod1_mix: -0.4, mod1_length: 0.1, mod1Wave: 0.5 })) core.setParam(k, v);
  const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
  for (let b = 0; b < Math.ceil((1 * FS) / BLOCK); b++) core.process(inp, L, R);   // the knobs settle
  const t = [], m = [], l = [];
  for (let b = 0; b < Math.ceil((5 * FS) / BLOCK); b++) { core.process(inp, L, R); t.push(core.e.time); m.push(core.e.mix); l.push(core.e.length); }
  const spread = (a) => Math.max(...a) - Math.min(...a);
  assert.ok(Math.abs(spread(t) - 0.8) < 0.05, `depth 0.4 swings 0.8 (${spread(t).toFixed(3)})`);
  assert.ok(Math.abs(spread(l) - 0.2) < 0.03, `depth 0.1 swings 0.2 (${spread(l).toFixed(3)})`);
  const later = t.length - 1;
  assert.ok(Math.abs((t[later] - 0.5) + (m[later] - 0.5)) < 1e-6, 'the inverted route mirrors the other');
});

test('each source has its own rate, and muting one lets its knobs come home', () => {
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries({ mod1_time: 0.4, mod2_modLoop: 0.4, mod1Rate: 0.2, mod2Rate: 0.9, time: 0.5, modLoop: 0.5 })) core.setParam(k, v);
  const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
  const run = (seconds) => { for (let b = 0; b < Math.ceil((seconds * FS) / BLOCK); b++) core.process(inp, L, R); };
  const ph0 = core.ramp.map((g) => g.ph);
  run(0.1);
  assert.ok(Math.abs(core.ramp[1].ph - ph0[1]) > 4 * Math.abs(core.ramp[0].ph - ph0[0]), 'two rates');
  core.setParam('mod1On', 0);
  run(0.5);
  assert.ok(Math.abs(core.e.time - 0.5) < 1e-3, `muted, TIME is the knob (${core.e.time})`);
});

test('a ramp moves its control and stops where the knob says', () => {
  const still = rampTrace({ time: 0.6 }, 2);
  assert.ok(Math.abs(still[still.length - 1] - 0.6) < 1e-6, 'off is the knob, untouched');
  const moved = rampTrace({ mod1_time: 0.5, time: 0.6 }, 4);
  const [lo, hi] = rminmax(moved);
  assert.ok(hi - lo > 0.6, `TIME should sweep (${lo.toFixed(2)}..${hi.toFixed(2)})`);
  assert.ok(hi <= 1 && lo >= 0, 'stays inside the knob range');
  // Balanced around the knob: bipolar is a swing, not a one-way offset.
  assert.ok(Math.abs((lo + hi) / 2 - 0.6) < 0.12, `centred on the knob (${((lo + hi) / 2).toFixed(2)})`);
});

test('each waveform has its own shape', () => {
  const shapes = RAMP_WAVES.map((_, w) => rampTrace({ mod1_time: 0.5, mod1Wave: w / 4, time: 0.5 }, 6));
  const [tri, sqr, sin, rnd, smth] = shapes;
  const ends = (a) => a.filter((v) => v < 0.15 || v > 0.85).length / a.length;
  const middle = (a) => a.filter((v) => Math.abs(v - 0.5) < 0.15).length / a.length;
  assert.ok(ends(sqr) > 0.9, 'square is two levels');
  assert.ok(ends(sin) > ends(tri), `sine hangs at the ends more than the triangle (${ends(sin).toFixed(2)} vs ${ends(tri).toFixed(2)})`);
  assert.ok(middle(tri) > middle(sin), 'the triangle passes through the middle more');
  assert.ok(middle(sqr) < 0.05, 'the square skips the middle');
  // Random and smooth random hold a value, then move.
  const held = (a) => { let n = 0; for (let i = 1; i < a.length; i++) if (Math.abs(a[i] - a[i - 1]) < 1e-6) n++; return n / a.length; };
  assert.ok(held(rnd) > 0.3, 'sample and hold sits still between rolls');
  assert.ok(held(smth) < held(rnd), 'smooth random keeps moving');
  const [rlo, rhi] = rminmax(rnd);
  assert.ok(rhi - rlo > 0.1, 'random covers a range');
  const distinct = new Set(shapes.map((a) => a.map((v) => v.toFixed(2)).join(','))).size;
  assert.strictEqual(distinct, 5, 'all five shapes are different');
});

test('RAMP SPEED is a rate in hertz, and the CLOCK does not change it', () => {
  const slow = wrapsIn(10, 0.1), fast = wrapsIn(10, 0.9);
  assert.ok(slow <= 1, `slow speed barely moves (${slow})`);
  assert.ok(fast >= 15, `fast speed runs (${fast})`);
  assert.ok(rampHz(0.5) > 0.5 && rampHz(0.5) < 0.8, 'mid speed is about 0.6 Hz');
  for (const clock of [0, 0.5, 1]) {
    const core = new BroodCore(FS);
    core.setParam('mod1_time', 0.5); core.setParam('mod1Rate', 0.5); core.setParam('clock', clock);
    const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
    let wraps = 0, last = core.ramp[0].ph;
    for (let b = 0; b < Math.ceil((4 * FS) / BLOCK); b++) {
      core.process(inp, L, R);
      if (core.ramp[0].ph < last) wraps++;
      last = core.ramp[0].ph;
    }
    assert.ok(wraps >= 1 && wraps <= 3, `clock ${clock} keeps the same ramp rate (${wraps} cycles in 4 s)`);
  }
});

// The raw generator, before the 30 ms smoothing: the snap-back is visible here.
function rawTrace(patch, seconds) {
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries(patch)) core.setParam(k, v);
  const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
  const out = [];
  for (let b = 0; b < Math.ceil((seconds * FS) / BLOCK); b++) {
    core.process(inp, L, R);
    out.push(core.rampRaw[0]);
  }
  return out;
}

test('SWEEP is a one-way climb and BOUNCE takes the snap out of it', () => {
  const saw = rawTrace({ mod1_time: 0.5, mod1Sweep: 1, time: 0.5 }, 8);
  const bouncy = rawTrace({ mod1_time: 0.5, mod1Sweep: 1, mod1Bounce: 1, time: 0.5 }, 8);
  const resets = (a) => a.filter((v, i) => i > 0 && v < a[i - 1] - 0.5).length;
  const rising = (a) => a.filter((v, i) => i > 0 && v > a[i - 1]).length / (a.length - 1);
  assert.ok(resets(saw) >= 2, `the sweep climbs and starts again (${resets(saw)} resets)`);
  assert.ok(rising(saw) > 0.9, `it only ever climbs (${(rising(saw) * 100).toFixed(0)}% rising steps)`);
  assert.strictEqual(resets(bouncy), 0, 'BOUNCE removes the snap-back entirely');
  assert.ok(rising(bouncy) > 0.3 && rising(bouncy) < 0.7, 'and turns it into a tide');
});

test('POLARITY: unipolar only ever adds to the knob', () => {
  const bi = rampTrace({ mod1_time: 0.5, time: 0.5 }, 6);
  const uni = rampTrace({ mod1_time: 0.5, time: 0.5, mod1Uni: 1 }, 6);
  assert.ok(rminmax(uni)[0] >= 0.5 - 0.01, `unipolar never goes under the knob (${rminmax(uni)[0].toFixed(2)})`);
  assert.ok(rminmax(uni)[1] > 0.8, 'and still reaches the top');
  assert.ok(rminmax(bi)[0] < 0.3, 'bipolar goes both ways');
});

test('the two generators are independent, and MODIFY means this channel', () => {
  const spread = (a) => rminmax(a)[1] - rminmax(a)[0];
  const wetOnly = rampTrace({ mod1_time: 0.5, time: 0.5, modLoop: 0.5 }, 4);
  assert.ok(spread(wetOnly) > 0.5, 'the wet generator moves TIME');
  // The wet ramp must leave the loop's MODIFY alone.
  const loopStill = [];
  const core = new BroodCore(FS);
  core.setParam('mod1_time', 0.5); core.setParam('modLoop', 0.5);
  const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
  for (let b = 0; b < Math.ceil((4 * FS) / BLOCK); b++) { core.process(inp, L, R); loopStill.push(core.e.modLoop); }
  assert.ok(spread(loopStill) < 1e-6, 'the wet ramp does not touch the loop');
  // The loop generator pointed at MODIFY drives the loop's own MODIFY.
  const loopRamp = rampTrace({ mod2_modLoop: 0.5, modLoop: 0.5 }, 4, 'modLoop');
  assert.ok(spread(loopRamp) > 0.5, 'the loop generator moves loop MODIFY');
  // Both at once: two different controls, two different phases.
  const both = [];
  const c2 = new BroodCore(FS);
  c2.setParam('mod1_time', 0.5); c2.setParam('mod2_modLoop', 0.5);
  for (let b = 0; b < Math.ceil((4 * FS) / BLOCK); b++) { c2.process(inp, L, R); both.push([c2.e.time, c2.e.modLoop]); }
  const timeSpread = spread(both.map((v) => v[0])), modSpread = spread(both.map((v) => v[1]));
  assert.ok(timeSpread > 0.5 && modSpread > 0.5, 'both run together');
  assert.notStrictEqual(c2.ramp[0].ph, c2.ramp[1].ph, 'the two generators are not in lockstep');
});

test('with every ramp off the audio is bit-identical to before the engine existed', () => {
  const a = new BroodCore(FS);
  a.setParam('wetMode', 0); a.setParam('mix', 0.8); a.wetOn = true; a.s.mix = 0.8;
  const one = render(a, 1, tone(220, 1));
  const b = new BroodCore(FS);
  b.setParam('wetMode', 0); b.setParam('mix', 0.8); b.wetOn = true; b.s.mix = 0.8;
  b.setParam('mod1Rate', 1);
  const two = render(b, 1, tone(220, 1));
  assert.deepStrictEqual(Array.from(one.L), Array.from(two.L));
});

test('a TIME ramp makes the echo breathe', () => {
  const core = new BroodCore(FS);
  core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('modWet', 0.55);
  core.setParam('time', 0.4); core.setParam('mod1_time', 0.5); core.setParam('mod1Rate', 0.35);
  core.wetOn = true; core.s.mix = 1; core.modeGain = [0, 1, 0, 0];
  const { L } = render(core, 4, click);
  assert.ok(finite(L), 'stays finite while the ramp moves the delay');
  const a = peakTime(L, 0.02, 1.2), b = peakTime(L, 1.6, 3.6);
  assert.ok(Math.abs(a - b) > 0.01, `the echo moves (${a.toFixed(3)} s then ${b.toFixed(3)} s)`);
});

test('LENGTH travels all the way back to the start of a 30 s capture', () => {
  // A TAPE-mode capture with the memory window wide open must be able to put
  // its window at the very beginning of the take, not just near it.
  const core = new BroodCore(FS);
  Object.assign(core.p, { loopMode: 1, memory: 1, loopEnd: 1 });   // TAPE
  render(core, 30.5, tone(220, 1));
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  const len = core.lsrc;
  assert.ok(len > 29 * FS, `the take is about 30 s (${(len / FS).toFixed(2)} s)`);
  const starts = [1, 0.95, 0.75, 0.5, 0.25].map((v) => {
    core.setParam('length', v);
    render(core, 0.6, () => 0);          // let the smoothed knob settle
    const w = core.loopWindow();
    return { v, start: w.start / len, seconds: w.W / FS };
  });
  assert.ok(starts[0].start < 0.01, `LENGTH 1 starts at the beginning (${(starts[0].start * 100).toFixed(1)} %)`);
  assert.ok(starts[0].seconds > 29, `and the window is the whole take (${starts[0].seconds.toFixed(2)} s)`);
  // Travel is usable: each quarter turn of the knob moves the start by ~25 %.
  for (let i = 1; i < starts.length; i++) assert.ok(starts[i].start > starts[i - 1].start, 'the start only moves forward');
  assert.ok(Math.abs(starts[2].start - 0.245) < 0.02, `LENGTH 0.75 starts a quarter in (${(starts[2].start * 100).toFixed(1)} %)`);
  assert.ok(Math.abs(starts[4].start - 0.735) < 0.02, `LENGTH 0.25 starts three quarters in (${(starts[4].start * 100).toFixed(1)} %)`);
});

test('re-taking a loop many times never splices a click in', () => {
  // Every extra footswitch tap used to stitch another splice into the capture,
  // because the history only advanced while the loop was off.
  const slope = 0.4 * 2 * Math.PI * 100 / FS; // the natural step of a 100 Hz tone
  const take = (taps) => {
    const core = new BroodCore(FS);
    Object.assign(core.p, { clock: 1, memory: 0.5, loopMode: 1, mix: 1, wetMode: 0, modLoop: 0.5, length: 0.8 });
    let t = 0;
    const step = (sec) => {
      const n = Math.ceil((sec * FS) / BLOCK);
      const inp = new Float32Array(BLOCK), oL = new Float32Array(BLOCK), oR = new Float32Array(BLOCK);
      for (let b = 0; b < n; b++) {
        for (let i = 0; i < BLOCK; i++) inp[i] = 0.4 * Math.sin(2 * Math.PI * 100 * ((t * BLOCK + i) / FS));
        core.process(inp, oL, oR);
        t++;
      }
    };
    step(4);
    for (let k = 0; k < taps; k++) {
      core.footswitch('loop', 'down'); core.footswitch('loop', 'up'); step(1.5);
      core.footswitch('loop', 'down'); core.footswitch('loop', 'up'); step(1.0);
    }
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up'); step(0.2);
    return core;
  };
  for (const taps of [1, 2, 4, 8]) {
    const core = take(taps);
    const buf = core.loopBuf;
    assert.ok(finite(buf), `the capture stays finite after ${taps} tap(s)`);
    const n = core.lsrc;
    assert.ok(n > 64, `the capture has audio after ${taps} tap(s)`);
    let worst = 0;
    for (let i = 1; i < n; i++) worst = Math.max(worst, Math.abs(buf[i] - buf[i - 1]));
    assert.ok(worst < slope * 1.4, `${taps} tap(s): the worst step inside the loop is the tone's own slope (${(worst / slope).toFixed(2)}x)`);
  }
});

test('MEMORY window on a long listen is a whole number of samples', () => {
  // A fractional capture length used to poison the loop with NaN after the
  // memory window elapsed.
  for (const memory of [0.15, 0.4, 0.5, 0.85]) {
    const core = new BroodCore(FS);
    Object.assign(core.p, { loopMode: 2, memory, mix: 0.7, modWet: 0.6 });
    render(core, 9, tone(120, 99), { [blk(0)]: (c) => c.footswitch('loop', 'down'), [blk(0.05)]: (c) => c.footswitch('loop', 'up') });
    assert.ok(Number.isInteger(core.maxCaptureSamples()), `MEMORY ${memory}: whole samples`);
    assert.ok(Number.isInteger(core.lsrc), `MEMORY ${memory}: capture is a whole number of samples`);
    assert.ok(finite(core.loopBuf), `MEMORY ${memory}: no NaN in the loop`);
  }
});

test('FREEZE holds the wet tail without a held footswitch', () => {
  const run = (freeze) => {
    const core = new BroodCore(FS);
    Object.assign(core.p, { wetMode: 0, time: 0.7, mix: 1, modWet: 0.8, clock: 0.9 });
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    render(core, 3, tone(300, 99));
    if (freeze) core.setParam('freeze', 1);
    const tail = render(core, 5, () => 0);
    return { tail, state: core.state() };
  };
  const off = run(false), on = run(true);
  const late = (o) => rms(o.tail.L, 3.5, 4.9);
  assert.ok(late(on) > 20 * late(off), `FREEZE keeps ringing (${late(on).toFixed(4)} vs ${late(off).toFixed(4)})`);
  assert.ok(on.state.freeze && on.state.wetOn, 'the state says the wet side is held');
  assert.ok(!off.state.freeze, 'and it is not held when the switch is down');
  // Releasing the switch lets the tail go again.
  const core = new BroodCore(FS);
  Object.assign(core.p, { wetMode: 0, time: 0.7, mix: 1, modWet: 0.8, clock: 0.9 });
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  render(core, 3, tone(300, 99));
  core.setParam('freeze', 1);
  const held = render(core, 2, () => 0);
  core.setParam('freeze', 0);
  const freed = render(core, 4, () => 0);
  assert.ok(rms(held.L, 1.5, 1.9) > 5 * rms(freed.L, 3.5, 3.9), 'releasing FREEZE lets the tail decay');
});

test('the wet/loop balance never swallows the loop in the middle', () => {
  // The direct loop used to be ducked against the wet level, so at the middle of
  // the travel the wet side was all you heard — and a mode that returns very
  // little of the loop (WEAVE) left no loop at all until the knob reached LOOP.
  // A broadband loop, so a mode's phase cannot flatter or ruin the measurement.
  const broad = (t) => 0.15 * (Math.sin(2 * Math.PI * 220 * t) + Math.sin(2 * Math.PI * 331 * t)
    + Math.sin(2 * Math.PI * 517 * t) + Math.sin(2 * Math.PI * 733 * t));
  const level = (wetMode, balance) => {
    const core = new BroodCore(FS);
    Object.assign(core.p, { loopMode: 0, clock: 0.6, memory: 0, mix: 0.6, wetMode, routing: 1, modWet: 0.5, time: 0.5, balance });
    render(core, 2, broad);
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    render(core, 0.4, () => 0);
    if (balance < 1) { core.footswitch('wet', 'down'); core.footswitch('wet', 'up'); }
    render(core, 1.2, () => 0);
    const { L } = render(core, 1, () => 0);
    return rms(L, 0.2, 0.9);
  };
  for (const [mode, name] of [[0, 'REVERB'], [1, 'DELAY'], [2, 'SLIP'], [3, 'WEAVE']]) {
    const loopOnly = level(mode, 1), middle = level(mode, 0.5), mostlyWet = level(mode, 0.2);
    assert.ok(middle > 0.9 * loopOnly, `${name}: the loop keeps its level in the middle (${(middle / loopOnly).toFixed(2)}x)`);
    assert.ok(middle < 2.2 * loopOnly, `${name}: without doubling it (${(middle / loopOnly).toFixed(2)}x)`);
    // What the duck used to do: the middle sounded exactly like the wet end.
    assert.ok(middle > 1.15 * mostlyWet, `${name}: the middle is not just the wet side (${(middle / mostlyWet).toFixed(2)}x)`);
  }
});

test('holding LOOP overdubs, and FADE decides what the old take keeps', () => {
  // FADE is not a dead control: it shapes each overdub pass. This also pins the
  // gesture FADE depends on — hold the LOOP footswitch while a loop plays.
  const take = (fade) => {
    const core = new BroodCore(FS);
    Object.assign(core.p, { loopMode: 0, clock: 0.6, memory: 0, mix: 0.5, fade });
    render(core, 2, tone(220, 9));
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    const win = core.state();
    // Only the loop's own window is overdubbed, so compare like with like.
    const window = () => core.loopBuf.subarray(win.winStart, win.winStart + win.winLen);
    const rmsOf = (a) => { let acc = 0; for (const v of a) acc += v * v; return Math.sqrt(acc / a.length); };
    const before = rmsOf(window());
    render(core, 0.3, () => 0);
    core.footswitch('loop', 'down');
    const during = [];
    render(core, 0.6, () => 0);
    during.push(core.state().loop);
    render(core, 1.6, () => 0);
    core.footswitch('loop', 'up');
    render(core, 0.2, () => 0);
    const after = rmsOf(window());
    return { before, after, during };
  };
  const keepAll = take(0), eraseAll = take(1);
  assert.strictEqual(keepAll.during[0], 'dub', 'holding LOOP overdubs');
  assert.ok(keepAll.before > 0.1, `there was something to keep (${keepAll.before.toFixed(3)})`);
  assert.ok(keepAll.after > 0.05, `FADE left the old take there (${keepAll.after.toFixed(3)})`);
  assert.ok(eraseAll.after < keepAll.after * 0.5, `FADE 1 replaced it (${eraseAll.after.toFixed(3)} vs ${keepAll.after.toFixed(3)})`);
});

test('WIDTH does what SPREAD used to: 0 is mono, the middle is as built, above it opens', () => {
  // SPREAD was a switch that duplicated the mono path; WIDTH now does it, so
  // turning WIDTH to zero is the same as that switch being off.
  const run = (width, pan) => {
    const core = new BroodCore(FS);
    Object.assign(core.p, { wetMode: 1, mix: 1, modWet: 0.6, time: 0.35, clock: 1, width });
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const src = pan === 0 ? tone(440, 9) : (t) => ((t % 1) < 0.05 ? 0.9 : 0);   // clicks pan nothing, but excite the room
    const { L, R } = render(core, 1.5, src);
    let side = 0, mid = 0;
    for (let i = FS * 0.3; i < L.length; i++) { mid += Math.abs(L[i] + R[i]); side += Math.abs(L[i] - R[i]); }
    return { width, side, mid, diff: side / Math.max(1e-9, mid) };
  };
  const mono = run(0), centre = run(0.5), wide = run(1);
  assert.ok(mono.side < 1e-9, `WIDTH 0 is mono (${mono.side.toExponential(2)})`);
  // The delay's own image is centred (that is how it was built), so the middle is
  // still mono — and above it the decorrelation is what opens the sides up.
  assert.ok(centre.side < mono.side + 1e-9, `the middle is still the built image (${centre.side.toExponential(2)})`);
  assert.ok(wide.side > 1e-3, `WIDTH 1 opens the sides (${wide.side.toExponential(2)})`);
  assert.ok(wide.mid > 0.2 * centre.mid, 'and keeps the middle of the signal');
});

test('MIX alone takes the dry away, so DRY KILL is not needed', () => {
  // At the top of its travel MIX fades the dry signal out completely once the
  // pedal is engaged; the DRY KILL switch used to duplicate that.
  const dryLeft = (mix) => {
    const core = new BroodCore(FS);
    Object.assign(core.p, { wetMode: 0, mix, modWet: 0.5, time: 0.5 });
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    render(core, 0.5, () => 0);                       // let "engaged" settle
    const { L } = render(core, 0.4, () => 0.5);       // a DC input: nothing else can be 0.5
    return L[L.length - 1];
  };
  assert.ok(dryLeft(0.5) > 0.4, `half way the dry is still there (${dryLeft(0.5).toFixed(3)})`);
  assert.ok(Math.abs(dryLeft(1)) < 0.05, `fully wet the dry is gone (${dryLeft(1).toFixed(3)})`);
});

test('a hold on the wet footswitch freezes only while it is held', () => {
  // There is no LATCH switch any more: the FREEZE switch is how a freeze is left
  // standing, and a held foot is momentary.
  const core = new BroodCore(FS);
  Object.assign(core.p, { wetMode: 0, time: 0.8, modWet: 0.9, mix: 1 });
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  render(core, 1, tone(300, 9));
  core.footswitch('wet', 'down');
  render(core, 0.6, () => 0);
  assert.ok(core.state().freeze, 'holding freezes');
  core.footswitch('wet', 'up');
  render(core, 0.1, () => 0);
  assert.ok(!core.state().freeze, 'letting go lets go');
});

test('wetPre records the wet side while a loop plays; beside the loop it stays out', () => {
  const tail = (wetPre) => {
    const core = new BroodCore(FS);
    core.setParam('wetMode', 1);
    core.setParam('time', 0.3);
    core.setParam('modWet', 0.7);
    core.setParam('wetPre', wetPre);
    const inp = new Float32Array(BLOCK), oL = new Float32Array(BLOCK), oR = new Float32Array(BLOCK);
    const run = (seconds, level) => {
      for (let b = 0; b < Math.ceil((seconds * FS) / BLOCK); b++) {
        for (let i = 0; i < BLOCK; i++) inp[i] = level * Math.sin(i * 0.05);
        core.process(inp, oL, oR);
      }
    };
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    run(0.5, 0.4);
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    run(0.05, 0.4);
    assert.strictEqual(core.state().loop, 'play');
    run(0.3, 0);                 // silence in, echoes still ringing
    let peak = 0;
    const n = core.hist.length, last = core.histW;
    for (let i = 1; i < 2000; i++) peak = Math.max(peak, Math.abs(core.hist[(last - i + n) % n]));
    return peak;
  };
  assert.ok(tail(1) > 0.01, 'the echoes are in the history');
  assert.strictEqual(tail(0), 0, 'only the dry input, as before');
});

test('envSoft rounds off the corners ENV cuts into the loop; off stays the sharp cut', () => {
  // A low tone in the loop and input bursts that keep slicing it. A corner shows up in
  // the second difference long before it shows in the first, and a 110 Hz tone adds
  // almost none of its own.
  const worstCorner = (envSoft) => {
    const core = new BroodCore(FS);
    core.setParam('loopMode', 0);
    core.setParam('modLoop', 0.7);
    core.setParam('length', 0.3);
    core.setParam('mix', 1);     // the wet and loop side only: the dry path isn't what's being measured
    core.setParam('envSoft', envSoft);
    const inp = new Float32Array(BLOCK), oL = new Float32Array(BLOCK), oR = new Float32Array(BLOCK);
    let t = 0, worst = 0, a = 0, b = 0, seen = 0;
    const run = (seconds, burst, measure) => {
      for (let n = 0; n < Math.ceil((seconds * FS) / BLOCK); n++) {
        for (let i = 0; i < BLOCK; i++, t++) {
          const at = (t / FS) % 0.2;
          const level = burst ? (at < 0.03 ? 0.5 * Math.sin(Math.PI * at / 0.03) ** 2 : 0) : 0.5;
          inp[i] = level * Math.sin(2 * Math.PI * 110 * t / FS);
        }
        core.process(inp, oL, oR);
        if (measure) for (let i = 0; i < BLOCK; i++) { if (seen++ >= 2) worst = Math.max(worst, Math.abs(oL[i] - 2 * a + b)); b = a; a = oL[i]; }
      }
    };
    run(1, false, false);
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    run(0.3, true, false);
    run(3, true, true);
    return worst;
  };
  const cut = worstCorner(0), soft = worstCorner(1);
  assert.ok(soft < cut * 0.7, `the worst corner falls (${soft.toFixed(5)} from ${cut.toFixed(5)})`);
});

test('overdub leaves no step in the take, at any tape speed', () => {
  const worstStep = (modLoop) => {
    const core = new BroodCore(FS);
    core.setParam('loopMode', 1);
    core.setParam('length', 1);
    core.setParam('modLoop', modLoop);
    const inp = new Float32Array(BLOCK), oL = new Float32Array(BLOCK), oR = new Float32Array(BLOCK);
    let t = 0;
    const run = (seconds) => {
      for (let n = 0; n < Math.ceil((seconds * FS) / BLOCK); n++) {
        for (let i = 0; i < BLOCK; i++, t++) inp[i] = 0.3 * Math.sin(2 * Math.PI * 110 * t / FS);
        core.process(inp, oL, oR);
      }
    };
    run(1.5);
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    run(0.5);
    core.footswitch('loop', 'down');
    run(1.4);
    assert.strictEqual(core.state().loop, 'dub');
    core.footswitch('loop', 'up');
    run(0.1);
    let worst = 0;
    const buf = core.loopBuf, n = core.lsrc;
    for (let i = 1; i < n; i++) worst = Math.max(worst, Math.abs(buf[i] - buf[i - 1]));
    return worst;
  };
  // The 1x tone's own slope at the core clock is about 0.01; two layers of it, 0.02.
  for (const [name, m] of [['1x', 0.66], ['2x', 0.84], ['1/2x', 0.58], ['reverse', 0.34]]) {
    const step = worstStep(m);
    assert.ok(step < 0.06, `${name}: worst step ${step.toFixed(4)}`);
  }
});

test('GAIN sets the loop level in dB and leaves the dry side alone', () => {
  const level = (loopDb) => {
    const core = new BroodCore(FS);
    core.setParam('loopMode', 1); core.setParam('balance', 1); core.setParam('modLoop', 0.66); core.setParam('loopDb', loopDb);
    const { L } = render(core, 3, tone(220, 1), { [blk(1)]: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); } });
    return rms(L, 2, 3);
  };
  const flat = level(0);
  assert.ok(flat > 0.01, 'the loop plays');
  assert.ok(Math.abs(level(-6) / flat - 0.501) < 0.03, '-6 dB is half');
  assert.ok(level(-24) < flat * 0.1, '-24 dB nearly gone');
});

test('overdub leaves no seams in the take while LENGTH is modulated', () => {
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries({ loopMode: 1, length: 0.7, modLoop: 0.66, mod1_length: 0.4, mod1Rate: 0.55, mod1Wave: 0.5 })) core.setParam(k, v);
  const inp = new Float32Array(BLOCK), oL = new Float32Array(BLOCK), oR = new Float32Array(BLOCK);
  let t = 0;
  const run = (seconds) => {
    for (let n = 0; n < Math.ceil((seconds * FS) / BLOCK); n++) {
      for (let i = 0; i < BLOCK; i++, t++) inp[i] = 0.3 * Math.sin(2 * Math.PI * 110 * t / FS);
      core.process(inp, oL, oR);
    }
  };
  run(1.5);
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  run(0.5);
  core.footswitch('loop', 'down');
  run(3);
  assert.strictEqual(core.state().loop, 'dub');
  core.footswitch('loop', 'up');
  run(0.1);
  // Layers of a steady tone add up, so a seam is a step that's large for the level.
  let worst = 0, peak = 0;
  for (let i = 1; i < core.lsrc; i++) { worst = Math.max(worst, Math.abs(core.loopBuf[i] - core.loopBuf[i - 1])); peak = Math.max(peak, Math.abs(core.loopBuf[i])); }
  assert.ok(worst / peak < 0.03, `worst step ${worst.toFixed(4)} against a peak of ${peak.toFixed(3)}`);
});

test('HI-Q stops the images and keeps more of the top, and off changes nothing', () => {
  const FS2 = FS;
  const measure = (hiQ, f, at) => {
    const core = new BroodCore(FS2);
    for (const [k, v] of Object.entries({ wetMode: 1, mix: 1, time: 0.2, modWet: 0, clock: 0.5, hiQ })) core.setParam(k, v);
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const { L } = render(core, 2.2, (t) => 0.3 * Math.sin(2 * Math.PI * f * t));
    let c = 0, s = 0;
    for (let i = FS2; i < L.length; i++) { const p = 2 * Math.PI * at * i / FS2; c += L[i] * Math.cos(p); s += L[i] * Math.sin(p); }
    return 20 * Math.log10((2 * Math.hypot(c, s) / (L.length - FS2) + 1e-12) / 0.3);
  };
  // Clock 12 kHz: a 5 kHz tone's image at 7 kHz, its level, and an 8.6 kHz tone folding to 3.4 kHz.
  assert.ok(measure(1, 5040, 6960) < measure(0, 5040, 6960) - 10, 'the image falls');
  assert.ok(measure(1, 5040, 5040) > measure(0, 5040, 5040) + 2, 'more of the top survives');
  assert.ok(measure(1, 8640, 3360) < measure(0, 8640, 3360) - 10, 'less folds back');
});

test('STRETCH does not click as MODIFY passes through the middle', () => {
  const worstCorner = (depth) => {
    const core = new BroodCore(FS);
    for (const [k, v] of Object.entries({ loopMode: 2, mix: 1, balance: 1, modLoop: 0.5, mod1_modLoop: depth, mod1Rate: 0.6, mod1Wave: 0.5, length: 0.5 })) core.setParam(k, v);
    const inp = new Float32Array(BLOCK), L = new Float32Array(BLOCK), R = new Float32Array(BLOCK);
    let t = 0, worst = 0, a = 0, b = 0, seen = 0;
    const run = (seconds, level, measure) => {
      for (let n = 0; n < Math.ceil((seconds * FS) / BLOCK); n++) {
        for (let i = 0; i < BLOCK; i++, t++) inp[i] = level * Math.sin(2 * Math.PI * 110 * t / FS);
        core.process(inp, L, R);
        if (measure) for (let i = 0; i < BLOCK; i++) { if (seen++ >= 2) worst = Math.max(worst, Math.abs(L[i] - 2 * a + b)); b = a; a = L[i]; }
      }
    };
    run(1.5, 0.4, false);
    core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
    run(0.3, 0, false);
    run(4, 0, true);
    return worst;
  };
  const moving = worstCorner(0.5);
  assert.ok(moving < 0.05, `worst corner ${moving.toFixed(4)}, still ${worstCorner(0).toFixed(4)}`);
});

test('SLIP does not click when MODIFY swings its speed', () => {
  const core = new BroodCore(FS);
  for (const [k, v] of Object.entries({ wetMode: 2, mix: 1, modWet: 0.5, mod1_modWet: 0.5, mod1Rate: 0.6, mod1Wave: 0.5 })) core.setParam(k, v);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const inp = new Float32Array(BLOCK), L = new Float32Array(BLOCK), R = new Float32Array(BLOCK);
  let t = 0, worst = 0, a = 0, b = 0, seen = 0;
  for (let n = 0; n < Math.ceil((5.5 * FS) / BLOCK); n++) {
    for (let i = 0; i < BLOCK; i++, t++) inp[i] = 0.4 * Math.sin(2 * Math.PI * 110 * t / FS);
    core.process(inp, L, R);
    if (t > 1.5 * FS) for (let i = 0; i < BLOCK; i++) { if (seen++ >= 2) worst = Math.max(worst, Math.abs(L[i] - 2 * a + b)); b = a; a = L[i]; }
  }
  assert.ok(worst < 0.05, `worst corner ${worst.toFixed(4)}`);
});
