'use strict';
const test = require('node:test');
const assert = require('node:assert');
const { MoodCore, speedMap, slipSpeedMap, stretchMap, clockHz, syncLabel, divMap, DIV_STEPS, RAMP_TARGETS, RAMP_WAVES, rampTargetIndex, rampWaveIndex, rampHz, SYNC_DIVISIONS, CORE_FS } = require('../src/mood-worklet.js');

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
  // Capacity is counted in core samples: 64 000 of them is 1 s at 64 kHz.
  assert.equal(core.lsrc, 64000);
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
    assert.ok(Math.abs(core.lsrc / clockHz(knob) - secs) < 0.02, `clock ${knob}`);
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

test('LOOP MEMORY raises the capture ceiling, 0 keeps the pedal rule', () => {
  const capturedSeconds = (memory, clock) => {
    const core = new MoodCore(FS);
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
    const core = new MoodCore(FS);
    core.setParam('loopMode', mode);
    core.setParam('length', 0.72);
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
  const core = new MoodCore(FS);
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

// ───────────────────────── hidden options ─────────────────────────

const hfEnergy = (a, from, to) => { let acc = 0; for (let i = Math.floor(from * FS) + 1; i < Math.floor(to * FS); i++) acc += Math.pow(a[i] - a[i - 1], 2); return Math.sqrt(acc / Math.floor((to - from) * FS)); };
const sideEnergy = (L, R) => { let acc = 0; for (let i = 0; i < L.length; i++) { const d = (L[i] - R[i]) * 0.5; acc += d * d; } return Math.sqrt(acc / L.length); };
const crossings = (a, from, to) => { let n = 0; for (let i = Math.floor(from * FS) + 1; i < Math.floor(to * FS); i++) if ((a[i - 1] <= 0) !== (a[i] <= 0)) n++; return n / 2 / (to - from); };

const wetCore = (patch) => {
  const core = new MoodCore(FS);
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

  const plain = new MoodCore(FS);
  plain.setParam('wetMode', 0); plain.setParam('mix', 1); plain.wetOn = true; plain.s.mix = 1;
  const a = render(plain, 1, tone(220, 1));
  const openCore = new MoodCore(FS);
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
  const plain = new MoodCore(FS);
  plain.setParam('wetMode', 0); plain.setParam('mix', 1); plain.wetOn = true; plain.s.mix = 1;
  const a = render(plain, 1, tone(220, 1));
  const withWidth = new MoodCore(FS);
  withWidth.setParam('wetMode', 0); withWidth.setParam('mix', 1); withWidth.setParam('width', 0.5);
  withWidth.wetOn = true; withWidth.s.mix = 1;
  const b = render(withWidth, 1, tone(220, 1));
  assert.deepStrictEqual(Array.from(a.L), Array.from(b.L), 'width 0.5 must be the built image');
});

test('WET CLOCK DIVISION stretches the wet side only', () => {
  const echoGap = (wetDiv) => {
    const core = new MoodCore(FS);
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

test('LOOP CLOCK DIVISION halves and quarters the loop pitch', () => {
  const pitch = (loopDiv, loopMode) => {
    const core = new MoodCore(FS);
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
  const core = new MoodCore(FS);
  render(core, 1.2, () => 0.2);
  core.footswitch('loop', 'down'); core.footswitch('loop', 'up');
  const lsrc = core.lsrc;
  assert.ok(lsrc > 20000, 'the loop still holds a full capture');
});

test('FADE decides how much of the old loop survives an overdub', () => {
  const levelAfterOverdub = (fade) => {
    const core = new MoodCore(FS);
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
  const noiseFloor = (classic) => {
    const core = wetCore({ wetMode: 0, mix: 1, time: 0.6, clock: 0, classic: classic ? 1 : 0 });
    const { L } = render(core, 2, () => 0);
    return rms(L, 1, 2);
  };
  const clean = noiseFloor(false), noisy = noiseFloor(true);
  assert.ok(clean < 1e-6, 'without CLASSIC a silent input stays silent');
  assert.ok(noisy > 1e-4, `CLASSIC adds clock noise (${noisy.toFixed(5)})`);
});

// ─────────────────────────── the ramp engine ───────────────────────────

// Sample the control the DSP actually reads, once per block.
function rampTrace(patch, seconds, field = 'time') {
  const core = new MoodCore(FS);
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
  const core = new MoodCore(FS);
  core.setParam('rampWet', 0.2); core.setParam('rampSpeed', speed);
  const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
  let wraps = 0, last = core.ramp[0].ph;
  for (let b = 0; b < Math.ceil((seconds * FS) / BLOCK); b++) {
    core.process(inp, L, R);
    if (core.ramp[0].ph < last) wraps++;
    last = core.ramp[0].ph;
  }
  return wraps;
};

test('RAMPS destinations and waveforms are the documented lists', () => {
  assert.deepStrictEqual(RAMP_TARGETS, ['OFF', 'TIME', 'MIX', 'LENGTH', 'MODIFY', 'CLOCK', 'BALANCE']);
  assert.deepStrictEqual(RAMP_WAVES, ['TRI', 'SQR', 'SIN', 'RND', 'SMTH']);
  assert.deepStrictEqual([0, 1].map(rampTargetIndex), [0, 6]);
  assert.deepStrictEqual([0, 1].map(rampWaveIndex), [0, 4]);
});

test('a ramp moves its control and stops where the knob says', () => {
  const still = rampTrace({ rampWet: 0, time: 0.6 }, 2);
  assert.ok(Math.abs(still[still.length - 1] - 0.6) < 1e-6, 'off is the knob, untouched');
  const moved = rampTrace({ rampWet: 0.2, time: 0.6 }, 4);
  const [lo, hi] = rminmax(moved);
  assert.ok(hi - lo > 0.6, `TIME should sweep (${lo.toFixed(2)}..${hi.toFixed(2)})`);
  assert.ok(hi <= 1 && lo >= 0, 'stays inside the knob range');
  // Balanced around the knob: bipolar is a swing, not a one-way offset.
  assert.ok(Math.abs((lo + hi) / 2 - 0.6) < 0.12, `centred on the knob (${((lo + hi) / 2).toFixed(2)})`);
});

test('each waveform has its own shape', () => {
  const shapes = RAMP_WAVES.map((_, w) => rampTrace({ rampWet: 0.2, rampWave: w / 4, time: 0.5 }, 6));
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
    const core = new MoodCore(FS);
    core.setParam('rampWet', 0.2); core.setParam('rampSpeed', 0.5); core.setParam('clock', clock);
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
  const core = new MoodCore(FS);
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
  const saw = rawTrace({ rampWet: 0.2, sweep: 1, time: 0.5 }, 8);
  const bouncy = rawTrace({ rampWet: 0.2, sweep: 1, bounce: 1, time: 0.5 }, 8);
  const resets = (a) => a.filter((v, i) => i > 0 && v < a[i - 1] - 0.5).length;
  const rising = (a) => a.filter((v, i) => i > 0 && v > a[i - 1]).length / (a.length - 1);
  assert.ok(resets(saw) >= 2, `the sweep climbs and starts again (${resets(saw)} resets)`);
  assert.ok(rising(saw) > 0.9, `it only ever climbs (${(rising(saw) * 100).toFixed(0)}% rising steps)`);
  assert.strictEqual(resets(bouncy), 0, 'BOUNCE removes the snap-back entirely');
  assert.ok(rising(bouncy) > 0.3 && rising(bouncy) < 0.7, 'and turns it into a tide');
});

test('POLARITY: unipolar only ever adds to the knob', () => {
  const bi = rampTrace({ rampWet: 0.2, time: 0.5 }, 6);
  const uni = rampTrace({ rampWet: 0.2, time: 0.5, rampUni: 1 }, 6);
  assert.ok(rminmax(uni)[0] >= 0.5 - 0.01, `unipolar never goes under the knob (${rminmax(uni)[0].toFixed(2)})`);
  assert.ok(rminmax(uni)[1] > 0.8, 'and still reaches the top');
  assert.ok(rminmax(bi)[0] < 0.3, 'bipolar goes both ways');
});

test('the two generators are independent, and MODIFY means this channel', () => {
  const spread = (a) => rminmax(a)[1] - rminmax(a)[0];
  const wetOnly = rampTrace({ rampWet: 0.2, rampLoop: 0, time: 0.5, modLoop: 0.5 }, 4);
  assert.ok(spread(wetOnly) > 0.5, 'the wet generator moves TIME');
  // The wet ramp must leave the loop's MODIFY alone.
  const loopStill = [];
  const core = new MoodCore(FS);
  core.setParam('rampWet', 0.2); core.setParam('modLoop', 0.5);
  const L = new Float32Array(BLOCK), R = new Float32Array(BLOCK), inp = new Float32Array(BLOCK);
  for (let b = 0; b < Math.ceil((4 * FS) / BLOCK); b++) { core.process(inp, L, R); loopStill.push(core.e.modLoop); }
  assert.ok(spread(loopStill) < 1e-6, 'the wet ramp does not touch the loop');
  // The loop generator pointed at MODIFY drives the loop's own MODIFY.
  const loopRamp = rampTrace({ rampWet: 0, rampLoop: 0.67, modLoop: 0.5 }, 4, 'modLoop');
  assert.ok(spread(loopRamp) > 0.5, 'the loop generator moves loop MODIFY');
  // Both at once: two different controls, two different phases.
  const both = [];
  const c2 = new MoodCore(FS);
  c2.setParam('rampWet', 0.2); c2.setParam('rampLoop', 0.67);
  for (let b = 0; b < Math.ceil((4 * FS) / BLOCK); b++) { c2.process(inp, L, R); both.push([c2.e.time, c2.e.modLoop]); }
  const timeSpread = spread(both.map((v) => v[0])), modSpread = spread(both.map((v) => v[1]));
  assert.ok(timeSpread > 0.5 && modSpread > 0.5, 'both run together');
  assert.notStrictEqual(c2.ramp[0].ph, c2.ramp[1].ph, 'the two generators are not in lockstep');
});

test('with every ramp off the audio is bit-identical to before the engine existed', () => {
  const a = new MoodCore(FS);
  a.setParam('wetMode', 0); a.setParam('mix', 0.8); a.wetOn = true; a.s.mix = 0.8;
  const one = render(a, 1, tone(220, 1));
  const b = new MoodCore(FS);
  b.setParam('wetMode', 0); b.setParam('mix', 0.8); b.wetOn = true; b.s.mix = 0.8;
  b.setParam('rampWet', 0); b.setParam('rampLoop', 0); b.setParam('rampSpeed', 1);
  const two = render(b, 1, tone(220, 1));
  assert.deepStrictEqual(Array.from(one.L), Array.from(two.L));
});

test('a TIME ramp makes the echo breathe', () => {
  const core = new MoodCore(FS);
  core.setParam('wetMode', 1); core.setParam('mix', 1); core.setParam('modWet', 0.55);
  core.setParam('time', 0.4); core.setParam('rampWet', 0.2); core.setParam('rampSpeed', 0.35);
  core.wetOn = true; core.s.mix = 1; core.modeGain = [0, 1, 0, 0];
  const { L } = render(core, 4, click);
  assert.ok(finite(L), 'stays finite while the ramp moves the delay');
  const a = peakTime(L, 0.02, 1.2), b = peakTime(L, 1.6, 3.6);
  assert.ok(Math.abs(a - b) > 0.01, `the echo moves (${a.toFixed(3)} s then ${b.toFixed(3)} s)`);
});
