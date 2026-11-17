import React, { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AudioEngine, DEMOS } from './audio-engine';
import './style.css';

const clamp = (v, min = 0, max = 1) => Math.min(max, Math.max(min, v));
const expMap = (t, a, b) => a * Math.pow(b / a, t);

// The pedal's clock, mirrored from native/dsp/brood.h: stepped, CLOCK lands on these; SMOOTH
// travels through the same rates, so the two agree at every step.
const CLOCK_STEPS = [2000, 3000, 4000, 6000, 8000, 12000, 16000, 24000, 32000, 48000, 64000];
function clockHz(knob, smooth) {
  knob = clamp(knob);
  if (!smooth) return CLOCK_STEPS[Math.floor(knob * 10 + 0.5)];
  const below = Math.min(9, Math.floor(knob * 10));
  return expMap(knob * 10 - below, CLOCK_STEPS[below], CLOCK_STEPS[below + 1]);
}
const CORE_FS = 64000, LOOP_SAMPLES = 64000, DELAY_MAX_SEC = 2.1, WEAVE_BUF_SEC = 3;

// Tempo-sync divisions. Duplicated from src/brood-worklet.js because an
// AudioWorklet script cannot import an ES module; test/dsp.test.js asserts the
// two copies agree, so they cannot drift apart silently.
const SYNC_DIVISIONS = [
  { beats: 1 / 8, label: '1/32' }, { beats: 1 / 6, label: '1/16T' }, { beats: 1 / 4, label: '1/16' },
  { beats: 1 / 3, label: '1/8T' }, { beats: 1 / 2, label: '1/8' }, { beats: 3 / 4, label: '1/8D' },
  { beats: 1, label: '1/4' }, { beats: 3 / 2, label: '1/4D' }, { beats: 2, label: '1/2' }, { beats: 4, label: '1/1' },
];
const syncIndex = (knob) => Math.round(clamp(knob) * (SYNC_DIVISIONS.length - 1));
const syncLabel = (knob) => SYNC_DIVISIONS[syncIndex(knob)].label;
// How long the division is, before a block clamps it to its own limit.
const syncSecondsRaw = (bpm, knob) => SYNC_DIVISIONS[syncIndex(knob)].beats * 60 / clamp(bpm, 20, 300);
const syncSeconds = (bpm, knob) => clamp(syncSecondsRaw(bpm, knob), 0.01, DELAY_MAX_SEC * 0.95);
// Modulation sync: one cycle is a musical span rather than a free rate.
const RAMP_DIV_LABELS = ['8 BARS', '4 BARS', '2 BARS', '1 BAR', '1/2', '1/4', '1/8', '1/16'];
const RAMP_DIV_BEATS = [32, 16, 8, 4, 2, 1, 0.5, 0.25];
const rampDivIndex = (knob) => Math.round(clamp(knob) * (RAMP_DIV_BEATS.length - 1));
const rampDivLabel = (knob) => RAMP_DIV_LABELS[rampDivIndex(knob)];
const rampCycleSeconds = (bpm, knob) => RAMP_DIV_BEATS[rampDivIndex(knob)] * 60 / clamp(bpm, 20, 300);
// The modulation sources, mirrored from src/brood-worklet.js and native/dsp/brood.h.
const MOD_TARGETS = ['time', 'mix', 'length', 'modWet', 'clock', 'modLoop', 'balance'];
const MOD_TARGET_NAMES = { time: 'TIME', mix: 'MIX', length: 'LENGTH', modWet: 'MODIFY / WET', clock: 'CLOCK', modLoop: 'MODIFY / LOOP', balance: 'BALANCE' };
const MOD_COLOURS = ['#b4452a', '#2f6f8f'];
const MOD_FLAGS = ['On', 'Uni', 'Sync', 'Hold'];
const RAMP_WAVES = ['TRI', 'SQR', 'SIN', 'SAW', 'RND', 'SMTH'];
const rampIndex = (v, count) => Math.round(clamp(v) * (count - 1));
const rampValue = (index, count) => count < 2 ? 0 : index / (count - 1);
const rampHertz = (v) => expMap(clamp(v), 0.05, 8);
// Hidden options mirror src/brood-worklet.js: the sides can run on divided clocks.
const DIV_STEPS = [1, 2, 4];
const divIndex = (v) => Math.round(clamp(v, 0, 1) * (DIV_STEPS.length - 1));
// Speeds, mirrored from native/dsp/brood.h, for the readouts.
const speedMap = (m) => (m < 0.5 ? -1 : 1) * Math.pow(2, Math.round(-1 + 3 * Math.min(1, Math.abs(m - 0.5) * 2)));
const slipSpeedMap = (m) => (m < 0.5 ? -1 : 1) * Math.pow(2, Math.round((-1 + 2 * Math.min(1, Math.abs(m - 0.5) * 2)) * 12) / 12);
function stretchMap(m) {
  const d = m - 0.5;
  if (Math.abs(d) < 0.03) return 0;
  return Math.sign(d) * Math.pow(2, -3 + 3 * (Math.abs(d) - 0.03) / 0.47);
}

// The plug-in's words for values (BroodProcessor::describe), in the same form.
const fmtTime = (s) => s < 1 ? `${Math.round(s * 1000)} ms` : `${s.toFixed(2)} s`;
const fmtSpeed = (s) => Math.abs(s) < 1e-9 ? 'frozen' : `${s < 0 ? 'reverse ' : ''}${Math.abs(s).toFixed(2)}x`;
const pct = (x) => `${Math.round(x * 100)}%`;
const signedRate = (s) => `${s < 0 ? '-' : '+'}${Math.abs(s).toFixed(2)}`;
const balanceLoop = (v) => Math.round(Math.min(1, 2 * (1 - v)) * 100);
const balanceWet = (v) => Math.round(Math.min(1, 2 * v) * 100);

const BASE = {
  time: 0.55, mix: 0.5, length: 0.72, modWet: 0.58, clock: 0.9, modLoop: 0.66,
  wetMode: 0, routing: 1, loopMode: 1,
  trails: 0, smooth: 0, balance: 0.5,
  sync: 0, bpm: 120, memory: 0, loopEnd: 1, start: 1, envSync: 0, recMode: 0, recBars: 0, tapeDelay: 0,
  tone: 1, fade: 0, width: 0.5, wetDiv: 0, loopDiv: 0, classic: 0, wearNoise: 1, wearPost: 0, freeze: 0,
  envSoft: 0, loopDb: 0, hiQ: 0,
  ...Object.fromEntries([1, 2].flatMap((n) => [
    [`mod${n}On`, 1], [`mod${n}Rate`, 0.45], [`mod${n}Wave`, 0], [`mod${n}Uni`, 0], [`mod${n}Sync`, 0], [`mod${n}Hold`, 0],
    ...MOD_TARGETS.map((t) => [`mod${n}_${t}`, 0]),
  ])),
};
const PRESETS = [
  { name: 'A LITTLE DAYDREAM', subtitle: 'a place to begin', category: 'SPACES', params: BASE },
  { name: 'SOFT FOCUS', subtitle: 'a warm, hazy reverb', category: 'SPACES', params: { ...BASE, time: 0.73, mix: 0.56, modWet: 0.83, length: 0.65, clock: 0.8, wetMode: 0, routing: 1, loopMode: 2, modLoop: 0.62 } },
  { name: 'GLASS ECHO', subtitle: 'a pocket of repeats', category: 'ECHOES', params: { ...BASE, time: 0.46, mix: 0.48, modWet: 0.70, wetMode: 1, routing: 0, loopMode: 1, clock: 0.9 } },
  { name: 'LITTLE GHOST', subtitle: 'a strange harmony', category: 'ECHOES', params: { ...BASE, time: 0.45, mix: 0.54, modWet: 0.83, wetMode: 2, routing: 1, loopMode: 1, clock: 0.8 } },
  { name: 'MELT AWAY', subtitle: 'a slow-motion memory', category: 'LOOPS', params: { ...BASE, time: 0.67, mix: 0.58, length: 0.35, modWet: 0.75, clock: 0.7, modLoop: 0.57, wetMode: 0, routing: 2, loopMode: 2 } },
];
const PRESET_CATEGORIES = ['SPACES', 'ECHOES', 'LOOPS', 'USER'];
const KNOB_DEFAULTS = { time: 0.55, mix: 0.5, length: 0.72, modWet: 0.58, clock: 0.9, modLoop: 0.66 };
const WET = ['REVERB', 'DELAY', 'SLIP', 'WEAVE'];
// RECORD's bar counts. 0 is FREE: it records until the next tap. Here there is no host
// transport, so a bar count starts at the tap, timed from the TEMPO field.
const REC_BARS = [0, 1, 2, 3, 4, 6, 8, 12, 16];

// One whispered sentence per knob per mode, from the plug-in (kWhisper in
// native/plugin/PluginEditor.cpp), in the knobs' order: TIME, MIX, LENGTH, MODIFY (wet),
// CLOCK, MODIFY (loop).
const KNOB_IDS = ['time', 'mix', 'length', 'modWet', 'clock', 'modLoop'];
const KNOB_MODE = [0, 2, 1, 0, 2, 1];   // 0 = wet modes, 1 = loop modes, 2 = shared
const WHISPER = [
  ['How long the room keeps breathing', 'How far back the echo remembers',
    'How wide a slice of time is borrowed', 'How far back the threads reach'],
  ['Your dry sound against Brood: the loop and the room. Left is dry alone, right is Brood alone, the middle both in full',
    'Your dry sound against Brood: the loop and the echoes. Left is dry alone, right is Brood alone, the middle both in full',
    'Your dry sound against Brood: the loop and the slipped slices. Left is dry alone, right is Brood alone, the middle both in full',
    'Your dry sound against Brood: the loop and the threads. Left is dry alone, right is Brood alone, the middle both in full'],
  ['How much of the moment gets caught', 'The size of the window that repeats',
    'How wide each grain is stretched', 'How much of the moment gets caught'],
  ['How much the surface smears', 'How much the echo wants to stay',
    'Which way, and how fast, time slides', 'Long cloth, or short clipped stitches'],
  ['The heartbeat everything obeys', 'The heartbeat everything obeys',
    'The heartbeat everything obeys', 'The heartbeat everything obeys'],
  ['How softly it must speak to be caught', 'Which way the tape runs, and how fast',
    'How far the grains drift from now', 'How softly it must speak to be caught'],
];
// A knob says what it does in this mode: MODIFY alone tells you nothing.
function knobName(i, p) {
  if (KNOB_IDS[i] === 'modWet') return ['SMEAR', 'FEEDBACK', 'RATE', 'STITCH'][p.wetMode];
  if (KNOB_IDS[i] === 'modLoop') return ['SENSITIVITY', 'SPEED', 'STRETCH'][p.loopMode];
  return ['TIME', 'MIX', 'LENGTH', 'MODIFY', 'CLOCK', 'MODIFY'][i];
}

function safeRead(key) {
  try { return JSON.parse(localStorage.getItem(key)); } catch (_) { return null; }
}
function safeWrite(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); } catch (_) { /* storage is optional */ }
}
function sanitize(input) {
  const out = { ...BASE };
  if (!input || typeof input !== 'object') return out;
  for (const key of Object.keys(out)) {
    if (!Number.isFinite(input[key])) continue;
    if (key === 'wetMode') out[key] = Math.round(clamp(input[key], 0, WET.length - 1));
    else if (key === 'routing' || key === 'loopMode') out[key] = Math.round(clamp(input[key], 0, 2));
    else if (['trails', 'smooth', 'sync', 'envSync', 'envSoft', 'hiQ', 'freeze', 'classic', 'wearPost', 'tapeDelay'].includes(key) || MOD_FLAGS.some((f) => key === `mod1${f}` || key === `mod2${f}`)) out[key] = input[key] ? 1 : 0;
    else if (key.includes('_')) out[key] = clamp(input[key], -1, 1);
    else if (key === 'loopDb') out[key] = clamp(input[key], -24, 12);
    else if (key === 'recMode') out[key] = input[key] ? 1 : 0;
    else if (key === 'recBars') out[key] = REC_BARS.includes(input[key]) ? input[key] : 0;
    else if (key === 'bpm') out[key] = clamp(Math.round(input[key]), 40, 300);
    else out[key] = clamp(input[key]);
  }
  return out;
}
// The readout's words for a value, as the plug-in says them (BroodProcessor::describe).
function describe(id, p, value) {
  const v = value ?? p[id];
  const hz = clockHz(p.clock, p.smooth), c = hz / CORE_FS;
  if (id === 'time') {
    // WET DIV runs the wet side slower, so every time on it is that many times longer.
    const div = DIV_STEPS[divIndex(p.wetDiv)], times = div > 1 ? ` x${div}` : '';
    const delayMost = DELAY_MAX_SEC * 0.95;   // the delay line's length
    if (p.wetMode === 0) return v > 0.985 ? 'decay infinite' : `decay ${fmtTime(expMap(v, 0.4, 25) * div / c)}`;
    if (p.wetMode === 1) {
      if (p.sync) return `${syncLabel(v)}${times} · ${fmtTime(Math.min(syncSeconds(p.bpm, v) * div, delayMost * CORE_FS / hz))}`;
      return `delay ${fmtTime(Math.min(expMap(v, 0.02, 2) * div, delayMost) / c)}`;
    }
    if (p.wetMode === 3) {
      if (p.sync) return `${syncLabel(v)}${times} back · ${fmtTime(Math.min(syncSecondsRaw(p.bpm, v) * div, WEAVE_BUF_SEC * 0.85))}`;
      return `memory ${fmtTime(Math.min(2.8, (0.2 + v * 2.6) * div))}`;
    }
    return `refresh ${fmtTime(expMap(v, 0.05, 1.5) * div / c)}`;
  }
  // On this page a playing loop mutes the dry sound (dryMuted is set while it does; never saved).
  if (id === 'mix' && p.dryMuted) return `dry muted / brood ${pct(Math.min(1, 2 * v))}`;
  if (id === 'mix') return `dry ${pct(v > 0.995 ? 0 : Math.min(1, 2 * (1 - v)))} / brood ${pct(Math.min(1, 2 * v))}`;
  if (id === 'length') {
    const full = LOOP_SAMPLES / hz;
    if (p.loopMode === 1) return `loop ${fmtTime(full * expMap(v, 0.02, 1))} of ${fmtTime(full)}`;
    // ENV slices to a length, so a synced LENGTH is a division of the beat.
    if (p.loopMode === 0 && p.envSync) return `${syncLabel(v)} slice · ${fmtTime(syncSecondsRaw(p.bpm, v))}`;
    return `slice ${fmtTime((p.loopMode === 0 ? expMap(v, 0.01, 1) : expMap(v, 0.01, 0.5)) / c)}`;
  }
  if (id === 'modWet') {
    if (p.wetMode === 0) return `smear ${pct(v)}`;
    if (p.wetMode === 1) return `feedback ${pct(v)}`;
    if (p.wetMode === 3) return v < 0.34 ? `long cloth ${pct(v)}` : `short stitches ${pct(v)}`;
    return `rate ${signedRate(slipSpeedMap(v))}`;
  }
  if (id === 'clock') return `sample rate ${(clockHz(v, p.smooth) / 1000).toFixed(1)} kHz`;
  if (id === 'modLoop') {
    if (p.loopMode === 0) return `sensitivity ${pct(v)}`;
    if (p.loopMode === 1) return `tape ${fmtSpeed(speedMap(v))}`;
    return `stretch ${fmtSpeed(stretchMap(v))}, same pitch`;
  }
  if (id === 'balance') return `loop ${balanceLoop(v)} / wet ${balanceWet(v)}`;
  if (id === 'wearPost') return v ? 'post loop: heard worn, recorded clean' : 'pre loop: recorded worn';
  if (id === 'memory') return v <= 0.001 ? `pedal ${fmtTime(LOOP_SAMPLES / hz)}` : `up to ${fmtTime(expMap(v, 1, 30))} of ${fmtTime(30)}`;
  if (id === 'loopEnd') return v >= 0.999 ? 'to the end' : `plays to ${pct(v)}`;
  // One meaning in every mode: 0 is the oldest audio, 1 the newest.
  if (id === 'start') return v <= 0.005 ? 'the oldest' : v >= 0.995 ? 'the newest' : `${pct(v)} in`;
  if (id === 'freeze') return v ? 'held, ringing' : 'moving';
  if (id === 'tone') return v > 0.995 ? 'open' : `hi-cut ${pct(1 - v)}`;
  if (id === 'recBars') return v === 0 ? 'free' : `${v} ${v === 1 ? 'bar' : 'bars'}`;
  if (id === 'wearNoise') return v <= 0.005 ? 'worn hiss off' : `worn hiss ${pct(v)}`;
  if (id === 'fade') return v <= 0.001 ? 'pedal decay' : `overdub keeps ${pct(1 - v)}`;
  // WIDTH is where the removed SPREAD lives now: hard left is mono.
  if (id === 'width') return v <= 0.005 ? 'mono' : v < 0.49 ? `narrow ${pct(v * 2)}` : v > 0.51 ? `wide ${pct((v - 0.5) * 2)}` : 'as built';
  if (/^mod[12]/.test(id)) {
    const field = id.slice(4), owner = id.slice(0, 4);
    if (field === 'Wave') return RAMP_WAVES[rampIndex(v, RAMP_WAVES.length)].toLowerCase();
    if (field === 'Rate') return p[`${owner}Sync`] ? `${rampDivLabel(v)} · ${fmtTime(rampCycleSeconds(p.bpm, v))} a turn` : `${fmtTime(1 / rampHertz(v))} a turn`;
    if (field === 'Sync') return v ? 'follows the tempo' : 'free rate';
    if (field === 'Hold') return v ? 'held where it is' : 'running';
    if (field.startsWith('_')) return `${v > 0 ? '+' : ''}${Math.round(v * 100)}%`;
  }
  if (id === 'loopDb') return `${Math.round(v * 2) > 0 ? '+' : ''}${(Math.round(v * 2) / 2).toFixed(1)} dB`;
  if (id === 'envSync') return v ? 'slice follows the tempo' : 'free slice';
  if (id === 'wetDiv' || id === 'loopDiv') { const d = DIV_STEPS[divIndex(v)]; return d === 1 ? '1x clock' : `${d}x slower`; }
  return v.toFixed(3);
}

// Settings and presets saved before START meant the same in every mode stored TAPE's START
// counted back from the newest audio. No take is stored, so the pedal's length stands in.
// Mirrors migrateTapeStart() in src/brood-worklet.js and native/dsp/brood.h.
function migrateStart(p) {
  if (!p || typeof p !== 'object' || p.loopMode !== 1 || !Number.isFinite(p.start)) return p;
  const len = 64000;
  const length = Number.isFinite(p.length) ? p.length : BASE.length;
  const W0 = Math.max(64, Math.round(len * Math.min(1, 0.02 + 0.98 * clamp(length))));
  const room = Math.max(0, len - W0);
  const oldAt = Math.round((1 - clamp(p.start)) * room);
  return { ...p, start: oldAt >= room ? 1 : clamp(oldAt / (len - 64)) };
}

// Settings and presets from before version 3 had BEFORE THE LOOP, and WET/LOOP counted the
// loop. Now ROUTING alone decides (BEFORE THE LOOP on becomes IN, off becomes LOOP), and
// WET/LOOP counts the wet side, so it turns round, with any modulation of it. Mirrors
// BroodProcessor::migrateRouting in native/plugin/PluginProcessor.cpp.
function migrateRouting(p) {
  if (!p || typeof p !== 'object') return p;
  const out = { ...p, routing: p.wetPre ? 0 : 2 };
  delete out.wetPre;
  if (Number.isFinite(p.balance)) out.balance = 1 - p.balance;
  for (const id of ['mod1_balance', 'mod2_balance']) if (Number.isFinite(p[id])) out[id] = -p[id];
  return out;
}

// Settings and presets from before version 4 had SWEEP and BOUNCE, and five shapes. SWEEP
// is the SAW shape now; SWEEP with BOUNCE is TRI at half the rate; BOUNCE alone goes; every
// shape moves to its place in a row of six. Mirrors migrateShape in src/brood-worklet.js.
function migrateShapes(p) {
  if (!p || typeof p !== 'object') return p;
  const out = { ...p };
  for (const n of [1, 2]) {
    const m = `mod${n}`, wave = Number.isFinite(p[`${m}Wave`]) ? p[`${m}Wave`] : 0;
    const rate = Number.isFinite(p[`${m}Rate`]) ? p[`${m}Rate`] : 0.45;
    if (!p[`${m}Sweep`]) out[`${m}Wave`] = [0, 1, 2, 4, 5][Math.round(clamp(wave) * 4)] / 5;
    else if (!p[`${m}Bounce`]) out[`${m}Wave`] = 3 / 5;
    else {
      out[`${m}Wave`] = 0;
      out[`${m}Rate`] = p[`${m}Sync`] ? Math.max(0, rampDivIndex(rate) - 1) / (RAMP_DIV_BEATS.length - 1)
        : clamp(rate - Math.log(2) / Math.log(8 / 0.05));
    }
    delete out[`${m}Sweep`];
    delete out[`${m}Bounce`];
  }
  return out;
}

// The window the waveform's markers ask for, as the parameters that give it: a copy of
// loopTrim() in native/plugin/LoopTrim.h. START counts from the oldest audio in every mode;
// a window against the end of the take is START 1, so it keeps following the newest audio.
// TAPE's window sits inside LENGTH's span, so there LENGTH gives way, only as far as needed.
function loopTrim(tape, takeLength, lengthKnob, wantedStart, wantedWidth) {
  const len = Math.max(64, takeLength);
  const shortest = Math.max(64, 0.02 * len);
  const width = clamp(wantedWidth, shortest, len);
  const first = clamp(wantedStart, 0, len - width);
  const out = { start: 0, end: 1, length: lengthKnob, firstSample: Math.round(first), width: Math.round(width) };
  const startFor = (span) => {
    if (first >= len - span - 0.5) return 1;
    return len > 64 ? clamp(first / (len - 64)) : 0;
  };
  if (!tape) { out.end = width / len; out.start = startFor(width); return out; }
  const wanted = Math.round(len * Math.min(1, 0.02 + 0.98 * clamp(lengthKnob)));
  const span = clamp(wanted, width, Math.max(width, len - first));
  if (span !== wanted) out.length = clamp((span / len - 0.02) / 0.98);
  out.start = startFor(span);
  out.end = clamp(width / span, 0.02, 1);
  return out;
}

// ───────── the design canvas ─────────
// Everything is drawn on the plug-in's design canvas, 1200 x 820, at the plug-in's own
// coordinates (BroodEditor::resized), and the whole canvas is scaled to the window, so
// nothing reflows. The Bench's row holds the source picker here.
const W = 1200, H = 820;
const INK = '#181815', PAPER = '#f6f3ea', MUTED = '#5f5f57', RULE = '#bdb9ab', LIT = '#c98f2e', MOSS = '#5d7046';
const DIMMED = '#6b6963';   // the take strip's dimmed audio: ink a third of the way to paper
const TICK = '#a09d96';     // its second ticks: ink most of the way to paper
// Font sizes: the plug-in's sizes are JUCE heights, so CSS size = height / hem. Names are drawn
// at 0.72 of the number in the code, values 2 under it.
const nf = (n) => ({ fontSize: `${(n * 0.72 / 1.266).toFixed(3)}px` });   // Overpass Medium
const vf = (n) => ({ fontSize: `${((n - 2) / 1.19).toFixed(3)}px` });      // Chivo Mono
const sf = (n) => ({ fontSize: `${(n / 1.215).toFixed(3)}px` });           // B612 Mono
const at = ([x, y, w, h]) => ({ left: x, top: y, width: w, height: h });
const centred = (w, h, cx, cy) => [Math.round(cx - w / 2), Math.round(cy - h / 2), w, h];
const inside = ([x, y, w, h], px, py) => px >= x && px < x + w && py >= y && py < y + h;
// (Nothing, for a control a phone's tab leaves off the page.)
const grow = (r, dx, dy = dx) => (r ? [r[0] - dx, r[1] - dy, r[2] + 2 * dx, r[3] + 2 * dy] : undefined);

// The LOOP and MODULATION sections' insides, from their area, as resized() works them out.
function loopRects(a) {
  const [x, y, w] = a;
  const slider = [x + 86, y + 36, w - 24 - 74 - 44, 21];
  return {
    memory: slider, balance: [slider[0] + 32, y + 61, slider[2] - 32, 21],
    memoryLabel: [x + 12, y + 36, 70, 21], balanceLabel: [x + 12, y + 61, 70, 21],
    memoryFigure: [x + w - 52, y + 36, 40, 21], wetEnd: [x + w - 52, y + 61, 40, 21],
    recMode: [x + 12, y + 86, 120, 21], recBars: [x + 138, y + 86, 104, 21],
  };
}
function modRects(a) {
  const [x, y, w] = a;
  const inner = w - 20, rest = inner - 66, chipW = Math.floor((rest - 6) / 2);
  const half = Math.floor((inner - 8) / 2), cell = Math.floor((inner - 16) / 3) + 8;
  return {
    chip0: [x + 10, y + 33, chipW, 120], chip1: [x + 16 + chipW, y + 33, rest - chipW - 6, 120], routes: [x + 10 + inner - 60, y + 33, 60, 120],
    rampWave: [x + 10, y + 171, half, 28], rampRate: [x + 18 + half, y + 171, inner - half - 8, 28],
    rampSwitch: [[x + 10, y + 217, cell - 8, 26], [x + 10 + cell, y + 217, cell - 8, 26], [x + 10 + 2 * cell, y + 217, inner - 2 * cell, 26]],
  };
}

// The desktop: the plug-in's own rectangles (BroodEditor::resized), design units 1200 x 820.
const CX = [162, 438, 714];
const R = {
  mast: [24, 24, 1152, 72], presetBar: [776, 30, 400, 36], pedal: [24, 108, 828, 548],
  loopView: [38, 116, 800, 100], gain: [708, 116, 130, 16], undo: [650, 116, 52, 15], clear: [590, 116, 56, 15],
  fsWet: centred(248, 46, CX[0], 245), fsLoop: [590, 222, 176, 46], overdub: [768, 222, 70, 43],
  character: centred(138, 24, CX[1], 234), wearPost: centred(160, 20, CX[1], 258),
  wetMode: [CX[0] - 124, 292, 248, 28], routing: [CX[1] - 124, 292, 248, 28], loopMode: [CX[2] - 124, 292, 248, 28],
  freeze: [38, 632, 54, 19], trails: [96, 632, 54, 19], smooth: centred(66, 19, CX[1], 642),
  wetDiv: [156, 629, 130, 25], loopDiv: [590, 629, 130, 25],
  tapeDelay: centred(54, 19, CX[0] + 29, 472), envSync: centred(54, 19, CX[2] - 29, 472), envSoft: centred(54, 19, CX[2] + 29, 472),
  lcd: [864, 108, 312, 112], loopArea: [864, 230, 312, 113], rampArea: [864, 353, 312, 303],
  hood: [24, 668, 1152, 67],
};
R.knob = KNOB_IDS.map((_, i) => [CX[i % 3] - 52, i < 3 ? 342 : 510, 104, 104]);
Object.assign(R, loopRects(R.loopArea), modRects(R.rampArea));
// The source picker, in the Bench's place under the bottom strip.
const SRC = {
  caption: [38, 748, 80, 50], picker: [118, 759, 156, 28], demo: [280, 759, 246, 28], load: [532, 759, 90, 28], status: [628, 759, 64, 28],
  tour: [698, 759, 64, 28], plugin: [768, 759, 120, 28], tempoLabel: [888, 759, 56, 28], tempo: [944, 759, 104, 28], start: [1060, 759, 116, 28],
  song: [118, 793, 1058, 20],   // a song's place, when the DEMO stepper is on a song
};
const DESK = {
  ...R, W, H, phone: false, src: SRC, overlay: R.pedal, knobValueW: 236,
  presetPrev: [776, 30, 32, 36], presetName: [808, 30, 270, 36], presetNext: [1078, 30, 32, 36], save: [1118, 30, 58, 36], reset: [650, 30, 66, 36],
  // LEVEL, TONE, FADE, WIDTH, NOISE: five even cells across the bottom strip.
  hoodCell: [0, 1, 2, 3, 4].map((i) => [38 + i * 229, 676, 205, 52]),
};
function deskLayout(p) {
  return { ...DESK, wetSync: centred(54, 19, CX[0] - (p.wetMode === 1 ? 29 : 0), 472) };
}

// A phone: the same parts, stacked on a canvas 390 wide, one column's knobs at a time.
const PW = 390, PH = 1676;
const PHONE = {
  W: PW, H: PH, phone: true, knobValueW: 176,
  presetBar: [12, 58, 366, 36], presetPrev: [12, 58, 32, 36], presetName: [48, 58, 214, 36], presetNext: [266, 58, 32, 36], save: [306, 58, 72, 36], reset: [102, 1591, 82, 32],
  lcd: [12, 112, 366, 112], loopView: [12, 234, 366, 100], gain: [248, 234, 130, 16], undo: [190, 234, 52, 15], clear: [130, 234, 56, 15],
  fsWet: [12, 344, 150, 52], fsLoop: [166, 344, 150, 52], overdub: [320, 344, 58, 49],
  tabs: [12, 406, 366, 36], tabSheet: [12, 450, 366, 306], overlay: [12, 112, 366, 644],
  loopArea: [12, 768, 366, 113], rampArea: [12, 891, 366, 260], hood: [12, 1161, 366, 236],
  hoodCell: [0, 1, 2, 3, 4].map((i) => [24, 1169 + i * 44, 342, 44]),
  src: {
    caption: [24, 1407, 120, 22], picker: [12, 1433, 366, 32], demo: [12, 1471, 366, 32], load: [12, 1511, 120, 32], status: [140, 1511, 238, 32],
    tempoLabel: [24, 1551, 50, 32], tempo: [74, 1551, 106, 32], start: [190, 1551, 188, 32], tour: [12, 1591, 82, 32], plugin: [192, 1591, 186, 32], song: [12, 1633, 366, 28],
  },
};
Object.assign(PHONE, loopRects(PHONE.loopArea), modRects(PHONE.rampArea));
function phoneLayout(p, tab) {
  const L = { ...PHONE, knob: [] };
  // The tab's two knobs side by side, big enough for a thumb.
  L.knob[tab] = [38, 524, 132, 132];
  L.knob[tab + 3] = [220, 524, 132, 132];
  L[['wetMode', 'routing', 'loopMode'][tab]] = [24, 462, 342, 34];
  // Its switches in a row, then its stepper.
  const plates = tab === 0 ? ['freeze', ...(p.wetMode !== 2 ? ['trails'] : []), ...(p.wetMode === 1 || p.wetMode === 3 ? ['wetSync'] : []), ...(p.wetMode === 1 ? ['tapeDelay'] : [])]
    : tab === 1 ? ['smooth'] : p.loopMode === 0 ? ['envSync', 'envSoft'] : [];
  plates.forEach((key, i) => { L[key] = [24 + i * 86, 690, 78, 26]; });
  if (tab === 0) L.wetDiv = [24, 722, 176, 30];
  if (tab === 1) { L.character = [24, 722, 176, 30]; L.wearPost = [206, 726, 160, 24]; }
  if (tab === 2) L.loopDiv = [24, 722, 176, 30];
  return L;
}

// Everything worth explaining, in the plug-in's words and order (rebuildHoverHelp): the first
// rectangle under the mouse wins. Source picker entries are the browser's own.
function helpTable(p, modOpen, L) {
  const out = [];
  const add = (r, label, text) => { if (r) out.push({ r, label, text }); };
  const wm = p.wetMode, lm = p.loopMode, worn = !!p.classic, record = p.recMode === 1;
  add(L.character, 'WEAR', 'Clean to worn. Clean= fewer artefacts. Worn= noise, a loop that warbles and a longer reverb.');
  if (wm !== 2) add(L.trails, 'TRAILS', 'The tails keep falling after you switch the wet side off.');
  add(L.smooth, 'SMOOTH', 'The sample rate slides instead of stepping, so CLOCK bends pitch as it turns.');
  add(L.freeze, 'FREEZE', 'The hold gesture without the holding: the wet side comes up frozen and rings on its own.');
  if (worn) add(L.wearPost, 'PRE / POST LOOP', 'Where the wear sits. Pre: the loop records it worn. Post: you hear it worn, but the loop keeps a clean take.');
  if (wm === 1) add(grow(L.tapeDelay, 4), 'TAPE', 'A tape echo: turning TIME bends the pitch, with wow, flutter and saturation. Off: TIME changes the delay without changing the pitch.');
  if (wm === 1 || wm === 3) add(grow(L.wetSync, 4), 'SYNC', 'TIME becomes a division of the beat, so the echoes or threads land on the grid.');
  if (lm === 0) {
    add(grow(L.envSync, 4), 'SYNC', 'The envelope slices by a division of the beat instead of a free length, so the loop stutters in time.');
    add(grow(L.envSoft, 4), 'SOFT', 'The slices fade in and out instead of cutting: a rounder, softer stutter with no clicks.');
  }
  add(L.chip0, 'MOD 1', 'Tap for its shape and rate. Drag it onto a control, or drag around a knob\'s edge, to route it. The square is on or off.');
  add(L.chip1, 'MOD 2', 'Tap for its shape and rate. Drag it onto a control, or drag around a knob\'s edge, to route it. The square is on or off.');
  add(L.routes, 'ROUTES', 'Every route in one list, with its depth.');
  if (modOpen) {
    add(L.rampWave, 'SHAPE', 'Click to step the shape: triangle, square, sine, sawtooth, random, smooth random.');
    add(L.rampRate, 'RATE', 'How slowly this source turns. With SYNC on it becomes a span of the beat.');
    add(L.rampSwitch[0], 'UNI', 'The swing sits above the knob instead of either side of it.');
    add(L.rampSwitch[1], 'SYNC', 'The rate becomes a division of the beat: eight bars down to a sixteenth.');
    add(L.rampSwitch[2], 'HOLD', 'This source stops where it is and its knobs stay pushed there. CC 52 holds both.');
  }
  add(grow(L.memory, 0, 2), 'MEMORY', 'In CAPTURE mode, MEMORY relates to how much of the input audio is captured. All the way left for short loops, and all the way right for loops of up to 30 seconds.');
  add(L.gain, 'GAIN', 'The loop\'s own level. Drag or scroll; hold Shift for fine. Double-click for 0 dB, right-click to normalise.');
  // CLEAR and UNDO sit inside the window, and the first match wins, so they go first.
  add(L.clear, 'CLEAR', 'Empties the take. UNDO brings it back.');
  add(L.undo, 'UNDO', 'One step back: a clear, a new take, or the last overdub layer. Press again to redo.');
  add(L.loopView, 'THE WINDOW', 'Drag START or END to trim the loop. Double-click for the whole take.');
  add(L.recMode, 'CAPTURE / RECORD', 'CAPTURE= a tap keeps what you just played. RECORD= tap to start, tap to play, or hold to overdub.');
  if (record) add(L.recBars, 'BARS', 'Free records until the next tap. A number records that many bars, then plays.');
  if (L.balance) add(grow([L.balance[0] - 32, L.balance[1], L.balance[2] + 32, L.balance[3]], 0, 2), 'BALANCE', 'Left= just the loop. Right= just the wet side. Middle= both in full. Drag up or down on it to add modulation.');
  // The plug-in's LEVEL drives into old tape past the middle; this one only sets the level
  // (IN THE PLUG-IN says so).
  add(L.hoodCell[0], 'LEVEL', 'How loud the input reaches Brood. The middle is as it comes in.');
  add(L.wetMode, 'WET MODE', 'Four ways of treating time: reverb, delay, slip, weave.');
  add(L.routing, 'ROUTING', 'What the wet side hears. IN= the input. IN + LOOP= input and loop. LOOP= just the loop.');
  add(L.loopMode, 'LOOP MODE', 'Three ways of holding a moment: envelope, tape, stretch.');
  add(L.fsWet, 'WET', 'Tap for the wet side, hold to freeze it where it stands. Key A.');
  add(L.fsLoop, 'LOOP', 'Tap to catch the recent past, tap again to stop, hold to overdub. Key L.');
  add(L.overdub, 'OVERDUB', 'Latches: layers into the playing loop until you switch it off. Needs a loop playing.');
  add(L.presetBar, 'PATCH', 'Click the name to search, star and audition sounds. The arrows step through them. SAVE keeps what you hear.');
  [['TONE', 'A high-cut on the wet side. Right is open, left is soft and far away.'],
    ['FADE', 'How much of the old take an overdub erases. Left keeps the pedal\'s slow erosion, right replaces it.'],
    ['WIDTH', 'How far apart the two speakers sit: left is mono, the middle is the image as built, right opens the sides.'],
    ['NOISE', 'How much of the worn clock\'s hiss is let through. Only heard with WEAR on WORN; left is silent.']]
    .forEach(([label, text], i) => add(L.hoodCell[i + 1], label, text));
  add(L.wetDiv, 'WET DIV', 'The wet side runs on a divided clock, so the room and the echo stretch out.');
  add(L.loopDiv, 'LOOP DIV', 'The loop runs on a divided clock: lower, longer, further from the other side.');
  // The source picker exists only in the browser.
  add(L.src.picker, 'SOURCE', 'What Brood hears: a demo loop, an audio file of your own, or the microphone. Use headphones with the microphone.');
  add(L.src.demo, 'DEMO', 'Samples to play Brood with. Click to step to the next. A sample sets TEMPO to its own.');
  add(L.src.song, 'SONG', 'Where you are in the song. Drag to jump; LOOP keeps what you just heard.');
  add(L.src.load, 'LOAD FILE', 'Choose an audio file from this computer. It loops into Brood; nothing is uploaded.');
  add(L.src.tempo, 'TEMPO', 'The beat SYNC and RECORD\'s bars count in. A host sets it in the plug-in; here you do.');
  add(L.reset, 'RESET', 'Every control back to A LITTLE DAYDREAM, LEVEL to 0 dB, no MOD routes. WET goes off and the loop is cleared, with nothing to UNDO. TEMPO and your sound stay.');
  add(L.src.tour, 'TOUR', 'A few steps through Brood, opening the panel a part at a time. The sound and every setting carry on as they are.');
  add(L.src.plugin, 'IN THE PLUG-IN', 'What the plug-in does that this page does not.');
  add(L.src.start, 'AUDIO', 'Starts and stops the sound on this page. Nothing plays until you press it.');
  // The knobs say what they do in the current mode. Last, so a switch beside a knob explains itself first.
  // MIX adds what only this page does: a playing loop mutes the dry sound.
  KNOB_IDS.forEach((id, i) => add(L.knob?.[i], knobName(i, p), `${WHISPER[i][KNOB_MODE[i] === 0 ? wm : KNOB_MODE[i] === 1 ? lm : 0]}.${id === 'mix' ? ' Here, the dry sound is muted while a loop plays.' : ''}`));
  return out;
}
// ───────── the wordmark ─────────
// "BROOD" in the thin cut of Squarepusher with its D redrawn, as an outline (never the font
// file): wordmarkOutline in native/plugin/PluginEditor.cpp, font units, y up, 360 tall.
const WORDMARK = [
  [[0, 360], [648, 360], [648, 0], [144, 0], [144, 72], [576, 72], [576, 144], [144, 144], [144, 216], [576, 216], [576, 288], [72, 288], [72, 0], [0, 0]],
  [[792, 360], [1440, 360], [1440, 144], [1008, 144], [1008, 72], [1440, 72], [1440, 0], [936, 0], [936, 216], [1368, 216], [1368, 288], [864, 288], [864, 0], [792, 0]],
  [[1584, 360], [2232, 360], [2232, 0], [1584, 0]],
  [[2160, 72], [2160, 288], [1656, 288], [1656, 72]],
  [[2376, 360], [3024, 360], [3024, 0], [2376, 0]],
  [[2952, 72], [2952, 288], [2448, 288], [2448, 72]],
  [[3168, 360], [3672, 360], [3816, 216], [3816, 144], [3672, 0], [3168, 0], [3168, 216], [3240, 216], [3240, 72], [3642, 72], [3744, 174], [3744, 186], [3642, 288], [3168, 288]],
];
const WORDMARK_PATH = WORDMARK.map((c) => `M${c.map(([x, y]) => `${x} ${360 - y}`).join('L')}Z`).join('');
function Wordmark({ r }) {
  return <svg className="abs" style={at(r)} viewBox="0 0 3816 360" role="img" aria-label="Brood"><path d={WORDMARK_PATH} fill={INK} fillRule="evenodd"/></svg>;
}

// ───────── small parts ─────────
// A line of text in a rectangle, as JUCE's drawText draws it: one line, vertically centred.
// With `fit`, text too wide for its box is squeezed sideways, as drawFittedText does, down to
// 0.7 of its width (JUCE's default), then cut.
function Txt({ r, align = 'left', cls = 'nm', size, color, children, style, fit }) {
  const el = useRef(null);
  const text = React.Children.toArray(children).join('');
  useLayoutEffect(() => {
    if (!fit) return undefined;
    const measure = () => {
      const box = el.current, node = box?.firstChild;
      if (!node) return;
      node.style.transform = '';
      const k = Math.max(0.7, Math.min(1, box.clientWidth / Math.max(1, node.scrollWidth)));
      if (k < 1) node.style.transform = `scaleX(${k.toFixed(3)})`;
    };
    measure();
    // Measured before the panel's fonts arrive, a text keeps the fallback's width: measure again
    // when they have.
    let live = true;
    document.fonts?.ready.then(() => { if (live) measure(); });
    return () => { live = false; };
  }, [fit, text, r[2], size?.fontSize]);
  return <div ref={el} className={`abs txt ${cls} ${align}`} style={{ ...at(r), lineHeight: `${r[3]}px`, color, ...size, ...style }}>
    {fit ? <span className="fit" style={{ transformOrigin: align === 'center' ? '50% 50%' : align === 'right' ? '100% 50%' : '0 50%' }}>{children}</span> : children}
  </div>;
}

// A line on the screen, never cut off: at its own size when it fits, then a little narrower
// (to 0.85) and a little smaller, step by step, down to `min`. At the smallest a value wraps
// onto the lines below; a title, with no room below, narrows further instead.
function ScreenLine({ r, size, min, color, wrap = false, children }) {
  const el = useRef(null);
  const words = React.Children.toArray(children).map((c) => (typeof c === 'string' || typeof c === 'number' ? c : '')).join('');
  useLayoutEffect(() => {
    const fitNow = () => {
      const box = el.current, span = box?.firstChild;
      if (!span) return;
      box.style.whiteSpace = 'nowrap'; box.style.lineHeight = `${r[3]}px`;
      for (let n = size; n >= min; n -= 0.5) {
        box.style.fontSize = sf(n).fontSize; span.style.transform = '';
        const k = box.clientWidth / Math.max(1, span.scrollWidth);
        if (k >= 1) return;
        if (k >= 0.85) { span.style.transform = `scaleX(${k.toFixed(3)})`; return; }
      }
      if (wrap) { box.style.whiteSpace = 'normal'; box.style.lineHeight = '1.12'; return; }
      span.style.transform = `scaleX(${Math.max(0.6, box.clientWidth / Math.max(1, span.scrollWidth)).toFixed(3)})`;
    };
    fitNow();
    let live = true;
    document.fonts?.ready.then(() => { if (live) fitNow(); });
    return () => { live = false; };
  }, [words, size, min, wrap, r[2], r[3]]);
  return <div ref={el} className="abs txt sc screen-line" style={{ ...at(r), color, ...sf(size), lineHeight: `${r[3]}px` }}>
    <span className="fit" style={{ transformOrigin: '0 50%' }}>{children}</span>
  </div>;
}

// Help on the screen: the screen's own size when it fits, and a little smaller when a long
// one wouldn't, so the whole sentence is always there to read rather than cut off.
function HelpText({ r, text }) {
  const el = useRef(null);
  useLayoutEffect(() => {
    const node = el.current;
    for (let n = 17; n >= 12; n -= 0.5) {
      node.style.fontSize = sf(n).fontSize;
      if (node.scrollHeight <= node.clientHeight + 1) break;
    }
  }, [text, r[2]]);
  return <div ref={el} className="abs lcd-help sc" style={{ ...at(r), ...sf(17) }}>{text}</div>;
}

// A pointer drag on a design-sized element, in design units.
function designPoint(element, event, width) {
  const box = element.getBoundingClientRect();
  const k = width / Math.max(1, box.width);
  return { x: (event.clientX - box.left) * k, y: (event.clientY - box.top) * k, k };
}

// A horizontal slider: a rounded track that fills to the value, and a round thumb (the kit's
// frameless drawLinearSlider). JUCE keeps the thumb inside, a thumb's radius from each end.
// modDepth (BALANCE): the slider is also a modulation handle, as a knob's edge is. Drag left or
// right and it moves; drag up or down and the selected source's swing on it follows, up for
// more. The first few pixels of movement decide which, and a tap without moving sets the value.
function Slider({ r, value, onChange, onReset, onTouch, label, valueText, disabled = false, children, targetId, modDepth }) {
  const el = useRef(null);
  const drag = useRef(null);
  const inset = Math.min(12, r[3] / 2);
  const span = r[2] - 2 * inset;
  const posAt = (event) => clamp((designPoint(el.current, event, r[2]).x - inset) / span);
  const down = (event) => {
    if (event.button !== 0 || disabled) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    onTouch?.();
    if (!modDepth) { drag.current = { mode: 'value' }; onChange(posAt(event)); return; }
    drag.current = { mode: null, x: event.clientX, y: event.clientY, k: designPoint(el.current, event, r[2]).k };
  };
  const move = (event) => {
    const d = drag.current;
    if (!d) return;
    if (!d.mode) {
      const dx = event.clientX - d.x, dy = event.clientY - d.y;
      if (Math.hypot(dx, dy) < 6) return;
      d.mode = Math.abs(dy) > Math.abs(dx) ? 'depth' : 'value';
      if (d.mode === 'depth') { d.source = modDepth.source(); d.start = modDepth.get(d.source); }
    }
    if (d.mode === 'value') onChange(posAt(event));
    else modDepth.set(d.source, clamp(d.start - (event.clientY - d.y) * d.k / (event.shiftKey ? 600 : 150), -1, 1));
  };
  const up = (event) => {
    if (drag.current && !drag.current.mode) onChange(posAt(event));   // a tap: the value goes there
    drag.current = null;
  };
  const key = (event) => {
    const step = event.shiftKey ? 0.01 : 0.05;
    let v = value;
    if (event.key === 'ArrowRight' || event.key === 'ArrowUp') v += step;
    else if (event.key === 'ArrowLeft' || event.key === 'ArrowDown') v -= step;
    else if (event.key === 'Home') v = 0;
    else if (event.key === 'End') v = 1;
    else return;
    event.preventDefault();
    onTouch?.();
    onChange(clamp(v));
  };
  const x = inset + span * clamp(value);
  return <div ref={el} className={`abs slider ${disabled ? 'off' : ''}`} style={at(r)} role="slider" tabIndex={disabled ? -1 : 0}
    aria-label={label} aria-valuemin="0" aria-valuemax="100" aria-valuenow={Math.round(value * 100)} aria-valuetext={valueText} aria-disabled={disabled}
    data-mod-target={targetId} onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up} onKeyDown={key}
    onDoubleClick={() => { if (onReset && !disabled) { onTouch?.(); onReset(); } }}>
    <svg width={r[2]} height={r[3]} aria-hidden="true">
      <rect x={inset} y={r[3] / 2 - 2.5} width={span} height="3" rx="1.5" fill={INK} fillOpacity=".16"/>
      <rect className="fill" x={inset} y={r[3] / 2 - 2.5} width={Math.max(0, x - inset)} height="3" rx="1.5" fill={INK}/>
      {children?.({ inset, span, cy: r[3] / 2 })}
      <circle cx={x} cy={r[3] / 2} r="6.5" fill={INK} fillOpacity=".45"/>
      <circle cx={x} cy={r[3] / 2 - 1} r="6.5" fill={INK}/>
    </svg>
  </div>;
}

// One plate across the column, the chosen cell a solid block (ThreeWay).
function ThreeWay({ r, labels, index, onChange, label }) {
  const n = labels.length;
  return <div className="abs threeway" style={at(r)} role="radiogroup" aria-label={label}>
    {labels.map((name, i) => {
      const x0 = Math.floor((r[2] - 2) * i / n), x1 = Math.floor((r[2] - 2) * (i + 1) / n);
      return <button key={name} type="button" role="radio" aria-checked={i === index} className={i === index ? 'on' : ''}
        style={{ left: x0, width: x1 - x0, height: r[3] - 2, ...nf(16) }} onClick={() => onChange(i)}><span>{name.toUpperCase()}</span></button>;
    })}
  </div>;
}

// A song's place (the browser's own, for songs a host page offers): the time so far, a track
// to drag or tap to jump, and the length. Arrows jump 5 s.
const mmss = (t) => (Number.isFinite(t) && t > 0 ? `${Math.floor(t / 60)}:${String(Math.floor(t % 60)).padStart(2, '0')}` : '0:00');
function SongBar({ r, engine, title }) {
  const el = useRef(null), last = useRef(0);
  const [st, setSt] = useState(() => engine.songState());
  const [drag, setDrag] = useState(null);   // where the finger is, 0 to 1, while dragging
  useEffect(() => { const id = setInterval(() => setSt(engine.songState()), 250); return () => clearInterval(id); }, [engine]);
  if (!st) return null;
  // With a title (on a desktop), it takes the left of the row and the bar the rest.
  const lead = title ? 320 : 0;
  const dur = st.duration || 0, side = 52, span = r[2] - lead - 2 * side;
  const pos = drag ?? (dur ? clamp(st.time / dur) : 0);
  const fracAt = (event) => clamp((designPoint(el.current, event, r[2]).x - lead - side) / span);
  const seek = (f) => { if (dur) engine.seekSong(f * dur); };
  const down = (event) => {
    if (event.button !== 0 || !dur) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    const f = fracAt(event); setDrag(f); seek(f); last.current = performance.now();
  };
  const move = (event) => {
    if (drag == null) return;
    const f = fracAt(event); setDrag(f);
    if (performance.now() - last.current > 150) { seek(f); last.current = performance.now(); }   // jumps while dragging, not every pixel
  };
  const up = (event) => { if (drag == null) return; const f = fracAt(event); seek(f); setDrag(null); setSt({ ...st, time: f * dur }); };
  const key = (event) => {
    const d = event.key === 'ArrowRight' || event.key === 'ArrowUp' ? 5 : event.key === 'ArrowLeft' || event.key === 'ArrowDown' ? -5 : 0;
    if (!d || !dur) return;
    event.preventDefault();
    engine.seekSong(clamp(st.time + d, 0, dur)); setSt({ ...st, time: clamp(st.time + d, 0, dur) });
  };
  const x = lead + side + span * pos, cy = r[3] / 2;
  return <div ref={el} className="abs slider songbar" style={at(r)} role="slider" tabIndex="0" aria-label="Song position"
    aria-valuemin="0" aria-valuemax={Math.round(dur)} aria-valuenow={Math.round(pos * dur)} aria-valuetext={`${mmss(pos * dur)} of ${mmss(dur)}`}
    onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up} onKeyDown={key}>
    {title && <Txt fit r={[0, 0, lead - 12, r[3]]} cls="vl" size={vf(15)} color={INK}>{title.toUpperCase()}</Txt>}
    <Txt r={[lead, 0, side - 8, r[3]]} align="right" cls="vl" size={vf(15)} color={MUTED}>{mmss(pos * dur)}</Txt>
    <svg className="abs" style={{ left: 0, top: 0 }} width={r[2]} height={r[3]} aria-hidden="true">
      <rect x={lead + side} y={cy - 1.5} width={span} height="3" rx="1.5" fill={INK} fillOpacity=".16"/>
      <rect x={lead + side} y={cy - 1.5} width={Math.max(0, x - lead - side)} height="3" rx="1.5" fill={INK} fillOpacity=".6"/>
      <circle cx={x} cy={cy} r="5" fill={INK}/>
    </svg>
    <Txt r={[lead + side + span + 8, 0, side - 8, r[3]]} cls="vl" size={vf(15)} color={MUTED}>{dur ? mmss(dur) : '--:--'}</Txt>
  </div>;
}

// A short list as a cell: anywhere on it steps forward; only the back arrow steps back.
function Stepper({ r, caption, names, index, onStep, capWidth = 44, label }) {
  const body = [2, 2, r[2] - 6, r[3] - 6];
  const arrow = clamp(Math.floor(Math.max(1, r[2] - 6) / 6), 12, 20);
  const innerW = body[2] - 4 - 2 * arrow - 3;
  const capW = Math.min(Math.floor(innerW / 2), capWidth);
  const tri = (right) => <svg className="abs arrow" style={{ left: right ? body[0] + body[2] - 2 - arrow : body[0] + 2, top: 0, width: arrow, height: r[3] }} aria-hidden="true">
    <path d={right ? `M${arrow / 2 - 2} ${r[3] / 2 - 4.5}L${arrow / 2 + 2} ${r[3] / 2 - 1}L${arrow / 2 - 2} ${r[3] / 2 + 2.5}Z` : `M${arrow / 2 + 2} ${r[3] / 2 - 4.5}L${arrow / 2 - 2} ${r[3] / 2 - 1}L${arrow / 2 + 2} ${r[3] / 2 + 2.5}Z`}/>
  </svg>;
  const capX = body[0] + 2 + arrow + 3;
  return <div className="abs stepper" style={at(r)} role="button" tabIndex="0" aria-label={`${label || caption}: ${names[index]}. Click to step`}
    onPointerDown={(event) => { if (event.button === 0) onStep(designPoint(event.currentTarget, event, r[2]).x < arrow + 2 ? -1 : 1); }}
    onKeyDown={(event) => {
      if (event.key === 'ArrowLeft' || event.key === 'ArrowDown') { event.preventDefault(); onStep(-1); }
      else if (event.key === 'ArrowRight' || event.key === 'ArrowUp' || event.key === ' ' || event.key === 'Enter') { event.preventDefault(); onStep(1); }
    }}>
    <div className="abs plate" style={at(body)}/>
    {tri(false)}{tri(true)}
    {caption && <Txt fit r={[capX, 0, capW, r[3]]} size={nf(16)} color={MUTED}>{caption}</Txt>}
    <Txt fit r={[capX + capW + 2, 0, innerW - capW - 2, r[3]]} align="center" cls="vl" size={vf(18)} color={INK}>{names[index]}</Txt>
  </div>;
}

// A small plate that belongs to the control above it, lit while it's on.
function Plate({ r, text, on, onClick, label, disabled = false }) {
  return <button type="button" className={`abs plate-switch ${on ? 'on' : ''}`} style={{ ...at(r), ...nf(15) }} role="switch" aria-checked={!!on}
    aria-label={label || text} disabled={disabled} onClick={onClick}>{text}</button>;
}

// A key: a plate, faint at rest, stronger under the mouse (the kit's frameless TextButton).
function Key({ r, children, onClick, label, disabled = false, pressed, cls = '' }) {
  return <button type="button" className={`abs key ${cls}`} style={at(r)} aria-label={label} aria-pressed={pressed} disabled={disabled} onClick={onClick}>
    <span className="nm" style={nf(clamp(r[3] * 0.62, 13, 18))}>{children}</span>
  </button>;
}
const ArrowGlyph = ({ dir }) => <svg width="12" height="12" viewBox="-6 -6 12 12" aria-hidden="true"><path d={dir < 0 ? 'M-3.5 0L3 -5L3 5Z' : 'M3.5 0L-3 -5L-3 5Z'} fill={INK}/></svg>;

// A side section: the sheet, a small square of its colour, its name, a dotted leader and a figure.
function Section({ r, mark, name, tail }) {
  return <div className="abs sheet" style={at(r)}>
    <div className="section-head">
      <i style={{ background: mark }}/>
      <span className="nm" style={nf(16)}>{name}</span>
      <b/>
      {tail && <span className="vl" style={{ ...vf(16), color: MUTED }}>{tail}</span>}
    </div>
  </div>;
}

// The menus the plug-in opens on a right-click.
function Menu({ menu, close }) {
  useEffect(() => {
    if (!menu) return undefined;
    const away = (event) => { if (!event.target.closest('.menu')) close(); };
    const esc = (event) => { if (event.key === 'Escape') close(); };
    window.addEventListener('pointerdown', away, true); window.addEventListener('keydown', esc);
    return () => { window.removeEventListener('pointerdown', away, true); window.removeEventListener('keydown', esc); };
  }, [menu, close]);
  if (!menu) return null;
  return <div className="abs menu" role="menu" style={{ left: Math.max(4, Math.min(menu.x, menu.w - 230)), top: Math.max(4, Math.min(menu.y, menu.h - 34 * menu.items.length - 40)) }}>
    {menu.title && <div className="menu-title nm" style={nf(16)}>{menu.title}</div>}
    {menu.items.map((item) => <button key={item.label} type="button" role="menuitem" disabled={item.disabled} className="nm" style={nf(18)}
      onClick={() => { close(); item.act?.(); }}>{item.label}</button>)}
  </div>;
}

// ───────── a knob ─────────
// A flat disc with a short pointer, inside an arc that fills to the value (the kit's frameless
// drawRotarySlider), with each modulation source's swing as a ring in its colour.
function Knob({ i, r, valueW = 236, params, effective, onChange, onTouch, modSource, selectSource, showReadout, openMenu, active, onActive }) {
  const id = KNOB_IDS[i], value = params[id], size = r[2];
  const el = useRef(null);
  const drag = useRef(null);
  const [hoverRing, setHoverRing] = useState(-1);
  const depth = (k) => params[`mod${k + 1}_${id}`] || 0;
  const routed = (k) => Math.abs(depth(k)) >= 0.005;
  const valueRef = useRef(value); valueRef.current = value;
  useEffect(() => {
    const node = el.current;
    const wheel = (event) => {
      event.preventDefault();
      onTouch(id);
      onChange(id, clamp(valueRef.current - event.deltaY * (event.shiftKey ? 0.0002 : 0.001)));
    };
    node.addEventListener('wheel', wheel, { passive: false });
    return () => node.removeEventListener('wheel', wheel);
  }, [id, onChange, onTouch]);
  // The rings are handles (sourceAtRing): a press around the knob drags that source's depth,
  // and where there's no ring yet it makes one for the selected source.
  const ringAt = (event) => {
    const { x, y } = designPoint(el.current, event, 104);
    const d = Math.hypot(x - 52, y - 52);
    if (d < 38 || d > 55) return -1;
    if (routed(0) && routed(1)) return d >= 48.5 ? 1 : 0;
    if (routed(0) !== routed(1) && modSource !== (routed(0) ? 0 : 1)) return modSource;
    return routed(0) ? 0 : routed(1) ? 1 : modSource;
  };
  const showDepth = (k, d) => showReadout(`MOD ${k + 1} > ${MOD_TARGET_NAMES[id]}`, `${Math.round(d * 100) > 0 ? '+' : ''}${Math.round(d * 100)}%`);
  const down = (event) => {
    if (event.button === 2) { event.preventDefault(); return; }
    if (event.button !== 0) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    event.currentTarget.focus();
    const ring = ringAt(event);
    if (ring >= 0 || event.altKey) {
      const k = ring >= 0 ? ring : modSource;
      drag.current = { y: event.clientY, k, start: depth(k), scale: designPoint(el.current, event, 104).k };
      selectSource(k);
      showDepth(k, depth(k));
      return;
    }
    drag.current = { y: event.clientY, x: event.clientX, start: value, scale: designPoint(el.current, event, 104).k };
    onActive?.(i);
    onTouch(id);
  };
  const move = (event) => {
    const d = drag.current;
    if (!d) { const ring = ringAt(event); if (ring !== hoverRing) setHoverRing(ring); return; }
    if (d.k !== undefined) {
      const next = clamp(d.start - (event.clientY - d.y) * d.scale / (event.shiftKey ? 600 : 150), -1, 1);
      onChange(`mod${d.k + 1}_${id}`, next);
      showDepth(d.k, next);
      return;
    }
    // Up and right both turn it up; the full travel is 220 design pixels.
    const moved = ((d.y - event.clientY) + (event.clientX - d.x)) * d.scale;
    onChange(id, clamp(d.start + moved / (event.shiftKey ? 880 : 220)));
  };
  const up = () => { drag.current = null; onActive?.(-1); };
  const key = (event) => {
    let v = value;
    const step = event.shiftKey ? 0.01 : 0.05;
    if (event.key === 'ArrowUp' || event.key === 'ArrowRight') v += step;
    else if (event.key === 'ArrowDown' || event.key === 'ArrowLeft') v -= step;
    else if (event.key === 'Home') v = 0;
    else if (event.key === 'End') v = 1;
    else if (event.key === 'Delete' || event.key === 'Backspace') {
      // All of this knob's modulation goes.
      event.preventDefault();
      onChange(`mod1_${id}`, 0); onChange(`mod2_${id}`, 0);
      return;
    }
    else return;
    event.preventDefault();
    onTouch(id);
    onChange(id, clamp(v));
  };
  const routeMenu = (event) => {
    event.preventDefault();
    const items = [0, 1].filter(routed).map((k) => ({ label: `Remove MOD ${k + 1} (${Math.round(depth(k) * 100)}%)`, act: () => onChange(`mod${k + 1}_${id}`, 0) }));
    openMenu(event, MOD_TARGET_NAMES[id], items.length ? items : [{ label: 'No routes. Drag a source onto this control.', disabled: true }]);
  };
  // Geometry in the knob's 104 box: the disc is 74 across, the arc 8 outside it.
  const c = 52, rd = 37, track = rd + 8, outer = 48;
  const a0 = -135, span = 270;
  const point = (rad, v) => { const a = (a0 + span * v) * Math.PI / 180; return [c + Math.sin(a) * rad, c - Math.cos(a) * rad]; };
  const arc = (rad, from, to) => {
    const [x0, y0] = point(rad, from), [x1, y1] = point(rad, to);
    return `M${x0.toFixed(2)} ${y0.toFixed(2)}A${rad} ${rad} 0 ${(to - from) * span > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
  };
  const dark = id === 'clock';
  const any = routed(0) || routed(1);
  const [ex, ey] = point(outer - 7, clamp(effective));
  const name = knobName(i, params);
  const valueText = active ?? describe(id, params).toUpperCase();
  return <>
    <Txt r={[r[0] + size / 2 - 80, r[1] - 20, 160, 20]} align="center" size={nf(20)} color={INK}>{name}</Txt>
    <div ref={el} className={`abs knob ${dark ? 'dark' : ''}`} style={{ ...at(r), cursor: hoverRing >= 0 ? 'ns-resize' : undefined }} data-mod-target={id}
      role="slider" tabIndex="0" aria-label={`${name}${id === 'modWet' ? ' (wet)' : id === 'modLoop' ? ' (loop)' : ''}`}
      aria-valuemin="0" aria-valuemax="100" aria-valuenow={Math.round(value * 100)} aria-valuetext={describe(id, params)}
      onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up} onPointerLeave={() => setHoverRing(-1)}
      onKeyDown={key} onFocus={() => onTouch(id)} onContextMenu={routeMenu}
      onDoubleClick={(event) => {
        // On a ring: that source comes off this knob, as a double-click resets anything else.
        const ring = ringAt(event);
        if (ring >= 0 && routed(ring)) { onChange(`mod${ring + 1}_${id}`, 0); setHoverRing(-1); showReadout(`MOD ${ring + 1}`, `${MOD_TARGET_NAMES[id].toLowerCase()} removed`); }
        else { onTouch(id); onChange(id, KNOB_DEFAULTS[id]); }
      }}>
      <svg width={size} height={size} viewBox="0 0 104 104" aria-hidden="true">
        <path d={arc(track, 0, 1)} fill="none" stroke={INK} strokeOpacity=".16" strokeWidth="2.6" strokeLinecap="round"/>
        {value > 0.001 && <path className="fill" d={arc(track, 0, clamp(value))} fill="none" stroke={INK} strokeWidth="2.6" strokeLinecap="round"/>}
        {[0, 1].map((k) => {
          if (!routed(k)) return null;
          const d = depth(k), uni = params[`mod${k + 1}Uni`];
          const lo = clamp(uni ? Math.min(value, value + d) : value - Math.abs(d)), hi = clamp(uni ? Math.max(value, value + d) : value + Math.abs(d));
          if (hi - lo < 0.001) return null;
          return <path key={k} d={arc(outer - 1.5 + 4 * k, lo, hi)} fill="none" stroke={MOD_COLOURS[k]} strokeWidth="2.5"/>;
        })}
        {hoverRing >= 0 && !routed(hoverRing) && <circle cx={c} cy={c} r={outer - 1.5 + 4 * hoverRing} fill="none" stroke={MOD_COLOURS[hoverRing]} strokeOpacity=".45"/>}
        <circle cx={c} cy={c + 2} r={rd} fill={INK} fillOpacity=".45"/>
        <circle cx={c} cy={c} r={rd} fill={dark ? '#191916' : '#e8e5db'}/>
        <rect x="-1.5" y={-rd * 0.86} width="3" height={rd * 0.5} rx="1.5" fill={dark ? '#ede9e0' : INK} transform={`translate(${c} ${c}) rotate(${a0 + span * value})`}/>
        {any && <circle cx={ex} cy={ey} r="2.5" fill={INK}/>}
      </svg>
    </div>
    <Txt fit r={[r[0] + size / 2 - valueW / 2, r[1] + size - 2, valueW, 18]} align="center" cls="vl" size={vf(active ? 18 : 16)} color={active ? INK : MUTED}>{valueText}</Txt>
  </>;
}

// ───────── a footswitch ─────────
// A big plate that darkens while it's held, with a light that's solid while this side is on.
function Footswitch({ r, name, keyName, lit, onGesture, which }) {
  const pressed = useRef(false);
  const [down, setDown] = useState(false);
  const release = () => { if (!pressed.current) return; pressed.current = false; setDown(false); onGesture(which, 'up'); };
  const press = () => { if (pressed.current) return; pressed.current = true; setDown(true); onGesture(which, 'down'); };
  useEffect(() => {
    const blur = () => release();
    window.addEventListener('blur', blur);
    return () => window.removeEventListener('blur', blur);
  });
  const w = r[2] - 3, h = r[3] - 3;
  const middle = w - 24 - 16 - 10 - 62 - 36;
  return <button type="button" className={`abs footswitch ${down ? 'down' : ''}`} style={at([r[0], r[1], w, h])}
    aria-label={`${name}: tap or hold. key ${keyName}`} aria-pressed={!!lit}
    onPointerDown={(event) => { if (event.button !== 0) return; event.currentTarget.setPointerCapture(event.pointerId); press(); }}
    onPointerUp={release} onPointerCancel={release}
    onKeyDown={(event) => { if ((event.key === ' ' || event.key === 'Enter') && !event.repeat) { event.preventDefault(); press(); } }}
    onKeyUp={(event) => { if (event.key === ' ' || event.key === 'Enter') { event.preventDefault(); release(); } }}>
    <i className="led" style={{ left: 13, top: h / 2 - 7, background: lit ? LIT : 'rgba(24,24,21,.25)' }}/>
    <Txt r={[38, 0, 62, h]} size={nf(24)} color={INK}>{name}</Txt>
    {middle >= 80 && <Txt r={[100, 0, middle, h]} align="center" cls="vl" size={vf(16)} color={MUTED}>TAP / HOLD</Txt>}
    <Txt r={[w - 12 - 36, 0, 36, h]} align="right" cls="vl" size={vf(16)} color={INK}>[ {keyName} ]</Txt>
  </button>;
}

// ───────── the take strip ─────────
// The take: an ink window, with what plays in pale and the rest of the take dimmed, START and
// END as markers, and the play heads. With nothing caught it says what the looper is doing.
function TakeStrip({ L, wave, state, params, scale, onChange, onTouch, showReadout, running, engine, openMenu }) {
  const canvas = useRef(null);
  const drag = useRef(null);
  const [hot, setHot] = useState(-1);
  const r = L.loopView, w = r[2], h = r[3];
  const lsrc = state?.lsrc || 0;
  const live = !lsrc || !wave || state?.recArmed || state?.loop === 'rec';
  const markers = () => {
    if (!state?.lsrc) return null;
    const s = clamp(state.winStart, 0, state.lsrc);
    return { s, e: clamp(s + state.winLen, s, state.lsrc), len: state.lsrc };
  };
  useEffect(() => {
    const c = canvas.current, ratio = (window.devicePixelRatio || 1) * scale;
    const pw = Math.round(w * ratio), ph = Math.round(h * ratio);
    if (c.width !== pw) c.width = pw;
    if (c.height !== ph) c.height = ph;
    const g = c.getContext('2d');
    g.setTransform(ratio, 0, 0, ratio, 0, 0);
    g.fillStyle = INK; g.fillRect(0, 0, w, h);
    const top = 16, bh = h - top, mid = top + bh / 2, rise = bh / 2 - 10;
    const font = (n) => `${(n / 1.215).toFixed(2)}px "B612 Mono", monospace`;
    g.textBaseline = 'middle';
    const led = state?.loop === 'rec' ? 2 : state?.loop === 'dub' ? 3 : state?.loop === 'play' ? 1 : 0;
    if (live) {
      // Silence, as the plug-in draws it: the words where the waveform would be.
      g.fillStyle = DIMMED; g.fillRect(12, mid + 14, w - 24, 1);
      const record = params.recMode === 1, bars = params.recBars;
      const idle = record ? 'PRESS LOOP TO RECORD_' : 'LISTENING. PRESS LOOP TO KEEP THE LAST MOMENT_';
      const text = state?.recArmed ? 'WAITING FOR THE BAR LINE_' : led === 2 ? (record && bars ? `RECORDING ${describe('recBars', params).toUpperCase()}_` : 'RECORDING_') : idle;
      const words = running ? text : 'PRESS START AUDIO, BELOW, TO HEAR BROOD_';
      // Smaller until it fits: a phone's strip is less than half as wide.
      let size = 18;
      g.font = font(size);
      while (size > 10 && g.measureText(words).width > w - 24) { size -= 0.5; g.font = font(size); }
      g.fillStyle = PAPER; g.textAlign = 'center';
      g.fillText(words, w / 2, top + (bh - 16) / 2);
      return;
    }
    const m = markers();
    const xs = w * m.s / m.len, xe = w * m.e / m.len;
    const clock = Math.max(1, state.clockHz);
    // A tick a second along the foot (every five, on a long take), so length can be read.
    const seconds = m.len / clock, every = seconds > 16 ? 5 : 1;
    g.fillStyle = TICK;
    for (let t = every; t < seconds; t += every) g.fillRect(w * t / seconds, h - 6, 1, 6);
    const peaks = wave.peaks, n = peaks.length, bw = w / n;
    const gain = Math.pow(10, params.loopDb / 20);   // what you see is the level you hear
    for (let i = 0; i < n; i++) {
      const a = Math.min(1, peaks[i] * 1.6 * gain) * rise, x = i * bw;
      g.fillStyle = x >= xs && x < xe ? PAPER : DIMMED;
      g.fillRect(x, mid - a, Math.max(1, bw - 0.4), 2 * a + 1);
    }
    // The play heads: TAPE has one; STRETCH shows both grains and ENV the slice, each as bright as it's loud.
    const heads = state.head || [state.pos, 0], levels = state.headLevel || [1, 0];
    for (let k = 0; k < 2; k++) {
      const level = clamp(levels[k]);
      if (level < 0.02) continue;
      g.globalAlpha = level; g.fillStyle = LIT;
      g.fillRect(w * (state.winStart + heads[k]) / m.len - 0.5, top, 1, bh);
    }
    g.globalAlpha = 1;
    const states = ['STOPPED', 'PLAYING', 'RECORDING', 'OVERDUB'];
    g.font = font(14); g.textAlign = 'left';
    g.fillStyle = led >= 2 ? LIT : PAPER;
    g.fillText(`${state.parked ? 'WAITING FOR PLAY' : states[led]}  ${seconds.toFixed(2)} S${state.follows ? '  FOLLOWS THE HOST' : ''}`, 6, 8.5);
    // The markers: a rule the full height, and a tab that names it. START's tab hangs inside
    // the window from the top, END's from the bottom, so the two never cover each other.
    const marker = (x, isEnd, lit) => {
      const mw = lit ? 3 : 2;
      g.fillStyle = lit ? LIT : PAPER;
      g.fillRect(clamp(x - mw / 2, 2, w - mw - 2), top, mw, bh);
      const label = isEnd ? 'END' : 'START';
      g.font = font(14);
      const tw = Math.ceil(g.measureText(label).width) + 8;
      const tx = clamp(isEnd ? x - tw : x, 2, w - tw - 2), ty = isEnd ? h - 13 : top;
      g.fillRect(tx, ty, tw, 13);
      g.fillStyle = INK; g.textAlign = 'center';
      g.fillText(label, tx + tw / 2, ty + 7);
      g.textAlign = 'left';
    };
    marker(xs, false, hot === 0 || drag.current?.which === 0);
    marker(xe, true, hot === 1 || drag.current?.which === 1);
  });

  // On a marker: that marker. Inside the window: the window. Outside it: the nearer marker.
  const regionAt = (x) => {
    const m = markers();
    if (!m || live) return -1;
    const xs = w * m.s / m.len, xe = w * m.e / m.len;
    const nearest = () => {
      const toStart = Math.abs(x - xs), toEnd = Math.abs(x - xe);
      if (Math.abs(toStart - toEnd) < 0.5) return x > xe ? 1 : 0;
      return toStart < toEnd ? 0 : 1;
    };
    if (Math.abs(x - xs) <= 8 || Math.abs(x - xe) <= 8) return nearest();
    return x > xs && x < xe ? 2 : nearest();
  };
  const moveMarker = (x) => {
    const d = drag.current;
    const len = Math.max(64, lsrc), tape = params.loopMode === 1;
    const atSample = x / w * len + d.grab;
    const shortest = Math.max(64, 0.02 * len);
    let first = d.s, width = d.e - d.s;
    if (d.which === 0) { first = clamp(atSample, 0, d.e - shortest); width = d.e - first; }
    else if (d.which === 1) width = clamp(atSample - d.s, shortest, len - d.s);
    else first = clamp(atSample, 0, Math.max(0, len - width));
    const trim = loopTrim(tape, len, params.length, first, width);
    onChange('start', trim.start);
    onChange('loopEnd', trim.end);
    if (tape) onChange('length', trim.length);
    const clock = Math.max(1, state.clockHz);
    onTouch(d.which === 1 ? 'loopEnd' : 'start', false);
    showReadout(d.which === 0 ? 'START' : d.which === 1 ? 'END' : 'THE WINDOW', `${(trim.firstSample / clock).toFixed(2)} S IN, PLAYS ${(trim.width / clock).toFixed(2)} S`);
  };
  const down = (event) => {
    if (event.button !== 0 || event.target !== canvas.current) return;
    const { x } = designPoint(canvas.current, event, w);
    const which = regionAt(x);
    if (which < 0) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    const m = markers(), len = Math.max(64, lsrc), pressed = x / w * len, reach = 8 * len / w;
    // Held where it was pressed, so a grab moves nothing. Outside the window a marker comes to the mouse.
    const insideWindow = pressed >= m.s - reach && pressed <= m.e + reach;
    drag.current = { which, s: m.s, e: m.e, grab: !insideWindow ? 0 : which === 1 ? m.e - pressed : m.s - pressed };
    setHot(which === 2 ? -1 : which);
    moveMarker(x);
  };
  const move = (event) => {
    const { x } = designPoint(canvas.current, event, w);
    if (drag.current) { moveMarker(x); return; }
    const region = regionAt(x);
    canvas.current.style.cursor = region === 2 ? 'grab' : region >= 0 ? 'ew-resize' : '';
    const next = region === 2 ? -1 : region;
    if (next !== hot) setHot(next);
  };
  const up = () => { drag.current = null; };
  const key = (event) => {
    // From the keyboard: the arrows move END, with shift they move START; right is towards the present.
    const d = event.key === 'ArrowRight' ? 0.01 : event.key === 'ArrowLeft' ? -0.01 : 0;
    if (!d) { if (event.key === 'Home') { event.preventDefault(); onChange('start', BASE.start); onChange('loopEnd', BASE.loopEnd); onTouch('loopEnd'); } return; }
    event.preventDefault();
    if (event.shiftKey) { onChange('start', clamp(params.start + d)); onTouch('start'); }
    else { onChange('loopEnd', clamp(params.loopEnd + d, 0.02, 1)); onTouch('loopEnd'); }
  };

  // CLEAR, UNDO and GAIN on the heading line.
  const can = running && !!state;
  const canClear = can && (lsrc > 0 || state.loop === 'rec' || state.recArmed);
  const canUndo = can && state.undoKind > 0 && state.loop !== 'rec' && !state.recArmed;
  const redo = !!state?.undoRedo;
  const gain = useRef(null);
  const db = params.loopDb, half = Math.round(db * 2);
  const normalise = () => {
    const peak = wave?.peaks ? Math.max(...wave.peaks) : 0;
    return peak > 1e-5 ? clamp(-1 - 20 * Math.log10(peak), -24, 12) : 0;
  };
  const gainDown = (event) => {
    if (event.button === 2) return;
    if (event.button !== 0) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    gain.current = { y: event.clientY, db, k: designPoint(event.currentTarget, event, 130).k };
    onTouch('loopDb');
  };
  const gainMove = (event) => {
    const gd = gain.current;
    if (!gd) return;
    // Each move adds its own change at the rate Shift sets now.
    gd.db = clamp(gd.db + (gd.y - event.clientY) * gd.k * (event.shiftKey ? 0.02 : 0.15), -24, 12);
    gd.y = event.clientY;
    onChange('loopDb', Math.round(gd.db * 100) / 100);
  };
  useEffect(() => {
    const el = document.getElementById('brood-gain');
    const wheel = (event) => { event.preventDefault(); onTouch('loopDb'); onChange('loopDb', clamp(db + (event.deltaY < 0 ? 0.5 : -0.5), -24, 12)); };
    el.addEventListener('wheel', wheel, { passive: false });
    return () => el.removeEventListener('wheel', wheel);
  }, [db, onChange, onTouch]);
  const what = ['', 'the take before', 'the cleared take', 'the layer'];
  return <>
    <div className="abs take" style={at(r)} tabIndex={live ? -1 : 0} role="group" onKeyDown={key}
      aria-label={live ? 'The take: nothing caught yet' : `The take. START ${describe('start', params)}, END ${describe('loopEnd', params)}. Arrow keys move END, shift and arrow keys move START.`}>
      <canvas ref={canvas} style={{ width: w, height: h }} onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up}
        onPointerLeave={() => { if (!drag.current && hot !== -1) setHot(-1); }}
        onDoubleClick={() => { onChange('start', BASE.start); onChange('loopEnd', BASE.loopEnd); onTouch('loopEnd'); }}/>
    </div>
    <button type="button" className="abs strip-key sc" style={{ ...at(L.clear), ...sf(14) }} disabled={!canClear}
      onClick={() => { engine.clearLoop(); showReadout('LOOP', 'cleared. undo brings it back'); }}>CLEAR</button>
    <button type="button" className="abs strip-key sc" style={{ ...at(L.undo), ...sf(14) }} disabled={!canUndo}
      onClick={() => {
        const kind = clamp(state.undoKind, 0, 3);
        engine.undoLoop();
        showReadout(redo ? 'REDO' : 'UNDO', `${redo ? 'again: ' : 'back to '}${kind === 3 ? (redo ? 'the layer is back' : 'before the layer') : what[kind]}`);
      }}>{redo ? 'REDO' : 'UNDO'}</button>
    <div id="brood-gain" className="abs gain sc" style={{ ...at(L.gain), ...sf(14), color: half === 0 ? PAPER : LIT }} role="slider" tabIndex="0"
      aria-label="Loop gain" aria-valuemin="-24" aria-valuemax="12" aria-valuenow={db} aria-valuetext={`${db.toFixed(1)} dB`}
      onPointerDown={gainDown} onPointerMove={gainMove} onPointerUp={() => { gain.current = null; }} onPointerCancel={() => { gain.current = null; }}
      onDoubleClick={() => { onTouch('loopDb'); onChange('loopDb', 0); }}
      onKeyDown={(event) => {
        const d = event.key === 'ArrowUp' || event.key === 'ArrowRight' ? 0.5 : event.key === 'ArrowDown' || event.key === 'ArrowLeft' ? -0.5 : 0;
        if (d) { event.preventDefault(); onTouch('loopDb'); onChange('loopDb', clamp(db + d, -24, 12)); }
      }}
      onContextMenu={(event) => {
        event.preventDefault();
        openMenu(event, null, [
          { label: 'Normalise (peak at -1 dB)', act: () => { onTouch('loopDb'); onChange('loopDb', Math.round(normalise() * 100) / 100); } },
          { label: 'Reset to 0 dB', act: () => { onTouch('loopDb'); onChange('loopDb', 0); } },
        ]);
      }}>GAIN {half > 0 ? '+' : ''}{(half / 2).toFixed(1)} DB</div>
  </>;
}

// ───────── modulation ─────────
// A chip: its number, how many routes it has, an on square, and the last two seconds of what it did.
function ModChip({ k, r, params, open, trace, onToggleOn, onClick, onDragTo, onDrop, openMenu, onChange }) {
  const drag = useRef(null);
  const on = !!params[`mod${k + 1}On`];
  const routes = MOD_TARGETS.filter((t) => Math.abs(params[`mod${k + 1}_${t}`]) >= 0.005).length;
  const w = r[2] - 2, h = r[3] - 2;
  const plot = [6, 22, w - 12, h - 26];
  const points = trace.map((v, i) => `${(plot[0] + plot[2] * i / (trace.length - 1)).toFixed(1)},${(plot[1] + plot[3] - plot[3] * v).toFixed(1)}`).join(' ');
  return <div className={`abs chip ${open ? 'open' : ''}`} style={{ ...at([r[0], r[1], w, h]), '--mod': MOD_COLOURS[k] }} role="button" tabIndex="0"
    aria-label={`MOD ${k + 1}, ${routes} ${routes === 1 ? 'route' : 'routes'}. Click to show its controls; drag onto a knob or BALANCE to route it.`} aria-expanded={open}
    onKeyDown={(event) => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); onClick(); } }}
    onContextMenu={(event) => { event.preventDefault(); openMenu(event, null, [{ label: 'Clear this source\'s routes', act: () => MOD_TARGETS.forEach((t) => onChange(`mod${k + 1}_${t}`, 0)) }]); }}
    onPointerDown={(event) => {
      if (event.button !== 0) return;
      const { x, y } = designPoint(event.currentTarget, event, w);
      if (x >= w - 24 && x <= w - 3 && y >= 1 && y <= 22) { onToggleOn(); return; }
      event.currentTarget.setPointerCapture(event.pointerId);
      drag.current = { x: event.clientX, y: event.clientY, moved: false };
    }}
    onPointerMove={(event) => {
      const d = drag.current;
      if (!d) return;
      if (!d.moved && Math.hypot(event.clientX - d.x, event.clientY - d.y) > 4) d.moved = true;
      if (d.moved) onDragTo(event);
    }}
    onPointerUp={(event) => {
      const d = drag.current;
      drag.current = null;
      if (!d) return;
      if (d.moved) onDrop(event); else onClick();
    }}
    onPointerCancel={() => { drag.current = null; onDrop(null); }}>
    <i className="num vl" style={{ background: on ? MOD_COLOURS[k] : RULE, ...vf(15) }}>{k + 1}</i>
    <Txt r={[24, 3, w - 48, 16]} cls="vl" size={vf(14)} color={MUTED}>{routes} {routes === 1 ? 'ROUTE' : 'ROUTES'}</Txt>
    <i className="on-box" style={{ background: on ? LIT : 'rgba(24,24,21,.18)' }}/>
    <svg className="abs" style={{ left: 0, top: 0 }} width={w} height={h} aria-hidden="true">
      <polyline points={points} fill="none" stroke={on ? MOD_COLOURS[k] : RULE} strokeWidth="1.5"/>
    </svg>
  </div>;
}

// The kit's compact switch: a short pill, lit when on, and its name.
function Switch({ r, text, on, onClick }) {
  return <button type="button" className={`abs switch ${on ? 'on' : ''}`} style={at(r)} role="switch" aria-checked={!!on} onClick={onClick}>
    <i className="pill" style={{ top: r[3] / 2 - 6 }}><i/></i>
    <Txt r={[21, 0, r[2] - 21, r[3]]} size={nf(15)} color={on ? INK : MUTED}>{text}</Txt>
  </button>;
}

// Every route in one list, with its depth, over the main sheet (RouteList).
function RouteList({ L, params, onChange, close, openMenu }) {
  const [rows, setRows] = useState(() => [0, 1].flatMap((k) => MOD_TARGETS.filter((t) => Math.abs(params[`mod${k + 1}_${t}`]) >= 0.005).map((t) => ({ k, t }))));
  const r = [L.overlay[0], L.overlay[1], L.overlay[2] + 3, L.overlay[3] + 3];
  const nameW = L.phone ? 116 : 192, closeW = L.phone ? 70 : 90, clearW = L.phone ? 92 : 110;
  const inner = [r[0] + 14, r[1] + 26 + 10, r[2] - 3 - 28, r[3] - 3 - 26 - 20];
  return <div className="abs overlay sheet" style={at([r[0], r[1], r[2] - 3, r[3] - 3])} role="dialog" aria-label="Routes">
    <div className="panel-head"><i className="sc" style={sf(16)}>M</i><span className="nm" style={nf(16)}>ROUTES</span><b/><span className="vl" style={{ ...vf(16), color: MUTED }}>{rows.length} {rows.length === 1 ? 'ROUTE' : 'ROUTES'}</span></div>
    {!rows.length && <Txt r={[14, 36, inner[2], inner[3] - 40]} align="center" cls="vl" size={vf(19)} color={MUTED}>NO ROUTES YET. DRAG A SOURCE ONTO A KNOB.</Txt>}
    {rows.map(({ k, t }, i) => {
      const id = `mod${k + 1}_${t}`, y = 36 + i * 32, d = params[id];
      return <React.Fragment key={id}>
        <i className="abs num vl" style={{ left: 16, top: y + 7, background: MOD_COLOURS[k], ...vf(16) }}>{k + 1}</i>
        <Txt fit r={[44, y, nameW, 32]} size={nf(19)} color={INK}>{MOD_TARGET_NAMES[t]}</Txt>
        <Slider r={[52 + nameW, y, inner[2] - nameW - 142, 32]} value={(d + 1) / 2} onChange={(v) => onChange(id, Math.round((v * 2 - 1) * 100) / 100)} label={`MOD ${k + 1} depth on ${MOD_TARGET_NAMES[t]}`} valueText={`${Math.round(d * 100)}%`}/>
        <Txt r={[inner[2] - 90, y, 60, 32]} align="right" cls="vl" size={vf(19)} color={MUTED}>{Math.round(d * 100) > 0 ? '+' : ''}{Math.round(d * 100)}%</Txt>
        <Key r={[inner[2] - 20, y + 2, 34, 28]} label={`Remove MOD ${k + 1} from ${MOD_TARGET_NAMES[t]}`} onClick={() => { onChange(id, 0); setRows((all) => all.filter((row) => !(row.k === k && row.t === t))); }}>X</Key>
      </React.Fragment>;
    })}
    {[0, 1].map((k) => <Key key={k} r={[13 + k * 82, r[3] - 3 - 10 - 32, 76, 32]} label={`Add a route for MOD ${k + 1}`} onClick={(event) => {
      const free = MOD_TARGETS.filter((t) => !rows.some((row) => row.k === k && row.t === t));
      openMenu(event, `MOD ${k + 1} TO`, free.length ? free.map((t) => ({ label: MOD_TARGET_NAMES[t], act: () => { onChange(`mod${k + 1}_${t}`, 0.5); setRows((all) => [...all, { k, t }]); } }))
        : [{ label: 'Routed to everything already.', disabled: true }]);
    }}>+ MOD {k + 1}</Key>)}
    <Key r={[r[2] - 3 - 14 - closeW - 8 - clearW, r[3] - 3 - 10 - 32, clearW, 32]} onClick={() => { [0, 1].forEach((k) => MOD_TARGETS.forEach((t) => onChange(`mod${k + 1}_${t}`, 0))); setRows([]); }}>CLEAR ALL</Key>
    <Key r={[r[2] - 3 - 14 - closeW, r[3] - 3 - 10 - 32, closeW, 32]} onClick={close}>CLOSE</Key>
  </div>;
}

// What the plug-in has that this page doesn't, in one place over the main sheet.
const PLUGIN_ONLY = [
  ['LEVEL', 'Past the middle, the input is driven into old tape: rounder, duller, a little hiss. Here LEVEL only sets how loud the input reaches Brood.'],
  ['THE TAKE STRIP', 'With nothing caught, the input scrolls past, newest at the right, over the window a tap would keep, so you can see what a tap will catch. Here the strip says what the looper is doing instead.'],
  ['AT TAP', 'Beside BARS in RECORD. Off: a bar count starts on the next bar line while the host plays, so loops land on the grid. On: it starts the moment you tap. Here there is no host, so a bar count always starts at the tap.'],
  ['TEMPO', 'Follows the host. Here you set it under the panel.'],
  ['MIX', 'Your dry sound plays on under a playing loop, at MIX\'s dry share. Here it is muted while a loop plays, and comes back when the loop stops or while you overdub.'],
  ['THE BENCH', 'Passes takes between Brood and Silt in a DAW: SEND the loop or a few bars of the output, TAKE one back as the loop.'],
];
function PluginOnly({ L, close }) {
  const r = L.overlay;
  return <div className="abs overlay sheet" style={at(r)} role="dialog" aria-label="In the plug-in">
    <div className="panel-head"><i className="sc" style={sf(16)}>+</i><span className="nm" style={nf(16)}>IN THE PLUG-IN</span><b/><span className="vl" style={{ ...vf(16), color: MUTED }}>NOT ON THIS PAGE</span></div>
    <dl className={`abs plugin-list ${L.phone ? 'narrow' : ''}`} style={at([14, 38, r[2] - 28, r[3] - 38 - 52])}>
      {PLUGIN_ONLY.map(([name, text]) => <React.Fragment key={name}>
        <dt className="nm" style={nf(19)}>{name}</dt>
        <dd className="vl" style={vf(17)}>{text}</dd>
      </React.Fragment>)}
    </dl>
    <Key r={[r[2] - 14 - 90, r[3] - 10 - 32, 90, 32]} onClick={close}>CLOSE</Key>
  </div>;
}

// The preset browser the plug-ins share: search, categories, stars. Clicking a preset plays
// it; KEEP keeps it, CANCEL puts back the sound there was.
function PresetBrowser({ L, patches, current, favourites, onChoose, onStar, onKeep, onCancel, onSave, onDelete }) {
  const [view, setView] = useState('ALL');
  const [search, setSearch] = useState('');
  const query = search.trim().toUpperCase();
  const shown = patches.filter((patch) => (view === 'ALL' || (view === 'FAV' ? favourites.includes(patch.name) : patch.category === view))
    && (!query || patch.name.toUpperCase().includes(query) || patch.category.includes(query)));
  const r = L.overlay, narrow = L.phone;
  const views = ['ALL', 'FAV', ...PRESET_CATEGORIES];
  // On a phone the search box has its own row, over the category keys.
  const keysX = narrow ? 10 : 218, keysY = narrow ? 70 : 34, listY = narrow ? 108 : 72;
  const keyW = Math.floor((r[2] - keysX - 10) / views.length);
  return <div className="abs overlay sheet" style={at(r)} role="dialog" aria-label="Presets" onKeyDown={(event) => {
    if (event.key === 'Escape') onCancel();
    if (event.key === 'Enter' && event.target.tagName !== 'BUTTON') onKeep();
  }}>
    <div className="panel-head"><i className="sc" style={sf(16)}>P</i><span className="nm" style={nf(16)}>PRESETS</span><b/><span className="vl" style={{ ...vf(16), color: MUTED }}>{shown.length} {shown.length === 1 ? 'SOUND' : 'SOUNDS'}</span></div>
    <input autoFocus={!narrow} className="abs search sc" style={{ ...at([10, 34, narrow ? r[2] - 20 : 200, 30]), ...sf(17) }} aria-label="Search presets" placeholder="SEARCH" value={search} onChange={(event) => setSearch(event.target.value)}/>
    {views.map((name, i) => <Key key={name} r={[keysX + i * keyW + 1, keysY, keyW - 2, 30]} pressed={view === name} onClick={() => setView(name)}><span className={view === name ? 'lit-text' : ''}>{name}</span></Key>)}
    <ul className={`abs preset-list ${narrow ? 'narrow' : ''}`} style={at([10, listY, r[2] - 20, r[3] - listY - 48])}>
      {shown.map((patch) => <li key={patch.name} className={patch.name === current ? 'current' : ''}>
        <button type="button" className="star" aria-pressed={favourites.includes(patch.name)} aria-label={`Favourite ${patch.name}`} onClick={() => onStar(patch.name)}>
          <svg width="16" height="16" viewBox="-8 -8 16 16" aria-hidden="true"><path d="M0 -7.5L2 -3L7.1 -2.3L3.3 1.1L4.4 6.1L0 3.5L-4.4 6.1L-3.3 1.1L-7.1 -2.3L-2 -3Z" fill={favourites.includes(patch.name) ? '#8f5f12' : 'none'} stroke={favourites.includes(patch.name) ? '#8f5f12' : MUTED} strokeWidth="1.2"/></svg>
        </button>
        <button type="button" className="name sc" style={sf(19)} onClick={() => onChoose(patch)}>{patch.name === current ? '> ' : ''}{patch.name}</button>
        <span className="sc" style={sf(17)}>{patch.category}</span><span className="sc" style={sf(17)}>{patch.factory ? 'FACTORY' : 'USER'}</span>
        {!patch.factory ? <button type="button" className="remove sc" style={sf(17)} aria-label={`Delete ${patch.name}`} onClick={() => onDelete(patch.name)}>X</button> : <span/>}
      </li>)}
    </ul>
    <Key r={[13, r[3] - 10 - 32, 94, 32]} onClick={() => onSave(PRESET_CATEGORIES.includes(view) ? view : 'USER')}>SAVE AS</Key>
    {!narrow && <Txt r={[113, r[3] - 10 - 32, r[2] - 113 - 196, 32]} align="center" cls="sc" size={sf(15)} color={MUTED}>CLICK TO HEAR   RETURN KEEPS   ESC GOES BACK</Txt>}
    <Key r={[r[2] - 10 - 180 + 3, r[3] - 10 - 32, 84, 32]} onClick={onCancel}>CANCEL</Key>
    <Key r={[r[2] - 10 - 90 + 3, r[3] - 10 - 32, 84, 32]} onClick={onKeep}>KEEP</Key>
  </div>;
}

// The whole canvas scaled to the window: nothing reflows, and a narrow window gets a small panel.
// On a phone (700 wide or less) the phone canvas fills the width and the page scrolls.
// ───────── the tour ─────────
// A picture book over the real panel: each step opens a few more controls and veils the rest,
// so the sound, the source and every setting carry on from step to step. What a step opens
// stays open in every step after it; the ring marks what this step is about. The screen keeps
// explaining whatever is pointed at. The last step is the whole panel.
const TOUR_DESK_H = 140;                       // the tour's strip under the desktop panel
const TOUR_BAR = [24, H + 4, 1152, 124];
const deskColumn = (c) => [24 + c * 276, 268, 276, 388];
// A knob with its name above and its readout below, for the tour's ring: as wide as the
// readout, and stopping short of the switch under it.
const knobCell = (L, i) => {
  const k = L.knob[i];
  if (!k) return undefined;
  const w = L.phone ? k[2] + 20 : 190;
  return [k[0] + k[2] / 2 - w / 2, k[1] - 26, w, k[3] + (L.phone ? 50 : 40)];
};
// Each step: its title, a sentence or two, one thing to try (desktop and phone wording where
// they differ), the phone tab it needs, the parts it leaves open, and its tasks in order: the
// control each one lights up, and what doing it means (now, against how things stood when the
// step began).
const TOUR = [
  { title: 'START WITH SOUND',
    body: 'Brood plays a demo loop, or your own file, or your voice with MIC.',
    try: 'Press START AUDIO.',
    holes: (L) => [L.phone ? [12, 1403, 366, 264] : [24, 744, 1152, 72], L.loopView],
    tasks: [{ target: (L) => L.src.start, done: (now) => now.running }] },
  { title: 'CATCH A LOOP', tab: 2,
    body: 'Brood is always listening: LOOP keeps the last few seconds, and MEMORY sets how many.',
    try: 'Tap LOOP (key L) on a phrase you like.',
    tryPhone: 'Tap LOOP on a phrase you like.',
    holes: (L) => [L.loopView, L.fsLoop, L.overdub, L.loopArea, ...(L.phone ? [L.tabs, L.tabSheet] : [deskColumn(2)])],
    tasks: [{ target: (L) => L.fsLoop, done: (now, was) => now.loop === 'play' && (was.loop !== 'play' || now.take !== was.take) }] },
  { title: 'START AND END', tab: 2,
    body: 'Drag START or END to trim the loop, or drag between them to slide it.',
    try: 'Drag END in until one phrase repeats.',
    holes: (L) => [L.loopView, L.fsLoop, ...(L.phone ? [L.tabs, L.tabSheet] : [grow(L.knob[2], 10)])],
    tasks: [{ target: (L) => L.loopView, done: (now, was) => !!now.take && (now.start !== was.start || now.loopEnd !== was.loopEnd) }] },
  { title: 'IN BETWEEN', tab: 1,
    body: 'MIX sets dry against Brood, BALANCE the loop against WET, ROUTING what WET hears. CLOCK is the sample rate. While a loop plays, the dry sound is muted.',
    try: 'Turn CLOCK down for an old-sampler sound.',
    holes: (L) => (L.phone ? [L.tabs, L.tabSheet, L.loopArea] : [deskColumn(1), [300, 216, 276, 54], L.loopArea]),
    tasks: [{ target: (L) => knobCell(L, 4), done: (now, was) => Math.abs(now.clock - was.clock) >= 0.05 }] },
  { title: 'ADD SOME SPACE', tab: 0, wet: true,
    body: 'WET adds space around the sound, and TIME sets how long it lasts.',
    try: 'Tap WET (key A), then turn TIME up.',
    tryPhone: 'Tap WET, then turn TIME up.',
    holes: (L) => [L.fsWet, ...(L.phone ? [L.tabs, L.tabSheet] : [0, 1].map((i) => [L.knob[i][0] - 20, L.knob[i][1] - 8, L.knob[i][2] + 40, L.knob[i][3] + 40]))],
    tasks: [{ target: (L) => L.fsWet, done: (now) => now.wetOn },
      { target: (L) => knobCell(L, 0), done: (now, was) => Math.abs(now.time - was.time) >= 0.05 }] },
  { title: 'FOUR KINDS OF SPACE', tab: 0, wet: true,
    body: 'REVERB, DELAY, SLIP and WEAVE each treat time differently. Hold WET to freeze it.',
    try: 'Pick DELAY, then hold WET.',
    holes: (L) => [L.fsWet, ...(L.phone ? [L.tabs, L.tabSheet] : [deskColumn(0)])],
    tasks: [{ target: (L) => L.wetMode, done: (now) => now.wetMode === 1 },
      { target: (L) => L.fsWet, done: (now, was) => now.freeze && !was.freeze }] },
  { title: 'MOVE A KNOB', tab: 0,
    body: 'MOD 1 and MOD 2 can turn knobs for you. Drag up or down around the edge of a knob to set how far one swings it. BALANCE works the same way.',
    try: 'Drag up around the edge of TIME.',
    holes: (L) => [L.loopArea, ...(L.phone ? [L.tabs, L.tabSheet] : [knobCell(L, 0), knobCell(L, 3)])],
    tasks: [{ target: (L) => knobCell(L, 0), done: (now, was) => now.timeMod > was.timeMod + 0.04 }] },
  { title: 'SHAPE THE MOVEMENT', tab: 0,
    body: 'Tap a MOD chip to open its controls: RATE sets how fast it moves, SHAPE how it moves.',
    try: 'Open MOD 1, then change its RATE and its SHAPE.',
    holes: (L) => [L.rampArea, ...(L.phone ? [L.tabs, L.tabSheet] : [knobCell(L, 0)])],
    tasks: [{ target: (L) => L.chip0, done: (now) => now.modOpen },
      { target: (L) => L.rampRate, done: (now, was) => Math.abs(now.rate - was.rate) >= 0.03 },
      { target: (L) => L.rampWave, done: (now, was) => now.wave !== was.wave }] },
  { title: 'ALL OF BROOD', all: true,
    body: 'That is the whole panel. Point at anything and the screen says what it does. IN THE PLUG-IN lists what only the plug-in can do.',
    bodyPhone: 'That is the whole panel. Tap anything and this bar says what it does. IN THE PLUG-IN lists what only the plug-in can do.',
    try: 'SAVE keeps a sound on this device. TOUR, under the panel, brings the tour back.' },
];
// The open parts of a step, a little larger than the controls; null for the whole panel.
function tourHoles(step, L, overlayOpen) {
  const s = TOUR[step];
  if (!s || s.all) return null;
  // Everything the steps so far have opened, this one's included.
  const out = [L.lcd, L.src.start, ...TOUR.slice(0, step + 1).flatMap((t) => (t.holes ? t.holes(L) : []))];
  if (overlayOpen) out.push(L.overlay);
  return out.filter(Boolean).map((r) => grow(r, 6));
}
// The tour's strip under the desktop panel: where we are, what this part does, one thing to
// try, and the way on.
function TourBar({ step, go, end, quiet, done }) {
  const s = TOUR[step], last = TOUR.length - 1;
  const box = { count: [14, 10, 190, 16], dots: (i) => [860 + i * 20, 10, 17, 24], all: [1020, 10, 34, 24], title: [14, 30, 200, 56], text: [224, 8, 620, 110],
    back: [860, 78, 138, 36], end: [1058, 10, 84, 24], next: [1004, 78, 138, 36] };
  const keys = (event) => {
    if (/INPUT|TEXTAREA/.test(event.target.tagName)) return;
    if (event.key === 'ArrowRight' && step < last) { event.preventDefault(); go(step + 1); }
    if (event.key === 'ArrowLeft' && step > 0) { event.preventDefault(); go(step - 1); }
  };
  return <section className="abs tourbar sheet" style={at(TOUR_BAR)} aria-label="Tour" onKeyDown={keys}>
    <Txt r={box.count} cls="vl" size={vf(16)} color={MUTED}>{s.all ? 'TOUR  THE END' : `TOUR  ${step + 1} / ${last}`}</Txt>
    {TOUR.slice(0, last).map((t, i) => <button key={t.title} type="button" className={`abs tour-dot vl ${i === step ? 'on' : ''}`} style={{ ...at(box.dots(i)), ...vf(16) }}
      aria-label={`Step ${i + 1}: ${t.title.toLowerCase()}`} aria-current={i === step ? 'step' : undefined} onClick={() => go(i)}>{i + 1}</button>)}
    <Key r={box.all} pressed={step === last} label="All of Brood: the whole panel" onClick={() => go(last)}>ALL</Key>
    <div key={step} className="abs tour-text" style={at([box.title[0], box.title[1], box.title[2], box.text[1] + box.text[3] - box.title[1]])} aria-live="polite">
      <h2 className="abs nm" style={{ ...at([0, 0, box.title[2], box.title[3]]), ...nf(22) }}>{s.title}</h2>
      <div className="abs tour-body vl" style={{ ...at([box.text[0] - box.title[0], box.text[1] - box.title[1], box.text[2], box.text[3]]), ...vf(17) }}>
        {s.body.split('\n\n').map((para) => <p key={para}>{para}</p>)}
        <p className={done ? 'tour-try done' : 'tour-try'}><b className="nm" style={nf(16)}>{done ? 'DONE' : 'TRY'}</b> {s.try}</p>
        {s.wet && quiet && <p className="tour-quiet">the wet side is quiet with these settings: {quiet.why}. <button type="button" className="tour-fix nm" style={nf(15)} onClick={quiet.fix}>SET IT UP</button></p>}
      </div>
    </div>
    <Key r={box.back} disabled={step === 0} onClick={() => go(step - 1)}>BACK</Key>
    <Key r={box.next} cls={done && step < last ? 'tour-ready' : ''} disabled={step === last} onClick={() => go(step + 1)}>NEXT</Key>
    <Key r={box.end} onClick={end}>END TOUR</Key>
  </section>;
}
// A phone's tour bar: as little of the screen as it can take. The title row opens and closes
// the step's sentence; the TRY line is always there; then back, a dot per step and next. It is
// laid out as ordinary flowing text, so its height is what its words need on this phone.
function PhoneTourBar({ step, go, end, quiet, done, help, clearHelp, open, setOpen }) {
  const s = TOUR[step], last = TOUR.length - 1;
  const body = s.bodyPhone || s.body;
  return <section className="tourbar tourbar-phone sheet" aria-label="Tour" onPointerDown={clearHelp}>
    <div className="tp-head">
      <button type="button" className="tp-title nm" style={nf(20)} aria-expanded={open} onClick={() => setOpen(!open)}>
        <span>{help ? help.label : s.title}</span>
        <i className="vl" style={vf(15)}>{s.all ? 'THE END' : `${step + 1} / ${last}`}</i>
        <svg className="tp-chev" width="12" height="12" viewBox="-6 -6 12 12" aria-hidden="true"><path d="M-4 -2L0 2L4 -2" fill="none" stroke="currentColor" strokeWidth="1.6"/></svg>
      </button>
      <button type="button" className="tp-key nm" style={nf(15)} onClick={end}>END TOUR</button>
    </div>
    <div key={help ? `help-${help.label}` : step} className="tp-text vl tour-text" style={vf(17)} aria-live="polite">
      {help ? <p>{help.text}</p> : <>
        {open && body.split('\n\n').map((para) => <p key={para}>{para}</p>)}
        <p className={done ? 'tour-try done' : 'tour-try'}><b className="nm" style={nf(15)}>{done ? 'DONE' : 'TRY'}</b> {s.tryPhone || s.try}</p>
        {s.wet && quiet && <p className="tour-quiet">the wet side is quiet with these settings: {quiet.why}. <button type="button" className="tour-fix nm" style={nf(15)} onClick={quiet.fix}>SET IT UP</button></p>}
      </>}
    </div>
    <div className="tp-nav">
      <button type="button" className="tp-key nm" style={nf(16)} aria-label="BACK" disabled={step === 0} onClick={() => go(step - 1)}>{'\u2039'} BACK</button>
      <div className="tp-dots">
        {TOUR.map((t, i) => <button key={t.title} type="button" className={`tp-dot ${i === step ? 'on' : ''} ${i === last ? 'all' : ''}`}
          aria-label={i === last ? 'All of Brood: the whole panel' : `Step ${i + 1}: ${t.title.toLowerCase()}`} aria-current={i === step ? 'step' : undefined} onClick={() => go(i)}><i/></button>)}
      </div>
      <button type="button" className={`tp-key nm ${done && step < last ? 'tour-ready' : ''}`} style={nf(16)} aria-label="NEXT" disabled={step === last} onClick={() => go(step + 1)}>NEXT {'\u203a'}</button>
    </div>
  </section>;
}

function useScale(tall) {
  const measure = () => {
    const phone = window.innerWidth <= 700;
    return { phone, scale: phone ? window.innerWidth / PW : Math.max(0.1, Math.min(window.innerWidth / W, window.innerHeight / (H + (tall ? TOUR_DESK_H : 0)))) };
  };
  const [fit, setFit] = useState(measure);
  useEffect(() => {
    const resize = () => setFit(measure());
    resize();
    window.addEventListener('resize', resize);
    return () => window.removeEventListener('resize', resize);
  }, [tall]);
  useEffect(() => { document.documentElement.classList.toggle('phone', fit.phone); }, [fit.phone]);
  useEffect(() => { document.documentElement.classList.toggle('touring', fit.phone && tall); }, [fit.phone, tall]);
  return fit;
}

// The input level, as the plug-in's LEVEL slider has it: a gain from silence to +24 dB, with
// unity in the middle of the travel (JUCE's skew from the mid point).
const LEVEL_MAX = 15.85, LEVEL_SKEW = Math.log(0.5) / Math.log(1 / LEVEL_MAX);
const levelPos = (g) => Math.pow(clamp(g / LEVEL_MAX), LEVEL_SKEW);
const levelGain = (pos) => LEVEL_MAX * Math.pow(clamp(pos), 1 / LEVEL_SKEW);
const levelDb = (g) => (g <= 1e-5 ? -100 : 20 * Math.log10(g));

function App() {
  const [params, setParams] = useState(() => {
    const now = safeRead('brood-params-v4');
    if (now) return sanitize(now);
    const v3 = safeRead('brood-params-v3');
    if (v3) return sanitize(migrateShapes(v3));
    const v2 = safeRead('brood-params-v2');
    return sanitize(migrateShapes(migrateRouting(v2 ?? migrateStart(safeRead('brood-params-v1')))));
  });
  // User presets, named and filed. Anything saved in the old slots becomes a preset.
  const [userPatches, setUserPatches] = useState(() => {
    const saved = safeRead('brood-user-presets-v4') ?? safeRead('brood-user-presets-v3') ?? safeRead('brood-user-presets-v2');
    const upgrade = (p) => (p.v >= 4 ? p.params : migrateShapes(p.v >= 3 ? p.params : migrateRouting(p.v >= 2 ? p.params : migrateStart(p.params))));
    if (Array.isArray(saved)) return saved.filter((p) => p && p.name).map((p) => ({ name: String(p.name), category: PRESET_CATEGORIES.includes(p.category) ? p.category : 'USER', v: 4, params: sanitize(upgrade(p)) }));
    const slots = safeRead('brood-user-patches-v1');
    const legacy = safeRead('brood-user-patch-v1');
    const old = Array.isArray(slots) ? slots : legacy ? [legacy] : [];
    return old.map((p, i) => (p ? { name: `USER ${i + 1}`, category: 'USER', v: 4, params: sanitize(migrateShapes(migrateRouting(migrateStart(p)))) } : null)).filter(Boolean);
  });
  const [favourites, setFavourites] = useState(() => { const f = safeRead('brood-favourites-v1'); return Array.isArray(f) ? f : []; });
  const [browsing, setBrowsing] = useState(null);   // the sound to go back to while the browser is open
  const [routesOpen, setRoutesOpen] = useState(false);
  const [pluginOpen, setPluginOpen] = useState(false);
  const [preset, setPreset] = useState(0);
  const [source, setSource] = useState('demo');
  const [demoId, setDemoId] = useState(() => { const d = safeRead('brood-demo-v1'); return typeof d === 'string' && d ? d : DEMOS[0].id; });
  // Songs a host page offers in songs.json beside the app (jackherz.com does): after the loops
  // in the DEMO stepper, streamed rather than loaded whole.
  const [songs, setSongs] = useState([]);
  const [fileName, setFileName] = useState('');
  const [inputGain, setInputGain] = useState(1);
  const [running, setRunning] = useState(false);
  const [busy, setBusy] = useState(false);
  const [state, setState] = useState(null);
  const [wave, setWave] = useState(null);
  const [notice, setNotice] = useState('');
  const [modSource, setModSource] = useState(0);
  const [modOpen, setModOpen] = useState(false);
  const [lcd, setLcd] = useState({ label: 'PATCH', value: PRESETS[0].name.toLowerCase(), follows: null });
  const [hover, setHover] = useState(null);
  // On a phone in the tour, a tap's help borrows the tour bar for a few seconds.
  const [barHelp, setBarHelp] = useState(null);
  // Whether the phone tour bar's sentence is open. Short screens (most phones inside the
  // site's frame) start with it closed.
  const [tourOpen, setTourOpen] = useState(() => window.innerHeight >= 640);
  const holder = useRef(null);
  const scroller = useRef(null);   // a phone's panel, scrolling on its own above the tour bar
  useEffect(() => { if (!barHelp) return undefined; const id = setTimeout(() => setBarHelp(null), 5000); return () => clearTimeout(id); }, [barHelp]);
  const [hoverKnob, setHoverKnob] = useState(-1);
  const [activeKnob, setActiveKnob] = useState(-1);   // the knob a finger is turning
  const [tab, setTab] = useState(0);                  // a phone's column: WET, SHARED, LOOP
  const [menu, setMenu] = useState(null);
  const [chipDrag, setChipDrag] = useState(null);   // { k, x, y, target } in design units
  const [blink, setBlink] = useState(true);
  // The tour: the step showing, or -1. A first visit opens it; after that it stays as it was left.
  const [tour, setTour] = useState(() => {
    const t = safeRead('brood-tour-v1');
    if (!t || typeof t.step !== 'number') return 0;
    return t.open ? clamp(Math.round(t.step), 0, TOUR.length - 1) : -1;
  });
  const { scale, phone } = useScale(tour >= 0);
  // A desktop's DEMO stepper is narrow: a song may give a short name for it (songs.json "short"),
  // and its full title goes beside the song bar. A phone's stepper has room for the full title.
  const sounds = [...DEMOS, ...songs.map((x) => ({ id: x.id, name: phone ? x.title : x.short || x.title, title: x.title, song: true }))];
  // The focus outline is for the keyboard. A finger or a mouse focuses a knob too, and phones
  // then leave the outline standing, so it shows only after a key, until the next touch or click.
  useEffect(() => {
    const root = document.documentElement;
    const pointer = () => root.classList.add('pointer-input');
    const keys = (e) => { if (!e.metaKey && !e.ctrlKey && !e.altKey) root.classList.remove('pointer-input'); };
    window.addEventListener('pointerdown', pointer, true); window.addEventListener('keydown', keys, true);
    return () => { window.removeEventListener('pointerdown', pointer, true); window.removeEventListener('keydown', keys, true); };
  }, []);
  const stage = useRef(null);
  const traces = useRef([new Array(64).fill(0), new Array(64).fill(0)]);
  const loopFlash = useRef({ cycles: -1, until: 0 });
  const lastLoop = useRef('off');
  const fileInput = useRef(null);
  const engineRef = useRef(null);
  const paramsRef = useRef(params); paramsRef.current = params;
  const runningRef = useRef(running); runningRef.current = running;
  if (!engineRef.current) {
    engineRef.current = new AudioEngine((message) => {
      if (message.type === 'state') {
        // The chips' traces: the last two seconds of each source, at the rate state arrives.
        for (let k = 0; k < 2; k++) { traces.current[k].shift(); traces.current[k].push(message.st.mods?.[k] ?? 0); }
        setState(message.st);
      }
      if (message.type === 'wave') setWave(message);
      // A new take plays all of itself: the knobs follow what the engine has already done.
      if (message.type === 'fit') setParams((previous) => ({ ...previous, start: 1, loopEnd: 1, ...(message.length ? { length: 1 } : {}) }));
    }, (message) => setNotice(message));
    engineRef.current.demoId = demoId;
  }
  useEffect(() => { engineRef.current.setParams(params); safeWrite('brood-params-v4', params); }, [params]);
  useEffect(() => { const engine = engineRef.current; return () => { engine.dispose(); }; }, []);
  useEffect(() => {
    let gone = false;
    fetch(`${import.meta.env.BASE_URL}songs.json`).then((r) => (r.ok ? r.json() : [])).catch(() => []).then((list) => {
      if (gone) return;
      const clean = (Array.isArray(list) ? list : []).filter((x) => x && typeof x.id === 'string' && typeof x.src === 'string' && typeof x.title === 'string');
      engineRef.current.setSongs(clean);
      setSongs(clean);
      // A remembered song that isn't offered here any more falls back to the first loop.
      setDemoId((id) => {
        if (DEMOS.some((d) => d.id === id) || clean.some((x) => x.id === id)) return id;
        engineRef.current.demoId = DEMOS[0].id;
        return DEMOS[0].id;
      });
    });
    return () => { gone = true; };
  }, []);
  // A host page can hand over a song already playing: ?song=<id>&t=<seconds>&play=1. Brood
  // carries it on from there if the browser lets sound start by itself; otherwise it waits at
  // that moment for START AUDIO.
  const handedOver = useRef(false);
  useEffect(() => {
    if (handedOver.current || !songs.length) return;
    handedOver.current = true;
    const q = new URLSearchParams(window.location.search);
    const song = songs.find((x) => x.id === q.get('song'));
    if (!song) return;
    const engine = engineRef.current;
    engine.songCue = Math.max(0, Number(q.get('t')) || 0);
    engine.demoId = song.id; engine.sourceKind = 'demo';
    setDemoId(song.id); setSource('demo');
    const ready = () => showReadout('SONG', `${song.title.toLowerCase()}, ready where it was. press START AUDIO`);
    if (q.get('play') !== '1') { ready(); return; }
    (async () => {
      try {
        if (!engine.context) await engine.create(paramsRef.current);
        engine.setParams(paramsRef.current);
        await Promise.race([engine.context.resume(), new Promise((resolve) => setTimeout(resolve, 700))]);
        if (!engine.running) { ready(); return; }
        engine.setInputLevel(inputGain);
        await engine.connectSource('demo');
        setRunning(true);
        showReadout('SONG', song.title.toLowerCase());
      } catch (_) {
        try { await engine.pause(); } catch (__) { /* already stopped */ }
        setRunning(false); ready();
      }
    })();
  }, [songs]);
  useEffect(() => { const id = setInterval(() => setBlink(Math.floor(performance.now() / 160) % 2 === 0), 40); return () => clearInterval(id); }, []);
  const update = useCallback((id, value) => setParams((previous) => ({ ...previous, [id]: value })), []);
  const showReadout = useCallback((label, value) => setLcd({ label, value, follows: null }), []);
  // The readout's own names for the parameters, so it never shows a raw id.
  const touch = useCallback((id, follow = true) => {
    const names = { loopEnd: 'END', start: 'START', balance: 'BALANCE', mod1Rate: 'MOD 1 RATE', mod2Rate: 'MOD 2 RATE', wetDiv: 'WET DIV', loopDiv: 'LOOP DIV', wearNoise: 'NOISE', loopDb: 'GAIN' };
    const i = KNOB_IDS.indexOf(id);
    if (follow) setLcd({ label: i >= 0 ? null : names[id] || id.toUpperCase(), value: '', follows: id, knob: i });
  }, []);
  useEffect(() => { if (notice) showReadout('AUDIO', notice.replace(/\.$/, '').toLowerCase()); }, [notice, showReadout]);
  const gesture = useCallback((which, action) => {
    if (!runningRef.current) { if (action === 'down') setNotice('Press START AUDIO first, then tap a switch to play.'); return; }
    engineRef.current.footswitch(which, action);
  }, []);
  const overlayOpen = !!browsing || routesOpen || pluginOpen;
  useEffect(() => {
    const held = {};
    const keyMap = { a: 'wet', l: 'loop' };
    const down = (e) => {
      const which = keyMap[e.key.toLowerCase()];
      if (!which || e.repeat || held[which] || overlayOpen || /INPUT|TEXTAREA|SELECT/.test(e.target.tagName)) return;
      held[which] = true; gesture(which, 'down');
    };
    const up = (e) => { const which = keyMap[e.key.toLowerCase()]; if (which && held[which]) { held[which] = false; gesture(which, 'up'); } };
    const blur = () => { for (const which of Object.keys(held)) if (held[which]) { held[which] = false; gesture(which, 'up'); } };
    window.addEventListener('keydown', down); window.addEventListener('keyup', up); window.addEventListener('blur', blur);
    return () => { window.removeEventListener('keydown', down); window.removeEventListener('keyup', up); window.removeEventListener('blur', blur); };
  }, [gesture, overlayOpen]);
  // The loop switch's state in words, when it changes.
  useEffect(() => {
    const now = running ? state?.loop || 'off' : 'off';
    if (now !== lastLoop.current) {
      const msg = { play: 'playing', rec: 'recording', dub: 'overdubbing' }[now];
      if (msg) showReadout('LOOP', msg);
      else if (lastLoop.current === 'play') showReadout('LOOP', 'stopped');
      lastLoop.current = now;
    }
  }, [state?.loop, running, showReadout]);

  const patches = [...PRESETS.map((p) => ({ ...p, factory: true })), ...userPatches.map((p) => ({ ...p, subtitle: 'saved on this device' }))];
  const currentPatch = patches[Math.min(preset, patches.length - 1)];
  const loadPatch = (index) => {
    const n = (index + patches.length) % patches.length;
    setPreset(n); setParams(sanitize(patches[n].params)); showReadout('PATCH', patches[n].name.toLowerCase());
  };
  const openBrowser = () => { setRoutesOpen(false); setPluginOpen(false); setBrowsing({ params, preset }); };
  // Every preset kept here is in today's form (v 2: START counts from the oldest audio in every mode).
  const storeUser = (next) => { const marked = next.map((p) => ({ ...p, v: 4 })); setUserPatches(marked); safeWrite('brood-user-presets-v4', marked); };
  const savePatch = (category) => {
    const name = (window.prompt('A name for this sound:', currentPatch.factory ? '' : currentPatch.name) || '').trim().toUpperCase();
    if (!name) return;
    if (PRESETS.some((p) => p.name === name)) { showReadout('PATCH', 'that name belongs to a factory preset'); return; }
    const next = [...userPatches.filter((p) => p.name !== name), { name, category, params: sanitize(params) }]
      .sort((a, b) => a.name.localeCompare(b.name));
    storeUser(next);
    setPreset(PRESETS.length + next.findIndex((p) => p.name === name));
    setBrowsing((b) => (b ? { params, preset: PRESETS.length + next.findIndex((p) => p.name === name) } : b));
    showReadout('PATCH', `saved ${name.toLowerCase()} on this device`);
  };
  const saveCurrent = () => {
    if (currentPatch.factory) { savePatch('USER'); return; }
    storeUser(userPatches.map((p) => (p.name === currentPatch.name ? { ...p, params: sanitize(params) } : p)));
    showReadout('PATCH', `saved ${currentPatch.name.toLowerCase()} on this device`);
  };
  const deletePatch = (name) => {
    if (!window.confirm(`Delete ${name}?`)) return;
    const at = PRESETS.length + userPatches.findIndex((p) => p.name === name);
    storeUser(userPatches.filter((p) => p.name !== name));
    if (preset === at) setPreset(0);
    else if (preset > at) setPreset(preset - 1);
  };
  const starPatch = (name) => {
    const next = favourites.includes(name) ? favourites.filter((n) => n !== name) : [...favourites, name];
    setFavourites(next); safeWrite('brood-favourites-v1', next);
  };
  // RESET: every setting back to how Brood first opens (TEMPO stays, since it belongs to the
  // sound playing), both MODs' routes cleared, the wet side off, the take gone, START and END
  // back at the ends. The sound source carries on.
  const fullReset = () => {
    engineRef.current.resetAll();
    lastLoop.current = 'off';   // the screen says RESET, not that the loop stopped
    setParams((p) => ({ ...sanitize(BASE), bpm: p.bpm }));
    setPreset(0); setModSource(0); setModOpen(false); setBrowsing(null); setRoutesOpen(false);
    setInputGain(1); engineRef.current.setInputLevel(1);
    setHover(null);
    showReadout('RESET', 'a little daydream, loop cleared');
  };
  const startAudio = async () => {
    if (busy) return;
    setBusy(true); setNotice('');
    try {
      if (engineRef.current.running) { await engineRef.current.pause(); setRunning(false); showReadout('AUDIO', 'stopped'); }
      else { engineRef.current.setInputLevel(inputGain); await engineRef.current.start(paramsRef.current); setRunning(true); showReadout('AUDIO', 'running'); }
    } catch (error) {
      setRunning(!!engineRef.current.running);
      setNotice(error?.name === 'NotAllowedError' ? 'Microphone access was denied. Choose DEMO or allow access in your browser.' : (error.message || 'Could not start audio.'));
    } finally { setBusy(false); }
  };
  const chooseSource = async (kind) => {
    if (kind === 'file' && !fileName) { fileInput.current?.click(); return; }
    setSource(kind); setNotice('');
    showReadout('SOURCE', kind === 'demo' ? `the demo: ${(sounds.find((d) => d.id === demoId)?.name || '').toLowerCase()}` : kind === 'file' ? fileName : 'the microphone. use headphones');
    try { await engineRef.current.connectSource(kind); }
    catch (error) { setNotice(error?.name === 'NotAllowedError' ? 'Microphone access was denied. Choose DEMO or allow access in your browser.' : (error.message || 'Could not connect input.')); }
  };
  // A demo loop: it becomes the source, and TEMPO follows it so SYNC and RECORD's bars fit.
  const chooseDemo = async (id) => {
    const demo = DEMOS.find((d) => d.id === id), song = songs.find((x) => x.id === id);
    if (!demo && !song) return;
    setDemoId(id); safeWrite('brood-demo-v1', id);
    setSource('demo'); setNotice('');
    if (demo) { update('bpm', demo.bpm); showReadout('DEMO', `${demo.name.toLowerCase()}: ${demo.detail}`); }
    else showReadout('SONG', `${song.title.toLowerCase()}${song.album && song.album !== song.title ? `, ${song.album.toLowerCase()}` : ''}`);
    try {
      if (source === 'demo') await engineRef.current.chooseDemo(id);
      else { engineRef.current.demoId = id; await engineRef.current.connectSource('demo'); }
    } catch (error) { setNotice(error.message || 'Could not load that demo.'); }
  };
  const readFile = async (event) => {
    const file = event.target.files?.[0];
    event.target.value = '';
    if (!file) return;
    showReadout(file.name, 'loading');
    try { await engineRef.current.chooseFile(file); setSource('file'); setFileName(file.name); showReadout(file.name, 'loaded, and looping into Brood'); }
    catch (error) { setNotice(error.message || 'Could not load that file.'); }
  };
  const setLevel = (pos) => {
    const g = levelGain(pos);
    setInputGain(g); engineRef.current.setInputLevel(g);
    const db = levelDb(g);
    showReadout('LEVEL', `${db.toFixed(1)} dB`);
  };
  const openMenu = useCallback((event, title, items) => {
    const box = stage.current.getBoundingClientRect(), w = stage.current.offsetWidth, k = w / box.width;
    setMenu({ x: (event.clientX - box.left) * k, y: (event.clientY - box.top) * k, w, h: stage.current.offsetHeight, title, items });
  }, []);
  const selectSource = useCallback((k) => setModSource(k), []);

  // Where each modulated control is now: the knob, plus each source's swing (the worklet's
  // applyRamps, without its smoothing).
  const effective = (t) => {
    const id = MOD_TARGETS[t];
    let v = params[id];
    for (let k = 0; k < 2; k++) {
      if (!params[`mod${k + 1}On`] || !running) continue;
      const raw = state?.mods?.[k] ?? 0;
      v += (params[`mod${k + 1}Uni`] ? raw : raw * 2 - 1) * params[`mod${k + 1}_${id}`];
    }
    return clamp(v);
  };
  // What the screen and the knobs describe: the settings, and whether a playing loop mutes the dry sound.
  const said = running && state?.loop === 'play' ? { ...params, dryMuted: true } : params;
  const routeMask = [0, 1].map((k) => MOD_TARGETS.map((t) => Math.abs(params[`mod${k + 1}_${t}`]) >= 0.005));
  const balanceRouted = routeMask[0][6] || routeMask[1][6];

  const L = phone ? phoneLayout(params, tab) : { ...deskLayout(params), H: H + (tour >= 0 ? TOUR_DESK_H : 0) };
  const holes = tour >= 0 ? tourHoles(tour, L, overlayOpen) : null;
  const open = (x, y) => !holes || holes.some((h) => inside(h, x, y));
  useEffect(() => { safeWrite('brood-tour-v1', { open: tour >= 0, step: Math.max(0, tour) }); }, [tour]);
  // A step's parts are the controls whose middles sit in its open areas. Every other control
  // fades as a whole (no cut-out shapes, so nothing is ever half covered) and is out of reach
  // for the mouse, a finger and the Tab key alike. The bare paper behind the panel stays.
  useLayoutEffect(() => {
    for (const el of stage.current?.children || []) {
      if (!(el instanceof HTMLElement)) continue;
      const keep = el.matches('.tourbar, .tour-ring, .menu, input[type=file], .sheet:empty') || !holes;
      const off = !keep && !open(el.offsetLeft + el.offsetWidth / 2, el.offsetTop + el.offsetHeight / 2);
      el.inert = off;
      el.classList.toggle('tour-off', off);
    }
  });
  // What would keep the wet side silent in the wet steps, and the least that puts it right.
  const loopPlaying = running && (state?.loop === 'play' || state?.loop === 'dub');
  const quietWhy = [
    params.mix < 0.05 && 'MIX is all dry',
    params.balance < 0.05 && 'BALANCE is the loop alone',
    params.routing === 2 && !loopPlaying && 'ROUTING is LOOP, and no loop is playing',
  ].filter(Boolean);
  const quiet = quietWhy.length ? {
    why: quietWhy.join('; '),
    fix: () => {
      setParams((p) => ({ ...p, ...(p.mix < 0.05 ? { mix: 0.5 } : {}), ...(p.balance < 0.05 ? { balance: 0.5 } : {}), ...(p.routing === 2 && !loopPlaying ? { routing: 1 } : {}) }));
      showReadout('TOUR', 'set up to hear the wet side');
    },
  } : null;
  // A step's lit control, centred in the room left between the top of the screen and the
  // phone's tour bar, or its top shown first if it's taller than that room.
  const showTarget = (step, onTab, task = 0) => {
    const target = TOUR[step]?.tasks?.[task]?.target(phone ? phoneLayout(params, onTab) : L);
    if (!phone || !target || !stage.current) return;
    const reduce = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches;
    // Inside the site's frame, first make sure the frame's bottom (where the bar is) is on screen.
    try {
      const frame = window.frameElement, host = window.parent;
      if (frame && host && host !== window) {
        const box = frame.getBoundingClientRect(), over = box.bottom - host.innerHeight;
        if (over > 0) host.scrollBy({ top: Math.min(over, Math.max(0, box.top)), behavior: reduce ? 'auto' : 'smooth' });
      }
    } catch (_) { /* another site's frame: leave it */ }
    // During the tour a phone's panel scrolls in its own area, above the bar.
    const box = scroller.current;
    if (!box) return;
    const room = box.clientHeight;
    const top = stage.current.getBoundingClientRect().top - box.getBoundingClientRect().top + box.scrollTop + target[1] * scale, tall = target[3] * scale;
    const y = tall > room - 16 ? top - 8 : top + tall / 2 - room / 2;
    box.scrollTo({ top: Math.max(0, y), behavior: reduce ? 'auto' : 'smooth' });
  };
  // A desktop inside a host page's frame (jackherz.com): bring the whole panel and the tour's
  // strip into view, in the middle of the window if they fit. If they're taller than the
  // window, their bottom goes to the window's bottom, where START AUDIO and the strip are.
  // On its own page Brood already fits the window, and there is nothing to scroll.
  const showPanel = () => {
    try {
      const frame = window.frameElement, host = window.parent;
      if (!frame || !host || host === window) return;
      const box = frame.getBoundingClientRect(), room = host.innerHeight;
      const by = box.height <= room ? box.top - (room - box.height) / 2 : box.bottom - room;
      const reduce = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches;
      if (Math.abs(by) > 2) host.scrollBy({ top: by, behavior: reduce ? 'auto' : 'smooth' });
    } catch (_) { /* another site's frame: leave it */ }
  };
  // Ending the tour on a phone hands the panel back to the page, at the same place.
  const endTour = () => {
    const y = phone ? scroller.current?.scrollTop || 0 : 0;
    setTour(-1);
    if (phone) requestAnimationFrame(() => window.scrollTo(0, y));
  };
  const goTour = (i) => {
    const next = clamp(i, 0, TOUR.length - 1);
    setTour(next); setHover(null); setBarHelp(null);
    // Opening the tour on a desktop: once the strip has drawn and the frame has grown to hold it.
    if (!phone) { if (tour < 0) setTimeout(showPanel, 160); return; }
    const onTab = TOUR[next].tab ?? tab;
    if (TOUR[next].tab != null) { setTab(TOUR[next].tab); setActiveKnob(-1); }
    // After the new step (and its tab) has drawn and the bar has its new height.
    setTimeout(() => showTarget(next, onTab), 80);
  };
  // Opening or closing the sentence changes the room: keep the lit control in view.
  useEffect(() => { if (phone && tour >= 0) { const id = setTimeout(() => showTarget(tour, tab, tourTask), 80); return () => clearTimeout(id); } return undefined; }, [tourOpen]);

  // The tour watches what Brood does, not taps: each step's tasks are read from Brood itself,
  // as changes from how things stood when the step began. The ring sits on the first task
  // still to do; once they're all done, TRY says DONE and NEXT lights up. It never moves on
  // by itself.
  const routeCount = routeMask[0].filter(Boolean).length + routeMask[1].filter(Boolean).length;
  const tourNow = {
    running, loop: running ? state?.loop || 'off' : 'off', take: running ? state?.lsrc || 0 : 0, time: params.time,
    start: params.start, loopEnd: params.loopEnd, clock: params.clock,
    wetOn: running && !!state?.wetOn, freeze: running && !!state?.freeze, wetMode: params.wetMode, routes: routeCount,
    timeMod: Math.abs(params.mod1_time) + Math.abs(params.mod2_time), modOpen,
    rate: params[`mod${modSource + 1}Rate`], wave: params[`mod${modSource + 1}Wave`],
  };
  const tourWas = useRef(null);
  const [tourTask, setTourTask] = useState(0);   // the first task in this step still to do
  useEffect(() => { tourWas.current = tour >= 0 ? { ...tourNow } : null; setTourTask(0); }, [tour]);
  const tasks = TOUR[tour]?.tasks || [];
  useEffect(() => {
    // A task, once done, stays done; take the ring on to the next one still to do.
    let i = tourTask;
    while (i < tasks.length && tourWas.current && tasks[i].done(tourNow, tourWas.current)) i += 1;
    if (i !== tourTask) setTourTask(i);
  });
  const tourDone = tasks.length > 0 && tourTask >= tasks.length;
  // The ring moving on to the step's next task brings that control into view too.
  useEffect(() => { if (phone && tour >= 0 && tourTask > 0 && tourTask < tasks.length) { const id = setTimeout(() => showTarget(tour, tab, tourTask), 120); return () => clearTimeout(id); } return undefined; }, [tourTask]);
  // A phone that opens on step 1 goes straight down to START AUDIO, as pressing 1 would.
  const openedAt = useRef(false);
  useEffect(() => {
    if (openedAt.current) return;
    openedAt.current = true;
    if (phone && tour === 0) goTour(0);
    // A desktop that opens in the tour (a first visit) does the same, once the host has sized the frame.
    else if (!phone && tour >= 0) setTimeout(showPanel, 600);
  }, []);
  // On a phone the key that opens a sheet can be far down the page: bring the sheet into view.
  useEffect(() => {
    if (!phone || !(browsing || routesOpen || pluginOpen)) return;
    const reduce = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches;
    document.querySelector('.overlay')?.scrollIntoView({ block: 'start', behavior: reduce ? 'auto' : 'smooth' });
  }, [phone, !!browsing, routesOpen, pluginOpen]);
  const toDesign = (event) => {
    const box = stage.current.getBoundingClientRect(), k = L.W / box.width;
    return { x: (event.clientX - box.left) * k, y: (event.clientY - box.top) * k };
  };
  // Hover help: the mouse mapped back into design units and matched against the table, so a
  // click is never intercepted. While a button is held, help stands down for the gesture.
  // A phone has no hover: a tap shows the help, and it stays until the next touch.
  const table = helpTable(params, modOpen, L);
  const helpAt = ({ x, y }) => {
    // An open browser or route list covers the main sheet: nothing under it explains itself.
    if (overlayOpen && inside(L.overlay, x, y)) return null;
    if (!open(x, y)) return null;
    return table.find((h) => inside(h.r, x, y)) || null;
  };
  const showHelp = (item) => { if (item?.label !== hover?.label || item?.text !== hover?.text) setHover(item ? { label: item.label, text: item.text } : null); };
  const onStageMove = (event) => {
    if (event.buttons || event.pointerType === 'touch' || phone) return;
    const at = toDesign(event);
    showHelp(helpAt(at));
    const knob = (overlayOpen && inside(L.overlay, at.x, at.y)) || !open(at.x, at.y) ? -1 : KNOB_IDS.findIndex((_, i) => L.knob[i] && inside(L.knob[i], at.x, at.y));
    if (knob !== hoverKnob) setHoverKnob(knob);
  };
  const tapStart = useRef(null);
  const onStageDown = (event) => {
    if (hover) setHover(null);
    if (barHelp) setBarHelp(null);
    tapStart.current = { x: event.clientX, y: event.clientY };
  };
  const onStageUp = (event) => {
    const t = tapStart.current;
    tapStart.current = null;
    if (!(phone || event.pointerType === 'touch') || !t || Math.hypot(event.clientX - t.x, event.clientY - t.y) > 8) return;
    const item = helpAt(toDesign(event));
    showHelp(item);
    // In the steps, a tap is using the control: the bar keeps the step. Only the last step, the
    // whole panel, lends the bar to help, and never for a button, which does what it says.
    if (phone && TOUR[tour]?.all) setBarHelp(item && !event.target.closest('button, [role=button], [role=radio], [role=switch]') ? { label: item.label, text: item.text } : null);
  };
  // Dragging a chip: the control it would land on, and a tag at the mouse.
  const targetRect = (t) => (t === 6 ? grow(L.balance, 4, 6) : L.knob[KNOB_IDS.indexOf(MOD_TARGETS[t])]);
  const chipTarget = (event) => {
    const { x, y } = toDesign(event);
    return { x, y, target: MOD_TARGETS.findIndex((_, t) => targetRect(t) && inside(targetRect(t), x, y)) };
  };
  const dropChip = (k, event) => {
    setChipDrag(null);
    if (!event) return;
    const { target } = chipTarget(event);
    if (target < 0) return;
    const key = `mod${k + 1}_${MOD_TARGETS[target]}`;
    const depth = Math.abs(params[key]) < 0.005 ? 0.5 : params[key];
    if (Math.abs(params[key]) < 0.005) update(key, 0.5);
    setModSource(k);
    showReadout(`MOD ${k + 1} > ${MOD_TARGET_NAMES[MOD_TARGETS[target]]}`, `${depth > 0 ? '+' : ''}${Math.round(depth * 100)}%`);
  };

  // The readout: help while the mouse rests on something; otherwise the last thing moved.
  const helping = !!hover && !menu;
  const lcdLabel = lcd.follows ? (lcd.knob >= 0 ? knobName(lcd.knob, params) : lcd.label) : lcd.label;
  const lcdValue = (lcd.follows ? describe(lcd.follows, said) : lcd.value).toUpperCase();
  const lsrc = state?.lsrc || 0;
  const meta = `${((state?.clockHz ?? clockHz(params.clock, params.smooth)) / 1000).toFixed(1)} kHz    ${lsrc > 0 && state ? `loop ${(state.winLen / state.clockHz).toFixed(2)} s` : 'no loop'}`;

  // The switches' lights: the wet side blinks while frozen; the loop blinks while it records or
  // layers, and flashes once each time a stopped looper fills its memory.
  if (state && state.histCycles !== loopFlash.current.cycles) { loopFlash.current = { cycles: state.histCycles, until: performance.now() + 120 }; }
  const wetLit = running && !!state?.wetOn && !(state.freeze && !blink);
  const loopState = running ? state?.loop || 'off' : 'off';
  const loopLit = loopState === 'play' || ((loopState === 'rec' || loopState === 'dub') && blink) || (running && loopState === 'off' && performance.now() < loopFlash.current.until);
  const canDub = running && !!state && (state.canOverdub || state.dubLatched);
  const record = params.recMode === 1;
  const worn = !!params.classic, wear = worn ? 2 : params.hiQ ? 0 : 1;
  const n = modSource + 1;
  const spanMode = !!params[`mod${n}Sync`];
  const rateValue = params[`mod${n}Rate`];
  const rateFigure = spanMode ? rampDivLabel(rateValue) : (() => { const secs = 1 / rampHertz(rateValue); return secs < 1 ? `${Math.round(secs * 1000)}ms` : `${secs.toFixed(secs < 10 ? 1 : 0)}s`; })();
  const memoryFigure = params.memory <= 0.001 ? 'PEDAL' : `${expMap(params.memory, 1, 30).toFixed(1)}s`;
  const hoodValues = [
    `${levelDb(inputGain) > 0.05 ? '+' : ''}${levelDb(inputGain).toFixed(1)} dB`,
    params.tone > 0.995 ? 'open' : `cut ${pct(1 - params.tone)}`,
    params.fade <= 0.001 ? 'pedal' : `keeps ${pct(1 - params.fade)}`,
    params.width <= 0.005 ? 'mono' : params.width < 0.49 ? `narrow ${pct(params.width * 2)}` : params.width > 0.51 ? `wide ${pct((params.width - 0.5) * 2)}` : 'as built',
    worn ? (params.wearNoise <= 0.005 ? 'off' : pct(params.wearNoise)) : 'when worn',
  ];
  const balanceShown = balanceRouted && running ? effective(6) : params.balance;
  const pressSwitch = (id, label, on, off) => { const next = params[id] ? 0 : 1; update(id, next); showReadout(label, next ? on : off); };
  const demo = sounds.find((d) => d.id === demoId) || sounds[0];
  const sourceText = notice || (source === 'demo' ? '' : source === 'file' ? fileName : 'use headphones');

  const lcdW = L.lcd[2];
  const src = L.src;
  const shownKnob = phone ? activeKnob : hoverKnob;
  const mast = phone
    ? <>
      <Wordmark r={[12, 18, 190.8, 18]}/>
      <Txt r={[210, 16, 168, 22]} align="right" size={nf(15)} color={MUTED}>MICRO-LOOPER + SPACE</Txt>
      <div className="abs" style={{ ...at([12, 102, 366, 2]), background: INK }}/>
    </>
    : <>
      <div className="abs" style={{ ...at([24, 94, 1152, 2]), background: INK }}/>
      <Wordmark r={[24, 41, 296.8, 28]}/>
      <Txt r={[343, 38, 300, 17]} size={nf(17)} color={MUTED}>GLYPH AUDIO</Txt>
      <Txt r={[343, 55, 300, 17]} size={nf(17)} color={MUTED}>MICRO-LOOPER + SPACE</Txt>
      <Txt r={[720, 30, 48, 34]} align="right" size={nf(17)} color={MUTED}>PATCH</Txt>
    </>;

  // A phone in the tour: the panel scrolls in its own area and the bar sits under it as
  // ordinary content. (A fixed bar inside the site's frame is drawn in one place on iPhones
  // and tapped in another, so NEXT landed on the TOUR key above it.)
  const touring = phone && tour >= 0;
  const panel = <div className="frame" style={{ width: L.W * scale, height: L.H * scale }}>
      <div ref={stage} className="stage" style={{ width: L.W, height: L.H, transform: `scale(${scale})` }}
        onPointerMove={onStageMove} onPointerDownCapture={onStageDown} onPointerUpCapture={onStageUp} onPointerLeave={() => { if (!phone) { setHover(null); setHoverKnob(-1); } }}>
        {mast}
        <Key r={L.presetPrev} label="Previous preset" onClick={() => loadPatch(preset - 1)}><ArrowGlyph dir={-1}/></Key>
        <Key r={L.presetName} label={`Browse presets: ${currentPatch.name}`} pressed={!!browsing} onClick={() => (browsing ? setBrowsing(null) : openBrowser())}>{currentPatch.name}</Key>
        <Key r={L.presetNext} label="Next preset" onClick={() => loadPatch(preset + 1)}><ArrowGlyph dir={1}/></Key>
        <Key r={L.save} onClick={saveCurrent}>SAVE</Key>
        <Key r={L.reset} label="Reset: every control back to A LITTLE DAYDREAM, and the loop cleared" onClick={fullReset}>RESET</Key>

        {/* the main sheet: three columns on a desktop, one at a time on a phone */}
        {!phone && <>
          <div className="abs sheet" style={at(R.pedal)}/>
          {['WET CHANNEL', 'SHARED', 'MICRO-LOOPER'].map((name, c) => <React.Fragment key={name}>
            {c > 0 && <div className="abs" style={{ ...at([24 + c * 276, 274, 1, 381]), background: RULE }}/>}
            <Txt r={[24 + c * 276, 272, 276, 16]} align="center" size={nf(15)} color={MUTED}>{name}</Txt>
          </React.Fragment>)}
        </>}
        {phone && <>
          <ThreeWay r={L.tabs} labels={['Wet channel', 'Shared', 'Micro-looper']} index={tab} label="Column" onChange={(i) => { setTab(i); setActiveKnob(-1); }}/>
          <div className="abs sheet" style={at(L.tabSheet)}/>
        </>}
        <TakeStrip L={L} wave={wave} state={running ? state : null} params={params} scale={scale} onChange={update} onTouch={touch} showReadout={showReadout}
          running={running} engine={engineRef.current} openMenu={openMenu}/>
        <Footswitch r={L.fsWet} name="WET" keyName="A" which="wet" lit={wetLit} onGesture={gesture}/>
        <Footswitch r={L.fsLoop} name="LOOP" keyName="L" which="loop" lit={loopLit} onGesture={gesture}/>
        <Plate r={L.overdub} text="OVERDUB" on={state?.dubLatched && running} disabled={!canDub}
          onClick={() => { const on = !state?.dubLatched; engineRef.current.overdub(on); showReadout('OVERDUB', on ? 'layering until you switch it off' : 'off'); }}/>
        {L.character && <Stepper r={L.character} caption="WEAR" names={['CLEAN', 'PEDAL', 'WORN']} index={wear} onStep={(d) => {
          const next = (wear + d + 3) % 3;
          setParams((p) => ({ ...p, hiQ: next === 0 ? 1 : 0, classic: next === 2 ? 1 : 0 }));
          showReadout('WEAR', next === 0 ? 'clean: fewer artefacts, wet a little later' : next === 2 ? 'worn: a noisy clock, a restless loop, a longer reverb' : 'the pedal as built');
        }}/>}
        {worn && L.wearPost && <ThreeWay r={L.wearPost} labels={['Pre loop', 'Post loop']} index={params.wearPost} label="Pre or post loop"
          onChange={(i) => { update('wearPost', i); showReadout('PRE / POST LOOP', describe('wearPost', params, i)); }}/>}
        {L.wetMode && <ThreeWay r={L.wetMode} labels={['Reverb', 'Delay', 'Slip', 'Weave']} index={params.wetMode} label="Wet mode"
          onChange={(i) => { update('wetMode', i); showReadout('WET MODE', WET[i].toLowerCase()); }}/>}
        {L.routing && <ThreeWay r={L.routing} labels={['In', 'In + Loop', 'Loop']} index={params.routing} label="Routing"
          onChange={(i) => { update('routing', i); showReadout('ROUTING', ['in', 'in + loop', 'loop'][i]); }}/>}
        {L.loopMode && <ThreeWay r={L.loopMode} labels={['Env', 'Tape', 'Stretch']} index={params.loopMode} label="Loop mode"
          onChange={(i) => { update('loopMode', i); showReadout('LOOP MODE', ['env', 'tape', 'stretch'][i]); }}/>}
        {KNOB_IDS.map((id, i) => L.knob[i] && <Knob key={id} i={i} r={L.knob[i]} valueW={L.knobValueW} params={said} effective={effective(MOD_TARGETS.indexOf(id))}
          onChange={update} onTouch={touch} modSource={modSource} selectSource={selectSource} showReadout={showReadout} openMenu={openMenu}
          active={shownKnob === i ? describe(id, said).toUpperCase() : null} onActive={setActiveKnob}/>)}
        {L.wetSync && (params.wetMode === 1 || params.wetMode === 3) && <Plate r={L.wetSync} text="SYNC" on={params.sync} onClick={() => pressSwitch('sync', 'SYNC', 'on the beat', 'free')}/>}
        {L.tapeDelay && params.wetMode === 1 && <Plate r={L.tapeDelay} text="TAPE" on={params.tapeDelay} onClick={() => pressSwitch('tapeDelay', 'TAPE', 'tape echo: TIME bends the pitch', 'digital: TIME crossfades')}/>}
        {L.envSync && params.loopMode === 0 && <Plate r={L.envSync} text="SYNC" on={params.envSync} onClick={() => pressSwitch('envSync', 'SYNC', 'on the beat', 'free')}/>}
        {L.envSoft && params.loopMode === 0 && <Plate r={L.envSoft} text="SOFT" on={params.envSoft} onClick={() => pressSwitch('envSoft', 'SOFT', 'slices fade', 'slices cut')}/>}
        {L.freeze && <Plate r={L.freeze} text="FREEZE" on={params.freeze} onClick={() => pressSwitch('freeze', 'FREEZE', 'held, ringing', 'moving')}/>}
        {L.trails && params.wetMode !== 2 && <Plate r={L.trails} text="TRAILS" on={params.trails} onClick={() => pressSwitch('trails', 'TRAILS', 'tails keep falling', 'tails cut')}/>}
        {L.smooth && <Plate r={L.smooth} text="SMOOTH" on={params.smooth} onClick={() => {
          const next = params.smooth ? 0 : 1;
          // Turning SMOOTH on starts from the step CLOCK is on, so the click doesn't move the sound.
          setParams((p) => ({ ...p, smooth: next, ...(next ? { clock: Math.floor(clamp(p.clock) * 10 + 0.5) / 10 } : {}) }));
          showReadout('SMOOTH', next ? 'clock glides' : 'clock steps');
        }}/>}
        {[['wetDiv', 'WET DIV', L.wetDiv], ['loopDiv', 'LOOP DIV', L.loopDiv]].map(([id, caption, r]) => {
          if (!r) return null;
          const index = divIndex(params[id]);
          return <Stepper key={id} r={r} caption={caption} capWidth={60} names={['1x', '2x', '4x']} index={index}
            onStep={(d) => { const next = rampValue((index + d + 3) % 3, 3); update(id, next); showReadout(caption, describe(id, params, next)); }}/>;
        })}

        {/* the screen */}
        <div className="abs lcd" style={at(L.lcd)} aria-live="polite">
          <ScreenLine r={[10, 6, lcdW - 72, 16]} size={15} min={11} color="rgba(246,243,234,.75)">{helping ? hover.label : lcdLabel}</ScreenLine>
          <i className="chip-label sc" style={sf(15)}>{helping ? 'HELP' : 'LIVE'}</i>
          {helping
            ? <HelpText r={[10, 22, lcdW - 20, 84]} text={hover.text}/>
            : <>
              {/* The cursor is always there to be measured and only its ink blinks, so a long value
                  squeezed to fit doesn't change width, and shake, as it blinks. */}
              <ScreenLine r={[10, 22, lcdW - 20, 69]} size={28} min={16} wrap color={PAPER}>{lcdValue}<span style={{ visibility: blink ? 'visible' : 'hidden' }}>_</span></ScreenLine>
              <Txt r={[10, 91, lcdW - 20, 15]} cls="sc" size={sf(15)} color="rgba(246,243,234,.75)" style={{ whiteSpace: 'pre' }}>{meta}</Txt>
            </>}
        </div>

        {/* LOOP */}
        <Section r={L.loopArea} mark={MOSS} name="LOOP" tail={describe('balance', params, balanceShown).toUpperCase()}/>
        <Txt r={L.memoryLabel} size={nf(16)} color={INK} style={{ opacity: record ? 0.35 : 1 }}>MEMORY</Txt>
        <Slider r={L.memory} value={params.memory} disabled={record} label="Loop memory" valueText={describe('memory', params)}
          onTouch={() => touch('memory')} onChange={(v) => update('memory', v)}/>
        <Txt r={L.memoryFigure} align="right" cls="vl" size={vf(14)} color={MUTED} style={{ opacity: record ? 0.35 : 1 }}>{memoryFigure}</Txt>
        <Txt r={L.balanceLabel} size={nf(16)} color={INK}>BALANCE</Txt>
        <Txt r={[L.balance[0] - 32, L.balance[1], 32, 21]} size={nf(15)} color={MUTED}>LOOP</Txt>
        <Txt r={L.wetEnd} size={nf(15)} color={MUTED}>WET</Txt>
        <Slider r={L.balance} value={params.balance} label="Balance" valueText={describe('balance', params)} targetId="balance"
          onTouch={() => touch('balance')} onChange={(v) => update('balance', v)}
          modDepth={{
            // Which source a vertical drag works on: the one already on BALANCE if only one is,
            // otherwise the selected one (as a knob's edge picks).
            source: () => (routeMask[0][6] !== routeMask[1][6] ? (routeMask[0][6] ? 0 : 1) : modSource),
            get: (k) => params[`mod${k + 1}_balance`] || 0,
            set: (k, v) => { update(`mod${k + 1}_balance`, v); setModSource(k); showReadout(`MOD ${k + 1} > BALANCE`, `${Math.round(v * 100) > 0 ? '+' : ''}${Math.round(v * 100)}%`); },
          }}>
          {({ inset, span, cy }) => {
            // Clear of the tab: the first source's swing under the track, the second's over it.
            const bars = [0, 1].map((k) => {
              const d = params[`mod${k + 1}_balance`];
              if (Math.abs(d) < 0.005) return null;
              const uni = params[`mod${k + 1}Uni`], v = params.balance;
              const lo = clamp(uni ? Math.min(v, v + d) : v - Math.abs(d)), hi = clamp(uni ? Math.max(v, v + d) : v + Math.abs(d));
              return <rect key={k} x={inset + span * lo} y={k === 0 ? cy + 8 : cy - 13} width={span * (hi - lo)} height="3" rx="1.5" fill={MOD_COLOURS[k]} fillOpacity={params[`mod${k + 1}On`] ? 1 : 0.35}/>;
            });
            if (!balanceRouted) return bars;
            const moving = [0, 1].filter((k) => routeMask[k][6] && params[`mod${k + 1}On`]);
            return <>{bars}<rect x={inset + span * effective(6) - 2.5} y={cy - 7.5} width="5" height="13" rx="2" fill={moving.length === 1 ? MOD_COLOURS[moving[0]] : INK}/></>;
          }}
        </Slider>
        <ThreeWay r={L.recMode} labels={['Capture', 'Record']} index={params.recMode} label="Capture or record"
          onChange={(i) => { update('recMode', i); showReadout('CAPTURE / RECORD', i ? 'record: tap, play, tap. or hold while you play' : 'capture: a tap keeps the recent past'); }}/>
        {record && <Stepper r={L.recBars} caption="BARS" names={['FREE', '1', '2', '3', '4', '6', '8', '12', '16']} index={REC_BARS.indexOf(params.recBars)}
          onStep={(d) => { const i = (REC_BARS.indexOf(params.recBars) + d + REC_BARS.length) % REC_BARS.length; update('recBars', REC_BARS[i]); const t = describe('recBars', params, REC_BARS[i]); showReadout('RECORD', `${t}${t === 'free' ? ': until the next tap' : ', then it plays'}`); }}/>}

        {/* MODULATION */}
        <Section r={L.rampArea} mark={INK} name="MODULATION"/>
        {[0, 1].map((k) => <ModChip key={k} k={k} r={k ? L.chip1 : L.chip0} params={params} open={modOpen && modSource === k} trace={traces.current[k]}
          onToggleOn={() => update(`mod${k + 1}On`, params[`mod${k + 1}On`] ? 0 : 1)}
          onClick={() => { setModOpen(!(modOpen && modSource === k)); setModSource(k); }}
          onDragTo={(event) => setChipDrag({ k, ...chipTarget(event) })} onDrop={(event) => dropChip(k, event)} openMenu={openMenu} onChange={update}/>)}
        <Key r={L.routes} pressed={routesOpen} onClick={() => { setBrowsing(null); setPluginOpen(false); setRoutesOpen(!routesOpen); }}>ROUTES</Key>
        {modOpen && <>
          <Stepper r={L.rampWave} caption="SHAPE" names={RAMP_WAVES} index={rampIndex(params[`mod${n}Wave`], RAMP_WAVES.length)} label={`MOD ${n} shape`}
            onStep={(d) => { const i = (rampIndex(params[`mod${n}Wave`], 6) + d + 6) % 6; update(`mod${n}Wave`, rampValue(i, 6)); showReadout(`MOD ${n} SHAPE`, RAMP_WAVES[i].toLowerCase()); }}/>
          <Txt r={[L.rampRate[0], L.rampRate[1], 34, 28]} size={nf(16)} color={MUTED}>{spanMode ? 'SPAN' : 'RATE'}</Txt>
          <Slider r={[L.rampRate[0] + 36, L.rampRate[1] + 3, L.rampRate[2] - 76, 22]} value={rateValue} label={`MOD ${n} rate`} valueText={describe(`mod${n}Rate`, params)}
            onTouch={() => touch(`mod${n}Rate`)} onChange={(v) => update(`mod${n}Rate`, v)} onReset={() => update(`mod${n}Rate`, BASE[`mod${n}Rate`])}/>
          <Txt r={[L.rampRate[0] + L.rampRate[2] - 40, L.rampRate[1], 40, 28]} align="right" cls="vl" size={vf(14)} color={MUTED}>{rateFigure}</Txt>
          {['Uni', 'Sync', 'Hold'].map((f, i) => <Switch key={f} r={L.rampSwitch[i]} text={f.toUpperCase()} on={params[`mod${n}${f}`]}
            onClick={() => { const next = params[`mod${n}${f}`] ? 0 : 1; update(`mod${n}${f}`, next); showReadout(`MOD ${n} ${f.toUpperCase()}`, next ? 'on' : 'off'); }}/>)}
        </>}

        {/* the bottom strip: five cells on a desktop, five rows on a phone */}
        <div className="abs sheet" style={at(L.hood)}/>
        {['LEVEL', 'TONE', 'FADE', 'WIDTH', 'NOISE'].map((name, i) => {
          const cell = L.hoodCell[i], id = ['input', 'tone', 'fade', 'width', 'wearNoise'][i];
          const nameW = Math.ceil(name.length * 8.6) + 2;
          const slider = [cell[0], cell[1] + 22, cell[2], 26];
          return <React.Fragment key={name}>
            <Txt r={[cell[0], cell[1], nameW, 18]} size={nf(16)} color={INK}>{name}</Txt>
            <Txt r={[cell[0] + nameW + 8, cell[1], cell[2] - nameW - 8, 18]} align="right" cls="vl" size={vf(14)} color={MUTED}>{hoodValues[i]}</Txt>
            {i === 0
              ? <Slider r={slider} value={levelPos(inputGain)} label="Input level" valueText={hoodValues[0]} onChange={setLevel} onReset={() => setLevel(0.5)}/>
              : <Slider r={slider} value={params[id]} label={name} valueText={describe(id, params)} onTouch={() => touch(id)}
                onChange={(v) => update(id, v)} onReset={() => update(id, BASE[id])}/>}
          </React.Fragment>;
        })}

        {/* the source picker: the browser's own, in the Bench's place */}
        <Txt r={src.caption} size={nf(20)} color={INK}>SOURCE</Txt>
        <ThreeWay r={src.picker} labels={['Demo', 'File', 'Mic']} index={['demo', 'file', 'live'].indexOf(source)} label="Source"
          onChange={(i) => chooseSource(['demo', 'file', 'live'][i])}/>
        <Stepper r={src.demo} caption="DEMO" capWidth={40} names={sounds.map((d) => d.name.toUpperCase())} index={sounds.indexOf(demo)} label="Demo loop or song"
          onStep={(d) => chooseDemo(sounds[(sounds.indexOf(demo) + d + sounds.length) % sounds.length].id)}/>
        {demo?.song && <SongBar r={src.song} engine={engineRef.current} title={phone ? null : demo.title}/>}
        <Key r={src.load} onClick={() => fileInput.current?.click()}>LOAD FILE</Key>
        <input ref={fileInput} type="file" accept="audio/*,.wav,.aif,.aiff,.flac" onChange={readFile} hidden/>
        <Txt fit r={src.status} cls="vl" size={vf(16)} color={notice ? INK : MUTED}>{sourceText.toUpperCase()}</Txt>
        <Key r={src.tour} pressed={tour >= 0} label="Tour: a few steps through Brood" onClick={() => (tour >= 0 ? endTour() : goTour(0))}>TOUR</Key>
        <Key r={src.plugin} pressed={pluginOpen} onClick={() => { setBrowsing(null); setRoutesOpen(false); setPluginOpen(!pluginOpen); }}>IN THE PLUG-IN</Key>
        <Txt r={src.tempoLabel} align={phone ? 'left' : 'right'} size={nf(16)} color={MUTED} style={{ paddingRight: 8 }}>TEMPO</Txt>
        <label className="abs tempo vl" style={{ ...at(src.tempo), ...vf(18) }}>
          <input type="number" min="40" max="300" step="1" aria-label="Tempo in BPM" value={params.bpm}
            onChange={(e) => update('bpm', clamp(Math.round(Number(e.target.value) || 120), 40, 300))}/>
          <span>BPM</span>
        </label>
        <button type="button" className={`abs start ${running ? 'on' : ''}`} style={{ ...at(src.start), ...nf(18) }} onClick={startAudio} disabled={busy}>
          <i className="led" style={{ background: running ? LIT : 'rgba(24,24,21,.25)', top: src.start[3] / 2 - 7 }}/>{busy ? 'ONE MOMENT' : running ? 'STOP AUDIO' : 'START AUDIO'}
        </button>

        {browsing && <PresetBrowser L={L} patches={patches} current={currentPatch.name} favourites={favourites} onStar={starPatch} onSave={savePatch} onDelete={deletePatch}
          onChoose={(patch) => { setPreset(patches.findIndex((p) => p.name === patch.name)); setParams(sanitize(patch.params)); }}
          onKeep={() => { setBrowsing(null); showReadout('PATCH', currentPatch.name.toLowerCase()); }}
          onCancel={() => { setParams(browsing.params); setPreset(browsing.preset); setBrowsing(null); }}/>}
        {pluginOpen && <PluginOnly L={L} close={() => setPluginOpen(false)}/>}
        {routesOpen && <RouteList L={L} params={params} onChange={update} openMenu={openMenu} close={() => setRoutesOpen(false)}/>}
        {chipDrag && <>
          {chipDrag.target >= 0 && <div className="abs drop" style={{ ...at(targetRect(chipDrag.target)), borderColor: MOD_COLOURS[chipDrag.k] }}/>}
          <div className="abs drag-tag vl" style={{ ...at(centred(58, 20, chipDrag.x, chipDrag.y - 16)), background: MOD_COLOURS[chipDrag.k], ...vf(16) }}>MOD {chipDrag.k + 1}</div>
        </>}
        {tour >= 0 && !phone && <TourBar step={tour} quiet={quiet} done={tourDone} go={goTour} end={endTour}/>}
        {tour >= 0 && !tourDone && (() => { const t = tasks[tourTask]?.target(L); return t && <div key={`${tour}-${tourTask}`} className="abs tour-ring" style={at(grow(t, 2))} aria-hidden="true"/>; })()}
        <Menu menu={menu} close={() => setMenu(null)}/>
      </div>
    </div>;
  return <div className={`page ${phone ? 'phone' : ''} ${touring ? 'touring' : ''}`}>
    {touring ? <div ref={scroller} className="phone-scroll">{panel}</div> : panel}
    {touring && <div ref={holder} className="tourbar-holder">
      <PhoneTourBar step={tour} quiet={quiet} done={tourDone} help={barHelp} clearHelp={() => { setHover(null); setBarHelp(null); }} go={goTour} end={endTour}
        open={tourOpen} setOpen={setTourOpen}/>
    </div>}
  </div>;
}

createRoot(document.getElementById('root')).render(<App/>);
