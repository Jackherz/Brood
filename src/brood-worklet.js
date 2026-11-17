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
// The take strip's live view: a peak for every this many ticks of history, kept as it's written.
const LIVE_COL_TICKS = 128;
const LIVE_COLS = MAX_LOOP_SAMPLES / LIVE_COL_TICKS;
// The arena of loop audio, its regions and the versions UNDO and REDO swap in (see brood.h).
const ARENA_TAKES = 4;
const ARENA_SAMPLES = MAX_LOOP_SAMPLES * ARENA_TAKES;
const REGIONS = 8, VERSIONS = 6, CHUNK = 4096, CHUNKS = Math.ceil(MAX_LOOP_SAMPLES / CHUNK);
const VERSION_KIND = { none: 0, take: 1, clear: 2, layer: 3, resample: 4 };
// RESAMPLE records one pass of the loop's window, up to its limit (see brood.h).
const RESAMPLE_MAX_SEC = 30;
const RESAMPLE_CAP = Math.trunc(RESAMPLE_MAX_SEC * 64000) + 1280;
const AUTO = { start: 0, loopEnd: 1, length: 2, loopMode: 3, modLoop: 4, loopDiv: 5, loopLevel: 6, routing: 7, freeze: 8, mod1On: 9, mod2On: 10 };
const CLOCK_MIN_HZ = 2000;
const CLOCK_MAX_HZ = 64000;
const CORE_FS = 64000;       // buffer timings are defined at the pedal’s 64 kHz clock
const LENGTH_FOLLOW = 3;     // TAPE window samples a LENGTH move may shift per tick
const CLOCK_STEPS = [2000, 3000, 4000, 6000, 8000, 12000, 16000, 24000, 32000, 48000, 64000];
const DELAY_MAX_SEC = 2.1;
const SLIP_BUF_SEC = 5.5;
const WET_HOLD_SEC = 0.01;   // how fast FREEZE takes hold of a reverb or delay
const WEAVE_CLEAR = 2048;    // how far a stitch keeps from the write point, in samples
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
// What a modulation source can move, in the order of Core::effective().
const MOD_TARGETS = ['time', 'wetMix', 'length', 'modWet', 'clock', 'modLoop', 'loopLevel'];

// LOOP LEVEL and INPUT LEVEL are 0..1 faders: off at the bottom, then dB on a straight line
// (LOOP LEVEL -48 to +12 dB, 0 dB at 0.8; INPUT LEVEL -48 to +24 dB, 0 dB at 2/3), the last 5%
// fading to silence. Mirrors levelGain in native/dsp/brood.h.
function levelGain(v, topDb) {
  v = Math.min(1, Math.max(0, v));
  if (v <= 0) return 0;
  const db = -48 + v * (topDb + 48);
  return Math.pow(10, db / 20) * Math.min(1, v / 0.05);
}
const loopLevelGain = (v) => levelGain(v, 12);
const inputLevelGain = (v) => levelGain(v, 24);
const loopLevelOfDb = (db) => Math.min(1, Math.max(0, (db + 48) / 60));
const inputLevelOfDb = (db) => Math.min(1, Math.max(0, (db + 48) / 72));
const RAMP_WAVES = ['TRI', 'SQR', 'SIN', 'SAW', 'RND', 'SMTH'];
const rampWaveIndex = (k) => Math.round(clamp(k, 0, 1) * (RAMP_WAVES.length - 1));
const rampHz = (k) => expMap(clamp(k, 0, 1), 0.05, 8);   // one cycle every 20 s … eight a second
// Settings saved before state version 4 had five shapes and SWEEP and BOUNCE: SWEEP becomes
// SAW, SWEEP with BOUNCE becomes TRI at half the rate (synced: the next longer span), BOUNCE
// alone goes, and each old shape moves to its place in the row of six. Mirrors migrateShape
// in native/dsp/brood.h. Returns the new { wave, rate }.
function migrateShape(wave, sweep, bounce, sync, rate) {
  let shape = [0, 1, 2, 4, 5][Math.round(clamp(wave, 0, 1) * 4)];
  if (sweep && bounce) {
    shape = 0;
    rate = sync ? Math.max(0, rampDivIndex(rate) - 1) / (RAMP_DIV_BEATS.length - 1)
      : Math.max(0, clamp(rate, 0, 1) - Math.log(2) / Math.log(8 / 0.05));
  } else if (sweep) shape = 3;
  return { wave: shape / (RAMP_WAVES.length - 1), rate };
}

// Stepped, CLOCK lands on these. SMOOTH travels through the same rates, so the two agree at
// every step and switching SMOOTH doesn't change the sound.
const clockHz = (knob, smooth = false) => {
  const k = clamp(knob, 0, 1);
  if (!smooth) return CLOCK_STEPS[Math.floor(k * 10 + 0.5)];
  const below = Math.min(9, Math.floor(k * 10));
  return expMap(k * 10 - below, CLOCK_STEPS[below], CLOCK_STEPS[below + 1]);
};
// Where stepped CLOCK settles: on the step itself, so SMOOTH picks up from there.
const clockTarget = (knob, smooth) => (smooth ? knob : Math.floor(clamp(knob, 0, 1) * 10 + 0.5) / 10);

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

// A low-pass state-variable filter (the topology-preserving form): LowPass's response, but
// built to be retuned while it runs, so a modulated CLOCK doesn't pop. Mirrors brood.h.
class SvfLowPass {
  constructor() { this.a1 = 1; this.a2 = 0; this.a3 = 0; this.ic1 = 0; this.ic2 = 0; }
  set(fc, fs, q) {
    const g = Math.tan(Math.PI * clamp(fc, 10, fs * 0.49) / fs), k = 1 / q;
    this.a1 = 1 / (1 + g * (g + k)); this.a2 = g * this.a1; this.a3 = g * this.a2;
  }
  run(x) {
    const v3 = x - this.ic2, v1 = this.a1 * this.ic1 + this.a2 * v3, v2 = this.ic2 + this.a2 * this.ic1 + this.a3 * v3;
    this.ic1 = 2 * v1 - this.ic1; this.ic2 = 2 * v2 - this.ic2;
    return v2;
  }
}

// Butterworth low-pass, 6th order (three sections) or, for CLASSIC, the 4th order
// the clock filters used to be. The steeper one keeps far less of the images a
// low CLOCK leaves above its band; CLASSIC keeps that grit on purpose.
class LowPassN {
  constructor() { this.a = new SvfLowPass(); this.b = new SvfLowPass(); this.c = new SvfLowPass(); this.sixth = true; }
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
// HI-Q: a windowed sinc in place of the four-point curve, for the crossings into and out of
// the core clock. A table of Kaiser-windowed sincs, one row per position between samples, rows
// interpolated. It looks ahead by half its length, so the wet side runs that much later: eight
// ticks going out, which is 1 ms at an 8 kHz clock. The dry side doesn't wait.
const SINC_PHASES = 256;
const besselI0 = (x) => { let sum = 1, term = 1; for (let k = 1; k < 32; k++) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; } return sum; };
function sincTable(taps, cutoff, beta) {
  const table = [];
  const mid = taps / 2 - 1;
  for (let ph = 0; ph <= SINC_PHASES; ph++) {
    const f = ph / SINC_PHASES, row = new Float64Array(taps);
    let total = 0;
    for (let k = 0; k < taps; k++) {
      const t = k - mid - f, x = Math.PI * cutoff * t;
      const sinc = Math.abs(x) < 1e-9 ? 1 : Math.sin(x) / x;
      const w = t / (taps / 2);
      row[k] = sinc * besselI0(beta * Math.sqrt(Math.max(0, 1 - w * w))) / besselI0(beta);
      total += row[k];
    }
    for (let k = 0; k < taps; k++) row[k] /= total;
    table.push(row);
  }
  return table;
}
const SINC_IN = sincTable(8, 0.97, 7), SINC_OUT = sincTable(16, 0.92, 8);
// `ring` holds the samples and `w` is the oldest; f is the position between samples mid and mid + 1.
function sincRead(table, taps, ring, w, f) {
  const pos = f * SINC_PHASES, i = Math.min(SINC_PHASES - 1, Math.floor(pos)), fr = pos - i;
  const a = table[i], b = table[i + 1];
  let y = 0;
  for (let k = 0; k < taps; k++) y += ring[(w + k) & (taps - 1)] * (a[k] + (b[k] - a[k]) * fr);
  return y;
}

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
  if (p >= n) p -= n;   // a hair below zero wraps to exactly n (see brood.h)
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
    // FREEZE and endless decay, glided so they don't step the tail
    this.hold = 0; this.ring = 0;
    this.holdA = 1 - Math.exp(-1 / (WET_HOLD_SEC * fs));
  }
  // The size moves at a capped speed, so no line's read head runs outside 0.75x to 1.25x
  // under a fast TIME modulation. Mirrors native/dsp/brood.h.
  setDecay(t60, size) {
    if (Math.abs(t60 - this.t60) < 1e-4 && Math.abs(size - this.size) < 1e-4) return;
    const most = 0.25 / this.len[7];
    this.size = this.t60 < 0 ? size : this.size + clamp(size - this.size, -most, most);
    this.t60 = t60;
    for (let i = 0; i < 8; i++) this.g[i] = Math.pow(10, (-3 * this.len[i] * this.size) / (t60 * this.fs));
  }
  // modify = smear: CCW clear and still, CW washed out (more input diffusion,
  // deeper delay modulation, softer highs)
  tick(input, modify, freeze, infinite) {
    let d = input * 0.35;
    const dg = 0.45 + 0.3 * modify;
    for (let i = 0; i < 4; i++) { this.diff[i].g = dg; d = this.diff[i].run(d); }
    this.hold += ((freeze ? 1 : 0) - this.hold) * this.holdA;
    this.ring += ((freeze || infinite ? 1 : 0) - this.ring) * this.holdA;
    const dampOpen = 1 - Math.exp((-TAU * expMap(modify, 9000, 3500)) / this.fs);
    const damp = dampOpen + (1 - dampOpen) * this.hold;
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
      const g = this.g[i] + (1 - this.g[i]) * this.ring;
      x[i] = this.lp[i] * g;
      sum += x[i];
    }
    const h = sum * 0.25; // Householder reflection: x - (2/N)·Σx
    let l = 0, r = 0;
    for (let i = 0; i < 8; i++) {
      const y = x[i] - h;
      const line = this.lines[i];
      line[this.w[i]] = y + d * (1 - this.hold);
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
    // FREEZE, and repeating exactly (FREEZE or the top of MODIFY)
    this.hold = 0; this.full = 0;
    this.holdA = 1 - Math.exp(-1 / (WET_HOLD_SEC * fs));
    // TAPE ECHO: one read head that glides to a new TIME, so the echo bends in pitch as a
    // tape echo's does, with a little wow and flutter and a gentle saturation in the loop.
    this.headD = -1; this.headV = 0; this.frozenD = -1; this.wowPh = 0; this.flutPh = 0; this.tapeMix = 0;
  }
  // At maximum feedback the delay repeats stably, like a looping delay.
  tick(input, delaySamples, modify, freeze, tape) {
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
    // TAPE ECHO, faded in and out over 30 ms so switching it under sound doesn't click.
    this.tapeMix = clamp(this.tapeMix + (tape ? 1 : -1) / (this.fs * 0.03), 0, 1);
    let tw = 0;   // how much of the tape head is heard
    if (this.tapeMix > 0) {
      // While frozen the line repeats exactly: TIME is ignored until FREEZE lets go.
      if (this.hold > 0.5) { if (this.frozenD < 0) this.frozenD = this.headD; } else this.frozenD = -1;
      const target = this.frozenD >= 0 ? this.frozenD : delaySamples;
      if (this.headD < 0) this.headD = target;
      // The head moves toward the new time no faster than half its own speed again,
      // so pitch bends at most an octave down or a fifth up, and never past the write head.
      const want = clamp((target - this.headD) / (0.1 * this.fs), -0.5, 0.5);
      this.headV += (want - this.headV) * 0.002;
      this.headD = clamp(this.headD + this.headV, 1, this.buf.length - 4);
      this.wowPh += TAU * 0.5 / this.fs; if (this.wowPh > TAU) this.wowPh -= TAU;
      this.flutPh += TAU * 6 / this.fs; if (this.flutPh > TAU) this.flutPh -= TAU;
      const wobble = this.headD * (0.0015 * Math.sin(this.wowPh) + 0.0005 * Math.sin(this.flutPh)) * (1 - this.hold);
      // Repeating exactly, the head reads whole samples: a fractional read written back
      // every pass would dull the frozen sound a little more each time round.
      const at = this.full > 0.999 ? Math.round(this.headD) : this.headD + wobble;
      const yTape = readFrac(this.buf, this.w - at);
      tw = 0.5 - 0.5 * Math.cos(Math.PI * this.tapeMix);
      y += (yTape - y) * tw;
    } else this.headD = delaySamples;   // off, the head waits where the knob is, ready to take over
    this.lp += this.lpA * (y - this.lp);
    this.hp += this.hpA * (this.lp - this.hp);
    // Glided, as what's written here comes back on every repeat: a hard switch would
    // leave a seam in the line that clicks each time round.
    this.hold += ((freeze ? 1 : 0) - this.hold) * this.holdA;
    this.full += ((freeze || modify > 0.985 ? 1 : 0) - this.full) * this.holdA;
    let fb = (this.lp - this.hp) * modify;
    // Tape saturates gently in the loop, not when repeating exactly.
    if (tw > 0) fb += (Math.tanh(1.3 * fb) / Math.tanh(1.3) - fb) * tw * (1 - this.full);
    const v = input * (1 - this.hold) + fb * (1 - this.full) + y * this.full;
    this.buf[this.w] = softClip(v);
    if (++this.w >= this.buf.length) this.w = 0;
    return y;
  }
}

// ───────────────────────── Wet channel: SLIP ──────────────────────────
// Repeating slices of recent audio replayed at another speed/direction.
// Writes into a buffer that FREEZE can stop. Writing again would put a step where the
// old audio ends, so the input slides in from the last sample held.
class WetWriter {
  constructor() { this.wasFrozen = false; this.seam = 1; this.from = 0; }
  // Returns the next write position.
  write(buf, w, input, frozen) {
    if (!frozen) {
      const n = buf.length;
      if (this.wasFrozen) { this.seam = 0; this.from = buf[w === 0 ? n - 1 : w - 1]; }
      if (this.seam < 1) { this.seam = Math.min(1, this.seam + 1 / 240); input = this.from + (input - this.from) * this.seam; }
      buf[w] = input;
      if (++w >= n) w = 0;
    }
    this.wasFrozen = frozen;
    return w;
  }
}

class Slip {
  constructor(fs) {
    this.fs = fs;
    this.buf = new Float32Array(Math.ceil(SLIP_BUF_SEC * fs));
    this.w = 0;
    this.heads = [{ pos: 0, g: 0, dir: 0, level: 1, dying: false }, { pos: 0, g: 0, dir: 0, level: 1, dying: false }];
    this.cur = 0;
    this.count = 1e9;
    this.speed = 1;
    this.writer = new WetWriter();
    this.filled = 0;   // how much of the buffer has been written since it was made
  }
  tick(input, sliceLen, speedTarget, freeze) {
    const n = this.buf.length;
    const freezing = freeze && !this.writer.wasFrozen;
    this.w = this.writer.write(this.buf, this.w, input, freeze);
    if (!freeze && this.filled < n) this.filled++;
    this.speed += (speedTarget - this.speed) * 0.002;
    const s = this.speed;
    const xf = Math.max(32, Math.min(0.25 * sliceLen, 0.03 * this.fs));
    // Freezing stops the write point, so a head running just behind it would reach it at
    // once: start a slice now, from far enough back. The head being replaced has to have
    // finished fading, or its restart would be a step.
    if (freezing) this.count = 1e9;
    // A slice also ends when its head has had to bow out (see brood.h).
    if ((++this.count >= sliceLen || (this.heads[this.cur].dying && this.count >= xf)) && this.heads[this.cur ^ 1].g <= 0) {
      this.count = 0;
      const margin = xf + 64;
      const behind = margin + Math.max(0, freeze ? s : s - 1) * (sliceLen + xf);
      // Never further back than has been written (see brood.h).
      const start = this.w - Math.min(behind, n - 2 * margin, Math.max(margin, this.filled - margin));
      this.cur ^= 1;
      const h = this.heads[this.cur];
      h.pos = start; h.g = 0; h.dir = 1; h.level = 1; h.dying = false;
      this.heads[this.cur ^ 1].dir = -1;
    }
    const step = 1 / xf;
    let out = 0;
    for (let k = 0; k < 2; k++) {
      const h = this.heads[k];
      if (h.dir > 0) h.g = Math.min(1, h.g + step);
      else if (h.dir < 0) h.g = Math.max(0, h.g - step);
      if (h.g > 0) {
        // A head that speeds up can catch the write point, where this moment's audio meets
        // the oldest, and that seam is a click. So it bows out before it gets there: once
        // it's close it fades for good, until the next slice replaces it.
        let ahead = (this.w - h.pos) % n; if (ahead < 0) ahead += n;
        const closing = freeze ? s : s - 1;   // toward the write point; negative, away and round to it
        if (ahead < 64 + 240 * Math.max(0, closing) || ahead > n - 64 + 240 * Math.min(0, closing)) h.dying = true;
        if (h.dying) h.level = Math.max(0, h.level - 1 / 240);
        out += readFrac(this.buf, h.pos) * Math.sin(h.g * Math.PI * 0.5) * h.level;
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
    this.writer = new WetWriter();
    this.threads = [0, 1, 2].map((_, i) => ({
      pos: -fs * (0.25 + 0.3 * i), ph: i / 3, rate: 1, dir: 1, pan: [-0.7, 0, 0.7][i], amp: 1,
      level: 1, dying: false, rolled: false, nextRate: 1, nextBack: 0, nextDir: 1,
    }));
  }
  rand01() {
    this.rng = (Math.imul(this.rng, 1103515245) + 12345) >>> 0;
    return this.rng / 4294967296;
  }
  // One roll per knob position. It waits for each thread's next stitch, where the window is shut.
  reroll(key, reachSec) {
    if (key === this.seedKey) return;
    this.seedKey = key;
    for (const th of this.threads) {
      const semis = WEAVE_PITCH[Math.floor(this.rand01() * WEAVE_PITCH.length)];
      th.nextRate = Math.pow(2, semis / 12);
      th.nextDir = this.rand01() < 0.3 ? -1 : 1;
      th.nextBack = this.fs * (0.15 + this.rand01() * 0.85 * Math.max(0.2, reachSec));
      th.rolled = true;
    }
  }
  tick(input, reachSec, modify, frozen) {
    const n = this.buf.length;
    this.w = this.writer.write(this.buf, this.w, input, frozen);
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
        let back = -1;
        if (this.rand01() < 0.45) back = this.fs * (0.12 + this.rand01() * Math.max(0.2, reachSec));
        if (th.rolled) { th.rate = th.nextRate; th.dir = th.nextDir; back = th.nextBack; th.rolled = false; }
        // The write point is where the newest audio meets the oldest. A thread drifts toward
        // or away from it by its speed, so it starts where a whole stitch stays clear.
        const drift = th.rate * th.dir - (frozen ? 0 : 1);
        const nearest = WEAVE_CLEAR + Math.max(0, drift) * grain;
        const farthest = Math.max(nearest, n - WEAVE_CLEAR + Math.min(0, drift) * grain);
        let ahead = (this.w - th.pos) % n; if (ahead < 0) ahead += n;
        th.pos = this.w - clamp(back >= 0 ? back : ahead, nearest, farthest);
        th.level = 1; th.dying = false;
        // Dropouts are partial, so a thread thins out instead of vanishing.
        th.amp = this.rand01() < chance * 0.35 ? 0 : 0.45 + 0.55 * this.rand01();
      }
      // MODIFY lengthening a stitch or FREEZE stopping the write point can still bring it
      // close: then it fades for good, until the next stitch.
      let ahead = (this.w - th.pos) % n; if (ahead < 0) ahead += n;
      if (ahead < WEAVE_CLEAR * 0.5 || ahead > n - WEAVE_CLEAR * 0.5) th.dying = true;
      if (th.dying) th.level = Math.max(0, th.level - 1 / 240);
      const win = Math.sin(th.ph * Math.PI);
      const v = readFrac(this.buf, th.pos) * win * win * th.amp * th.level;
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
      time: 0.5, wetMix: 0, length: 0.75, modWet: 0.5, clock: 1, modLoop: 0.5,
      // ROUTING: 0 IN, 1 IN + LOOP, 2 LOOP, 3 OFF. MONITOR: 0 AUTO, 1 ON, 2 OFF (see brood.h).
      wetMode: 0, routing: 0, loopMode: 0, monitor: 0,
      trails: 0, smooth: 0,
      sync: 0, bpm: 120,
      loopEnd: 1, start: 1, memory: 0,
      tone: 1, fade: 0, width: 0.5, wetDiv: 0, loopDiv: 0, classic: 0, wearNoise: 1, wearPost: 0, freeze: 0, tapeDelay: 0, overdubLatch: 0,
      // RECORD: the loop switch records what's played after it instead of keeping the recent
      // past. recBars 0 records until the next tap; otherwise that many bars, then plays.
      // Bars start at the next bar line while the host plays, unless recAtTap.
      recMode: 0, recBars: 0, recAtTap: 0, barQuarters: 4, hostPlaying: 0, ppq: 0, barStartPpq: 0,
      envSoft: 0, loopLevel: 0.8, hiQ: 0,
      // The two modulation sources: each its own rate and shape, and a depth (-1..1,
      // 0 is no route) for each control it can move.
      ...Object.fromEntries([1, 2].flatMap((n) => [
        [`mod${n}On`, 1], [`mod${n}Rate`, 0.45], [`mod${n}Wave`, 0], [`mod${n}Uni`, 0],
        [`mod${n}Sync`, 0], [`mod${n}Hold`, 0],
        ...MOD_TARGETS.map((t) => [`mod${n}_${t}`, 0]),
      ])),
      envSync: 0,
    };
    // host-rate smoothed knobs
    this.s = { time: 0.5, wetMix: 0, length: 0.75, modWet: 0.5, clock: 1, modLoop: 0.5, loopLevel: 0.8,
      tone: 1, fade: 0, width: 0.5, modRate: [0.45, 0.45] };
    // What the DSP actually reads: the smoothed knobs plus whatever a ramp is
    // moving. Rebuilt every host sample, so the ramps never disturb a knob.
    this.e = { time: 0.5, wetMix: 0, length: 0.75, modWet: 0.5, clock: 1, modLoop: 0.5, loopLevel: 0.8 };
    this.hostCount = 0;
    this.phase = 0;
    this.clockRatio = 1;
    this.aa = new LowPassN();
    this.dubAA = [new LowPassN(), new LowPassN()]; this.dubAARate = -1;
    this.coreIn = new LowPassN(); this.coreOut = [new LowPassN(), new LowPassN()]; this.coreW = 0;   // CLOCK above the host's rate   // in front of an overdub written slower than 1x
    this.reconL = new LowPassN();
    this.reconR = new LowPassN();
    this.filterClock = -1;
    this.filterClassic = -1;
    // The last four core ticks per channel, oldest first. The host-rate output is
    // a Hermite curve through the middle two (one tick of latency), or for
    // CLASSIC the old straight line between the newest two.
    this.ticks = [[0, 0, 0, 0], [0, 0, 0, 0]];
    this.xin = [0, 0, 0, 0];   // the last four host input samples, oldest first
    this.xinHi = new Float64Array(8); this.ticksHi = [new Float64Array(16), new Float64Array(16)];   // HI-Q's longer memory of both
    this.xinHiW = 0; this.ticksHiW = 0;
    this.hiMix = 0;            // HI-Q, faded in and out so switching it doesn't click
    this.aaHi = [new LowPassN(), new LowPassN()];

    // wet channel
    this.reverb = new Reverb(CORE_FS);
    this.delay = new Delay(CORE_FS);
    this.slip = new Slip(CORE_FS);
    this.weave = new Weave(CORE_FS);
    this.freeze = false;
    this.freezeTapped = false;
    // The mix as it stands (mixTick): the monitor gate, what WET hears of the input and the
    // loop, WET MIX's gains for the source and for WET, and the share of WET a take keeps.
    this.mon = 1; this.rIn = 0; this.rLoop = 0; this.dGain = 1; this.gGain = 0; this.recK = 0; this.wetMixNow = 0;
    this.routingNow = 0;
    this.dubGain = 0; this.dubPrev = 0; this.dubLast = -1; this.dubPos = 0; this.dubW = 64; this.dubStart = 0;
    this.loopLevel = 1;
    this.softStart = 0; this.softPh = 0; this.softS = 0; this.softOldFade = 0; this.softOldS = 0;   // SOFT: the slice being let go
    this.wetOut = 0;                           // core-rate gain
    this.lastWet = 0;                          // the wet side's last tick, for an overdub to layer
    this.loopBare = 0;                         // the loop's last tick before LOOP LEVEL
    this.modeGain = [1, 0, 0, 0];               // crossfade between wet modes
    this.decor = [new Allpass(Math.round(113 * CORE_FS / 48000), 0.6), new Allpass(Math.round(337 * CORE_FS / 48000), 0.6)];

    // loop channel
    const nLoop = MAX_LOOP_SAMPLES;
    this.histCycles = 0;
    this.hist = new Float32Array(nLoop);
    this.histW = 0;
    this.histCount = 0;
    this.liveCols = new Float32Array(LIVE_COLS);
    this.histTotal = 0;
    this.dubLo = Infinity; this.dubHi = -1;   // cells an overdub wrote since the strip looked
    this.arena = new Float32Array(ARENA_SAMPLES);
    this.regions = Array.from({ length: REGIONS }, () => ({ off: 0, len: 0, used: false }));
    this.curRegion = -1;
    this.loopBuf = null;                        // the take playing: a view of its region
    this.versions = Array.from({ length: VERSIONS }, () => ({ used: false, undone: false, id: 0, kind: 0, region: -1, len: 0, follows: false, anchor: 0, base: -1, dirty: new Uint8Array(CHUNKS) }));
    // RESAMPLE under way, and the tail left ringing after RESAMPLE moved ROUTING.
    this.resampling = false; this.rsInTail = false; this.rsWetSeen = false; this.rsLimitHit = false; this.rsFollows = false;
    this.rsRegion = -1; this.rsW = 0; this.rsLen = 0; this.rsTail = 0; this.wetSide = 0;
    this.rsTravel = 0; this.rsTarget = 0; this.rsAnchor = 0; this.travelStep = 0; this.rsFeedIn = 1;
    // The handover: the old loop dips out over 5 ms, the takes swap, the new one dips in (see brood.h).
    this.rsHandover = false; this.rsHandLimit = false; this.rsSinceEnd = 0; this.rsDip = 1;
    this.rsLimitAt = 0; this.rsStartHost = 0;
    this.ring = false; this.ringRouting = -1;
    this.nextVersionId = 1; this.layerVersion = -1; this.pendingSwap = -1;
    this.versionEvents = [];                    // the core's news for the page's UNDO history
    this.pendingOp = null;                      // 'clear' | 'swap', waiting for the loop to fade out
    this.resumeAfterOp = false;
    this.dubLatched = false; this.latchSeen = false;
    this.fitted = false;
    // Transport takes: caught while the host played, anchored at the song position of their
    // first sample. The undo copy keeps its own.
    this.takeFollows = false; this.parked = false;
    this.anchorPpq = 0;
    this.lastHostPlaying = false; this.lastPpq = 0; this.lastBlockN = 0; this.blockStartCount = 0;
    this.pendingLock = false; this.lockGain = 1; this.lockShift = NaN;
    this.sliceFresh = false; this.gridS = -1; this.gridLine = 0; this.gridTLine = 0;   // ENV synced to a playing host
    this.sliceS = -1; this.sliceIn = 1;
    this.lsrc = 0;
    this.loopState = 'off';                     // off | play
    this.overdub = false;
    this.loopGain = 0;
    this.loopPos = 0;
    this.heardPos = [0, 0]; this.heardLevel = [1, 0];   // for the playhead: see heardAt
    // Moving START moves the read head; see loopTick for the 25 ms crossfade these
    // four carry from where the loop was playing to where it now begins.
    this.startA = 0; this.startB = 0; this.startFade = 1; this.startLen = -1;
    // The fade carries the whole window, not just its start: each head has its own length
    // (winA, winB), and the head faded from reads shiftA further on, where it was playing.
    // A window that changes length mid-fade waits, like START, for the fade to land.
    // carryA: how far the playhead has wrapped round winB since the fade began; head A's
    // playhead carries on through it to its own window's end instead of following.
    this.winA = 64; this.winB = 64; this.shiftA = 0; this.carryA = 0;
    // LENGTH in TAPE sizes the window. When it moves, the playhead keeps its place as a
    // share of the window (the way the Digitakt does it), and the window's start follows
    // without a crossfade, since nothing jumps. These say what the last tick played.
    this.lastW = -1; this.lastWant = 0; this.lastMode = -1; this.lastTrim = -1; this.lastWhere = -1;
    this.lengthSlew = -1;
    this.tapeSpeed = 1;
    this.stretchSpeed = 0;
    this.env = 0;
    this.envGate = false;
    this.envHold = 0;
    this.envMix = 0;
    this.slice = { start: 0, ph: 0 };
    this.grain = [{ ph: 0, start: 0, dir: 1 }, { ph: 0.5, start: 0, dir: 1 }];
    this.rng = 22222;
    this.toneL = 0; this.toneR = 0;              // TONE hi-cut state
    this.warmIn = [0, 0]; this.warmOut = [0, 0];
    this.wowPh = 0;                              // CLASSIC loop wobble
    // Two generators, each with its own phase and its own seeded stream (never the
    // shared rng: STRETCH grains and CLASSIC hiss draw from that one).
    this.ramp = [
      { ph: 0, prev: 0.5, next: 0.5, rng: 0x9e3779b9 },
      { ph: 0.5, prev: 0.5, next: 0.5, rng: 0x5bd1e995 },
    ];
    this.rampRaw = [0, 0];      // per core tick
    this.rampVal = [0, 0];      // smoothed to audio rate
    this.modGainS = [1, 1];     // ON, smoothed
    this.modDepthS = [new Array(7).fill(0), new Array(7).fill(0)];   // depths, smoothed

    this.fsDown = { wet: null, loop: null };   // press state for footswitches
    this.rec = { phase: 'idle', histIdx: 0, histCycle: 0, endAt: -1, barTarget: 0 };   // RECORD
    this.engaged = 0;
    this.dropUndo = false;                      // a full reset: the cleared take isn't kept for UNDO
    this.events = [];                           // UI notifications (loop captured, ...)
    this.peakIn = 0; this.peakOut = 0;
  }

  setParam(name, value) {
    if (name in this.p) this.p[name] = value;
  }

  // which: 'wet' | 'loop', action: 'down' | 'up'
  footswitch(which, action) {
    const now = this.hostCount;
    // The FREEZE key: a hold freezes WET for as long as it's held; a tap asks the page to flip
    // the FREEZE switch (takeFreezeTap), which is a setting and so is saved.
    if (which === 'wet') {
      if (action === 'down') this.fsDown.wet = { t: now, held: false };
      else if (this.fsDown.wet) {
        const held = this.fsDown.wet.held;
        this.fsDown.wet = null;
        if (held) this.freeze = false;
        else this.freezeTapped = true;
      }
      return;
    }
    this.settlePending();   // a CLEAR or UNDO still fading out happens now
    this.parked = false;    // a tap with a transport loop parked works as with any stopped loop
    if (action === 'down') {
      const playing = this.loopState === 'play';
      const p = this.p, rec = this.rec;
      // RECORD, with nothing playing: a tap while recording (or armed) ends it there.
      if (!playing && rec.phase !== 'idle') {
        if (rec.phase === 'recording') this.closeRecording();
        else rec.phase = 'idle';   // armed: a second tap stands it down
        this.fsDown.loop = null;
        return;
      }
      this.fsDown.loop = { t: now, held: false, histIdx: this.histW, histCycle: this.histCycles, playing };
      if (!playing && p.recMode === 1) {
        // Recording starts here, or at the next bar line when bars are chosen and the
        // host is playing. A hold records from the press either way.
        rec.histIdx = this.histW; rec.histCycle = this.histCycles; rec.endAt = -1;
        if (p.recBars > 0 && !p.recAtTap && p.hostPlaying) {
          const into = (p.ppq - p.barStartPpq) / Math.max(0.25, p.barQuarters);
          rec.barTarget = p.barStartPpq + Math.ceil(into - 1e-9) * Math.max(0.25, p.barQuarters);
          rec.phase = 'armed';
        } else this.startRecording(false);
      }
    } else if (this.fsDown.loop) {
      const d = this.fsDown.loop;
      this.fsDown.loop = null;
      if (!d.playing && this.p.recMode === 1 && this.rec.phase !== 'idle') {
        // RECORD: a hold ends on release; a tap leaves it running (to the next tap,
        // the bar count or the ceiling).
        if (d.held) this.closeRecording();
        return;
      }
      if (d.playing) {
        // A tap stops the loop, and a latched OVERDUB with it. Letting go of a hold
        // ends its layer, unless OVERDUB is latched.
        if (!d.held) { this.loopState = 'off'; this.overdub = false; this.dubLatched = false; this.events.push('loop-stop'); }
        else { this.overdub = this.dubLatched; this.events.push('loop-wave'); }
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

  // CLEAR: the take goes, and UNDO brings it back. A playing loop fades out first, the way a
  // tap stops it, so nothing clicks. A recording, or a wait for the bar line, stands down.
  clearLoop() {
    this.settlePending();
    this.rec.phase = 'idle';
    if (this.resampling) this.cancelResample();
    if (this.lsrc < 64) return;
    this.overdub = false; this.dubLatched = false;
    if (this.loopState === 'play' || this.loopGain > 0) { this.loopState = 'off'; this.pendingOp = 'clear'; this.resumeAfterOp = false; return; }
    this.doClear();
  }

  // The page's RESET: the wet side and any layer stand down, the take goes (fading out first if
  // it's playing, as CLEAR does) and nothing is kept for UNDO. The settings are the page's to
  // put back; the memory of what was just heard carries on.
  fullReset() {
    this.fsDown.wet = null; this.freeze = false; this.freezeTapped = false;
    this.fsDown.loop = null;
    this.overdub = false; this.dubLatched = false; this.p.overdubLatch = 0;
    this.clearLoop();
    if (this.pendingOp) this.dropUndo = true; else this.forgetUndo();
  }
  forgetUndo() {
    // Nothing kept for UNDO: every version goes (the take playing, if any, keeps its region).
    for (let v = 0; v < VERSIONS; v++) this.freeVersion(v);
    this.layerVersion = -1; this.pendingSwap = -1; this.dropUndo = false;
    this.versionEvents.push({ type: 'reset' });
  }

  // UNDO and REDO: swap a version back in (see brood.h).
  swapVersion(id) {
    this.settlePending();
    const v = this.versionIndex(id);
    if (v < 0) return;
    this.resumeAfterOp = this.loopState === 'play' && this.versions[v].kind === VERSION_KIND.layer;
    this.overdub = false; this.dubLatched = false;
    this.pendingSwap = id; this.pendingOp = 'swap';
    if (this.loopState === 'play' || this.loopGain > 0) { this.loopState = 'off'; return; }
    this.settlePending();
  }
  releaseVersion(id) { const v = this.versionIndex(id); if (v >= 0) this.freeVersion(v); }
  cancelActive() {
    if (this.rec.phase !== 'idle') { this.rec.phase = 'idle'; return true; }
    if (this.resampling) { this.cancelResample(); return true; }
    return false;
  }
  // RESAMPLE's key (see brood.h).
  resampleKey() {
    this.settlePending();
    if (this.resampling) { this.beginHandover(false); return; }
    if (!this.canResample()) return;
    const r = this.allocRegion(RESAMPLE_CAP);
    if (r < 0) return;
    this.resampling = true; this.rsHandover = false; this.rsLimitHit = false; this.rsRegion = r; this.rsW = 0; this.rsTravel = 0; this.rsInTail = false; this.rsLen = 0; this.rsTail = 0; this.rsWetSeen = false;
    this.rsTarget = this.resampleTarget();
    this.rsLimitAt = this.hostCount + Math.round(RESAMPLE_MAX_SEC * this.fs);
    this.rsStartHost = this.hostCount;
    this.rsFollows = this.takeFollows && !!this.p.hostPlaying; this.rsAnchor = this.songNow();
  }
  setSide(side) { this.wetSide = side === 1 ? 1 : 0; }
  resampleEstimate() {
    const rate = Math.abs(this.loopRate()) * this.clockRatio * this.fs;
    const need = this.resampleTarget();
    return rate < 1e-6 ? 1e9 : need / rate;
  }
  canResample() { return this.loopState === 'play' && this.lsrc >= 64 && this.pendingOp === null && !this.overdub && this.dubGain < 1e-4 && this.rec.phase === 'idle'; }
  resampleTarget() {
    return this.lsrc < 64 ? 0 : (this.p.loopMode === 1 && this.lengthSlew >= 0 ? this.loopWindowAt(this.lengthSlew) : this.loopWindow()).W;
  }
  cancelResample() { if (!this.resampling) return; this.resampling = false; this.rsHandover = false; this.freeRegion(this.rsRegion); this.rsRegion = -1; }
  beginHandover(limit) {
    if (!this.resampling || this.rsHandover) return;
    this.rsHandover = true; this.rsHandLimit = limit; this.rsSinceEnd = 0;
  }
  finishResample(limit) {
    if (!this.resampling) return;
    this.resampling = false; this.rsHandover = false;
    const off = this.regions[this.rsRegion].off, a = this.arena;
    let main = this.rsInTail ? this.rsLen : this.rsW;
    let F = this.rsInTail ? this.rsW - this.rsLen : 0;
    if (F <= 0) {
      F = Math.min(Math.round(0.01 * this.clockRatio * this.fs), Math.trunc(main / 4));
      main -= F;
    }
    if (main < 64) { this.freeRegion(this.rsRegion); this.rsRegion = -1; return; }
    for (let i = 0; i < F; i++) {
      const w = i / F;
      a[off + i] = a[off + i] * w + a[off + main + i] * (1 - w);
    }
    this.dropRedo(); this.endLayer();
    const v = this.keepTake(VERSION_KIND.resample);
    const region = this.rsRegion; this.rsRegion = -1; this.regions[region].len = Math.ceil(main / CHUNK) * CHUNK;
    this.setTake(region);
    // Where the new take should be by now (see brood.h).
    this.lsrc = main; this.loopPos = (this.rsW - main + this.rsSinceEnd) % main;
    this.grain[0].ph = 0; this.grain[1].ph = 0.5;
    this.grain[0].start = 0; this.grain[1].start = 0;
    this.grain[0].dir = 1; this.grain[1].dir = 1;
    this.grain[0].off = -1; this.grain[1].off = -1;
    this.takeFollows = this.rsFollows; this.anchorPpq = this.rsAnchor; this.parked = false; this.pendingLock = false; this.lockGain = 1;
    this.loopState = 'play'; this.events.push('loop-wave');
    this.rsLimitHit = limit;
    const p = this.p, autos = [];
    this.fitWindow();
    autos.push({ which: AUTO.start, value: 1 }, { which: AUTO.loopEnd, value: 1 }, { which: AUTO.length, value: 1 });
    p.length = 1; this.s.length = 1;
    p.loopMode = 1; autos.push({ which: AUTO.loopMode, value: 1 });
    p.modLoop = 0.66; this.s.modLoop = 0.66; this.tapeSpeed = 1; autos.push({ which: AUTO.modLoop, value: 0.66 });
    p.loopDiv = 0; autos.push({ which: AUTO.loopDiv, value: 0 });
    p.loopLevel = 0.8; this.s.loopLevel = 0.8; autos.push({ which: AUTO.loopLevel, value: 0.8 });
    if (this.rsWetSeen && (p.routing === 1 || p.routing === 2)) {
      p.routing = p.routing === 2 ? 3 : 0; autos.push({ which: AUTO.routing, value: p.routing });
      this.ring = true; this.ringRouting = p.routing;
      if (p.freeze > 0.5) { p.freeze = 0; autos.push({ which: AUTO.freeze, value: 0 }); }
      this.freeze = false;
    }
    for (let k = 0; k < 2; k++) {
      const n = k + 1;
      let loopOnly = !!p[`mod${n}On`], any = false;
      for (let t2 = 0; t2 < MOD_TARGETS.length; t2++) {
        if (Math.abs(p[`mod${n}_${MOD_TARGETS[t2]}`]) < 1e-6) continue;
        any = true;
        if (t2 !== 2 && t2 !== 5 && t2 !== 6) loopOnly = false;
      }
      if (loopOnly && any) { p[`mod${n}On`] = 0; autos.push({ which: k === 0 ? AUTO.mod1On : AUTO.mod2On, value: 0 }); }
    }
    this.announce(v, autos);
  }
  resampleTick(loopOut, wetHere) {
    if (this.loopState !== 'play') { this.finishResample(false); return; }
    if (this.rsHandover) {
      this.rsSinceEnd++;
      this.rsDip = Math.max(0, this.rsDip - 1 / (0.005 * CORE_FS));
      if (this.rsDip <= 0) this.finishResample(this.rsHandLimit);
      return;
    }
    const rl = this.rLoop * (1 - this.rIn);
    if (rl * this.gGain > 1e-3) this.rsWetSeen = true;
    this.arena[this.regions[this.rsRegion].off + this.rsW++] = softClip(loopOut * (1 - rl * (1 - this.dGain)) + rl * this.gGain * wetHere);
    if (!this.rsInTail) {
      this.rsTravel += this.travelStep;
      if (this.rsTravel >= this.rsTarget) {
        this.rsInTail = true; this.rsLen = this.rsW;
        this.rsTail = Math.max(1, Math.min(Math.round(0.01 * this.clockRatio * this.fs), Math.trunc(this.rsLen / 4)));
      }
    }
    if (this.rsInTail && this.rsW - this.rsLen >= this.rsTail) { this.beginHandover(false); return; }
    if (this.rsW >= RESAMPLE_CAP) this.beginHandover(true);
  }
  resamplingNeed() {
    if (!this.resampling) return this.resampleEstimate();
    const rate = Math.abs(this.loopRate()) * this.clockRatio * this.fs;
    const secs = (this.hostCount - this.rsStartHost) / this.fs;
    return rate < 1e-6 ? 1e9 : secs + Math.max(0, this.rsTarget - this.rsTravel) / rate;
  }
  popVersionEvent() { return this.versionEvents.length ? this.versionEvents.shift() : null; }

  // ── the arena and its versions ── (mirrors native/dsp/brood.h)
  versionIndex(id) {
    for (let v = 0; v < VERSIONS; v++) if (this.versions[v].used && this.versions[v].id === id) return v;
    return -1;
  }
  freeRegion(r) { if (r >= 0) { const g = this.regions[r]; g.off = 0; g.len = 0; g.used = false; } }
  freeVersion(v) {
    const ver = this.versions[v];
    if (!ver.used) return;
    this.freeRegion(ver.region);
    if (this.layerVersion === v) this.layerVersion = -1;
    ver.used = false; ver.region = -1;
  }
  evictOldest() {
    let oldest = -1;
    for (let v = 0; v < VERSIONS; v++)
      if (this.versions[v].used && v !== this.layerVersion && (oldest < 0 || this.versions[v].id < this.versions[oldest].id)) oldest = v;
    if (oldest < 0) return false;
    const id = this.versions[oldest].id;
    this.freeVersion(oldest);
    this.versionEvents.push({ type: 'evicted', id });
    return true;
  }
  allocRegion(n) {
    n = Math.ceil(n / CHUNK) * CHUNK;   // whole chunks (see brood.h)
    for (;;) {
      let slot = -1;
      for (let r = 0; r < REGIONS; r++) if (!this.regions[r].used) { slot = r; break; }
      if (slot >= 0) {
        let at = 0;
        for (;;) {
          let moved = false;
          for (const r of this.regions) if (r.used && at < r.off + r.len && r.off < at + n) { at = r.off + r.len; moved = true; }
          if (!moved) break;
        }
        if (at + n <= ARENA_SAMPLES) { const g = this.regions[slot]; g.off = at; g.len = n; g.used = true; return slot; }
      }
      if (!this.evictOldest()) return -1;
    }
  }
  dropRedo() {
    for (let v = 0; v < VERSIONS; v++)
      if (this.versions[v].used && this.versions[v].undone && v !== this.layerVersion) {
        const id = this.versions[v].id;
        this.freeVersion(v); this.versionEvents.push({ type: 'released', id });
      }
  }
  newVersion(kind) {
    let slot = -1;
    for (let v = 0; v < VERSIONS; v++) if (!this.versions[v].used) { slot = v; break; }
    while (slot < 0) {
      if (!this.evictOldest()) return -1;
      for (let v = 0; v < VERSIONS; v++) if (!this.versions[v].used) { slot = v; break; }
    }
    const ver = this.versions[slot];
    ver.used = true; ver.undone = false; ver.id = this.nextVersionId++; ver.kind = kind;
    ver.region = -1; ver.len = 0; ver.follows = false; ver.anchor = 0; ver.base = -1;
    return slot;
  }
  keepTake(kind) {
    const v = this.newVersion(kind);
    if (v < 0) return -1;
    const ver = this.versions[v];
    if (this.lsrc >= 64 && this.curRegion >= 0) { ver.region = this.curRegion; ver.len = this.lsrc; }
    else this.freeRegion(this.curRegion);
    ver.follows = this.takeFollows; ver.anchor = this.anchorPpq;
    this.curRegion = -1; this.loopBuf = null;
    return v;
  }
  announce(v, autos = []) {
    if (v < 0) return;
    this.versionEvents.push({ type: 'added', id: this.versions[v].id, kind: this.versions[v].kind, autos: autos.slice(0, 12) });
  }
  keepChunk(cell) {
    if (this.layerVersion < 0) return;
    const ver = this.versions[this.layerVersion];
    const c = Math.floor(cell / CHUNK);
    if (c < 0 || c >= CHUNKS || ver.dirty[c]) return;
    ver.dirty[c] = 1;
    const from = c * CHUNK, to = Math.min(this.lsrc, from + CHUNK);
    if (ver.region >= 0 && to > from) this.arena.copyWithin(this.regions[ver.region].off + from, this.regions[this.curRegion].off + from, this.regions[this.curRegion].off + to);
  }
  setTake(r) { this.curRegion = r; this.loopBuf = r >= 0 ? this.arena.subarray(this.regions[r].off, this.regions[r].off + MAX_LOOP_SAMPLES > ARENA_SAMPLES ? ARENA_SAMPLES : this.regions[r].off + MAX_LOOP_SAMPLES) : null; }

  canOverdub() { return this.loopState === 'play' && this.lsrc >= 64 && this.pendingOp === null && this.p.monitor !== 2 && !this.resampling; }
  takeFreezeTap() { const t = this.freezeTapped; this.freezeTapped = false; return t; }

  // INPUT MONITOR's target (see brood.h).
  monTarget() {
    const m = this.p.monitor;
    if (m === 1) return 1;
    if (m === 2) return 0;
    return this.loopState === 'play' && !this.overdub ? 0 : 1;
  }
  // The routing WET has now (see brood.h).
  routingEffective() {
    const r = Math.min(3, Math.max(0, this.p.routing | 0));
    if (r === 1 && this.resampling) return 2;
    if (r === 1 && (this.loopState !== 'play' || this.overdub)) return 0;
    return r;
  }
  mixTick(aR) {
    this.routingNow = this.routingEffective();
    const rn = this.routingNow;
    this.mon += (this.monTarget() - this.mon) * aR;
    this.rIn += ((rn === 0 || rn === 1 ? 1 : 0) - this.rIn) * aR;
    this.rLoop += ((rn === 1 || rn === 2 ? 1 : 0) - this.rLoop) * aR;
    this.wetMixNow = clamp(this.e.wetMix, 0, 1);
    this.dGain = Math.min(1, 2 * (1 - this.wetMixNow));
    this.gGain = Math.min(1, 2 * this.wetMixNow);
    this.recK = this.rIn * (1 - this.rLoop) * this.mon;
    // RESAMPLE: the input's way into WET closes in 5 ms, all the way (see brood.h).
    const step = 1 / (0.005 * this.fs);
    this.rsFeedIn = this.resampling ? Math.max(0, this.rsFeedIn - step) : Math.min(1, this.rsFeedIn + step);
  }

  // A new layer: a version that keeps the old side of every chunk it writes (keepChunk).
  beginLayer() {
    if (this.overdub) return;
    this.dropRedo();
    const v = this.newVersion(VERSION_KIND.layer);
    if (v >= 0) {
      const r = this.allocRegion(this.lsrc);
      const ver = this.versions[v];
      ver.region = r; ver.len = this.lsrc; ver.base = this.curRegion; ver.follows = this.takeFollows; ver.anchor = this.anchorPpq;
      ver.dirty.fill(0);
      if (r < 0) this.freeVersion(v);
      else { this.layerVersion = v; this.announce(v); }
    }
    this.overdub = true;
  }
  endLayer() { this.layerVersion = -1; }

  doClear() {
    this.dropRedo();
    const v = this.keepTake(VERSION_KIND.clear);
    this.takeFollows = false; this.parked = false;
    this.lsrc = 0; this.loopPos = 0;
    this.events.push('loop-wave');
    this.announce(v);
  }

  runOp(op) {
    if (op === 'clear') { this.doClear(); return; }
    const v = this.versionIndex(this.pendingSwap);
    this.pendingSwap = -1;
    if (v < 0) return;
    const ver = this.versions[v];
    this.endLayer();
    if (ver.kind === VERSION_KIND.layer) {
      if (ver.base === this.curRegion && ver.region >= 0 && this.curRegion >= 0) {
        const a = this.arena, liveOff = this.regions[this.curRegion].off, copyOff = this.regions[ver.region].off;
        for (let c = 0; c < CHUNKS; c++) {
          if (!ver.dirty[c]) continue;
          const from = c * CHUNK, to = Math.min(Math.min(this.lsrc, ver.len), from + CHUNK);
          for (let i = from; i < to; i++) { const t = a[liveOff + i]; a[liveOff + i] = a[copyOff + i]; a[copyOff + i] = t; }
        }
      }
    } else {
      const r = this.curRegion, len = this.lsrc, fol = this.takeFollows, anc = this.anchorPpq;
      this.setTake(ver.region); this.lsrc = ver.len; this.takeFollows = ver.follows; this.anchorPpq = ver.anchor;
      ver.region = r; ver.len = len; ver.follows = fol; ver.anchor = anc;
      if (this.curRegion < 0) this.lsrc = 0;
      this.loopPos = 0;
      this.grain[0].ph = 0; this.grain[1].ph = 0.5;
      this.grain[0].start = 0; this.grain[1].start = 0;
      this.grain[0].dir = 1; this.grain[1].dir = 1;
      this.grain[0].off = -1; this.grain[1].off = -1;
    }
    ver.undone = !ver.undone;
    this.events.push('loop-wave');
  }

  // A CLEAR or a swap waiting for the loop to fade out happens now (see brood.h).
  settlePending() {
    if (!this.pendingOp) return;
    const op = this.pendingOp;
    this.pendingOp = null;
    const v = this.versionIndex(this.pendingSwap);
    const layer = v >= 0 && this.versions[v].kind === VERSION_KIND.layer;
    this.runOp(op);
    this.parked = false;
    if (op === 'swap' && this.lsrc >= 64 && (!layer || this.resumeAfterOp)) {
      if (this.takeFollows && !this.p.hostPlaying) this.parked = true;
      else { this.loopState = 'play'; if (this.takeFollows) { this.pendingLock = true; this.lockGain = 0; this.lockShift = NaN; } }
    }
    this.resumeAfterOp = false;
    if (this.dropUndo) this.forgetUndo();
  }

  // ── the host's transport ── (mirrors native/dsp/brood.h)
  songNow() { return this.p.ppq + (this.hostCount - this.blockStartCount) * this.p.bpm / (60 * this.fs); }
  // The loop's playback rate now, in window ticks a core tick (see brood.h).
  loopRate() {
    const r = this.p.loopMode === 1 ? this.tapeSpeed : this.p.loopMode === 2 ? this.stretchSpeed : 1;
    return r / divMap(this.p.loopDiv);
  }
  songTicks(winStart, W) {
    const secs = (this.songNow() - this.anchorPpq) * 60 / Math.max(5, this.p.bpm);
    let q = (secs * this.clockRatio * this.fs * this.loopRate() - winStart) % W; if (q < 0) q += W;
    return q;
  }
  followTransport(n) {
    const playing = !!this.p.hostPlaying;
    if (this.takeFollows && this.lsrc >= 64) {
      if (this.lastHostPlaying && !playing && this.loopState === 'play') {
        this.loopState = 'off'; this.overdub = false; this.dubLatched = false; this.parked = true;
      } else if (!this.lastHostPlaying && playing && this.parked) {
        this.parked = false; this.loopState = 'play'; this.pendingLock = true; this.lockGain = 0;
        this.lockShift = NaN;   // from the song itself
      } else if (this.lastHostPlaying && playing && this.loopState === 'play' && this.lastBlockN > 0) {
        const expected = this.lastPpq + this.lastBlockN * this.p.bpm / (60 * this.fs);
        if (Math.abs(this.p.ppq - expected) > 0.05) {   // a jump, not drift
          this.pendingLock = true;
          // At 1x it locks back to the song; at any other rate it moves by the jump (see brood.h).
          const rate = this.loopRate();
          this.lockShift = Math.abs(rate - 1) < 1e-3 ? NaN : (this.p.ppq - expected) * 60 / Math.max(5, this.p.bpm) * this.clockRatio * this.fs * rate;
        }
      }
    }
    this.lastHostPlaying = playing; this.lastPpq = this.p.ppq; this.lastBlockN = n;
  }

  // Copy the most recent n samples of history into the loop buffer and play.
  // How much history a capture may use, in core samples. MEMORY 0 keeps the
  // pedal's clock-derived capacity; above that it is 1–30 s of real time.
  // Floored to a whole sample: MEMORY is a real-time ceiling, so its sample count
  // is fractional, and a fractional capture length made histW - n fractional,
  // which read undefined out of the ring and filled the loop buffer with NaN.
  // In CAPTURE it's MEMORY; RECORD ignores MEMORY: the bars, or the second tap, decide, up to
  // all the buffer holds (32 s). Mirrors maxCaptureSamples() in native/dsp/brood.h.
  maxCaptureSamples() {
    const clock = clockHz(this.e.clock, this.p.smooth);
    const most = Math.min(MAX_LOOP_SAMPLES - 1, MAX_LOOP_SECONDS * clock);
    if (this.p.recMode === 1) return Math.floor(Math.max(64, most));
    const user = this.p.memory > 0.001 ? expMap(clamp(this.p.memory), 1, 30) * clock : LOOP_SAMPLES;
    return Math.floor(Math.max(64, Math.min(user, most)));
  }

  // RECORD: from here, for the chosen bars (from the next bar line, when `atBar`).
  startRecording(atBar) {
    const p = this.p, rec = this.rec;
    if (atBar) { rec.histIdx = this.histW; rec.histCycle = this.histCycles; }
    rec.phase = 'recording';
    rec.endAt = p.recBars > 0 ? this.hostCount + Math.round(p.recBars * Math.max(0.25, p.barQuarters) * 60 / Math.max(5, p.bpm) * this.fs) : -1;
  }

  // Everything written since the recording began becomes the loop, which plays at once.
  closeRecording() {
    const rec = this.rec;
    const written = (this.histCycles - rec.histCycle) * this.hist.length + this.histW - rec.histIdx;
    rec.phase = 'idle';
    this.capture(Math.min(this.hist.length - 1, Math.max(0, written)));
  }

  capture(n) {
    n = Math.floor(Math.min(n, this.hist.length - 1, this.histCount, this.maxCaptureSamples()));
    if (n < 64) return;
    // The take this replaces becomes a version UNDO can swap back (see brood.h).
    this.dropRedo();
    this.endLayer();
    const region = this.allocRegion(n);
    if (region < 0) return;
    const v = this.keepTake(VERSION_KIND.take);
    this.setTake(region);
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
    this.grain[0].dir = 1; this.grain[1].dir = 1;
    this.grain[0].off = -1; this.grain[1].off = -1;
    this.loopState = 'play';
    this.events.push('loop-wave');
    this.fitWindow();
    // Caught while the host plays: it follows the transport, anchored at the song position
    // its first sample belongs to.
    this.takeFollows = !!this.p.hostPlaying; this.parked = false; this.pendingLock = false; this.lockGain = 1;
    this.anchorPpq = this.songNow() - n / Math.max(1, this.clockRatio * this.fs) * this.p.bpm / 60;
    const autos = [{ which: AUTO.start, value: 1 }, { which: AUTO.loopEnd, value: 1 }, { which: AUTO.length, value: 1 }];
    this.announce(v, this.p.loopMode === 1 ? autos : autos.slice(0, 2));
  }

  // A new take plays all of itself: END at the end, START out of the way and, in TAPE,
  // LENGTH full, so 2 bars recorded is 2 bars heard. The core sets its own copy at once,
  // so not one tick plays the old window; takeFitted() tells the panel to move its knobs.
  fitWindow() {
    this.p.loopEnd = 1; this.p.start = 1;
    if (this.p.loopMode === 1) { this.p.length = 1; this.s.length = 1; }
    this.lengthSlew = -1; this.startLen = -1;   // the window starts afresh, not gliding from the old one
    this.fitted = true;
  }

  // Once after a take arrived and fitted the window: null, or whether LENGTH moved too.
  takeFitted() {
    if (!this.fitted) return null;
    this.fitted = false;
    return { length: this.p.loopMode === 1 };
  }

  // Loop region being played. LENGTH decides how big the window is (TAPE takes it
  // from the newest part of the capture, ENV and STRETCH from the whole take), END
  // decides how much of that window sounds before it wraps, and START decides where
  // the window sits inside the capture — 0 is where each mode has always begun.
  loopWindow() { return this.loopWindowAt(this.e.length); }

  // The window for a given LENGTH. Overdub asks for the knob's own, ignoring modulation.
  loopWindowAt(lengthNow) {
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
    const W0 = Math.max(64, Math.round(len * Math.min(1, 0.02 + 0.98 * clamp(lengthNow))));
    // START means the same as in ENV and STRETCH: 0 is the oldest audio, 1 the newest.
    // At 1 the window sits against the end of the take whatever LENGTH is, so TAPE
    // still follows the newest audio as LENGTH turns; anywhere else START is a fixed
    // point in the take that LENGTH grows and shrinks the window from.
    const travel = Math.max(0, Math.round(where * Math.max(0, len - 64)));
    return { W: Math.max(64, Math.floor(W0 * trim)), start: Math.min(travel, Math.max(0, len - W0)) };
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
    if (wave === 1) return ph < 0.5 ? 1 : 0;          // square
    if (wave === 2) return 0.5 + 0.5 * Math.sin(TAU * ph);
    if (wave === 3) return ph;                        // sawtooth: climbs, then snaps back
    if (wave === 4) return g.next;                    // sample and hold
    if (wave === 5) {                                 // smooth random
      const t = ph * ph * (3 - 2 * ph);
      return g.prev + (g.next - g.prev) * t;
    }
    return 1 - Math.abs(2 * ph - 1);                  // triangle
  }

  // Advance both sources, once per host sample, not per core tick: a source is a time
  // function, so turning CLOCK down must not slow it down. HOLD stops the phase, and a
  // source with no routes still runs, so its chip shows what it would do.
  rampTick() {
    for (let k = 0; k < 2; k++) {
      const n = k + 1, p = this.p, g = this.ramp[k];
      if (!p[`mod${n}Hold`]) {
        // Free-running by default; synced, one cycle is a musical span instead.
        const hz = p[`mod${n}Sync`] ? 1 / rampCycleSeconds(p.bpm, this.s.modRate[k]) : rampHz(this.s.modRate[k]);
        g.ph += hz / this.fs;
        if (g.ph >= 1) { g.ph -= 1; g.prev = g.next; g.next = this.rampRand(g); }
      }
      this.rampRaw[k] = this.rampShape(g, rampWaveIndex(p[`mod${n}Wave`]));
    }
  }

  // Rebuild the effective controls from the smoothed knobs, then let each source push
  // what it's routed to, around where the knob sits.
  applyRamps(a) {
    const e = this.e, s = this.s, p = this.p;
    for (const key of MOD_TARGETS) e[key] = s[key];
    for (let k = 0; k < 2; k++) {
      const n = k + 1;
      this.rampVal[k] += (this.rampRaw[k] - this.rampVal[k]) * a;
      this.modGainS[k] += ((p[`mod${n}On`] ? 1 : 0) - this.modGainS[k]) * a;
      const swing = (p[`mod${n}Uni`] ? this.rampVal[k] : this.rampVal[k] * 2 - 1) * this.modGainS[k];
      for (let t = 0; t < MOD_TARGETS.length; t++) {
        this.modDepthS[k][t] += (p[`mod${n}_${MOD_TARGETS[t]}`] - this.modDepthS[k][t]) * a;
        e[MOD_TARGETS[t]] += swing * this.modDepthS[k][t];
      }
    }
    for (const key of MOD_TARGETS) e[key] = clamp(e[key], 0, 1);
  }

  // One-pole coefficient for the TONE hi-cut. The amount of cut is the caller's;
  // this is only the filter's corner, worked out once.
  toneCoeff() {
    if (this.toneC === undefined) this.toneC = 1 - Math.exp((-TAU * TONE_HZ) / CORE_FS);
    return this.toneC;
  }

  // `x` is what the looper listens to; `rec` is what an overdub lays down.
  // Where the loop is heard from, inside the window, and how loud each read is (0 to 1).
  // TAPE: one head, the playhead. STRETCH: its two grains. ENV: the slice while it's the
  // louder sound (and the last one fading under it), else the playhead. Mirrors brood.h.
  heardAt(heard, hpos, hlev, W = 1) {
    if (!heard) { this.heardPos[0] = this.loopPos; this.heardPos[1] = 0; this.heardLevel[0] = 1; this.heardLevel[1] = 0; return; }
    for (let k = 0; k < 2; k++) { this.heardPos[k] = ((hpos[k] % W) + W) % W; this.heardLevel[k] = hlev[k]; }
  }

  loopTick(x, rec) {
    // The detector and overdub use clean input, even when the wet channel is on.
    const fs = CORE_FS;
    const a = Math.abs(x);
    this.env += (a - this.env) * (a > this.env ? 0.02 : 0.0005);

    this.travelStep = 0;
    const target = this.loopState === 'play' ? 1 : 0;
    this.loopGain += (target - this.loopGain) * 0.003;
    if (this.loopState === 'off' && this.loopGain < 1e-4) {
      this.loopGain = 0;
      if (this.pendingOp) this.settlePending();   // faded out: CLEAR or UNDO now
      this.heardAt(false);
      this.loopBare = 0;
      return 0;
    }
    if (this.lsrc < 64) { this.heardAt(false); this.loopBare = 0; return 0; }

    // TAPE: the window follows LENGTH at up to three samples a tick, so the playhead,
    // which keeps its share of the window, never runs faster than four times normal
    // (two octaves up). Faster, deep LENGTH modulation zipped through the take.
    const len0 = Math.max(64, this.lsrc);
    const maxL = LENGTH_FOLLOW / (0.98 * len0);
    if (this.lengthSlew < 0 || this.lsrc !== this.startLen) this.lengthSlew = this.e.length;
    else this.lengthSlew += clamp(this.e.length - this.lengthSlew, -maxL, maxL);
    const win = this.p.loopMode === 1 ? this.loopWindowAt(this.lengthSlew) : this.loopWindow();
    // The window the controls ask for. What plays is winB from startB, which takes this
    // window up through a crossfade (below), so a dragged marker never jumps the loop.
    const tW = win.W, winStart = win.start;
    // The song lock: dip for 5 ms, move the playhead to where the song says, come back.
    if (this.pendingLock) {
      this.lockGain = Math.max(0, this.lockGain - 1 / (0.005 * CORE_FS));
      if (this.lockGain <= 0) {
        if (Number.isNaN(this.lockShift)) this.loopPos = this.songTicks(winStart, tW);
        else { this.loopPos = (this.loopPos + this.lockShift) % tW; if (this.loopPos < 0) this.loopPos += tW; }
        this.lockShift = NaN;
        this.pendingLock = false;
      }
    } else if (this.lockGain < 1) this.lockGain = Math.min(1, this.lockGain + 1 / (0.005 * CORE_FS));
    const buf = this.loopBuf;
    const len = Math.max(64, this.lsrc);
    const mode = this.p.loopMode;
    const m = this.e.modLoop;
    const trimNow = clamp(this.p.loopEnd, 0.02, 1), whereNow = clamp(this.p.start, 0, 1);
    // Only LENGTH moved the window: same mode, same END, same START, same take, and what
    // plays is what was asked for last tick (no marker fade still under way).
    const lengthMoved = mode === 1 && this.lastMode === 1 && this.lsrc === this.startLen && trimNow === this.lastTrim && whereNow === this.lastWhere && this.lastW > 0 && tW !== this.lastW
      && this.startFade >= 1 && this.winB === this.lastW && this.startB === this.lastWant;
    if (lengthMoved) this.loopPos = this.loopPos * tW / this.lastW;
    // A change of loop mode keeps the playhead where it is in the take when the new
    // window holds it; otherwise it crossfades from there to the new window's start.
    const modeSwitched = this.lastMode >= 0 && mode !== this.lastMode && this.lsrc === this.startLen && this.lastW > 0;
    let switchFrom = -1;
    if (modeSwitched) {
      const absNow = this.startB + this.loopPos;
      const inNew = absNow - winStart;
      if (inNew >= 0 && inNew < tW) this.loopPos = inNew;
      else { this.loopPos = 0; switchFrom = Math.floor(absNow); }
    }
    // LOOP CLOCK DIVISION: the loop reads at 1/div of the clock, so a divided
    // loop plays back slower and lower and takes div times as long to come round.
    const ldiv = divMap(this.p.loopDiv);
    const lstep = 1 / ldiv;

    // START moves the read head, and a jump in the read head is a click. So a
    // change of START is a crossfade from where the loop was playing to where it
    // now begins: 25 ms, weighted by a raised cosine so the fade has no corners
    // of its own either. A capture or a restore resets it — there is a gain ramp
    // around both, so landing instantly there cannot be heard.
    if (this.lsrc !== this.startLen) { this.startLen = this.lsrc; this.startA = winStart; this.startB = winStart; this.winA = this.winB = tW; this.shiftA = 0; this.carryA = 0; this.startFade = 1; }
    if (lengthMoved && winStart !== this.lastWant) {
      // The window slid with LENGTH and the playhead slid with it: move both heads
      // the same way, so any fade under way carries on and none starts.
      this.startA += winStart - this.lastWant;
      this.startB += winStart - this.lastWant;
    }
    if (lengthMoved) { this.winA = this.winB = tW; }
    if (modeSwitched) {
      if (switchFrom >= 0) { this.startA = switchFrom; this.startB = winStart; this.startFade = 0; }
      else { this.startA = winStart; this.startB = winStart; this.startFade = 1; }
      this.winA = this.winB = tW; this.shiftA = 0; this.carryA = 0;
    }
    this.lastW = tW; this.lastWant = winStart; this.lastMode = mode; this.lastTrim = trimNow; this.lastWhere = whereNow;
    if (this.startFade >= 1) {
      this.startA = this.startB; this.winA = this.winB; this.shiftA = 0; this.carryA = 0;   // the last fade has landed
      if (winStart !== this.startB || tW !== this.winB) {
        // A new window. The playhead keeps its place in it, wrapped if the window got
        // shorter; head A carries on where it was (shiftA on), and the slices and grains,
        // which count from the playhead's frame, move into the new one with it.
        const pOld = this.loopPos;
        let pNew = pOld % tW; if (pNew < 0) pNew += tW;
        this.shiftA = pOld - pNew;
        if (this.shiftA !== 0) {
          this.loopPos = pNew;
          this.slice.start -= this.shiftA; this.softStart -= this.shiftA;
          for (const g of this.grain) g.start -= this.shiftA;
        }
        this.startB = winStart; this.winB = tW; this.startFade = 0;
      }
    } else {
      // The head we are fading *to* stays put while it fades in. Moving it mid-fade
      // is the same jump in a smaller dose: a knob still being dragged simply starts
      // the next fade as soon as this one lands.
      this.startFade = Math.min(1, this.startFade + 1 / (CORE_FS * 0.025));
    }
    const wB = 0.5 - 0.5 * Math.cos(Math.PI * this.startFade);   // 0 -> A, 1 -> B
    // From here on the window is the one playing.
    const W = this.winB;
    let pos = this.loopPos % W; if (pos < 0) pos += W;
    const posAtStart = pos;
    // One head's read: the take at base + p wrapped into its window, faded at that window's
    // seam (over 4 ms, or 20 ms with a raised cosine for SOFT), and by shape where shape is
    // the smaller.
    const at = (base, Wh, p, shape, soft) => {
      let q = p % Wh; if (q < 0) q += Wh;
      const Fh = Math.min((soft ? 0.02 : 0.004) * fs, Wh / 4);
      let edge = Math.min(1, q / Fh, (Wh - q) / Fh);
      if (soft) edge = 0.5 - 0.5 * Math.cos(Math.PI * edge);
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
      return hermite(buf[im1], buf[i0], buf[i1], buf[i2], f) * Math.min(shape, edge);
    };
    // The loop at p, through the crossfade: head A where the loop was, head B where it is now.
    // main: p is the playhead, so head A takes it on past any wrap since the fade began.
    const rd = (p, shape = 1, soft = false, main = false) => {
      if (this.startFade >= 1) return at(this.startB, W, p, shape, soft);
      const a = at(this.startA, this.winA, p + this.shiftA + (main ? this.carryA : 0), shape, soft);
      if (this.startFade <= 0) return a;
      return a * (1 - wB) + at(this.startB, W, p, shape, soft) * wB;
    };
    let y;
    // What the playhead shows: the reads STRETCH and ENV make, where they're heard;
    // otherwise the playhead itself. Drawn only, never heard.
    const hpos = [0, 0], hlev = [1, 0];
    let heard = false;

    if (mode === 2) {
      // STRETCH: two Hann grains at 1x pitch; LENGTH = grain (slice) size,
      // MODIFY = speed and direction of the position through the loop.
      const G = Math.min(expMap(this.e.length, 0.01 * fs, 0.5 * fs), W);
      // A divided loop stretches each grain by div as well, or the grains would
      // still be read at full speed and the division would only thin them out.
      const Gs = G * ldiv;
      // A grain keeps the direction it started with. Flipping grains already sounding as
      // MODIFY passes the middle jumped both read heads, and that was a click.
      const dir = this.stretchSpeed < 0 ? -1 : 1;
      y = 0;
      for (let k = 0; k < 2; k++) {
        const g = this.grain[k];
        // The read head runs on its own count at normal speed (see native/dsp/brood.h).
        if (!(g.off >= 0)) g.off = g.ph * G;
        g.ph += 1 / Gs;
        g.off += G / Gs;
        if (g.ph >= 1) { g.ph -= 1; g.start = pos + (this.rand() - 0.5) * 0.2 * G; g.dir = dir; g.off = g.ph * G; }
        const p = g.start + g.dir * g.off;
        const sw = Math.sin(Math.PI * g.ph);
        y += rd(p) * sw * sw;
        hpos[k] = p; hlev[k] = sw * sw;
      }
      // Blend to the unprocessed loop at full clockwise, per MKII behavior.
      const normal = clamp((m - 0.97) / 0.03, 0, 1);
      y = y * (1 - normal) + rd(pos, 1, false, true) * normal;
      // Full clockwise is the plain loop: the playhead again once that's the louder.
      if (normal < 0.5) { heard = true; hlev[0] *= 1 - normal; hlev[1] *= 1 - normal; }
      this.stretchSpeed += (stretchMap(m) - this.stretchSpeed) * 0.001;
      pos += this.stretchSpeed * lstep;
    } else if (mode === 1) {
      // TAPE: LENGTH = loop length, MODIFY = speed and direction (pitch follows)
      y = rd(pos, 1, false, true);
      this.tapeSpeed += (speedMap(m, 0.04) - this.tapeSpeed) * 0.00025;
      pos += this.tapeSpeed * lstep;
    } else {
      // ENV: playing interrupts the loop. The slice under the playhead when you
      // hit a note repeats until you stop; then the loop carries on in time.
      // LENGTH = slice size (grains → short phrases), MODIFY = sensitivity.
      const thr = expMap(1 - m, 0.008, 0.35);
      const soft = !!this.p.envSoft;
      if (this.env > thr) {
        if (!this.envGate) {
          this.envGate = true;
          // SOFT: a new slice while the last is still sounding would jump the read
          // head, which is the click. The last one is let go over 20 ms.
          if (soft && this.envMix > 1e-4) { this.softStart = this.slice.start; this.softPh = this.slice.ph; this.softOldS = this.softS; this.softOldFade = 1; }
          this.slice.start = pos;
          this.slice.ph = 0;
          this.sliceFresh = true;
        }
        this.envHold = 0.08 * fs;
      } else if (this.envGate && --this.envHold <= 0) this.envGate = false;
      this.envMix += ((this.envGate ? 1 : 0) - this.envMix) * (soft ? 0.0008 : 0.005);
      const softF = 0.02 * fs;
      const main = rd(pos, 1, soft, true);
      let sl = 0;
      if (this.envMix > 1e-4) {
        // ENV slices to a length, so synced LENGTH is a division of the beat,
        // converted into core ticks the same way the delay's is.
        const S = this.p.envSync
          ? Math.min(W, Math.max(64, syncSecondsRaw(this.p.bpm, this.e.length) * this.clockRatio * this.fs))
          : Math.min(expMap(this.e.length, 0.01 * fs, 1.0 * fs), W);
        const sl0 = this.slice;
        // Synced, a slice plays at its own length (sliceS) and a new LENGTH takes over at the next
        // slice start; with the host playing, slices land on its grid. Mirrors native/dsp/brood.h.
        const synced = !!this.p.envSync;
        if (this.sliceFresh || this.sliceS <= 0) this.sliceS = S;   // a slice keeps its length (see brood.h)
        const handOff = () => { this.softStart = sl0.start; this.softPh = sl0.ph; this.softOldS = soft ? this.softS : this.sliceS; this.softOldFade = 1; };
        const grid = synced && !!this.p.hostPlaying;
        if (grid) {
          const t = this.songNow() * 60 / Math.max(5, this.p.bpm) * this.clockRatio * this.fs * lstep;
          const tLine = Math.floor(t / S), tInto = t - tLine * S;
          if (this.sliceFresh) {
            sl0.ph = tInto; sl0.start = pos - tInto; this.gridLine = tLine;
          } else if (S !== this.sliceS && S === this.gridS && tLine !== this.gridTLine) {
            handOff(); this.sliceS = S; sl0.ph = tInto; this.gridLine = tLine;
          } else {
            const line = Math.floor(t / this.sliceS), into = t - line * this.sliceS;
            if (line !== this.gridLine) {
              if (soft) handOff();
              const carried = sl0.ph - this.sliceS;
              if (Math.abs(carried - into) < 4) sl0.ph = carried;
              else { if (!soft) handOff(); sl0.ph = into; this.sliceIn = 0; }
              this.gridLine = line;
            }
          }
          this.gridS = S; this.gridTLine = tLine;
          if (soft) this.softS = this.sliceS;
        } else this.gridS = -1;
        this.sliceFresh = false;
        if (this.sliceIn < 1) this.sliceIn = Math.min(1, this.sliceIn + 1 / ((soft ? 0.02 : 0.004) * CORE_FS));
        if (soft) {
          // The slice length glides, so a moving LENGTH can't cut a fade short.
          this.softS = this.softS <= 0 ? S : this.softS + (S - this.softS) * 0.0005;
          if (grid) this.softS = this.sliceS;
          if (sl0.ph >= this.softS) sl0.ph -= this.softS;
          const F3 = Math.min(softF, this.softS / 4);
          // Every slice read also fades over the take's seam (see brood.h).
          sl = rd(sl0.start + sl0.ph, 0.5 - 0.5 * Math.cos(Math.PI * Math.min(1, sl0.ph / F3, (this.softS - sl0.ph) / F3))) * this.sliceIn;
          hpos[0] = sl0.start + sl0.ph; hlev[0] = 1; hpos[1] = this.softStart + this.softPh;
          hlev[1] = this.softOldFade > 0 ? 0.5 - 0.5 * Math.cos(Math.PI * this.softOldFade) : 0;
          if (this.softOldFade > 0) {
            const F4 = Math.min(softF, this.softOldS / 4);
            const shape = 0.5 - 0.5 * Math.cos(Math.PI * clamp(Math.min(this.softPh / F4, (this.softOldS - this.softPh) / F4), 0, 1));
            sl += rd(this.softStart + this.softPh, shape) * (0.5 - 0.5 * Math.cos(Math.PI * this.softOldFade));
            this.softPh += lstep;
            this.softOldFade = Math.max(0, this.softOldFade - 1 / softF);
          }
        } else {
          if (!grid && sl0.ph >= this.sliceS) { sl0.ph -= this.sliceS; this.sliceS = S; }
          const F2 = Math.min(0.004 * fs, this.sliceS / 4);
          sl = rd(sl0.start + sl0.ph, Math.max(0, Math.min(1, sl0.ph / F2, (this.sliceS - sl0.ph) / F2))) * this.sliceIn;
          hpos[0] = sl0.start + sl0.ph; hlev[0] = 1; hpos[1] = this.softStart + this.softPh; hlev[1] = Math.max(0, this.softOldFade);
          if (this.softOldFade > 0) {
            const F4 = Math.min(0.004 * CORE_FS, this.softOldS / 4);
            const shape = clamp(Math.min(this.softPh / F4, (this.softOldS - this.softPh) / F4), 0, 1);
            sl += rd(this.softStart + this.softPh, shape) * this.softOldFade;
            this.softPh += lstep;
            this.softOldFade = Math.max(0, this.softOldFade - 1 / (0.004 * CORE_FS));
          }
        }
        sl0.ph += lstep;
      }
      y = main * (1 - this.envMix) + sl * this.envMix;
      heard = this.envMix > 0.5;   // the slice is the louder sound: show where it reads
      pos += lstep;
    }

    // CLASSIC: the MK I looper never sat still — a slow wow with a little flutter.
    let wow = 0;
    if (this.p.classic) {
      this.wowPh += (TAU * 0.9) / CORE_FS;
      if (this.wowPh > TAU) this.wowPh -= TAU;
      wow = 0.006 * Math.sin(this.wowPh) + 0.002 * Math.sin(this.wowPh * 5.7);
      pos += wow;
    }
    // WORN after the loop: the wobble is heard, not written. The overdub's head moves as
    // if the playhead were steady, so what it lays down isn't smeared.
    const dubWow = this.p.wearPost ? wow : 0;

    // Overdub fades in and out over about 8 ms, because a hard start or stop is a step
    // recorded into the take, there on every pass from then on. Its write head keeps its
    // own place in the window it started in, moving at the read head's speed, so a
    // modulated LENGTH or START can't throw it around the take and leave seams: what's
    // laid down stays continuous, and the read head finds it as it comes round. It writes
    // every cell it crosses, once, and fades at that window's edges.
    // Overdubbing slower than normal is a decimation: filter what's laid down first, just under
    // the rate it records at, so nothing folds back as aliasing (see brood.h).
    {
      const rate = Math.abs(pos - posAtStart - dubWow);
      this.travelStep = rate;   // RESAMPLE's length counts this
      if (Math.abs(rate - this.dubAARate) > 0.002 * Math.max(rate, 0.01)) {
        this.dubAARate = rate;
        const fc = Math.max(20, 0.4 * Math.min(1, rate) * CORE_FS);
        this.dubAA[0].set(fc, CORE_FS, true); this.dubAA[1].set(fc, CORE_FS, true);
      }
      const lp = this.dubAA[1].run(this.dubAA[0].run(rec));
      const slow = clamp((1 - rate) / 0.1, 0, 1);
      if (slow > 0) rec += (lp - rec) * slow;
    }
    this.dubGain += ((this.overdub ? 1 : 0) - this.dubGain) * 0.002;
    if (this.dubGain > 1e-4) {
      const starting = this.dubLast < 0;
      if (starting) {
        // The knob's window, not the modulated one, lined up with where the read head is
        // so an unmodulated loop layers exactly where you hear it.
        const base = this.loopWindowAt(this.s.length);
        this.dubW = base.W; this.dubStart = base.start;
        this.dubPos = Math.round(this.startB) + (mode === 1 ? this.loopPos : pos) - this.dubStart;
      }
      else this.dubPos += pos - posAtStart - dubWow;
      this.dubPos %= this.dubW; if (this.dubPos < 0) this.dubPos += this.dubW;
      const dubW = this.dubW;
      const q = Math.min(dubW - 1, Math.floor(this.dubPos));
      // FADE: how much of the old loop survives each overdub pass. 0 keeps the
      // pedal's slow decay; 1 lets the new layer replace it completely.
      // IN + LOOP with the wet side on: the old loop comes back through the wet side, so the
      // layer replaces it by that share instead of stacking a second copy (see brood.h).
      const loss = 1 - 0.97 * (1 - clamp(this.s.fade, 0, 1));
      const dubF = Math.min(0.004 * fs, dubW / 4);
      const write = (cellQ, v) => {
        const amount = this.dubGain * Math.min(1, cellQ / dubF, (dubW - cellQ) / dubF);
        const wi = Math.min(len - 1, Math.max(0, this.dubStart + cellQ));
        this.keepChunk(wi);   // the old side, for UNDO, before it's written over
        if (wi < this.dubLo) this.dubLo = wi;
        if (wi > this.dubHi) this.dubHi = wi;
        buf[wi] = buf[wi] * (1 - loss * amount) + v * amount;
      };
      if (starting) write(q, rec);
      else {
        let d = q - this.dubLast;
        if (d * 2 > dubW) d -= dubW; else if (d * 2 < -dubW) d += dubW;
        const count = Math.min(Math.abs(d), 16), dir = d > 0 ? 1 : -1;
        for (let k = 1; k <= count; k++) write(((this.dubLast + dir * k) % dubW + dubW) % dubW, this.dubPrev + (rec - this.dubPrev) * k / count);
      }
      this.dubLast = q; this.dubPrev = rec;
    } else { this.dubLast = -1; if (!this.overdub) this.endLayer(); }   // the layer has faded out: it's done
    if (this.startFade < 1) this.carryA += Math.floor(pos / W) * W;
    this.loopPos = ((pos % W) + W) % W;
    this.heardAt(heard, hpos, hlev, W);
    // GAIN: the loop's own level, in dB, smoothed so a move doesn't click.
    this.loopLevel = loopLevelGain(this.e.loopLevel);
    this.loopBare = y * this.loopGain * this.lockGain * this.rsDip;
    return this.loopBare * this.loopLevel;
  }

  coreTick(x) {
    const p = this.p, e = this.e;
    // ROUTING decides what WET hears; what a take or a layer keeps follows (see brood.h).
    const loopOut = this.loopTick(x, softClip(x * (1 - this.recK * (1 - this.dGain)) + this.recK * this.gGain * this.lastWet));
    // LOOP LEVEL sets the loop's level for WET too, so turning it down quietens both.
    const wetSrc = x * this.mon * this.rIn * this.rsFeedIn + loopOut * this.rLoop;
    // The FREEZE switch holds WET as it is, so a reverb can be left ringing on its own.
    const frozen = this.freeze || p.freeze > 0.5;
    // WET's output: on while it's fed or frozen; after ROUTING OFF, as TRAILS says.
    if (this.ring && p.routing !== this.ringRouting) this.ring = false;   // ROUTING moved since: as TRAILS says again
    const outT = this.routingNow !== 3 || p.trails || frozen || this.ring ? 1 : 0;
    this.wetOut += (outT - this.wetOut) * 0.003;
    for (let k = 0; k < 4; k++) this.modeGain[k] += ((p.wetMode === k ? 1 : 0) - this.modeGain[k]) * 0.002;

    const freeze = frozen;
    // WET CLOCK DIVISION: the wet side runs at 1/div of the clock, so every time
    // constant on that side is div times longer.
    const wdiv = divMap(p.wetDiv);
    // CLASSIC: the MK I clock was noisy, most of all when it was slow.
    const hiss = p.classic ? (this.rand() * 2 - 1) * (0.0006 + 0.01 * (1 - this.clockRatio)) * clamp(p.wearNoise, 0, 1) : 0;
    // WORN after the loop, with the wet side recorded: hiss fed to the wet side would be
    // recorded with it, so it joins the output after the looper instead.
    const hissAfter = !!p.wearPost && (p.routing === 0 || p.routing === 1);
    const wIn = wetSrc + (hissAfter ? 0 : hiss);
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
      mono += this.delay.tick(wIn, delayTicks, e.modWet, freeze, !!p.tapeDelay) * this.modeGain[1];
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
      // Closing it warms as well as darkens: the range's saturation, the same curve Silt's
      // filter drives, with a little second harmonic and its offset taken out.
      const warm = (x, ch) => {
        const y = Math.tanh(1.5 * x) / 1.5 + 0.03 * x * x / (1 + x * x);
        this.warmOut[ch] = y - this.warmIn[ch] + 0.9995 * this.warmOut[ch];
        this.warmIn[ch] = y;
        return this.warmOut[ch];
      };
      const warmL = warm(wl, 0), warmR = warm(wr, 1);
      const c = this.toneCoeff();
      this.toneL += c * (warmL - this.toneL); this.toneR += c * (warmR - this.toneR);
      wl += (this.toneL - wl) * toneAmt;
      wr += (this.toneR - wr) * toneAmt;
    }
    wl *= this.wetOut; wr *= this.wetOut;

    // The history always advances, whether or not a loop is playing: pausing it
    // left a splice in the buffer, and every extra tap stitched in another one,
    // which is what put clicks inside the next loop. It keeps the wet side too, unless
    // ROUTING is on LOOP.
    const wetNow = (wl + wr) * 0.5;
    const kept = softClip(x * (1 - this.recK * (1 - this.dGain)) + this.recK * this.gGain * wetNow);
    this.lastWet = wetNow;
    if (this.resampling) this.resampleTick(loopOut, this.wetSide === 1 ? wr : wl);
    else if (this.rsDip < 1) this.rsDip = Math.min(1, this.rsDip + 1 / (0.005 * CORE_FS));   // the new take dips in
    this.hist[this.histW] = kept;
    // The strip's picture of it: read only, never heard. (Float32, as the C++ keeps it.)
    const col = Math.floor(this.histW / LIVE_COL_TICKS);
    if (this.histW % LIVE_COL_TICKS === 0) this.liveCols[col] = 0;
    const mag = Math.abs(Math.fround(kept));
    if (mag > this.liveCols[col]) this.liveCols[col] = mag;
    this.histTotal++;
    if (++this.histW >= this.hist.length) { this.histW = 0; this.histCycles++; }
    if (this.histCount < this.hist.length) this.histCount++;
    // RECORD at the MEMORY ceiling: it closes, keeping what was played first.
    if (this.rec.phase === 'recording') {
      const written = (this.histCycles - this.rec.histCycle) * this.hist.length + this.histW - this.rec.histIdx;
      if (written >= this.maxCaptureSamples()) this.closeRecording();
    }

    // What's heard from the core: the loop, blended away by WET MIX where WET hears it, and WET
    // at WET MIX's share (see brood.h). The input's share is added at the host's rate.
    const ld = loopOut * (1 - this.rLoop * (1 - this.dGain));
    for (const ch of this.ticks) { ch[0] = ch[1]; ch[1] = ch[2]; ch[2] = ch[3]; }
    const hissOut = hissAfter ? hiss * this.wetOut : 0;
    let outL = (wl + hissOut) * this.gGain + ld, outR = (wr + hissOut) * this.gGain + ld;
    {
      const fL = this.coreOut[0].run(outL), fR = this.coreOut[1].run(outR);
      if (this.coreW > 0) { outL += (fL - outL) * this.coreW; outR += (fR - outR) * this.coreW; }
    }
    this.ticks[0][3] = outL;
    this.ticks[1][3] = outR;
    this.ticksHi[0][this.ticksHiW] = this.ticks[0][3]; this.ticksHi[1][this.ticksHiW] = this.ticks[1][3];
    this.ticksHiW = (this.ticksHiW + 1) & 15;
  }

  // in: Float32Array (mono), outL/outR: Float32Array
  process(input, outL, outR) {
    const n = outL.length;
    const p = this.p, s = this.s, fs = this.fs;
    const a = 1 - Math.exp(-1 / (0.03 * fs));
    const aClock = 1 - Math.exp(-1 / ((p.smooth ? 1.5 : 0.08) * fs)); // Smooth dip: glide the clock
    const holdN = HOLD_SEC * fs;
    const aR = 1 - Math.exp(-1 / (0.01 * fs));   // routing and the monitor: 10 ms
    // footswitch hold detection
    const fw = this.fsDown.wet;
    if (fw && !fw.held && this.hostCount - fw.t >= holdN) {
      fw.held = true;
      this.freeze = true;
    }
    const fl = this.fsDown.loop;
    if (fl && !fl.held && this.hostCount - fl.t >= holdN) {
      fl.held = true;
      if (fl.playing) { if (this.p.monitor !== 2) this.beginLayer(); }   // no overdub with the monitor OFF
      // RECORD held: it records from the press for as long as it's held, bars or not.
      else if (this.rec.phase !== 'idle') { this.rec.histIdx = fl.histIdx; this.rec.histCycle = fl.histCycle; this.rec.phase = 'recording'; this.rec.endAt = -1; }
    }
    // INPUT MONITOR OFF ends a layer under way (keeping it) and refuses a new one.
    if (this.p.monitor === 2 && (this.overdub || this.dubLatched)) { this.overdub = false; this.dubLatched = false; }
    const latch = !!this.p.overdubLatch;
    if (latch !== this.latchSeen) {
      this.latchSeen = latch;
      if (latch) { if (this.canOverdub()) { this.beginLayer(); this.dubLatched = true; } }
      else if (this.dubLatched) {
        this.dubLatched = false;
        if (!(fl && fl.held && fl.playing)) this.overdub = false;   // a hold keeps its layer
      }
    }
    this.followTransport(n);
    const blockStart = this.hostCount;
    this.blockStartCount = this.hostCount;

    let pkIn = 0, pkOut = 0;
    for (let i = 0; i < n; i++) {
      s.time += (p.time - s.time) * a;
      s.wetMix += (p.wetMix - s.wetMix) * a;
      s.length += (p.length - s.length) * a;
      s.modWet += (p.modWet - s.modWet) * a;
      s.modLoop += (p.modLoop - s.modLoop) * a;
      s.loopLevel += (p.loopLevel - s.loopLevel) * a;
      s.clock += (clockTarget(p.clock, p.smooth) - s.clock) * aClock;
      s.tone += (p.tone - s.tone) * a;
      s.fade += (p.fade - s.fade) * a;
      s.width += (p.width - s.width) * a;
      s.modRate[0] += (p.mod1Rate - s.modRate[0]) * a;
      s.modRate[1] += (p.mod2Rate - s.modRate[1]) * a;
      this.rampTick();
      this.applyRamps(a);

      if ((i & 31) === 0) {
        const c = clockHz(this.e.clock, !!p.smooth) / fs;
        this.clockRatio = c;
        const classicNow = p.classic ? (p.wearPost ? 2 : 1) : 0;
        const wornIn = p.classic && !p.wearPost;   // the worn conversion on the way in is what a loop records
        if (Math.abs(c - this.filterClock) > 0.002 * c || classicNow !== this.filterClassic) {
          this.filterClock = c; this.filterClassic = classicNow;
          const fc = Math.min(0.45 * c * fs, 0.45 * fs, 20000);
          this.aa.set(fc, fs, !wornIn); this.reconL.set(fc, fs, !p.classic); this.reconR.set(fc, fs, !p.classic);
          // HI-Q's input: twice the filter, a little higher up, so more of the top survives
          // and less folds back.
          const fcHi = Math.min(0.48 * c * fs, 0.45 * fs, 20000);
          this.aaHi[0].set(fcHi, fs, true); this.aaHi[1].set(fcHi, fs, true);
          // CLOCK above the host's rate: filter the conversions at the core's own rate too
          // (see brood.h), faded in from the host's rate to 5% above it.
          this.coreW = clamp((c - 1) / 0.05, 0, 1);
          const fcCore = Math.min(0.45 * fs, 20000);   // they always run, so they fade in warm
          this.coreIn.set(fcCore, c * fs, true); this.coreOut[0].set(fcCore, c * fs, true); this.coreOut[1].set(fcCore, c * fs, true);
        }
      }

      const dry = input ? input[i] : 0;
      const ad = Math.abs(dry); if (ad > pkIn) pkIn = ad;
      this.mixTick(aR);
      const xa = this.aa.run(dry);
      const xin = this.xin;
      xin[0] = xin[1]; xin[1] = xin[2]; xin[2] = xin[3]; xin[3] = xa;
      // HI-Q's sinc conversion, or always with CLOCK above the host's rate (see brood.h).
      this.hiMix += (((p.hiQ && !p.classic) || this.coreW > 0 ? 1 : 0) - this.hiMix) * 0.002;
      const hi = this.hiMix > 1e-5;
      if (hi) { this.xinHi[this.xinHiW] = this.aaHi[1].run(this.aaHi[0].run(dry)); this.xinHiW = (this.xinHiW + 1) & 7; }
      this.phase += this.clockRatio;
      while (this.phase >= 1) {
        this.phase -= 1;
        // Each core tick falls somewhere between two host samples; read the input
        // there (one host sample late, so the curve has a neighbour on both sides).
        // Handing every tick the latest host sample instead repeated one sample in
        // four at 48 kHz, a stair-step jitter that was recorded into every loop.
        // CLASSIC keeps that hold as part of its grit.
        const u = 1 - this.phase / this.clockRatio;
        let xt = p.classic && !p.wearPost ? xa : hermite(xin[0], xin[1], xin[2], xin[3], u);
        if (hi) xt += (sincRead(SINC_IN, 8, this.xinHi, this.xinHiW, u) - xt) * this.hiMix;
        { const f = this.coreIn.run(xt); if (this.coreW > 0) xt += (f - xt) * this.coreW; }
        this.coreTick(xt);
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
      const sincL = hi ? sincRead(SINC_OUT, 16, this.ticksHi[0], this.ticksHiW, f) : 0;
      const sincR = hi ? sincRead(SINC_OUT, 16, this.ticksHi[1], this.ticksHiW, f) : 0;
      let el = this.reconL.run(il);
      let er = this.reconR.run(ir);
      // HI-Q: the sinc already stops the images, so the droop of the filter isn't needed.
      if (hi) { el += (sincL - el) * this.hiMix; er += (sincR - er) * this.hiMix; }
      // STEREO WIDTH: mid/side on the effect bus only — the dry signal must not
      // move. 0.5 is the image as built, so the neutral setting is untouched.
      if (Math.abs(s.width - 0.5) > 0.002) {
        const mid = (el + er) * 0.5, side = (el - er) * 0.5 * (s.width * 2);
        el = mid + side; er = mid - side;
      }

      // The input as heard, at the host's rate (see brood.h).
      const heardIn = this.mon * (1 - this.rIn * (1 - this.dGain));
      const frozenNow = this.freeze || p.freeze > 0.5;
      const engagedT = this.loopState === 'play' || frozenNow || (this.gGain > 1e-6 && this.wetOut > 1e-6) || this.mon < 0.9999 ? 1 : 0;
      this.engaged += (engagedT - this.engaged) * 0.001;
      let l = dry * heardIn + el;
      let r = dry * heardIn + er;
      const fullyBypassed = this.engaged < 1e-4 && heardIn > 0.9999;
      if (!fullyBypassed) { l = softLimit(l); r = softLimit(r); }
      outL[i] = l; outR[i] = r;
      const ao = Math.max(Math.abs(l), Math.abs(r)); if (ao > pkOut) pkOut = ao;
      this.hostCount++;
      if (this.resampling && this.hostCount >= this.rsLimitAt) this.beginHandover(true);   // the 30 s limit, in host time
      // RECORD: the bar line, and the bar count, in host time, so moving CLOCK can't
      // stretch them. Checked every sample, so both engines close on the same one.
      if (this.rec.phase === 'armed') {
        const now = p.ppq + (this.hostCount - blockStart) * p.bpm / 60 / fs;
        if (!p.hostPlaying || now >= this.rec.barTarget) this.startRecording(true);
      } else if (this.rec.phase === 'recording' && this.rec.endAt >= 0 && this.hostCount >= this.rec.endAt) this.closeRecording();
    }
    this.peakIn = Math.max(pkIn, this.peakIn * 0.9);
    this.peakOut = Math.max(pkOut, this.peakOut * 0.9);
  }

  // Snapshot for the UI.
  state() {
    const fl = this.fsDown.loop;
    let loop = this.loopState;
    if ((fl && fl.held && !fl.playing) || this.rec.phase === 'recording') loop = 'rec';
    else if (this.overdub && this.loopState === 'play') loop = 'dub';
    // TAPE plays a window that follows LENGTH at a capped speed: report that one, so the
    // playhead drawn is the playhead heard.
    const win = this.lsrc <= 0 ? { W: 0, start: 0 } : this.p.loopMode === 1 && this.lengthSlew >= 0 ? this.loopWindowAt(this.lengthSlew) : this.loopWindow();
    return {
      wetFed: this.routingNow !== 3, freeze: this.freeze || this.p.freeze > 0.5, loop,
      inputHeard: this.monTarget() > 0.5, routingNow: this.routingNow,
      resampling: this.resampling, resampleLimitHit: this.rsLimitHit, canResample: this.canResample(),
      resampleSecs: this.resampling ? (this.hostCount - this.rsStartHost) / this.fs : 0,
      resampleNeed: this.resamplingNeed(),
      mods: this.rampRaw.slice(), lsrc: this.lsrc, winStart: win.start, winLen: win.W, winSpan: this.lsrc > 0 ? this.loopSpan() : 0, pos: this.loopPos,
      head: this.heardPos.slice(), headLevel: this.heardLevel.slice(),
      clockHz: this.clockRatio * this.fs, histCycles: this.histCycles, peakIn: this.peakIn, peakOut: this.peakOut,
      recArmed: this.rec.phase === 'armed',
      canOverdub: this.canOverdub(), dubLatched: this.dubLatched,
      follows: this.takeFollows && this.lsrc >= 64, parked: this.parked,
    };
  }

  loopPeaks(nBins) {
    const out = new Float32Array(nBins);
    this.loopPeaksRange(out, nBins, 0, nBins - 1);
    return out;
  }

  // The same for bins first..last only, so an overdub redraws just what it wrote.
  loopPeaksRange(out, nBins, first, last) {
    const n = this.lsrc;
    if (!n) return;
    first = Math.max(0, first); last = Math.min(nBins - 1, last);
    for (let b = first; b <= last; b++) {
      const a = Math.floor((b * n) / nBins), z = Math.max(a + 1, Math.floor(((b + 1) * n) / nBins));
      let pk = 0;
      for (let i = a; i < z; i += 4) { const v = Math.abs(this.loopBuf[i]); if (v > pk) pk = v; }
      out[b] = pk;
    }
  }

  // Cells of the take an overdub wrote since the last call, or null.
  takeDubDirty() {
    if (this.dubHi < 0) return null;
    const r = { lo: this.dubLo, hi: this.dubHi };
    this.dubLo = Infinity; this.dubHi = -1;
    return r;
  }

  // ── the history, for the take strip's live view (read only) ──
  liveColumns() { return this.liveCols; }
  histWritten() { return this.histTotal; }
  histWritePos() { return this.histW; }
  // The strip's window: in CAPTURE the most a tap keeps; in RECORD the chosen bars, or with
  // FREE 8 s, widening as the take grows.
  memoryTicks() {
    if (this.p.recMode !== 1) return this.maxCaptureSamples();
    const p = this.p, clock = clockHz(this.e.clock, p.smooth);
    let want = p.recBars > 0 ? p.recBars * Math.max(0.25, p.barQuarters) * 60 / Math.max(5, p.bpm) * clock : 8 * clock;
    if (p.recBars === 0) want = Math.max(want, this.takeSoFar() + clock);
    return Math.floor(Math.min(this.maxCaptureSamples(), Math.max(64, want)));
  }
  // Ticks kept so far by a RECORD take, or by a CAPTURE hold; -1 when nothing is.
  takeSoFar() {
    const since = (idx, cycle) => Math.min(this.maxCaptureSamples(), Math.max(0, (this.histCycles - cycle) * this.hist.length + this.histW - idx));
    if (this.rec.phase === 'recording') return since(this.rec.histIdx, this.rec.histCycle);
    const fl = this.fsDown.loop;
    if (fl && fl.held && !fl.playing && this.p.recMode === 0) return since(fl.histIdx, fl.histCycle);
    return -1;
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
      this.right.setSide(1);   // RESAMPLE records each engine's own side of WET
      this.scratchL = new Float32Array(128);
      this.scratchR = new Float32Array(128);
      this.sinceState = 0;
      this.dubTimer = 0;
      this.port.onmessage = (e) => {
        const m = e.data;
        for (const core of [this.left, this.right]) {
          if (m.type === 'param') core.setParam(m.name, m.value);
          else if (m.type === 'fs') core.footswitch(m.which, m.action);
          else if (m.type === 'clear') core.clearLoop();
          else if (m.type === 'swap') core.swapVersion(m.id);
          else if (m.type === 'release') core.releaseVersion(m.id);
          else if (m.type === 'cancelActive') core.cancelActive();
          else if (m.type === 'resample') core.resampleKey();
          else if (m.type === 'reset') core.fullReset();
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
      // OVERDUB on with nothing to layer into (or the loop stopped under it): set it back to
      // off, as the plug-in does, so the next press is a fresh switch-on.
      for (const core of [this.left, this.right]) if (core.p.overdubLatch && !core.dubLatched) core.p.overdubLatch = 0;
      // The UNDO history's news, from the left engine (the right one keeps step with it).
      for (let ev; (ev = this.left.popVersionEvent());) this.port.postMessage({ type: 'version', ev });
      this.right.versionEvents.length = 0;
      // A tap on the FREEZE key: the page flips the FREEZE switch.
      const tapped = this.left.takeFreezeTap(); this.right.takeFreezeTap();
      if (tapped) this.port.postMessage({ type: 'freezeTap' });
      // A new take fitted the window: tell the panel, so its START, END (and LENGTH in TAPE) follow.
      const fit = this.left.takeFitted();
      this.right.takeFitted();
      if (fit) this.port.postMessage({ type: 'fit', length: fit.length });
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

// Sets saved before START meant the same in every mode stored TAPE's START counted back
// from the newest audio. The value that puts the window in the same place now, for the
// LENGTH and take length that were saved (0 was the newest, so it becomes exactly 1).
function migrateTapeStart(oldStart, length, takeLength) {
  const len = Math.max(64, takeLength);
  const W0 = Math.max(64, Math.round(len * Math.min(1, 0.02 + 0.98 * clamp(length, 0, 1))));
  const room = Math.max(0, len - W0);
  const oldAt = Math.round((1 - clamp(oldStart, 0, 1)) * room);
  if (oldAt >= room) return 1;
  return clamp(oldAt / Math.max(1, len - 64), 0, 1);
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { BroodCore, VERSION_KIND, AUTO, migrateTapeStart, migrateShape, speedMap, slipSpeedMap, stretchMap, expMap, clockHz, syncLabel, syncSeconds, syncSecondsRaw, rampDivLabel, rampCycleSeconds, RAMP_DIV_BEATS, divMap, DIV_STEPS, MOD_TARGETS, RAMP_WAVES, rampWaveIndex, rampHz, SYNC_DIVISIONS, CLOCK_STEPS, CORE_FS, LOOP_SAMPLES, CLOCK_MIN_HZ, CLOCK_MAX_HZ, HOLD_SEC, LIVE_COL_TICKS, MAX_LOOP_SAMPLES, readFrac, levelGain, loopLevelGain, inputLevelGain, loopLevelOfDb, inputLevelOfDb };
}
