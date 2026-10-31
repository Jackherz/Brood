'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { MoodCore, speedMap, slipSpeedMap, stretchMap, clockHz, syncLabel, SYNC_DIVISIONS, CORE_FS } = require('../src/mood-worklet.js');

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
  const core = new MoodCore(FS);
  const { L, R } = render(core, 0.5, tone(220, 1));
  const src = tone(220, 1);
  for (let i = 1000; i < 20000; i += 97) {
    assert.ok(Math.abs(L[i] - src(i / FS)) < 1e-6);
    assert.ok(Math.abs(R[i] - src(i / FS)) < 1e-6);
  }
});

test('bypass remains transparent above nominal full scale', () => {
  const core = new MoodCore(FS);
  const src = (t) => 1.4 * Math.sin(2 * Math.PI * 220 * t);
  const { L } = render(core, 0.2, src);
  for (let i = 0; i < 9000; i += 73) assert.ok(Math.abs(L[i] - src(i / FS)) < 1e-6);
});

test('delay repeats at the TIME setting, and half clock doubles it', () => {
  const measure = (clock) => {
    const core = new MoodCore(FS);
    core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('dryKill', 1);
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
  const delay = new MoodCore(FS).delay;
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
    const core = new MoodCore(rate);
    core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('dryKill', 1); core.setParam('clock', 1);
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
    const core = new MoodCore(FS);
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
    const reverb = new MoodCore(FS).reverb;
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
  const core = new MoodCore(FS);
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
  const core = new MoodCore(FS);
  core.setParam('wetMode', 1); core.setParam('modWet', 1); core.setParam('time', 0.2);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const { L } = render(core, 6, tone(440, 0.3));
  assert.ok(finite(L));
  assert.ok(L.every((v) => Math.abs(v) <= 1));
});

test('slip produces pitch-shifted slices in all directions', () => {
  for (const m of [0, 0.15, 0.4, 1]) {
    const core = new MoodCore(FS);
    core.setParam('wetMode', 2); core.setParam('modWet', m); core.setParam('dryKill', 1);
    core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
    const { L } = render(core, 2, tone(220, 2));
    assert.ok(finite(L));
    assert.ok(rms(L, 1, 2) > 0.02, `slip output m=${m}`);
  }
});

test('tapping the loop switch captures what was just played', () => {
  const core = new MoodCore(FS);
  core.setParam('dryKill', 1); core.setParam('mix', 1);
  // play a tone for 1 s, then silence; tap at 1.0 s
  const { L } = render(core, 3, tone(330, 1), {
    [blk(1.0)]: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); },
  });
  assert.strictEqual(core.state().loop, 'play');
  assert.ok(rms(L, 2, 3) > 0.1, 'loop keeps playing after input stops');
});

test('holding the loop switch records a fresh loop of that length', () => {
  const core = new MoodCore(FS);
  core.setParam('length', 1);
  render(core, 1.5, tone(330, 3), {
    [blk(0.5)]: (c) => c.footswitch('loop', 'down'),
    [blk(1.3)]: (c) => c.footswitch('loop', 'up'),
  });
  const secs = core.lsrc / CORE_FS;
  assert.ok(Math.abs(secs - 0.8) < 0.01, `loop length ${secs}s`);
});

test('a hold longer than loop capacity captures the newest full buffer', () => {
  const core = new MoodCore(FS);
  core.setParam('clock', 1); core.s.clock = 1;
  render(core, 2.1, tone(330, 3), {
    [blk(0.2)]: (c) => c.footswitch('loop', 'down'),
    [blk(1.8)]: (c) => c.footswitch('loop', 'up'),
  });
  assert.equal(core.state().loop, 'play');
  assert.equal(core.lsrc, 63999);
});

test('tap while playing stops the loop; hold while playing overdubs', () => {
  const core = new MoodCore(FS);
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
    const core = new MoodCore(FS);
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
    const core = new MoodCore(FS);
    core.setParam('clock', knob); core.s.clock = knob;
    render(core, secs + 0.5, tone(330, 100), {
      [blk(secs + 0.4)]: (c) => { c.footswitch('loop', 'down'); c.footswitch('loop', 'up'); },
    });
    assert.strictEqual(core.lsrc, 63999, `clock ${knob}`);
  }
});

test('ENV mode: playing interrupts the loop with a repeating slice', () => {
  const core = new MoodCore(FS);
  core.setParam('loopMode', 0); core.setParam('modLoop', 0.8); core.setParam('length', 0.2);
  core.setParam('dryKill', 1); core.setParam('mix', 1);
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
  const core = new MoodCore(FS);
  core.setParam('wetMode', 1); core.setParam('routing', 2); core.setParam('mix', 1); core.setParam('dryKill', 1);
  core.setParam('time', 0.15);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const { L } = render(core, 1.5, tone(220, 1));
  assert.ok(rms(L, 0.5, 1) > 0.01, 'wet receives input even in LOOP routing when looper is off');
});

test('listening history includes wet ambience and overdubs use clean input', () => {
  const run = (wet) => {
    const core = new MoodCore(FS);
    core.setParam('wetMode', 1); core.setParam('time', 0.2); core.setParam('modWet', 0.9);
    if (wet) { core.footswitch('wet', 'down'); core.footswitch('wet', 'up'); }
    render(core, 1.0, tone(330, 0.15));
    let energy = 0;
    // Recent history after the dry tone has stopped should contain the wet repeats.
    for (let i = 35000; i < 60000; i++) energy += core.hist[i] * core.hist[i];
    return energy;
  };
  assert.ok(run(true) > run(false) + 0.1, 'wet delay is captured by the listening looper');
});

test('WEAVE keeps three threads audible, repeatable and knob-responsive', () => {
  const run = (time, modify) => {
    const core = new MoodCore(FS);
    core.setParam('wetMode', 3); core.setParam('mix', 1); core.setParam('dryKill', 1);
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
  const core = new MoodCore(FS);
  core.setParam('wetMode', 3); core.setParam('mix', 1); core.setParam('modWet', 1);
  core.setParam('time', 1); core.setParam('clock', 0); core.setParam('spread', 1);
  core.footswitch('wet', 'down'); core.footswitch('wet', 'up');
  const { L, R } = render(core, 2, tone(196, 5));
  for (let i = 0; i < L.length; i++) {
    assert.ok(Number.isFinite(L[i]) && Number.isFinite(R[i]), 'finite');
    assert.ok(Math.abs(L[i]) <= 1 && Math.abs(R[i]) <= 1, 'bounded');
  }
});

test('tempo sync puts the delay on the musical division, at any clock', () => {
  const echoAt = (time, bpm, clock) => {
    const core = new MoodCore(FS);
    core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('dryKill', 1); core.setParam('modWet', 0.5);
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
  const core = new MoodCore(FS);
  core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('dryKill', 1); core.setParam('modWet', 0.5);
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
    const core = new MoodCore(FS);
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
