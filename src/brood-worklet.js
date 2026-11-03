// Brood DSP core.
//
// Loads in an AudioWorkletGlobalScope (registers 'brood-processor') and in Node
// (exports BroodCore for offline tests and rendering).
//
// Signal flow
//   input ─┬───────────────────────────────────────────── dry ──┐
//          └─ anti-alias LPF ─► [ CORE @ clock rate ] ─► reconstruction LPF ─► mix ─► out L/R
//
// Everything inside the core runs at a variable virtual CLOCK rate (2–64 kHz),
// like the pedal's CLOCK knob: lower clock = longer delays/loops, lower
// bandwidth, and anything already in a buffer shifts down in pitch. All times
// below are expressed in core samples at full clock (seconds * fs).
'use strict';

const TAU = Math.PI * 2;
const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
const expMap = (t, a, b) => a * Math.pow(b / a, t);

const HOLD_SEC = 0.35;        // footswitch hold threshold
// Micro-looper buffer is a fixed number of samples, so CLOCK sets loop length:
// 64 000 samples = 1 s at 64 kHz … 32 s at 2 kHz (MKII doubled the MKI's 0.5–16 s).
const LOOP_SAMPLES = 64000;      // pedal capacity, in core samples (1 s @ 64 kHz … 32 s @ 2 kHz)
// The buffer is allocated for the longest loop the panel allows; MEMORY caps how
// much of it a capture may use, so the pedal default still applies at 0.
const MAX_LOOP_SECONDS = 32;
const MAX_LOOP_SAMPLES = LOOP_SAMPLES * MAX_LOOP_SECONDS;
const CLOCK_MIN_HZ = 2000;
const CLOCK_MAX_HZ = 64000;
const CORE_FS = 64000;       // buffer timings are defined at the pedal’s 64 kHz clock
const CLOCK_STEPS = [2000, 3000, 4000, 6000, 8000, 12000, 16000, 24000, 32000, 48000, 64000];
const DELAY_MAX_SEC = 2.1;
const SLIP_BUF_SEC = 5.5;
const WEAVE_BUF_SEC = 3.0;   // Fieldtone Weaver captures a couple of seconds; three gives the threads room
const WEAVE_PITCH = [0, 7, -5, 12, -12, 4, -17, 19];   // root-sympathetic intervals, semitones

// Tempo sync for time-based effects. The knob becomes a musical division
// instead of a free time; the host supplies the tempo in the plugin and the
// front panel supplies it in the browser.
const SYNC_DIVISIONS = [
  { beats: 1 / 8, label: '1/32' }, { beats: 1 / 6, label: '1/16T' }, { beats: 1 / 4, label: '1/16' },
  { beats: 1 / 3, label: '1/8T' }, { beats: 1 / 2, label: '1/8' }, { beats: 3 / 4, label: '1/8D' },
  { beats: 1, label: '1/4' }, { beats: 3 / 2, label: '1/4D' }, { beats: 2, label: '1/2' }, { beats: 4, label: '1/1' },
];
const syncIndex = (knob) => Math.round(clamp(knob, 0, 1) * (SYNC_DIVISIONS.length - 1));
const syncLabel = (knob) => SYNC_DIVISIONS[syncIndex(knob)].label;
// How long the division is at this tempo, before anything clamps it to a block's
// own limit: the delay clamps to its line, WEAVE to its buffer, ENV to the take.
const syncSecondsRaw = (bpm, knob) => SYNC_DIVISIONS[syncIndex(knob)].beats * 60 / clamp(bpm, 20, 300);
const syncSeconds = (bpm, knob) => clamp(syncSecondsRaw(bpm, knob), 0.01, DELAY_MAX_SEC * 0.95);

// The modulation generators sync to lengths rather than rates: one cycle is a
// musical span, from eight bars down to a sixteenth. Beats per cycle, so a slower
// tempo makes a longer cycle, exactly like the delay's divisions.
const RAMP_DIV_LABELS = ['8 BARS', '4 BARS', '2 BARS', '1 BAR', '1/2', '1/4', '1/8', '1/16'];
const RAMP_DIV_BEATS = [32, 16, 8, 4, 2, 1, 0.5, 0.25];
const rampDivIndex = (knob) => Math.round(clamp(knob, 0, 1) * (RAMP_DIV_BEATS.length - 1));
const rampDivLabel = (knob) => RAMP_DIV_LABELS[rampDivIndex(knob)];
const rampCycleSeconds = (bpm, knob) => RAMP_DIV_BEATS[rampDivIndex(knob)] * 60 / clamp(bpm, 20, 300);
// Hidden options (on the pedal you hold both footswitches to reach these).
// The two sides can run on different clock divisions, so the wet side and the
// loop can drift apart in time and pitch.
const DIV_STEPS = [1, 2, 4];
const divMap = (knob) => DIV_STEPS[Math.round(clamp(knob, 0, 1) * (DIV_STEPS.length - 1))];
// TONE is a hi-cut on the wet bus: a fixed 2.6 kHz one-pole, blended in by amount.
const TONE_HZ = 2600;

// ───── RAMPS ─────
// Each channel has a function generator that can drive one control. The first
// position is off; MODIFY means this channel's own MODIFY knob.
const RAMP_TARGETS = ['OFF', 'TIME', 'MIX', 'LENGTH', 'MODIFY', 'CLOCK', 'BALANCE'];
const RAMP_KEYS = ['', 'time', 'mix', 'length', 'modWet', 'clock', 'balance'];
const RAMP_WAVES = ['TRI', 'SQR', 'SIN', 'RND', 'SMTH'];
const rampTargetIndex = (k) => Math.round(clamp(k, 0, 1) * (RAMP_TARGETS.length - 1));
const rampWaveIndex = (k) => Math.round(clamp(k, 0, 1) * (RAMP_WAVES.length - 1));
const rampHz = (k) => expMap(clamp(k, 0, 1), 0.05, 8);   // one cycle every 20 s … eight a second
const RAMP_DEPTH = 0.5;

const clockHz = (knob, smooth = false) => smooth
  ? expMap(clamp(knob, 0, 1), CLOCK_MIN_HZ, CLOCK_MAX_HZ)
  : CLOCK_STEPS[Math.round(clamp(knob, 0, 1) * (CLOCK_STEPS.length - 1))];

// MKII tape: octave steps, with reverse on the left and forward on the right.
function speedMap(m) {
  const distance = Math.min(1, Math.abs(m - 0.5) * 2);
  return (m < 0.5 ? -1 : 1) * Math.pow(2, Math.round(-1 + 3 * distance));
}

// Slip moves in semitone steps over approximately two octaves.
function slipSpeedMap(m) {
  const distance = Math.min(1, Math.abs(m - 0.5) * 2);
  return (m < 0.5 ? -1 : 1) * Math.pow(2, Math.round((-1 + 2 * distance) * 12) / 12);
}

// STRETCH: noon freezes; clockwise full is normal forward playback.
function stretchMap(m) {
  const d = m - 0.5;
  if (Math.abs(d) < 0.03) return 0;
  const t = (Math.abs(d) - 0.03) / 0.47;
  return Math.sign(d) * Math.pow(2, -3 + 3 * t);
}

// Transposed direct form II biquad, low-pass only.
class LowPass {
  constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
  set(fc, fs, q) {
    const w = (TAU * clamp(fc, 10, fs * 0.49)) / fs;
    const cw = Math.cos(w), alpha = Math.sin(w) / (2 * q), a0 = 1 + alpha;
    this.b0 = (1 - cw) / 2 / a0; this.b1 = (1 - cw) / a0; this.b2 = this.b0;
    this.a1 = (-2 * cw) / a0; this.a2 = (1 - alpha) / a0;
  }
  run(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
}

// Butterworth low-pass, 6th order (three biquads) or, for CLASSIC, the 4th order
// the clock filters used to be. The steeper one keeps far less of the images a
// low CLOCK leaves above its band; CLASSIC keeps that grit on purpose.
class LowPassN {
  constructor() { this.a = new LowPass(); this.b = new LowPass(); this.c = new LowPass(); this.sixth = true; }
  set(fc, fs, sixth) {
    this.sixth = sixth;
    if (sixth) { this.a.set(fc, fs, 0.5176); this.b.set(fc, fs, 0.7071); this.c.set(fc, fs, 1.9319); }
    else { this.a.set(fc, fs, 0.5412); this.b.set(fc, fs, 1.3066); }
  }
  run(x) {
    const y = this.b.run(this.a.run(x));
    return this.sixth ? this.c.run(y) : y;
  }
}

// Four-point, third-order Hermite: x0..x1 is the span, xm1 and x2 its neighbours.
// Linear interpolation dulls the top and leaves images of everything a read head
// slides over; this is the cheapest curve that is smooth through the points.
function hermite(xm1, x0, x1, x2, f) {
  const c1 = 0.5 * (x1 - xm1);
  const c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2;
  const c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
  return ((c3 * f + c2) * f + c1) * f + x0;
}

class Allpass {
  constructor(len, g) { this.buf = new Float32Array(len); this.i = 0; this.g = g; }
  run(x) {
    const d = this.buf[this.i];
    const v = x + this.g * d;
    this.buf[this.i] = v;
    if (++this.i >= this.buf.length) this.i = 0;
    return d - this.g * v;
  }
}

// Fractional read from a circular buffer, Hermite-interpolated.
function readFrac(buf, pos) {
  const n = buf.length;
  let p = pos % n; if (p < 0) p += n;
  const i0 = Math.floor(p), f = p - i0;
  const im1 = i0 === 0 ? n - 1 : i0 - 1;
  const i1 = i0 + 1 >= n ? i0 + 1 - n : i0 + 1;
  const i2 = i0 + 2 >= n ? i0 + 2 - n : i0 + 2;
  return hermite(buf[im1], buf[i0], buf[i1], buf[i2], f);
}

// ───────────────────────── Wet channel: REVERB ─────────────────────────
class Reverb {
  constructor(fs) {
    const k = fs / 48000;
    const lens = [1031, 1327, 1523, 1789, 2053, 2311, 2617, 2963].map((l) => Math.round(l * k));
    this.lines = lens.map((l) => new Float32Array(l + 64));
    this.len = lens;
    this.w = new Int32Array(8);
    this.lp = new Float32Array(8);
    this.modPh = new Float64Array(8).map((_, i) => i * 0.77);
    this.modRate = [0.31, 0.43, 0.53, 0.61, 0.37, 0.47, 0.57, 0.67].map((r) => (TAU * r) / fs);
    this.diff = [142, 107, 379, 277].map((l) => new Allpass(Math.round(l * k), 0.7));
    this.fs = fs;
    this.g = new Float32Array(8);
    this.t60 = -1;
    this.size = 1;
    this.out = [0, 0];
    this.x = new Float32Array(8);
  }
  setDecay(t60, size) {
    if (Math.abs(t60 - this.t60) < 1e-4 && Math.abs(size - this.size) < 1e-4) return;
    this.t60 = t60; this.size = size;
    for (let i = 0; i < 8; i++) this.g[i] = Math.pow(10, (-3 * this.len[i] * size) / (t60 * this.fs));
  }
  // modify = smear: CCW clear and still, CW washed out (more input diffusion,
  // deeper delay modulation, softer highs)
  tick(input, modify, freeze, infinite) {
    let d = input * 0.35;
    const dg = 0.45 + 0.3 * modify;
    for (let i = 0; i < 4; i++) { this.diff[i].g = dg; d = this.diff[i].run(d); }
    const damp = freeze ? 1 : 1 - Math.exp((-TAU * expMap(modify, 9000, 3500)) / this.fs);
    const depth = 1 + modify * modify * 18 * (this.fs / 48000);
    const x = this.x;
    let sum = 0;
    for (let i = 0; i < 8; i++) {
      const line = this.lines[i];
      this.modPh[i] += this.modRate[i];
      if (this.modPh[i] > TAU) this.modPh[i] -= TAU;
      const delay = this.len[i] * this.size - depth * (1 + Math.sin(this.modPh[i]));
      const v = readFrac(line, this.w[i] - delay);
      this.lp[i] += damp * (v - this.lp[i]);
      const g = freeze || infinite ? 1 : this.g[i];
      x[i] = this.lp[i] * g;
      sum += x[i];
    }
    const h = sum * 0.25; // Householder reflection: x - (2/N)·Σx
    let l = 0, r = 0;
    for (let i = 0; i < 8; i++) {
      const y = x[i] - h;
      const line = this.lines[i];
      line[this.w[i]] = y + (freeze ? 0 : d);
      if (++this.w[i] >= line.length) this.w[i] = 0;
      if (i & 1) r += i & 2 ? -x[i] : x[i];
      else l += i & 2 ? -x[i] : x[i];
    }
    this.out[0] = l * 0.6;
    this.out[1] = r * 0.6;
    return this.out;
  }
}

// ───────────────────────── Wet channel: DELAY ─────────────────────────
class Delay {
  constructor(fs) {
    this.fs = fs;
    this.buf = new Float32Array(Math.ceil(DELAY_MAX_SEC * fs) + 8);
    this.w = 0;
    this.d = -1; // active read head; the first tick snaps to the knob
    this.nextD = -1; this.fade = 1;
    this.lp = 0; this.hp = 0;
    this.lpA = 1 - Math.exp((-TAU * 6500) / fs);
    this.hpA = 1 - Math.exp((-TAU * 70) / fs);
  }
  // At maximum feedback the delay repeats stably, like a looping delay.
  tick(input, delaySamples, modify, freeze) {
    if (this.d < 0) this.d = delaySamples;
    // MKII delay time changes crossfade between fixed read heads rather than
    // sweeping one head through the buffer (which would bend the pitch).
    if (this.fade >= 1 && Math.abs(delaySamples - this.d) > this.fs * 0.0005) {
      this.nextD = delaySamples; this.fade = 0;
    }
    let y = readFrac(this.buf, this.w - this.d);
    if (this.fade < 1) {
      const next = readFrac(this.buf, this.w - this.nextD);
      y += (next - y) * this.fade;
      this.fade += 1 / (this.fs * 0.03);
      if (this.fade >= 1) { this.d = this.nextD; this.fade = 1; }
    }
    this.lp += this.lpA * (y - this.lp);
    this.hp += this.hpA * (this.lp - this.hp);
    const fbSig = freeze || modify > 0.985 ? y : this.lp - this.hp;
    const fb = freeze || modify > 0.985 ? 1 : modify;
    let v = (freeze ? 0 : input) + fbSig * fb;
    v = v > 1 || v < -1 ? Math.tanh(v) : v;
    this.buf[this.w] = v;
    if (++this.w >= this.buf.length) this.w = 0;
    return y;
  }
}

// ───────────────────────── Wet channel: SLIP ──────────────────────────
// Repeating slices of recent audio replayed at another speed/direction.
class Slip {
  constructor(fs) {
    this.fs = fs;
    this.buf = new Float32Array(Math.ceil(SLIP_BUF_SEC * fs));
    this.w = 0;
    this.heads = [{ pos: 0, g: 0, dir: 0 }, { pos: 0, g: 0, dir: 0 }];
    this.cur = 0;
    this.count = 1e9;
    this.lastStart = 0;
    this.speed = 1;
  }
  tick(input, sliceLen, speedTarget, freeze) {
    const n = this.buf.length;
    if (!freeze) {
      this.buf[this.w] = input;
      if (++this.w >= n) this.w = 0;
    }
    this.speed += (speedTarget - this.speed) * 0.002;
    const s = this.speed;
    const xf = Math.max(32, Math.min(0.25 * sliceLen, 0.03 * this.fs));
    if (++this.count >= sliceLen) {
      this.count = 0;
      const margin = xf + 64;
      let start;
      if (freeze) start = this.lastStart;
      else {
        const behind = margin + Math.max(0, s - 1) * (sliceLen + xf);
        start = this.w - behind;
        this.lastStart = start;
      }
      this.cur ^= 1;
      const h = this.heads[this.cur];
      h.pos = start; h.g = 0; h.dir = 1;
      this.heads[this.cur ^ 1].dir = -1;
    }
    const step = 1 / xf;
    let out = 0;
    for (let k = 0; k < 2; k++) {
      const h = this.heads[k];
      if (h.dir > 0) h.g = Math.min(1, h.g + step);
      else if (h.dir < 0) h.g = Math.max(0, h.g - step);
      if (h.g > 0) {
        out += readFrac(this.buf, h.pos) * Math.sin(h.g * Math.PI * 0.5);
        h.pos += s;
      }
    }
    return out;
  }
}


// ──────────────────────────── Wet channel: WEAVE ───────────────────────
// Re-sampler in the spirit of the Fieldtone Weaver: keep a few seconds of what
// just happened, then let three threads read it back at once. Each thread has
// its own pitch, direction, grain size and dropout, all rolled from one seeded
// pattern. TIME chooses how far back the threads may reach; MODIFY walks the
// pattern from long overlapping cloth (texture) into short clipped stitches
// (rhythm). The pattern is re-rolled only when the integer position of the knobs
// changes, so the same knob position always draws the same fabric — chance, but
// repeatable, and no undo.
class Weave {
  constructor(fs) {
    this.fs = fs;
    this.buf = new Float32Array(Math.ceil(WEAVE_BUF_SEC * fs));
    this.w = 0;
    this.rng = 0x5eed;
    this.seedKey = -1;
    this.threads = [0, 1, 2].map((_, i) => ({
      pos: -fs * (0.25 + 0.3 * i), ph: i / 3, rate: 1, dir: 1, pan: [-0.7, 0, 0.7][i], amp: 1,
    }));
  }
  rand01() {
    this.rng = (Math.imul(this.rng, 1103515245) + 12345) >>> 0;
    return this.rng / 4294967296;
  }
  // One roll per knob position: rate, direction and dropout are drawn here.
  reroll(key, reachSec) {
    if (key === this.seedKey) return;
    this.seedKey = key;
    for (const th of this.threads) {
      const semis = WEAVE_PITCH[Math.floor(this.rand01() * WEAVE_PITCH.length)];
      th.rate = Math.pow(2, semis / 12);
      th.dir = this.rand01() < 0.3 ? -1 : 1;
      th.amp = this.rand01() < 0.85 ? 1 : 0.45;
      th.pos = this.w - this.fs * (0.15 + this.rand01() * 0.85 * Math.max(0.2, reachSec));
    }
  }
  tick(input, reachSec, modify, frozen) {
    const n = this.buf.length;
    if (!frozen) {
      this.buf[this.w] = input;
      if (++this.w >= n) this.w = 0;
    }
    const key = Math.floor(clamp(reachSec, 0, 3) * 8) * 16 + Math.floor(clamp(modify, 0, 1) * 16);
    this.reroll(key, reachSec);

    const grain = Math.max(64, expMap(1 - clamp(modify, 0, 1), 0.02, 0.55) * this.fs);
    const chance = 0.55 * clamp(modify, 0, 1);   // more rhythm, more holes
    let l = 0, r = 0;
    for (const th of this.threads) {
      th.ph += 1 / grain;
      if (th.ph >= 1) {
        th.ph -= 1;
        // A new stitch: maybe move somewhere else in the cloth, maybe not.
        if (this.rand01() < 0.45) {
          th.pos = this.w - this.fs * (0.12 + this.rand01() * Math.max(0.2, reachSec));
        }
        // Dropouts are partial, so a thread thins out instead of vanishing.
        th.amp = this.rand01() < chance * 0.35 ? 0 : 0.45 + 0.55 * this.rand01();
      }
      const win = Math.sin(th.ph * Math.PI);
      const v = readFrac(this.buf, th.pos) * win * win * th.amp;
      th.pos += th.rate * th.dir;
      const a = (th.pan + 1) * Math.PI / 4;
      l += v * Math.cos(a);
      r += v * Math.sin(a);
    }
    return [l * 0.7, r * 0.7];
  }
}

// ─────────────────────────────── Core ─────────────────────────────────
class BroodCore {
  constructor(fs) {
    this.fs = fs;
    this.p = {
      time: 0.5, mix: 0.5, length: 0.75, modWet: 0.5, clock: 1, modLoop: 0.5,
      wetMode: 0, routing: 0, loopMode: 0,
      trails: 0, smooth: 0, balance: 0.5,
      sync: 0, bpm: 120,
      loopEnd: 1, start: 0, memory: 0,
      tone: 1, fade: 0, width: 0.5, wetDiv: 0, loopDiv: 0, classic: 0, freeze: 0,
      rampWet: 0, rampLoop: 0, rampSpeed: 0.45, rampWave: 0, rampUni: 0, bounce: 0, sweep: 0, rampSync: 0,
      envSync: 0,
    };
    // host-rate smoothed knobs
    this.s = { time: 0.5, mix: 0.5, length: 0.75, modWet: 0.5, clock: 1, modLoop: 0.5, balance: 0.5,
      tone: 1, fade: 0, width: 0.5, rampSpeed: 0.45 };
    // What the DSP actually reads: the smoothed knobs plus whatever a ramp is
    // moving. Rebuilt every host sample, so the ramps never disturb a knob.
    this.e = { time: 0.5, mix: 0.5, length: 0.75, modWet: 0.5, clock: 1, modLoop: 0.5, balance: 0.5 };
    this.hostCount = 0;
    this.phase = 0;
    this.clockRatio = 1;
    this.aa = new LowPassN();
    this.reconL = new LowPassN();
    this.reconR = new LowPassN();
    this.filterClock = -1;
    this.filterClassic = -1;
    // The last four core ticks per channel, oldest first. The host-rate output is
    // a Hermite curve through the middle two (one tick of latency), or for
    // CLASSIC the old straight line between the newest two.
    this.ticks = [[0, 0, 0, 0], [0, 0, 0, 0]];
    this.xin = [0, 0, 0, 0];   // the last four host input samples, oldest first

    // wet channel
    this.reverb = new Reverb(CORE_FS);
    this.delay = new Delay(CORE_FS);
    this.slip = new Slip(CORE_FS);
    this.weave = new Weave(CORE_FS);
    this.wetOn = false;
    this.freeze = false;
    this.wetMomentary = false;
    this.wetIn = 0; this.wetOut = 0;           // core-rate gains
    this.modeGain = [1, 0, 0, 0];               // crossfade between wet modes
    this.decor = [new Allpass(Math.round(113 * CORE_FS / 48000), 0.6), new Allpass(Math.round(337 * CORE_FS / 48000), 0.6)];

    // loop channel
    const nLoop = MAX_LOOP_SAMPLES;
    this.histCycles = 0;
    this.hist = new Float32Array(nLoop);
    this.histW = 0;
    this.histCount = 0;
    this.loopBuf = new Float32Array(nLoop);
    this.lsrc = 0;
    this.loopState = 'off';                     // off | play
    this.overdub = false;
    this.loopGain = 0;
    this.loopPos = 0;
    // Moving START moves the read head; see loopTick for the 25 ms crossfade these
    // four carry from where the loop was playing to where it now begins.
    this.startA = 0; this.startB = 0; this.startFade = 1; this.startLen = -1;
    this.tapeSpeed = 1;
    this.stretchSpeed = 0;
    this.env = 0;
    this.envGate = false;
    this.envHold = 0;
    this.envMix = 0;
    this.slice = { start: 0, ph: 0 };
    this.grain = [{ ph: 0, start: 0 }, { ph: 0.5, start: 0 }];
    this.rng = 22222;
    this.toneL = 0; this.toneR = 0;              // TONE hi-cut state
    this.wowPh = 0;                              // CLASSIC loop wobble
    // Two generators, each with its own phase and its own seeded stream (never the
    // shared rng: STRETCH grains and CLASSIC hiss draw from that one).
    this.ramp = [
      { ph: 0, dir: 1, prev: 0.5, next: 0.5, rng: 0x9e3779b9 },
      { ph: 0.5, dir: 1, prev: 0.5, next: 0.5, rng: 0x5bd1e995 },
    ];
    this.rampRaw = [0, 0];      // per core tick
    this.rampVal = [0, 0];      // smoothed to audio rate

    this.fsDown = { wet: null, loop: null };   // press state for footswitches
    this.engaged = 0;
    this.events = [];                           // UI notifications (loop captured, ...)
    this.peakIn = 0; this.peakOut = 0;
  }

  setParam(name, value) {
    if (name in this.p) this.p[name] = value;
  }

  // which: 'wet' | 'loop', action: 'down' | 'up'
  footswitch(which, action) {
    const now = this.hostCount;
    if (which === 'wet') {
      if (action === 'down') {
        this.fsDown.wet = { t: now, held: false };
        if (!this.wetOn) { this.wetOn = true; this.wetMomentary = true; }
        else this.wetMomentary = false;
      } else if (this.fsDown.wet) {
        const held = this.fsDown.wet.held;
        this.fsDown.wet = null;
        if (held) {                       // a hold freezes for as long as it is held
          this.freeze = false;
          if (this.wetMomentary) this.wetOn = false;
        } else if (!this.wetMomentary) {
          this.wetOn = false;
          this.freeze = false;
        }
        this.wetMomentary = false;
      }
      return;
    }
    if (action === 'down') {
      const playing = this.loopState === 'play';
      this.fsDown.loop = { t: now, held: false, histIdx: this.histW, histCycle: this.histCycles, playing };
    } else if (this.fsDown.loop) {
      const d = this.fsDown.loop;
      this.fsDown.loop = null;
      if (d.playing) {
        this.overdub = false;
        if (!d.held) { this.loopState = 'off'; this.events.push('loop-stop'); }
        else this.events.push('loop-wave');
      } else if (d.held) {
        // A hold can outlast the circular history. Keep the newest full buffer
        // instead of wrapping the requested duration back to a tiny fragment.
        const written = (this.histCycles - d.histCycle) * this.hist.length + this.histW - d.histIdx;
        this.capture(Math.min(this.hist.length - 1, Math.max(0, written)));
      } else {
        this.capture(this.histCount); // retroactive: grab everything heard so far
      }
    }
  }

  // Copy the most recent n samples of history into the loop buffer and play.
  // How much history a capture may use, in core samples. MEMORY 0 keeps the
  // pedal's clock-derived capacity; above that it is 1–30 s of real time.
  // Floored to a whole sample: MEMORY is a real-time ceiling, so its sample count
  // is fractional, and a fractional capture length made histW - n fractional,
  // which read undefined out of the ring and filled the loop buffer with NaN.
  maxCaptureSamples() {
    const clock = clockHz(this.e.clock, this.p.smooth);
    const user = this.p.memory > 0.001 ? expMap(clamp(this.p.memory), 1, 30) * clock : LOOP_SAMPLES;
    return Math.floor(Math.min(MAX_LOOP_SAMPLES - 1, Math.max(64, Math.min(user, MAX_LOOP_SECONDS * clock))));
  }

  capture(n) {
    n = Math.floor(Math.min(n, this.hist.length - 1, this.histCount, this.maxCaptureSamples()));
    if (n < 64) return;
    const N = this.hist.length;
    let r = this.histW - n; if (r < 0) r += N;
    for (let i = 0; i < n; i++) {
      this.loopBuf[i] = this.hist[r];
      if (++r >= N) r = 0;
    }
    this.lsrc = n;
    this.loopPos = 0;
    this.tapeSpeed = speedMap(this.e.modLoop, 0.04);
    this.stretchSpeed = stretchMap(this.e.modLoop);
    this.grain[0].ph = 0; this.grain[1].ph = 0.5;
    this.grain[0].start = 0; this.grain[1].start = 0;
    this.loopState = 'play';
    this.events.push('loop-wave');
  }

  // Loop region being played. LENGTH decides how big the window is (TAPE takes it
  // from the newest part of the capture, ENV and STRETCH from the whole take), END
  // decides how much of that window sounds before it wraps, and START decides where
  // the window sits inside the capture — 0 is where each mode has always begun.
  loopWindow() {
    const len = Math.max(64, this.lsrc);
    const trim = clamp(this.p.loopEnd, 0.02, 1);
    const where = clamp(this.p.start, 0, 1);
    if (this.p.loopMode !== 1) {
      const W0 = Math.max(64, Math.floor(len * trim));
      // ENV and STRETCH: END says how much of the take plays, START says where
      // that window sits in it. Zero is the oldest audio, which is where these
      // two modes have always played from, so nothing changes at the default.
      //
      // START is measured against the take, not against the room END happens to
      // leave, so dragging END moves the end marker and nothing else. It gives ground
      // only when the window would otherwise run past the end of the take — and it
      // cannot move at all while the window fills the take, since there is nowhere to
      // slide to.
      const travel = Math.max(0, Math.round(where * Math.max(0, len - 64)));
      const start = Math.min(travel, Math.max(0, len - W0));
      return { W: W0, start };
    }
    // Linear in the fraction of the capture: at 1 the window is everything, so
    // the start of a long recording is actually reachable. The old exponential
    // curve spent 95% of the knob's travel on the last few seconds.
    // Rounded, not floored: at 1 the window is exactly the whole take, so the
    // start is sample 0 rather than one sample late.
    const W0 = Math.max(64, Math.round(len * Math.min(1, 0.02 + 0.98 * clamp(this.e.length))));
    // TAPE takes its window from the newest audio, so START reaches back into
    // the take; zero is the tail, as before.
    const room = Math.max(0, len - W0);
    return { W: Math.max(64, Math.floor(W0 * trim)), start: Math.round((1 - where) * room) };
  }

  // The widest the window can be: the whole take in ENV and STRETCH, LENGTH's
  // share of it in TAPE. The waveform's markers measure END against this.
  loopSpan() {
    const len = Math.max(64, this.lsrc);
    if (this.p.loopMode !== 1) return len;
    return Math.max(64, Math.round(len * Math.min(1, 0.02 + 0.98 * clamp(this.e.length))));
  }

  rand() { this.rng = (Math.imul(this.rng, 1103515245) + 12345) >>> 0; return this.rng / 4294967296; }

  // ── ramps ──
  // A second, private random stream per generator: the shared rng belongs to the
  // STRETCH grains and CLASSIC hiss, and drawing from it here would move them.
  rampRand(g) {
    g.rng = (Math.imul(g.rng, 1664525) + 1013904223) >>> 0;
    return g.rng / 4294967296;
  }

  // Value of the chosen shape at the current phase, 0..1.
  rampShape(g, wave) {
    const ph = g.ph;
    if (this.p.sweep) return ph;                      // SWEEP: it only ever climbs
    if (wave === 1) return ph < 0.5 ? 1 : 0;          // square
    if (wave === 2) return 0.5 + 0.5 * Math.sin(TAU * ph);
    if (wave === 3) return g.next;                    // sample and hold
    if (wave === 4) {                                 // smooth random
      const t = ph * ph * (3 - 2 * ph);
      return g.prev + (g.next - g.prev) * t;
    }
    return 1 - Math.abs(2 * ph - 1);                  // triangle
  }

  // Advance both generators. This runs once per host sample, not per core tick:
  // the ramp is a time function, so turning CLOCK down must not slow it down.
  // BOUNCE ping-pongs the phase instead of letting it reset, so nothing snaps back.
  rampTick() {
    // Free-running by default; synced, one cycle is a musical span instead.
    const hz = this.p.rampSync ? 1 / rampCycleSeconds(this.p.bpm, this.s.rampSpeed) : rampHz(this.s.rampSpeed);
    const rate = hz / this.fs;
    const wave = rampWaveIndex(this.p.rampWave);
    for (let k = 0; k < 2; k++) {
      const g = this.ramp[k];
      if (rampTargetIndex(k === 0 ? this.p.rampWet : this.p.rampLoop) === 0) { this.rampRaw[k] = 0; continue; }
      g.ph += rate * g.dir;
      if (g.ph >= 1) {
        if (this.p.bounce) { g.ph = 2 - g.ph; g.dir = -1; } else { g.ph -= 1; }
        g.prev = g.next; g.next = this.rampRand(g);
      } else if (g.ph <= 0) {
        if (this.p.bounce) { g.ph = -g.ph; g.dir = 1; } else { g.ph += 1; }
        g.prev = g.next; g.next = this.rampRand(g);
      }
      this.rampRaw[k] = this.rampShape(g, wave);
    }
  }

  // Rebuild the effective controls from the smoothed knobs, then let the ramps
  // push whichever control they are pointed at. Always starts from the knob, so
  // turning a ramp off leaves the control exactly where the knob says.
  applyRamps(a) {
    this.rampVal[0] += (this.rampRaw[0] - this.rampVal[0]) * a;
    this.rampVal[1] += (this.rampRaw[1] - this.rampVal[1]) * a;
    const e = this.e, sm = this.s;
    e.time = sm.time; e.mix = sm.mix; e.length = sm.length; e.modWet = sm.modWet;
    e.clock = sm.clock; e.modLoop = sm.modLoop; e.balance = sm.balance;
    for (let k = 0; k < 2; k++) {
      const idx = rampTargetIndex(k === 0 ? this.p.rampWet : this.p.rampLoop);
      if (idx === 0) continue;
      const v = this.rampVal[k];
      const off = (this.p.rampUni ? v : v * 2 - 1) * RAMP_DEPTH;
      let key = RAMP_KEYS[idx];
      if (k === 1 && key === 'modWet') key = 'modLoop';   // MODIFY means this channel's own
      e[key] = clamp(e[key] + off, 0, 1);
    }
  }

  // One-pole coefficient for the TONE hi-cut. The amount of cut is the caller's;
  // this is only the filter's corner, worked out once.
  toneCoeff() {
    if (this.toneC === undefined) this.toneC = 1 - Math.exp((-TAU * TONE_HZ) / CORE_FS);
    return this.toneC;
  }

  loopTick(x) {
    // The detector and overdub use clean input, even when the wet channel is on.
    const fs = CORE_FS;
    const a = Math.abs(x);
    this.env += (a - this.env) * (a > this.env ? 0.02 : 0.0005);

    const target = this.loopState === 'play' ? 1 : 0;
    this.loopGain += (target - this.loopGain) * 0.003;
    if (this.loopState === 'off' && this.loopGain < 1e-4) { this.loopGain = 0; return 0; }
    if (this.lsrc < 64) return 0;

    const win = this.loopWindow();
    const W = win.W, winStart = win.start;
    const buf = this.loopBuf;
    const len = Math.max(64, this.lsrc);
    const mode = this.p.loopMode;
    const m = this.e.modLoop;
    // LOOP CLOCK DIVISION: the loop reads at 1/div of the clock, so a divided
    // loop plays back slower and lower and takes div times as long to come round.
    const ldiv = divMap(this.p.loopDiv);
    const lstep = 1 / ldiv;
    let pos = this.loopPos % W; if (pos < 0) pos += W;

    // START moves the read head, and a jump in the read head is a click. So a
    // change of START is a crossfade from where the loop was playing to where it
    // now begins: 25 ms, weighted by a raised cosine so the fade has no corners
    // of its own either. A capture or a restore resets it — there is a gain ramp
    // around both, so landing instantly there cannot be heard.
    if (this.lsrc !== this.startLen) { this.startLen = this.lsrc; this.startA = winStart; this.startB = winStart; this.startFade = 1; }
    if (this.startFade >= 1) {
      this.startA = this.startB;                   // the last fade has landed
      if (winStart !== this.startA) { this.startB = winStart; this.startFade = 0; }
    } else {
      // The head we are fading *to* stays put while it fades in. Moving it mid-fade
      // is the same jump in a smaller dose: a knob still being dragged simply starts
      // the next fade as soon as this one lands.
      this.startFade = Math.min(1, this.startFade + 1 / (CORE_FS * 0.025));
    }
    const wB = 0.5 - 0.5 * Math.cos(Math.PI * this.startFade);   // 0 -> A, 1 -> B
    const at = (base, p) => {
      let q = p % W; if (q < 0) q += W;
      const abs = base + q;
      let i0 = Math.floor(abs);
      let f = abs - i0;
      if (i0 < 0) { i0 = 0; f = 0; }
      let i1 = i0 + 1;
      if (i1 >= len) { i1 = len - 1; if (i0 > i1) i0 = i1; f = 0; }
      // Hermite, with the neighbours held at the take's edges rather than
      // wrapped: the take is not circular, the window is.
      const im1 = i0 > 0 ? i0 - 1 : 0;
      const i2 = Math.min(i1 + 1, len - 1);
      return hermite(buf[im1], buf[i0], buf[i1], buf[i2], f);
    };
    const rd = (p) => {
      if (this.startFade >= 1) return at(this.startB, p);
      if (this.startFade <= 0) return at(this.startA, p);
      return at(this.startA, p) * (1 - wB) + at(this.startB, p) * wB;
    };
    const F = Math.min(0.004 * fs, W / 4);
    const edgeAt = (p) => { let q = p % W; if (q < 0) q += W; return Math.min(1, q / F, (W - q) / F); };
    let y;

    if (mode === 2) {
      // STRETCH: two Hann grains at 1x pitch; LENGTH = grain (slice) size,
      // MODIFY = speed and direction of the position through the loop.
      const G = Math.min(expMap(this.e.length, 0.01 * fs, 0.5 * fs), W);
      // A divided loop stretches each grain by div as well, or the grains would
      // still be read at full speed and the division would only thin them out.
      const Gs = G * ldiv;
      const dir = this.stretchSpeed < 0 ? -1 : 1;
      y = 0;
      for (let k = 0; k < 2; k++) {
        const g = this.grain[k];
        g.ph += 1 / Gs;
        if (g.ph >= 1) { g.ph -= 1; g.start = pos + (this.rand() - 0.5) * 0.2 * G; }
        const p = g.start + dir * g.ph * G;
        const sw = Math.sin(Math.PI * g.ph);
        y += rd(p) * sw * sw * edgeAt(p);
      }
      // Blend to the unprocessed loop at full clockwise, per MKII behavior.
      const normal = clamp((m - 0.97) / 0.03, 0, 1);
      y = y * (1 - normal) + rd(pos) * edgeAt(pos) * normal;
      this.stretchSpeed += (stretchMap(m) - this.stretchSpeed) * 0.001;
      pos += this.stretchSpeed * lstep;
    } else if (mode === 1) {
      // TAPE: LENGTH = loop length, MODIFY = speed and direction (pitch follows)
      y = rd(pos) * edgeAt(pos);
      this.tapeSpeed += (speedMap(m, 0.04) - this.tapeSpeed) * 0.00025;
      pos += this.tapeSpeed * lstep;
    } else {
      // ENV: playing interrupts the loop. The slice under the playhead when you
      // hit a note repeats until you stop; then the loop carries on in time.
      // LENGTH = slice size (grains → short phrases), MODIFY = sensitivity.
      const thr = expMap(1 - m, 0.008, 0.35);
      if (this.env > thr) {
        if (!this.envGate) {
          this.envGate = true;
          this.slice.start = pos;
          this.slice.ph = 0;
        }
        this.envHold = 0.08 * fs;
      } else if (this.envGate && --this.envHold <= 0) this.envGate = false;
      this.envMix += ((this.envGate ? 1 : 0) - this.envMix) * 0.005;
      const main = rd(pos) * edgeAt(pos);
      let sl = 0;
      if (this.envMix > 1e-4) {
        // ENV slices to a length, so synced LENGTH is a division of the beat,
        // converted into core ticks the same way the delay's is.
        const S = this.p.envSync
          ? Math.min(W, Math.max(64, syncSecondsRaw(this.p.bpm, this.e.length) * this.clockRatio * fs))
          : Math.min(expMap(this.e.length, 0.01 * fs, 1.0 * fs), W);
        const sl0 = this.slice;
        if (sl0.ph >= S) sl0.ph -= S;
        const F2 = Math.min(0.004 * fs, S / 4);
        sl = rd(sl0.start + sl0.ph) * Math.min(1, sl0.ph / F2, (S - sl0.ph) / F2);
        sl0.ph += lstep;
      }
      y = main * (1 - this.envMix) + sl * this.envMix;
      pos += lstep;
    }

    // CLASSIC: the MK I looper never sat still — a slow wow with a little flutter.
    if (this.p.classic) {
      this.wowPh += (TAU * 0.9) / CORE_FS;
      if (this.wowPh > TAU) this.wowPh -= TAU;
      pos += 0.006 * Math.sin(this.wowPh) + 0.002 * Math.sin(this.wowPh * 5.7);
    }

    if (this.overdub) {
      // FADE: how much of the old loop survives each overdub pass. 0 keeps the
      // pedal's slow decay; 1 lets the new layer replace it completely.
      const keep = 0.97 * (1 - clamp(this.s.fade));
      let q = Math.floor(mode === 1 ? this.loopPos : pos) % W; if (q < 0) q += W;
      const wi = Math.min(len - 1, Math.max(0, Math.round(this.startB) + q));
      buf[wi] = buf[wi] * keep + x;
    }
    this.loopPos = ((pos % W) + W) % W;
    return y * this.loopGain;
  }

  coreTick(x) {
    const p = this.p, e = this.e;
    const loopOut = this.loopTick(x);

    // Routing only changes the wet input when the loop is playing.
    const wetSrc = this.loopState !== 'play' ? x : p.routing === 0 ? x : p.routing === 1 ? x + loopOut : loopOut;
    // The FREEZE switch is the hold gesture without the holding: it brings the wet
    // side up and freezes it, so a reverb can be left ringing on its own.
    const held = p.freeze > 0.5;
    const wetOn = this.wetOn || held;
    const inT = wetOn ? 1 : 0;
    const outT = wetOn || p.trails ? 1 : 0;
    this.wetIn += (inT - this.wetIn) * 0.003;
    this.wetOut += (outT - this.wetOut) * 0.003;
    for (let k = 0; k < 4; k++) this.modeGain[k] += ((p.wetMode === k ? 1 : 0) - this.modeGain[k]) * 0.002;

    const freeze = this.freeze || held;
    // WET CLOCK DIVISION: the wet side runs at 1/div of the clock, so every time
    // constant on that side is div times longer.
    const wdiv = divMap(p.wetDiv);
    // CLASSIC: the MK I clock was noisy, most of all when it was slow.
    const hiss = p.classic ? (this.rand() * 2 - 1) * (0.0006 + 0.01 * (1 - this.clockRatio)) : 0;
    const wIn = wetSrc * this.wetIn + hiss;
    let wl = 0, wr = 0;
    const fs = CORE_FS;
    if (this.modeGain[0] > 1e-4) {
      // CLASSIC: the MK I reverb rings far longer than the MK II's.
      this.reverb.setDecay(expMap(e.time, 0.4, p.classic ? 32 : 25) * wdiv, expMap(e.time, 0.35, 1));
      const o = this.reverb.tick(wIn, e.modWet, freeze, e.time > 0.985);
      wl += o[0] * this.modeGain[0]; wr += o[1] * this.modeGain[0];
    }
    let mono = 0;
    if (this.modeGain[1] > 1e-4) {
      // Synced: the division is real seconds, converted into core ticks at the
      // current clock so the echoed note stays on the beat at any clock setting.
      let delayTicks = expMap(e.time, 0.02 * fs, 2 * fs);
      if (p.sync) delayTicks = clamp(syncSeconds(p.bpm, e.time) * this.clockRatio * this.fs, 1, DELAY_MAX_SEC * fs * 0.95);
      delayTicks = clamp(delayTicks * wdiv, 1, DELAY_MAX_SEC * fs * 0.95);
      mono += this.delay.tick(wIn, delayTicks, e.modWet, freeze) * this.modeGain[1];
    }
    if (this.modeGain[2] > 1e-4) {
      mono += this.slip.tick(wIn, expMap(e.time, 0.05 * fs, 1.5 * fs) * wdiv, slipSpeedMap(e.modWet), freeze) * this.modeGain[2];
    }
    if (this.modeGain[3] > 1e-4) {
      // Synced, TIME is a division of the beat: how far back the threads reach
      // becomes a musical distance, so a stitch lands on the grid.
      const reach = this.p.sync
        ? Math.min(WEAVE_BUF_SEC * 0.85, syncSecondsRaw(this.p.bpm, e.time) * wdiv)
        : Math.min(2.8, (0.2 + e.time * 2.6) * wdiv);
      const o = this.weave.tick(wIn, reach, e.modWet, freeze);
      wl += o[0] * this.modeGain[3]; wr += o[1] * this.modeGain[3];
    }
    wl += mono; wr += mono;
    // WIDTH is the whole stereo story now that SPREAD has gone: 0 is mono, 0.5 is
    // the image as built, and above the middle the two sides stop sharing — the
    // mono path is decorrelated so even a centred source opens up.
    const wv = clamp(this.s.width, 0, 1);
    if (Math.abs(wv - 0.5) > 0.002) {
      const mid = (wl + wr) * 0.5, side = (wl - wr) * 0.5 * (wv * 2);
      wl = mid + side; wr = mid - side;
    }
    if (wv > 0.5) {
      const amt = (wv - 0.5) * 2;                       // 0 at the middle, 1 at the top
      const d = this.decor[1].run(this.decor[0].run((wl + wr) * 0.5));
      wl += d * 0.5 * amt; wr -= d * 0.5 * amt;
    }
    // TONE: hi-cut on the wet bus. A blend, so fully open is bit-transparent.
    const toneAmt = 1 - clamp(this.s.tone);
    if (toneAmt > 1e-4) {
      const c = this.toneCoeff();
      this.toneL += c * (wl - this.toneL); this.toneR += c * (wr - this.toneR);
      wl += (this.toneL - wl) * toneAmt;
      wr += (this.toneR - wr) * toneAmt;
    }
    wl *= this.wetOut; wr *= this.wetOut;

    // The history always advances, whether or not a loop is playing: pausing it
    // left a splice in the buffer, and every extra tap stitched in another one,
    // which is what put clicks inside the next loop. While a loop plays only the
    // dry input is recorded — feeding the loop back would be a feedback path —
    // and while listening the live wet channel is caught as well, as before.
    const listening = this.loopState === 'off';
    this.hist[this.histW] = softClip(x + (listening ? (wl + wr) * 0.5 : 0));
    if (++this.histW >= this.hist.length) { this.histW = 0; this.histCycles++; }
    if (this.histCount < this.hist.length) this.histCount++;

    // WET / LOOP is a blend of two complete worlds, not a ducking contest: the
    // loop keeps its own level all the way across the knob, and the wet side adds
    // to it. It used to be ducked against the wet level so the loop would not be
    // heard twice, but the wet side always returns it late (a reverb tail, an
    // echo, a grain) — and in SLIP and WEAVE it returns very little — so that duck
    // simply swallowed the loop in the middle of the travel. Where the wet side
    // does hand the loop back, it arrives as an effect on top of it, which is what
    // the blend is for. At 100 % loop the wet side is gone and the loop is whole.
    const bal = e.balance;
    const wetLvl = Math.min(1, 2 * (1 - bal)), loopLvl = Math.min(1, 2 * bal);
    const ld = loopOut * loopLvl;
    for (const ch of this.ticks) { ch[0] = ch[1]; ch[1] = ch[2]; ch[2] = ch[3]; }
    this.ticks[0][3] = wl * wetLvl + ld;
    this.ticks[1][3] = wr * wetLvl + ld;
  }

  // in: Float32Array (mono), outL/outR: Float32Array
  process(input, outL, outR) {
    const n = outL.length;
    const p = this.p, s = this.s, fs = this.fs;
    const a = 1 - Math.exp(-1 / (0.03 * fs));
    const aClock = 1 - Math.exp(-1 / ((p.smooth ? 1.5 : 0.08) * fs)); // Smooth dip: glide the clock
    const holdN = HOLD_SEC * fs;
    // footswitch hold detection
    const fw = this.fsDown.wet;
    if (fw && !fw.held && this.hostCount - fw.t >= holdN) {
      fw.held = true;
      this.freeze = true;
    }
    const fl = this.fsDown.loop;
    if (fl && !fl.held && this.hostCount - fl.t >= holdN) { fl.held = true; if (fl.playing) this.overdub = true; }

    let pkIn = 0, pkOut = 0;
    for (let i = 0; i < n; i++) {
      s.time += (p.time - s.time) * a;
      s.mix += (p.mix - s.mix) * a;
      s.length += (p.length - s.length) * a;
      s.modWet += (p.modWet - s.modWet) * a;
      s.modLoop += (p.modLoop - s.modLoop) * a;
      s.balance += (p.balance - s.balance) * a;
      s.clock += (p.clock - s.clock) * aClock;
      s.tone += (p.tone - s.tone) * a;
      s.fade += (p.fade - s.fade) * a;
      s.width += (p.width - s.width) * a;
      s.rampSpeed += (p.rampSpeed - s.rampSpeed) * a;
      this.rampTick();
      this.applyRamps(a);

      if ((i & 31) === 0) {
        const c = clockHz(this.e.clock, !!p.smooth) / fs;
        this.clockRatio = c;
        const classicNow = p.classic ? 1 : 0;
        if (Math.abs(c - this.filterClock) > 0.002 * c || classicNow !== this.filterClassic) {
          this.filterClock = c; this.filterClassic = classicNow;
          const fc = Math.min(0.45 * c * fs, 0.45 * fs, 20000);
          this.aa.set(fc, fs, !p.classic); this.reconL.set(fc, fs, !p.classic); this.reconR.set(fc, fs, !p.classic);
        }
      }

      const dry = input ? input[i] : 0;
      const ad = Math.abs(dry); if (ad > pkIn) pkIn = ad;
      const xa = this.aa.run(dry);
      const xin = this.xin;
      xin[0] = xin[1]; xin[1] = xin[2]; xin[2] = xin[3]; xin[3] = xa;
      this.phase += this.clockRatio;
      while (this.phase >= 1) {
        this.phase -= 1;
        // Each core tick falls somewhere between two host samples; read the input
        // there (one host sample late, so the curve has a neighbour on both sides).
        // Handing every tick the latest host sample instead repeated one sample in
        // four at 48 kHz, a stair-step jitter that was recorded into every loop.
        // CLASSIC keeps that hold as part of its grit.
        const u = 1 - this.phase / this.clockRatio;
        this.coreTick(p.classic ? xa : hermite(xin[0], xin[1], xin[2], xin[3], u));
      }
      const f = this.phase;
      const t = this.ticks;
      let il, ir;
      if (p.classic) {
        il = t[0][2] + (t[0][3] - t[0][2]) * f;
        ir = t[1][2] + (t[1][3] - t[1][2]) * f;
      } else {
        il = hermite(t[0][0], t[0][1], t[0][2], t[0][3], f);
        ir = hermite(t[1][0], t[1][1], t[1][2], t[1][3], f);
      }
      let el = this.reconL.run(il);
      let er = this.reconR.run(ir);
      // STEREO WIDTH: mid/side on the effect bus only — the dry signal must not
      // move. 0.5 is the image as built, so the neutral setting is untouched.
      if (Math.abs(s.width - 0.5) > 0.002) {
        const mid = (el + er) * 0.5, side = (el - er) * 0.5 * (s.width * 2);
        el = mid + side; er = mid - side;
      }

      // dry stays at unity up to noon, then fades; true unity when nothing is engaged
      const latchFreeze = p.freeze > 0.5;   // the FREEZE switch, per sample
      const engagedT = this.wetOn || latchFreeze || this.loopState === 'play' ? 1 : 0;
      this.engaged += (engagedT - this.engaged) * 0.001;
      const dg = 1 - this.engaged * (1 - Math.min(1, 2 * (1 - this.e.mix)));
      const wg = Math.min(1, 2 * this.e.mix);
      let l = dry * dg + el * wg;
      let r = dry * dg + er * wg;
      // A fully bypassed plugin must be transparent, including signals above 0 dBFS.
      const fullyBypassed = !p.trails && !this.wetOn && !latchFreeze && this.loopState === 'off' && this.engaged < 1e-4;
      if (!fullyBypassed) { l = softLimit(l); r = softLimit(r); }
      outL[i] = l; outR[i] = r;
      const ao = Math.max(Math.abs(l), Math.abs(r)); if (ao > pkOut) pkOut = ao;
      this.hostCount++;
    }
    this.peakIn = Math.max(pkIn, this.peakIn * 0.9);
    this.peakOut = Math.max(pkOut, this.peakOut * 0.9);
  }

  // Snapshot for the UI.
  state() {
    const fl = this.fsDown.loop;
    let loop = this.loopState;
    if (fl && fl.held && !fl.playing) loop = 'rec';
    else if (this.overdub && this.loopState === 'play') loop = 'dub';
    const win = this.lsrc > 0 ? this.loopWindow() : { W: 0, start: 0 };
    return {
      wetOn: this.wetOn || this.p.freeze > 0.5, freeze: this.freeze || this.p.freeze > 0.5, loop,
      lsrc: this.lsrc, winStart: win.start, winLen: win.W, winSpan: this.lsrc > 0 ? this.loopSpan() : 0, pos: this.loopPos,
      clockHz: this.clockRatio * this.fs, histCycles: this.histCycles, peakIn: this.peakIn, peakOut: this.peakOut,
    };
  }

  loopPeaks(nBins) {
    const out = new Float32Array(nBins);
    const n = this.lsrc;
    if (!n) return out;
    for (let b = 0; b < nBins; b++) {
      const a = Math.floor((b * n) / nBins), z = Math.max(a + 1, Math.floor(((b + 1) * n) / nBins));
      let pk = 0;
      for (let i = a; i < z; i += 4) { const v = Math.abs(this.loopBuf[i]); if (v > pk) pk = v; }
      out[b] = pk;
    }
    return out;
  }
}

function softClip(x) {
  const ax = Math.abs(x);
  if (ax < 0.9) return x;
  return Math.sign(x) * (0.9 + 0.1 * Math.tanh((ax - 0.9) / 0.1));
}

// The output stage's limiter: the same shape as softClip with a wider knee. It
// starts bending at 0.7 and reaches the same 1.0 ceiling, so a loud reverb or an
// overdub peak is rounded over rather than flattened in the last tenth.
function softLimit(x) {
  const ax = Math.abs(x);
  if (ax < 0.7) return x;
  return Math.sign(x) * (0.7 + 0.3 * Math.tanh((ax - 0.7) / 0.3));
}

if (typeof AudioWorkletProcessor !== 'undefined') {
  // Independent left/right engines preserve stereo input. Both receive the same
  // gestures and parameters; the left engine supplies the left channel and the
  // right engine the right one, so a mono source keeps each engine's own stereo
  // (reverb, WEAVE's panning, WIDTH) instead of collapsing to dual mono.
  class BroodProcessor extends AudioWorkletProcessor {
    constructor() {
      super();
      this.left = new BroodCore(sampleRate);
      this.right = new BroodCore(sampleRate);
      this.scratchL = new Float32Array(128);
      this.scratchR = new Float32Array(128);
      this.sinceState = 0;
      this.dubTimer = 0;
      this.port.onmessage = (e) => {
        const m = e.data;
        for (const core of [this.left, this.right]) {
          if (m.type === 'param') core.setParam(m.name, m.value);
          else if (m.type === 'fs') core.footswitch(m.which, m.action);
        }
      };
    }
    process(inputs, outputs) {
      const out = outputs[0];
      const outL = out[0];
      if (!outL) return true;
      if (this.scratchL.length !== outL.length) {
        this.scratchL = new Float32Array(outL.length);
        this.scratchR = new Float32Array(outL.length);
      }
      const outR = out[1] || this.scratchR;
      const inp = inputs[0] || [];
      const inL = inp[0] || null;
      const inR = inp[1] || inL;
      this.left.process(inL, outL, this.scratchL);
      this.right.process(inR, this.scratchL, outR);
      this.sinceState += outL.length;
      if (this.sinceState >= sampleRate / 30) {
        this.sinceState = 0;
        const core = this.left;
        const st = core.state();
        const rst = this.right.state();
        st.peakIn = Math.max(st.peakIn, rst.peakIn);
        st.peakOut = Math.max(st.peakOut, rst.peakOut);
        let wave = false;
        while (core.events.length) {
          const ev = core.events.shift();
          if (ev === 'loop-wave') wave = true;
          else this.port.postMessage({ type: 'event', ev });
        }
        this.right.events.length = 0;
        if (st.loop === 'dub' && ++this.dubTimer % 10 === 0) wave = true;
        this.port.postMessage({ type: 'state', st });
        if (wave) {
          const peaks = core.loopPeaks(256);
          const rightPeaks = this.right.loopPeaks(256);
          for (let i = 0; i < peaks.length; i++) peaks[i] = Math.max(peaks[i], rightPeaks[i]);
          this.port.postMessage({ type: 'wave', peaks, lsrc: core.lsrc });
        }
      }
      return true;
    }
  }
  registerProcessor('brood-processor', BroodProcessor);
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { BroodCore, speedMap, slipSpeedMap, stretchMap, expMap, clockHz, syncLabel, syncSeconds, syncSecondsRaw, rampDivLabel, rampCycleSeconds, RAMP_DIV_BEATS, divMap, DIV_STEPS, RAMP_TARGETS, RAMP_WAVES, rampTargetIndex, rampWaveIndex, rampHz, SYNC_DIVISIONS, CLOCK_STEPS, CORE_FS, LOOP_SAMPLES, CLOCK_MIN_HZ, CLOCK_MAX_HZ, HOLD_SEC };
}
