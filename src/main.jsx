import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AudioEngine } from './audio-engine';
import './style.css';

const clamp = (v, min = 0, max = 1) => Math.min(max, Math.max(min, v));
const clockSteps = [2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64];
const clockKhz = (v, smooth) => smooth ? 2 * Math.pow(32, v) : clockSteps[Math.round(v * 10)];
const expMap = (t, a, b) => a * Math.pow(b / a, t);
const percent = (v) => `${Math.round(v * 100)}%`;

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
const syncSeconds = (bpm, knob) => clamp(syncSecondsRaw(bpm, knob), 0.01, 2.1 * 0.95);
// Modulation sync: one cycle is a musical span rather than a free rate.
const RAMP_DIV_LABELS = ['8 BARS', '4 BARS', '2 BARS', '1 BAR', '1/2', '1/4', '1/8', '1/16'];
const RAMP_DIV_BEATS = [32, 16, 8, 4, 2, 1, 0.5, 0.25];
const rampDivIndex = (knob) => Math.round(clamp(knob) * (RAMP_DIV_BEATS.length - 1));
const rampDivLabel = (knob) => RAMP_DIV_LABELS[rampDivIndex(knob)];
const rampCycleSeconds = (bpm, knob) => RAMP_DIV_BEATS[rampDivIndex(knob)] * 60 / clamp(bpm, 20, 300);
const timeText = (v) => v < 1 ? `${Math.round(v * 1000)} ms` : `${v.toFixed(2)} s`;
// The loop trim, mirroring the worklet: a memory ceiling of one second at the
// far left (the pedal's own buffer) up to half a minute at the right.
const memoryText = (v) => v <= 0.001 ? 'PEDAL' : `${expMap(v, 1, 30).toFixed(1)}s`;
// The modulation sources, mirrored from src/brood-worklet.js.
const MOD_TARGETS = ['time', 'mix', 'length', 'modWet', 'clock', 'modLoop', 'balance'];
const MOD_TARGET_NAMES = { time: 'TIME', mix: 'MIX', length: 'LENGTH', modWet: 'MODIFY / WET', clock: 'CLOCK', modLoop: 'MODIFY / LOOP', balance: 'WET / LOOP' };
const MOD_COLOURS = ['#b4452a', '#2f6f8f'];
const MOD_FLAGS = ['On', 'Uni', 'Bounce', 'Sweep', 'Sync', 'Hold'];
const RAMP_WAVES = ['TRI', 'SQR', 'SIN', 'RND', 'SMTH'];
const rampIndex = (v, names) => Math.round(clamp(v) * (names.length - 1));
const rampValue = (index, names) => names.length < 2 ? 0 : index / (names.length - 1);
const rampHertz = (v) => expMap(clamp(v), 0.05, 8);
const rampHint = (v) => `${(1 / rampHertz(v)).toFixed(1)}s A TURN`;
// Hidden options mirror src/brood-worklet.js: the sides can run on divided clocks.
const DIV_STEPS = [1, 2, 4];
const divIndex = (v) => Math.round(clamp(v, 0, 1) * (DIV_STEPS.length - 1));

const BASE = {
  time: 0.55, mix: 0.5, length: 0.72, modWet: 0.58, clock: 0.9, modLoop: 0.66,
  wetMode: 0, routing: 1, loopMode: 1,
  trails: 0, smooth: 0, balance: 0.5,
  sync: 0, bpm: 120, memory: 0, loopEnd: 1, start: 0, envSync: 0,
  tone: 1, fade: 0, width: 0.5, wetDiv: 0, loopDiv: 0, classic: 0, freeze: 0,
  wetPre: 0, envSoft: 0, loopDb: 0,
  ...Object.fromEntries([1, 2].flatMap((n) => [
    [`mod${n}On`, 1], [`mod${n}Rate`, 0.45], [`mod${n}Wave`, 0], [`mod${n}Uni`, 0], [`mod${n}Bounce`, 0], [`mod${n}Sweep`, 0], [`mod${n}Sync`, 0], [`mod${n}Hold`, 0],
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
const LOOP = ['ENV', 'TAPE', 'STRETCH'];
const ROUTE = ['IN', 'BOTH', 'LOOP'];
// Whispered hints, not labels. Each says roughly which way to turn without
// giving away the mechanism, so there is still something to discover — and the
// exact value appears in the readout beside the knob.
const WHISPERS = {
  time: [
    'how long the room keeps breathing',
    'how far back the echo remembers',
    'how wide a slice of time is borrowed',
    'how far back the threads reach',
  ],
  modWet: [
    'how much the surface smears',
    'how much the echo wants to stay',
    'which way, and how fast, time slides',
    'long cloth, or short clipped stitches',
  ],
  length: [
    'how much of the moment gets caught',
    'the size of the window that repeats',
    'how wide each grain is stretched',
  ],
  modLoop: [
    'how softly it must speak to be caught',
    'which way the tape runs, and how fast',
    'how far the grains drift from now',
  ],
};
const SHARED_WHISPER = {
  mix: 'how much of what you hear is not here',
  clock: 'the heartbeat everything obeys',
  routing: 'what the wet side is allowed to hear',
};

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
    else if (['trails', 'smooth', 'sync', 'envSync', 'wetPre', 'envSoft', 'freeze', 'classic'].includes(key) || MOD_FLAGS.some((f) => key === `mod1${f}` || key === `mod2${f}`)) out[key] = input[key] ? 1 : 0;
    else if (key.includes('_')) out[key] = clamp(input[key], -1, 1);
    else if (key === 'loopDb') out[key] = clamp(input[key], -24, 12);
    else if (key === 'bpm') out[key] = clamp(Math.round(input[key]), 40, 300);
    else out[key] = clamp(input[key]);
  }
  return out;
}
function describe(id, p) {
  const v = p[id];
  const rate = clockKhz(p.clock, p.smooth);
  const ratio = rate / 64;
  if (id === 'clock') return `${rate.toFixed(rate < 10 && p.smooth ? 1 : 0)} kHz`;
  if (id === 'time') {
    if (p.sync && p.wetMode === 1) return `${syncLabel(v)} · ${timeText(syncSeconds(p.bpm, v))}`;
    if (p.wetMode === 3) return p.sync ? `${syncLabel(v)} BACK · ${timeText(syncSecondsRaw(p.bpm, v))}` : `${timeText(0.2 + v * 2.6)} BACK`;
    if (p.wetMode === 0) return v > 0.985 ? 'INFINITE' : timeText(expMap(v, 0.4, 25) / ratio);
    return timeText(expMap(v, p.wetMode === 1 ? 0.02 : 0.05, p.wetMode === 1 ? 2 : 1.5) / ratio);
  }
  if (id === 'length' && p.loopMode === 1) return timeText((64 / rate) * expMap(v, 0.02, 1));
  if (id === 'length' && p.loopMode === 0 && p.envSync) return `${syncLabel(v)} SLICE · ${timeText(syncSecondsRaw(p.bpm, v))}`;
  if (id === 'length') return timeText(expMap(v, 0.01, p.loopMode === 0 ? 1 : 0.5) / ratio);
  if (id === 'modWet' && p.wetMode === 3) return v < 0.34 ? 'LONG CLOTH' : v < 0.7 ? 'MIXED' : 'STITCHES';
  if (id === 'modWet' && p.wetMode === 2) {
    const magnitude = Math.pow(2, Math.round((-1 + 2 * Math.abs(v - 0.5) * 2) * 12) / 12);
    return `${v < 0.5 ? 'REV' : 'FWD'} ${magnitude.toFixed(2)}×`;
  }
  if (id === 'modLoop' && p.loopMode === 1) {
    const magnitude = Math.pow(2, Math.round(-1 + 3 * Math.abs(v - 0.5) * 2));
    return `${v < 0.5 ? 'REV' : 'FWD'} ${magnitude}×`;
  }
  if (id === 'modLoop' && p.loopMode === 2 && Math.abs(v - 0.5) < 0.03) return 'FROZEN';
  if (id === 'memory') return v <= 0.001 ? `PEDAL · ${timeText(64 / rate)}` : `UP TO ${timeText(expMap(v, 1, 30))}`;
  if (id === 'loopEnd') return v >= 0.999 ? 'TO THE END' : `PLAYS TO ${percent(v)}`;
  // ENV and STRETCH play from the oldest audio, so START moves towards the present;
  // TAPE plays the newest, so START reaches back. Same knob, two honest readouts.
  if (id === 'start') {
    // A percentage, not a duration: how far the window has moved into the room
    // LENGTH and END leave is exact, whereas the take's length is not known here.
    if (v <= 0.005) return p.loopMode === 1 ? 'THE NEWEST' : 'THE OLDEST';
    return (p.loopMode === 1 ? 'REACHES BACK ' : 'SLIDES ON ') + percent(v);
  }
  if (id === 'tone') return v > 0.995 ? 'OPEN' : `HI-CUT ${percent(1 - v)}`;
  if (id === 'fade') return v <= 0.001 ? 'PEDAL DECAY' : `KEEPS ${percent(1 - v)}`;   // per overdub pass
  // WIDTH took SPREAD's job: all the way left is mono, the middle is the image as
  // built, and only past the middle do the sides open.
  if (id === 'width') return v <= 0.005 ? 'MONO' : v < 0.49 ? `NARROW ${percent(v * 2)}` : v > 0.51 ? `WIDE ${percent((v - 0.5) * 2)}` : 'AS BUILT';
  if (id === 'wetDiv' || id === 'loopDiv') { const d = DIV_STEPS[divIndex(v)]; return d === 1 ? '1× CLOCK' : `${d}× SLOWER`; }
  if (id === 'envSync') return v ? 'TEMPO' : 'FREE';
  if (id === 'freeze') return v ? 'HELD, RINGING' : 'MOVING';
  return percent(v);
}

function Spark({ size = 18, className = '' }) {
  return <svg className={className} width={size} height={size} viewBox="0 0 16 16" shapeRendering="crispEdges" aria-hidden="true"><path d="M7 0h2v4h2v2h2v1h3v2h-3v1h-2v2H9v4H7v-4H5v-2H3V9H0V7h3V6h2V4h2z" fill="currentColor" /></svg>;
}
function Heart({ size = 18, className = '' }) {
  return <svg className={className} width={size} height={size} viewBox="0 0 16 16" shapeRendering="crispEdges" aria-hidden="true"><path d="M2 2h4v1h1v1h2V3h1V2h4v1h1v6h-1v2h-2v2h-2v2H6v-2H4v-2H2V9H1V3h1z" fill="currentColor" /></svg>;
}
function Eyes({ className = '' }) {
  return <svg className={className} width="37" height="25" viewBox="0 0 37 25" shapeRendering="crispEdges" aria-hidden="true"><path d="M5 1h8v2h3v4h2V6h2V3h3V1h8v2h3v4h2v10h-2v4h-3v2h-8v-2h-3v-4h-2v1h-2v3h-3v2H5v-2H2v-4H0V7h2V3h3z" fill="white" stroke="currentColor" strokeWidth="2"/><path d="M9 8h4v8H9zm15 0h4v8h-4z" fill="currentColor"/><path d="M10 8h2v3h-2zm15 0h2v3h-2z" fill="white"/></svg>;
}
function Orbit({ className = '' }) {
  return <svg className={className} width="98" height="60" viewBox="0 0 98 60" aria-hidden="true"><g fill="none" stroke="currentColor" strokeWidth="1.6"><ellipse cx="49" cy="30" rx="23" ry="23"/><ellipse cx="49" cy="30" rx="45" ry="11" transform="rotate(-14 49 30)"/><path d="M26 30h46M49 7c-8 6-11 14-11 23s3 17 11 23M49 7c8 6 11 14 11 23s-3 17-11 23"/></g><path fill="currentColor" d="M77 4h2v4h3v2h-3v4h-2v-4h-3V8h3z"/></svg>;
}
function TinyArrow({ direction = 'right' }) {
  return <span aria-hidden="true" className="tiny-arrow">{direction === 'left' ? '◀' : '▶'}</span>;
}

// `toggle` is the one switch that belongs to a knob rather than to the panel: the
// tempo switch for a block that can follow the beat. It sits straight under its
// knob and is rendered only when the mode in question actually uses it, so it
// never appears as a dead control.
function Knob({ id, title, sub, value, params, onChange, onTouch, dark = false, toggle, modSource = 0 }) {
  const element = useRef(null);
  const drag = useRef(null);
  const valueRef = useRef(value);
  valueRef.current = value;
  const changeRef = useRef(onChange);
  changeRef.current = onChange;
  useEffect(() => {
    const el = element.current;
    const wheel = (event) => {
      event.preventDefault();
      changeRef.current(id, clamp(valueRef.current - event.deltaY * (event.shiftKey ? 0.0002 : 0.001)));
      onTouch(id);
    };
    el.addEventListener('wheel', wheel, { passive: false });
    return () => el.removeEventListener('wheel', wheel);
  }, [id, onTouch]);
  const [hoverRing, setHoverRing] = useState(-1);
  const ringAt = (event) => {
    const box = element.current.getBoundingClientRect();
    const d = Math.hypot(event.clientX - (box.left + box.width / 2), event.clientY - (box.top + box.height / 2)) * 120 / box.width;
    if (d < 43 || d > 63) return -1;
    const on = [0, 1].map((k) => Math.abs(params[`mod${k + 1}_${id}`] || 0) >= 0.005);
    if (on[0] && on[1]) return d >= 57 ? 1 : 0;
    return on[0] && modSource !== 1 ? 0 : on[1] && modSource !== 0 ? 1 : modSource;
  };
  const down = (event) => {
    if (event.button !== 0) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    // The rings are handles, as in the plug-in: a press around the knob drags that
    // source's depth, and where there's no ring yet it makes one for the selected source.
    const ring = ringAt(event);
    const depthKey = `mod${(ring >= 0 ? ring : modSource) + 1}_${id}`;
    drag.current = ring >= 0 || event.altKey ? { y: event.clientY, x: event.clientX, depthKey, value: params[depthKey] } : { y: event.clientY, x: event.clientX, value };
    onTouch(id);
    event.currentTarget.focus();
  };
  const move = (event) => {
    if (!drag.current) { const r = ringAt(event); if (r !== hoverRing) setHoverRing(r); return; }
    if (drag.current.depthKey) { onChange(drag.current.depthKey, clamp(drag.current.value + (drag.current.y - event.clientY) / 150, -1, 1)); return; }
    const delta = (drag.current.y - event.clientY) + (event.clientX - drag.current.x) * 0.25;
    onChange(id, clamp(drag.current.value + delta / (event.shiftKey ? 700 : 180)));
  };
  const key = (event) => {
    let v = value;
    const step = event.shiftKey ? 0.05 : 0.01;
    if (event.key === 'ArrowUp' || event.key === 'ArrowRight') v += step;
    else if (event.key === 'ArrowDown' || event.key === 'ArrowLeft') v -= step;
    else if (event.key === 'Home') v = 0;
    else if (event.key === 'End') v = 1;
    else return;
    event.preventDefault();
    event.stopPropagation();
    onTouch(id);
    onChange(id, clamp(v));
  };
  // Five marks instead of a ring of twenty-one: the scale still reads at a
  // glance, and the knob stays the loudest thing in the cell.
  const ticks = Array.from({ length: 5 }, (_, i) => {
    const a = (-135 + 270 * i / 4) * Math.PI / 180;
    return <line key={i} x1={60 + Math.sin(a) * 50} y1={60 - Math.cos(a) * 50}
      x2={60 + Math.sin(a) * 55} y2={60 - Math.cos(a) * 55} stroke="currentColor" strokeWidth={1.4} />;
  });
  return <div className="knob-block">
    <div className="knob-heading"><span>{title}</span><span className="knob-index">{id === 'time' ? '01' : id === 'mix' ? '02' : id === 'length' ? '03' : id === 'modWet' ? '04' : id === 'clock' ? '05' : '06'}</span></div>
    <div ref={element} data-mod-target={id} style={hoverRing >= 0 ? { cursor: 'ns-resize' } : undefined} onPointerLeave={() => setHoverRing(-1)} className={`knob ${dark ? 'knob-dark' : ''}`} role="slider" tabIndex="0"
      aria-label={`${title}${id === 'modWet' ? ' wet' : id === 'modLoop' ? ' loop' : ''}`}
      aria-valuemin="0" aria-valuemax="100" aria-valuenow={Math.round(value * 100)} aria-valuetext={describe(id, params)}
      onPointerDown={down} onPointerMove={move} onPointerUp={() => { drag.current = null; }} onPointerCancel={() => { drag.current = null; }}
      onKeyDown={key} onFocus={() => onTouch(id)} onDoubleClick={() => onChange(id, KNOB_DEFAULTS[id])}>
      <svg viewBox="0 0 120 120" aria-hidden="true">
        <g className="knob-ticks">{ticks}</g>
        {hoverRing >= 0 && Math.abs(params[`mod${hoverRing + 1}_${id}`] || 0) < 0.005 && <circle cx="60" cy="60" r={55 + hoverRing * 3.5} fill="none" stroke={MOD_COLOURS[hoverRing]} strokeOpacity=".45"/>}
        {[0, 1].map((k) => {
          // Each source's swing, as a ring in its colour.
          const d = params[`mod${k + 1}_${id}`] || 0;
          if (Math.abs(d) < 0.005) return null;
          const uni = params[`mod${k + 1}Uni`];
          const lo = clamp(uni ? Math.min(value, value + d) : value - Math.abs(d)), hi = clamp(uni ? Math.max(value, value + d) : value + Math.abs(d));
          const r = 55 + k * 3.5;
          const at = (v) => { const a = (-135 + 270 * v) * Math.PI / 180; return `${(60 + Math.sin(a) * r).toFixed(2)} ${(60 - Math.cos(a) * r).toFixed(2)}`; };
          return <path key={k} d={`M ${at(lo)} A ${r} ${r} 0 ${(hi - lo) * 270 > 180 ? 1 : 0} 1 ${at(hi)}`} fill="none" stroke={MOD_COLOURS[k]} strokeWidth="2.6"/>;
        })}
        {/* A flat disc with a thin pointer: no ring, just the five marks around
            it, one line standing up from the middle, and a hard offset shadow so
            it still belongs to this panel. */}
        <circle className="knob-shadow" cx="62.6" cy="63" r="41" />
        <circle className="knob-face" cx="60" cy="60" r="41" />
        <g transform={`rotate(${-135 + value * 270} 60 60)`}><rect className="knob-pointer" x="58.6" y="26" width="2.8" height="34" /></g>
      </svg>
    </div>
    {/* The slot is always there, so a knob with a switch and its neighbour
        without one keep their whispers on the same line. */}
    <div className="knob-sync-slot">
      {(toggle || []).map((t) => <button key={t.id} type="button" className={`knob-sync ${params[t.id] ? 'on' : ''}`} role="switch"
        aria-checked={!!params[t.id]} aria-label={t.name}
        onClick={() => onChange(t.id, params[t.id] ? 0 : 1)}>{t.label}</button>)}
    </div>
    <p className="knob-whisper">{sub}</p>
    <div className="knob-caption"><b>{describe(id, params)}</b></div>
  </div>;
}

function ModeSelector({ label, options, value, onChange, help, hint, disabled = false }) {
  return <div className={`mode-selector ${disabled ? 'stood-aside' : ''}`} {...(help ? help(label, hint) : {})}>
    <div className="micro-label"><span>{label}</span><span className="micro-dots">•••</span></div>
    <div className="mode-buttons" role="radiogroup" aria-label={label}>
      {options.map((option, i) => <button key={option} type="button" role="radio" aria-checked={value === i} disabled={disabled} className={value === i ? 'selected' : ''} onClick={() => onChange(i)}>{option}</button>)}
    </div>
  </div>;
}

function Footswitch({ kind, active, state, running, onGesture }) {
  const pressed = useRef(false);
  const [down, setDown] = useState(false);
  const release = () => {
    if (!pressed.current) return;
    pressed.current = false; setDown(false); onGesture(kind, 'up');
  };
  const press = () => {
    if (pressed.current) return;
    if (!running) { onGesture(kind, 'down'); return; }
    pressed.current = true; setDown(true); onGesture(kind, 'down');
  };
  useEffect(() => {
    const onBlur = () => release();
    window.addEventListener('blur', onBlur);
    return () => { window.removeEventListener('blur', onBlur); if (pressed.current) onGesture(kind, 'up'); };
  }, [kind, onGesture]);
  return <button type="button" className={`footswitch ${down ? 'pressed' : ''} ${active ? 'active' : ''}`} aria-label={`${kind === 'wet' ? 'Wet channel' : 'Micro-looper'}: tap to toggle, hold for ${kind === 'wet' ? 'freeze' : 'overdub'}`} aria-pressed={active}
    onPointerDown={(event) => { if (event.button !== 0) return; event.currentTarget.setPointerCapture(event.pointerId); press(); }} onPointerUp={release} onPointerCancel={release}
    onKeyDown={(event) => { if ((event.key === ' ' || event.key === 'Enter') && !event.repeat) { event.preventDefault(); press(); } }}
    onKeyUp={(event) => { if (event.key === ' ' || event.key === 'Enter') { event.preventDefault(); release(); } }}>
    <div className="foot-top"><span className="led"/><strong>{kind === 'wet' ? 'WET CHANNEL' : 'MICRO-LOOPER'}</strong><span className="foot-state">{state}</span></div>
    <div className="foot-bottom"><span>TAP {kind === 'wet' ? 'ON / OFF' : 'TO CATCH'}</span><span>HOLD {kind === 'wet' ? 'FREEZE' : 'OVERDUB'}</span></div>
  </button>;
}

function Scope({ engineRef, running }) {
  const canvas = useRef(null);
  useEffect(() => {
    let frame;
    const g = canvas.current.getContext('2d');
    const w = canvas.current.width, h = canvas.current.height;
    let data = null;
    const draw = () => {
      g.clearRect(0, 0, w, h);
      g.fillStyle = '#f9f8f2'; g.fillRect(0, 0, w, h);
      g.strokeStyle = '#d9d8d1'; g.lineWidth = 1;
      for (let x = 0; x <= w; x += 24) { g.beginPath(); g.moveTo(x + 0.5, 0); g.lineTo(x + 0.5, h); g.stroke(); }
      for (let y = 0; y <= h; y += 20) { g.beginPath(); g.moveTo(0, y + 0.5); g.lineTo(w, y + 0.5); g.stroke(); }
      const analyser = engineRef.current?.analyser;
      if (running && analyser) {
        if (!data || data.length !== analyser.fftSize) data = new Float32Array(analyser.fftSize);
        analyser.getFloatTimeDomainData(data);
        g.strokeStyle = '#171714'; g.lineWidth = 2;
        g.beginPath();
        for (let i = 0; i < data.length; i += 2) {
          const x = i / (data.length - 1) * w;
          const y = h / 2 - clamp(data[i], -1, 1) * (h * 0.42);
          if (i === 0) g.moveTo(x, y); else g.lineTo(x, y);
        }
        g.stroke();
      } else {
        g.setLineDash([3, 4]); g.strokeStyle = '#787872'; g.beginPath(); g.moveTo(0, h / 2 + .5); g.lineTo(w, h / 2 + .5); g.stroke(); g.setLineDash([]);
      }
      frame = requestAnimationFrame(draw);
    };
    draw();
    return () => cancelAnimationFrame(frame);
  }, [engineRef, running]);
  return <div className="scope-screen">
    <div className="scope-label"><span>IN / OUT</span><span>{running ? '● SIGNAL' : '○ STANDBY'}</span></div>
    <canvas ref={canvas} width="360" height="112" aria-label={running ? 'Live output waveform' : 'Audio monitor in standby'} />
    {!running && <span className="scope-idle">WAITING FOR SOUND<span className="cursor">_</span></span>}
    <div className="scope-bottom"><span>OSCILLOSCOPE 01</span><span>✳</span></div>
  </div>;
}

// The take, with START and END as markers you drag. Pressing anywhere picks up the
// nearer marker; START hangs a tab from the top and END stands one on the bottom,
// so the pair stays readable when they nearly meet. Double-click opens the whole take.
const WAVE_W = 620, WAVE_H = 84;
function loopMarkers(state) {
  if (!state?.lsrc) return null;
  const s = clamp(state.winStart, 0, state.lsrc);
  return { s, e: clamp(s + state.winLen, s, state.lsrc), len: state.lsrc };
}
function nearestMarker(m, at) {
  // Equal distance (a window squeezed to a sliver): right of the pair is END's
  // side, left is START's, so both stay reachable.
  const toStart = Math.abs(at - m.s), toEnd = Math.abs(at - m.e);
  if (Math.abs(toStart - toEnd) < m.len / WAVE_W / 2) return at > m.e ? 1 : 0;
  return toStart < toEnd ? 0 : 1;
}

function LoopWave({ wave, state, params, onChange, onTouch, help }) {
  const canvas = useRef(null);
  const drag = useRef(null);
  const [hot, setHot] = useState(-1);
  const stateRef = useRef(state); stateRef.current = state;
  const paramsRef = useRef(params); paramsRef.current = params;
  useEffect(() => {
    const c = canvas.current, g = c.getContext('2d'), w = c.width, h = c.height;
    g.clearRect(0, 0, w, h);
    g.fillStyle = '#f8f7f0'; g.fillRect(0, 0, w, h);
    g.strokeStyle = '#d5d4cc'; g.lineWidth = 1;
    g.beginPath(); g.moveTo(0, h / 2 + .5); g.lineTo(w, h / 2 + .5); g.stroke();
    const m = loopMarkers(state);
    if (wave?.peaks && m) {
      const peaks = wave.peaks;
      const start = m.s / m.len, end = m.e / m.len;
      const levelGain = Math.pow(10, params.loopDb / 20);   // what you see is the level you hear
      // Trimmed-away audio stays visible but recedes.
      g.fillStyle = '#e6e5dd';
      g.fillRect(0, 0, start * w, h);
      g.fillRect(end * w, 0, w - end * w, h);
      for (let i = 0; i < peaks.length; i++) {
        const x = Math.floor(i / peaks.length * w), amplitude = Math.max(1, Math.min(1, peaks[i] * 2 * levelGain) * (h / 2 - 14));
        const inside = i / peaks.length >= start && i / peaks.length < end;
        g.fillStyle = inside ? '#151512' : '#b3b3ab';
        g.fillRect(x, Math.floor(h / 2 - amplitude), 2, Math.max(2, Math.floor(amplitude * 2)));
      }
      g.fillStyle = '#77776f';
      g.fillRect(Math.round(clamp((state.winStart + state.pos) / m.len) * w), 12, 1, h - 24);
      // The markers: a full-height line, and a labelled tab the hand can find.
      const marker = (x, top, label, lit) => {
        const px = Math.round(clamp(x, 0, 1) * w);
        g.fillStyle = '#151512';
        g.fillRect(Math.min(w - 2, Math.max(0, px - 1)), 0, 2, h);
        g.font = '11px Terminal, monospace';
        const tw = Math.ceil(g.measureText(label).width) + 8;
        const tx = top ? Math.min(px, w - tw) : Math.max(0, px - tw);
        const ty = top ? 0 : h - 13;
        g.fillStyle = lit ? '#151512' : '#f8f7f0';
        g.fillRect(tx, ty, tw, 13);
        g.strokeStyle = '#151512'; g.strokeRect(tx + .5, ty + .5, tw - 1, 12);
        g.fillStyle = lit ? '#f8f7f0' : '#151512';
        g.textBaseline = 'middle';
        g.fillText(label, tx + 4, ty + 7);
      };
      marker(start, true, 'START', hot === 0);
      marker(end, false, 'END', hot === 1);
    } else {
      g.fillStyle = '#9a9a92';
      for (let x = 6; x < w; x += 12) g.fillRect(x, h / 2, 2, 2);
    }
  }, [wave, state, hot, params.loopDb]);

  const sampleAt = (event) => {
    const r = canvas.current.getBoundingClientRect();
    return clamp((event.clientX - r.left) / Math.max(1, r.width)) * Math.max(64, stateRef.current.lsrc);
  };
  // The inverse of loopWindow() in the worklet: a marker position in samples of the
  // take becomes START and END. ENV and STRETCH measure both against the whole take;
  // TAPE measures END against LENGTH's span and START backwards from the tail.
  const moveMarker = (at) => {
    const st = stateRef.current, m = loopMarkers(st);
    if (!m || !drag.current) return;
    const len = Math.max(64, st.lsrc), span = Math.max(64, st.winSpan || len);
    const tape = paramsRef.current.loopMode === 1;
    const minW = Math.max(64, 0.02 * span);          // END never closes the window
    if (drag.current.which === 1) {
      onChange('loopEnd', clamp(clamp(at - m.s, minW, span) / span));
      onTouch('loopEnd');
      return;
    }
    const anchor = drag.current.anchorEnd;           // START moves under a fixed end
    const room = tape ? len - span : len - 64;
    // START may not pass END, may not open the window wider than the span, and in
    // TAPE cannot leave the room LENGTH allows.
    const lo = Math.max(0, anchor - span);
    let xs = clamp(at, lo, Math.max(lo, anchor - minW));
    if (tape) xs = Math.min(xs, Math.max(0, room));
    onChange('start', room <= 0 ? 0 : clamp(tape ? 1 - xs / room : xs / room));
    onChange('loopEnd', clamp((anchor - xs) / span, 0.02, 1));
    onTouch('start');
  };
  const down = (event) => {
    const m = loopMarkers(stateRef.current);
    if (event.button !== 0 || !m) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    const at = sampleAt(event);
    drag.current = { which: nearestMarker(m, at), anchorEnd: m.e };
    setHot(drag.current.which);
    moveMarker(at);
  };
  const move = (event) => {
    if (drag.current) { moveMarker(sampleAt(event)); return; }
    const m = loopMarkers(stateRef.current);
    const h = m ? nearestMarker(m, sampleAt(event)) : -1;
    if (h !== hot) setHot(h);
  };
  const up = () => { drag.current = null; };
  const reset = () => { onChange('start', BASE.start); onChange('loopEnd', BASE.loopEnd); onTouch('loopEnd'); };
  // From the keyboard: the arrows move END, with shift they move START; either way
  // right is towards the present.
  const key = (event) => {
    const d = event.key === 'ArrowRight' ? 0.01 : event.key === 'ArrowLeft' ? -0.01 : 0;
    if (!d) { if (event.key === 'Home') { event.preventDefault(); reset(); } return; }
    event.preventDefault();
    const p = paramsRef.current;
    if (event.shiftKey) { onChange('start', clamp(p.start + (p.loopMode === 1 ? -d : d))); onTouch('start'); }
    else { onChange('loopEnd', clamp(p.loopEnd + d, 0.02, 1)); onTouch('loopEnd'); }
  };
  const has = !!(wave && state?.lsrc);
  return <div className={`loop-wave ${has ? 'live' : ''}`} tabIndex={has ? 0 : -1} role="group"
    aria-label={has ? `Captured loop. START ${describe('start', params)}, END ${describe('loopEnd', params)}. Arrow keys move END, shift and arrow keys move START.` : 'No loop captured yet'}
    onKeyDown={key} {...help('START / END', 'drag the markers, or press anywhere and the nearer one jumps to you. moving START keeps the end where it is. double-click to hear the whole take again.')}>
    <canvas ref={canvas} width={WAVE_W} height={WAVE_H} className={hot >= 0 && has ? 'grab' : ''}
      onPointerDown={down} onPointerMove={move} onPointerUp={up} onPointerCancel={up}
      onPointerLeave={() => { if (!drag.current) setHot(-1); }} onDoubleClick={reset}/>
    {!wave && <span>YOUR LITTLE MOMENTS LIVE HERE_</span>}
  </div>;
}

function TrimStrip({ params, onChange, help }) {
  const hint = 'how much of the recent past the looper may keep';
  return <div className="trim-strip" role="group" aria-label="Loop trim">
    <label className="trim-control" {...help('MEMORY', hint)}>
      <span>MEMORY</span>
      <input type="range" min="0" max="1" step="0.005" value={params.memory} aria-label={`MEMORY: ${hint}`} onChange={(event) => onChange('memory', Number(event.target.value))}/>
      <b>{memoryText(params.memory)}</b>
    </label>
    <label className="trim-control trim-gain" {...help('LOOP GAIN', 'the loop\u2019s own level.')}>
      <span>GAIN</span>
      <input type="range" min="-24" max="12" step="0.5" value={params.loopDb} aria-label="Loop gain in dB" onDoubleClick={() => onChange('loopDb', 0)} onChange={(event) => onChange('loopDb', Number(event.target.value))}/>
      <b>{params.loopDb > 0 ? '+' : ''}{params.loopDb.toFixed(1)} DB</b>
    </label>
    <div className="trim-readout"><span>START</span><b>{describe('start', params)}</b></div>
    <div className="trim-readout"><span>END</span><b>{describe('loopEnd', params)}</b></div>
    <span className="trim-hint">DRAG THE MARKERS · DOUBLE-CLICK FOR ALL</span>
  </div>;
}

// The options the pedal hides behind both footswitches. They used to live on a
// separate page of the browser version; they sit on the panel now, so nothing
// has to be opened to be reached.
function HoodPanel({ params, onChange, help }) {
  const rows = [
    ['tone', 'TONE', 'a hi-cut on the wet side. right is open; left is soft and far away.'],
    ['fade', 'FADE', 'how much of the old take survives an overdub: hold the LOOP footswitch while a loop plays to record on top of it. left keeps the pedal\u2019s slow erosion; right lets the new layer win.'],
    ['width', 'WIDTH', 'how far apart the two speakers sit: left is mono, the middle is the image as built, right opens the sides.'],
    ['wetDiv', 'WET DIV', 'the wet side runs on a divided clock, so the room and the echo stretch out.'],
    ['loopDiv', 'LOOP DIV', 'the loop runs on a divided clock: lower, longer, further from the other side.'],
  ];
  return <div className="hood-panel">
    <div className="side-rule hood-heading"><span>UNDER THE HOOD</span><span>THE OPTIONS UNDER THE OPTIONS</span></div>
    {rows.map(([id, label, hint]) => (
      <label key={id} className="level-row hood-row" {...help(label, hint)}>
        <span>{label}</span>
        <input type="range" min="0" max="1" step="0.005" value={params[id]} aria-label={`${label}: ${hint}`} onChange={(event) => onChange(id, Number(event.target.value))}/>
        <b>{describe(id, params)}</b>
      </label>
    ))}
    {/* The hold gesture without the holding: the wet side comes up frozen. */}
    <div className="hood-freeze">
      <Dip label="FREEZE" checked={params.freeze} onClick={() => onChange('freeze', params.freeze ? 0 : 1)}
        help={help('FREEZE', 'the hold gesture without the holding: the wet side comes up frozen and rings on its own.')}/>
      <div className="hood-freeze-copy"><b>{describe('freeze', params)}</b>a hold you no longer have to hold.</div>
    </div>
  </div>;
}

// A short list as a cell: the arrows step either way and clicking anywhere else steps
// forward, so pressing it is the obvious thing to do with it.
function Stepper({ label, names, value, onChange, hint, help }) {
  const index = rampIndex(value, names);
  const step = (d) => onChange(rampValue((index + d + names.length) % names.length, names));
  return <div className="stepper" role="group" aria-label={`${label}: ${names[index]}. ${hint}`} title={`${label}: ${names[index]}. Click to step`} onClick={() => step(1)} {...(help || {})}>
    <button type="button" aria-label={`previous ${label}`} onClick={(event) => { event.stopPropagation(); step(-1); }}>◀</button>
    <span className="stepper-cap">{label}</span>
    <b aria-live="polite">{names[index]}</b>
    <button type="button" aria-label={`next ${label}`} onClick={(event) => { event.stopPropagation(); step(1); }}>▶</button>
  </div>;
}

// The modulation panel: two sources you drag onto what they should move, the selected
// one's shape, rate and switches, and every route in a list.
function ModPanel({ params, onChange, help, source, setSource, state }) {
  const [dragging, setDragging] = useState(-1);
  const n = source + 1;
  const synced = !!params[`mod${n}Sync`];
  // A row stays once it's there, even at 0%, so a depth can pass through zero: only its
  // × takes it away.
  const shownRoutes = useRef(new Set());
  [0, 1].forEach((k) => MOD_TARGETS.forEach((t) => { if (Math.abs(params[`mod${k + 1}_${t}`]) >= 0.005) shownRoutes.current.add(`${k}${t}`); }));
  const routes = [0, 1].flatMap((k) => MOD_TARGETS.filter((t) => shownRoutes.current.has(`${k}${t}`)).map((t) => ({ k, t })));
  const drop = (k, event) => {
    setDragging(-1);
    const hit = document.elementFromPoint(event.clientX, event.clientY)?.closest('[data-mod-target]');
    if (!hit) return;
    const key = `mod${k + 1}_${hit.dataset.modTarget}`;
    if (Math.abs(params[key]) < 0.005) onChange(key, 0.5);
    setSource(k);
  };
  return <div className="ramp-panel">
    <div className="side-rule ramp-heading"><span>MODULATION</span><span>DRAG A SOURCE ONTO A KNOB</span></div>
    <div className="mod-chips">
      {[0, 1].map((k) => <div key={k} className={`mod-chip ${source === k ? 'selected' : ''} ${dragging === k ? 'dragging' : ''}`} role="button" tabIndex="0" aria-label={`Modulation source ${k + 1}`}
        onClick={() => setSource(k)} onPointerDown={(event) => { if (event.button === 0 && !event.target.closest('button')) { event.currentTarget.setPointerCapture(event.pointerId); setDragging(k); } }}
        onPointerUp={(event) => { if (dragging === k) drop(k, event); }} {...help(`MOD ${k + 1}`, 'click to show its controls. drag it onto any knob or WET / LOOP to route it. alt-drag a knob to set the selected source\u2019s depth.')}>
        <span className="mod-num" style={{ background: MOD_COLOURS[k] }}>{k + 1}</span>
        <span className="mod-count">{MOD_TARGETS.filter((t) => Math.abs(params[`mod${k + 1}_${t}`]) >= 0.005).length} ROUTES</span>
        <button type="button" className={`mod-on ${params[`mod${k + 1}On`] ? 'on' : ''}`} role="switch" aria-checked={!!params[`mod${k + 1}On`]} aria-label={`Source ${k + 1} on`} onClick={(event) => { event.stopPropagation(); onChange(`mod${k + 1}On`, params[`mod${k + 1}On`] ? 0 : 1); }}/>
        <i className="mod-meter"><i style={{ width: `${(state?.mods?.[k] ?? 0) * 100}%`, background: MOD_COLOURS[k] }}/></i>
      </div>)}
    </div>
    <div className="ramp-grid">
      <Stepper label="SHAPE" names={RAMP_WAVES} value={params[`mod${n}Wave`]} onChange={(v) => onChange(`mod${n}Wave`, v)} hint="click to step the shape"
        help={help('MODULATION SHAPE', 'click to step the shape: triangle, square, sine, random, smooth random.')}/>
      <label className="rate-row" {...help('MODULATION RATE', 'how slowly this source turns. with SYNC on, a musical span instead of a free speed.')}><span>{synced ? 'SPAN' : 'RATE'}</span>
        <input aria-label="Modulation rate" type="range" min="0" max="1" step="0.005" value={params[`mod${n}Rate`]} onChange={(event) => onChange(`mod${n}Rate`, Number(event.target.value))}/>
        <b>{synced ? `${rampDivLabel(params[`mod${n}Rate`])} · ${timeText(rampCycleSeconds(params.bpm, params[`mod${n}Rate`]))}` : rampHint(params[`mod${n}Rate`])}</b>
      </label>
    </div>
    <div className="dips ramp-dips">
      {[['Uni', 'UNI', 'the swing sits above the knob instead of either side of it.'],
        ['Bounce', 'BOUNCE', 'the swing reflects instead of snapping back, so nothing jumps.'],
        ['Sweep', 'SWEEP', 'the shape is ignored and the knob just climbs, again and again.'],
        ['Sync', 'SYNC', 'the rate becomes a division of the beat: eight bars down to a sixteenth.'],
        ['Hold', 'HOLD', 'this source stops where it is and its knobs stay pushed there.']].map(([f, label, hint]) => <Dip key={f} label={label} checked={params[`mod${n}${f}`]} onClick={() => onChange(`mod${n}${f}`, params[`mod${n}${f}`] ? 0 : 1)} help={help(label, hint)}/>)}
    </div>
    {routes.length > 0 && <ul className="mod-routes" aria-label="Modulation routes">
      {routes.map(({ k, t }) => <li key={`${k}${t}`}>
        <span className="mod-num" style={{ background: MOD_COLOURS[k] }}>{k + 1}</span>
        <span>{MOD_TARGET_NAMES[t]}</span>
        <input type="range" aria-label={`Source ${k + 1} depth on ${MOD_TARGET_NAMES[t]}`} min="-1" max="1" step="0.01" value={params[`mod${k + 1}_${t}`]} onChange={(event) => onChange(`mod${k + 1}_${t}`, Number(event.target.value))}/>
        <b>{Math.round(params[`mod${k + 1}_${t}`] * 100)}%</b>
        <button type="button" aria-label={`Remove source ${k + 1} from ${MOD_TARGET_NAMES[t]}`} onClick={() => { shownRoutes.current.delete(`${k}${t}`); onChange(`mod${k + 1}_${t}`, 0); }}>×</button>
      </li>)}
    </ul>}
  </div>;
}

// The browser the plug-ins share: search, categories, stars. Clicking a preset plays
// it; KEEP keeps it, CANCEL puts back the sound there was.
function PresetBrowser({ patches, current, favourites, onChoose, onStar, onKeep, onCancel, onSave, onDelete }) {
  const [view, setView] = useState('ALL');
  const [search, setSearch] = useState('');
  const query = search.trim().toUpperCase();
  const shown = patches.filter((patch) => (view === 'ALL' || (view === 'FAV' ? favourites.includes(patch.name) : patch.category === view))
    && (!query || patch.name.toUpperCase().includes(query) || patch.category.includes(query)));
  return <div className="preset-browser" role="dialog" aria-label="Presets" onKeyDown={(event) => {
    if (event.key === 'Escape') onCancel();
    if (event.key === 'Enter' && event.target.tagName !== 'BUTTON') onKeep();
  }}>
    <div className="preset-head"><span>{shown.length} {shown.length === 1 ? 'SOUND' : 'SOUNDS'}</span><span>CLICK TO HEAR / ENTER KEEPS / ESC GOES BACK</span></div>
    <div className="preset-top">
      <input autoFocus aria-label="Search presets" placeholder="SEARCH" value={search} onChange={(event) => setSearch(event.target.value)}/>
      <div className="preset-views" role="group" aria-label="Preset category">
        {['ALL', 'FAV', ...PRESET_CATEGORIES].map((name) => <button key={name} type="button" aria-pressed={view === name} className={view === name ? 'selected' : ''} onClick={() => setView(name)}>{name}</button>)}
      </div>
    </div>
    <ul className="preset-list">
      {shown.map((patch) => <li key={patch.name} className={patch.name === current ? 'current' : ''}>
        <button type="button" className="preset-star" aria-pressed={favourites.includes(patch.name)} aria-label={`Favourite ${patch.name}`} onClick={() => onStar(patch.name)}>{favourites.includes(patch.name) ? '★' : '☆'}</button>
        <button type="button" className="preset-name" onClick={() => onChoose(patch)}>{patch.name}</button>
        <span>{patch.category}</span><span>{patch.factory ? 'FACTORY' : 'USER'}</span>
        {!patch.factory && <button type="button" className="preset-remove" aria-label={`Delete ${patch.name}`} onClick={() => onDelete(patch.name)}>×</button>}
      </li>)}
    </ul>
    <div className="preset-foot">
      <button type="button" onClick={() => onSave(PRESET_CATEGORIES.includes(view) ? view : 'USER')}>SAVE AS...</button>
      <span/>
      <button type="button" onClick={onCancel}>CANCEL</button>
      <button type="button" className="keep" onClick={onKeep}>KEEP</button>
    </div>
  </div>;
}

function Dip({ label, checked, onClick, help }) {
  return <button type="button" className="dip" role="switch" aria-checked={!!checked} aria-label={label} onClick={onClick} {...(help || {})}>
    <span className={`dip-track ${checked ? 'on' : ''}`}><span /></span><span>{label}</span>
  </button>;
}

function App() {
  const [params, setParams] = useState(() => sanitize(safeRead('brood-params-v1')));
  // User presets, named and filed. Anything saved in the old slots becomes a preset.
  const [userPatches, setUserPatches] = useState(() => {
    const saved = safeRead('brood-user-presets-v2');
    if (Array.isArray(saved)) return saved.filter((p) => p && p.name).map((p) => ({ name: String(p.name), category: PRESET_CATEGORIES.includes(p.category) ? p.category : 'USER', params: sanitize(p.params) }));
    const slots = safeRead('brood-user-patches-v1');
    const legacy = safeRead('brood-user-patch-v1');
    const old = Array.isArray(slots) ? slots : legacy ? [legacy] : [];
    return old.map((p, i) => (p ? { name: `USER ${i + 1}`, category: 'USER', params: sanitize(p) } : null)).filter(Boolean);
  });
  const [favourites, setFavourites] = useState(() => { const f = safeRead('brood-favourites-v1'); return Array.isArray(f) ? f : []; });
  const [browsing, setBrowsing] = useState(null);   // the sound to go back to while the browser is open
  const [preset, setPreset] = useState(0);
  const [source, setSource] = useState('demo');
  const [fileName, setFileName] = useState('');
  const [inputLevel, setInputLevel] = useState(0.8);
  const [running, setRunning] = useState(false);
  const [busy, setBusy] = useState(false);
  const [state, setState] = useState(null);
  const [wave, setWave] = useState(null);
  const [notice, setNotice] = useState('');
  const [guideOpen, setGuideOpen] = useState(false);
  const [touched, setTouched] = useState('clock');
  const [modSource, setModSource] = useState(0);
  // One sentence about whatever the mouse is resting on, for the controls that do
  // not already explain themselves. It shows in the footer, where the readout is.
  const [hover, setHover] = useState(null);
  const fileInput = useRef(null);
  const engineRef = useRef(null);
  const paramsRef = useRef(params); paramsRef.current = params;
  const runningRef = useRef(running); runningRef.current = running;
  if (!engineRef.current) {
    engineRef.current = new AudioEngine((message) => {
      if (message.type === 'state') setState(message.st);
      if (message.type === 'wave') setWave(message);
    }, (message) => setNotice(message));
  }
  useEffect(() => { engineRef.current.setParams(params); safeWrite('brood-params-v1', params); }, [params]);
  useEffect(() => { const engine = engineRef.current; return () => { engine.dispose(); }; }, []);
  const update = React.useCallback((id, value) => setParams((previous) => ({ ...previous, [id]: value })), []);
  const help = React.useCallback((label, text) => ({
    onMouseEnter: () => setHover({ label, text }),
    onMouseLeave: () => setHover(null),
    onFocus: () => setHover({ label, text }),
    onBlur: () => setHover(null),
  }), []);
  const touch = React.useCallback((id) => setTouched(id), []);
  const gesture = React.useCallback((which, action) => {
    if (!runningRef.current) { if (action === 'down') setNotice('Start audio first, then tap a channel to play.'); return; }
    engineRef.current.footswitch(which, action);
  }, []);
  useEffect(() => {
    const held = {};
    const keyMap = { a: 'wet', l: 'loop' };
    const down = (e) => {
      const which = keyMap[e.key.toLowerCase()];
      if (!which || e.repeat || held[which] || /INPUT|TEXTAREA|SELECT|BUTTON/.test(e.target.tagName) || e.target.getAttribute('role') === 'slider') return;
      held[which] = true; gesture(which, 'down');
    };
    const up = (e) => { const which = keyMap[e.key.toLowerCase()]; if (which && held[which]) { held[which] = false; gesture(which, 'up'); } };
    const blur = () => { for (const which of Object.keys(held)) if (held[which]) { held[which] = false; gesture(which, 'up'); } };
    window.addEventListener('keydown', down); window.addEventListener('keyup', up); window.addEventListener('blur', blur);
    return () => { window.removeEventListener('keydown', down); window.removeEventListener('keyup', up); window.removeEventListener('blur', blur); };
  }, [gesture]);
  const patches = [...PRESETS.map((p) => ({ ...p, factory: true })), ...userPatches.map((p) => ({ ...p, subtitle: 'saved on this device' }))];
  const currentPatch = patches[Math.min(preset, patches.length - 1)];
  const loadPatch = (index) => {
    const n = (index + patches.length) % patches.length;
    setPreset(n); setParams(sanitize(patches[n].params)); setNotice(`Loaded ${patches[n].name.toLowerCase()}.`);
  };
  const stepPatch = (d) => loadPatch(preset + d);
  const openBrowser = () => setBrowsing({ params, preset });
  const storeUser = (next) => { setUserPatches(next); safeWrite('brood-user-presets-v2', next); };
  const savePatch = (category) => {
    const name = (window.prompt('A name for this sound:', currentPatch.factory ? '' : currentPatch.name) || '').trim().toUpperCase();
    if (!name) return;
    if (PRESETS.some((p) => p.name === name)) { setNotice('That name belongs to a factory preset.'); return; }
    const next = [...userPatches.filter((p) => p.name !== name), { name, category, params: sanitize(params) }]
      .sort((a, b) => a.name.localeCompare(b.name));
    storeUser(next);
    setPreset(PRESETS.length + next.findIndex((p) => p.name === name));
    setBrowsing((b) => (b ? { params, preset: PRESETS.length + next.findIndex((p) => p.name === name) } : b));
    setNotice(`Saved ${name.toLowerCase()} on this device.`);
  };
  const saveCurrent = () => {
    if (currentPatch.factory) { savePatch('USER'); return; }
    storeUser(userPatches.map((p) => (p.name === currentPatch.name ? { ...p, params: sanitize(params) } : p)));
    setNotice(`Saved ${currentPatch.name.toLowerCase()} on this device.`);
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
  const startAudio = async () => {
    if (busy) return;
    setBusy(true); setNotice('');
    try {
      if (engineRef.current.running) { await engineRef.current.pause(); setRunning(false); }
      else { await engineRef.current.start(paramsRef.current); setRunning(true); }
    } catch (error) {
      setRunning(!!engineRef.current.running);
      setNotice(error?.name === 'NotAllowedError' ? 'Microphone access was denied. Choose DEMO or allow access in your browser.' : (error.message || 'Could not start audio.'));
    } finally { setBusy(false); }
  };
  const chooseSource = async (kind) => {
    if (kind === 'file') { fileInput.current?.click(); return; }
    setSource(kind); setNotice('');
    try { await engineRef.current.connectSource(kind); }
    catch (error) { setNotice(error?.name === 'NotAllowedError' ? 'Microphone access was denied. Choose DEMO or allow access in your browser.' : (error.message || 'Could not connect input.')); }
  };
  const readFile = async (event) => {
    const file = event.target.files?.[0];
    event.target.value = '';
    if (!file) return;
    setNotice('Loading your audio file…');
    try { await engineRef.current.chooseFile(file); setSource('file'); setFileName(file.name); setNotice('File loaded and looping.'); }
    catch (error) { setNotice(error.message || 'Could not load that file.'); }
  };
  const wetActive = !!state?.wetOn;
  const loopActive = !!state && state.loop !== 'off';
  const loopStatus = !running ? 'LISTENING' : state?.loop === 'dub' ? 'OVERDUB' : state?.loop === 'rec' ? 'RECORDING' : state?.loop === 'play' ? 'PLAYING' : 'LISTENING';
  const wetStatus = state?.freeze && running ? 'FROZEN' : wetActive ? 'ON' : 'OFF';
  const meter = (v = 0) => `${clamp((20 * Math.log10(v + 1e-9) + 60) / 60) * 100}%`;

  return <div className="page">
    <div className="utility-bar layout-width">
      <div className="utility-brand"><span className="brand-mark"><Spark size={14}/></span><span>BROOD <i>/</i> AUDIO OBJECTS</span></div>
      <div className="utility-center">A LITTLE MACHINE FOR HAPPY ACCIDENTS <span>✳</span></div>
      <div className="utility-actions">
        <button className="utility-help" onClick={() => setGuideOpen(!guideOpen)} aria-expanded={guideOpen}><span>?</span> {guideOpen ? 'CLOSE GUIDE' : 'HOW TO PLAY'}</button>
      </div>
    </div>

    <main className="window layout-width">
      <div className="window-bar">
        <div className="window-name"><span className="window-icon">✳</span> BROOD.EXE <span className="bar-divider">/</span> <span className="window-sub">A SOUND EXPERIMENT</span></div>
        <div className="window-actions"><button title="Reset controls" aria-label="Reset controls" onClick={() => { setParams({ ...BASE }); setPreset(0); setNotice('Controls reset.'); }}>↺</button><button title="Open guide" aria-label="Open guide" onClick={() => setGuideOpen(!guideOpen)}>?</button><span aria-hidden="true">×</span></div>
      </div>
      <header className="hero">
        <div className="hero-title">
          <div className="hero-kicker"><Spark size={12}/> MICRO-LOOPER <span>+</span> SPATIAL EFFECTS</div>
          <div className="wordmark">BROOD<span className="wordmark-dot">.</span></div>
          <div className="hero-subtitle">a little world of sound <span>&amp;</span> space</div>
        </div>
        <div className="hero-art" aria-hidden="true"><Orbit/><div className="art-caption">SOUND GOES IN<br/>SOMETHING NEW COMES OUT</div></div>
        <div className="patch-box">
          <div className="patch-top"><span>✦ &nbsp;PATCH SELECTOR</span><span>{String(preset + 1).padStart(2, '0')} / {String(patches.length).padStart(2, '0')}</span></div>
          <div className="patch-controls"><button aria-label="Previous patch" onClick={() => stepPatch(-1)}><TinyArrow direction="left"/></button><button className="patch-name" aria-label={`Browse presets: ${currentPatch.name}`} aria-expanded={!!browsing} onClick={() => (browsing ? setBrowsing(null) : openBrowser())}>{currentPatch.name}</button><button aria-label="Next patch" onClick={() => stepPatch(1)}><TinyArrow/></button></div>
          <div className="patch-bottom"><span>{patches[preset]?.subtitle || 'a place to begin'}</span><button onClick={saveCurrent}>[ SAVE ]</button></div>
        </div>
      </header>

      <div className="body-grid">
        <section className="control-panel" aria-label="Effect controls">
          {browsing && <PresetBrowser patches={patches} current={currentPatch.name} favourites={favourites} onStar={starPatch} onSave={savePatch} onDelete={deletePatch}
            onChoose={(patch) => { setPreset(patches.indexOf(patches.find((p) => p.name === patch.name))); setParams(sanitize(patch.params)); }}
            onKeep={() => setBrowsing(null)}
            onCancel={() => { setParams(browsing.params); setPreset(browsing.preset); setBrowsing(null); }}/>}
          <div className="section-heading"><div><span className="heading-num">01</span><strong>THE CONTROL ROOM</strong><span className="heading-detail">/ MAKE A LITTLE MAGIC</span></div><span className="heading-end">DRAG KNOBS TO ADJUST ↕</span></div>
          <div className="control-columns">
            <div className="channel-column wet-column">
              <div className="column-title"><span className="pixel-icon">✳</span><div><strong>WET CHANNEL</strong><small>SPACE &amp; TIME</small></div><span className="column-no">A</span></div>
              <ModeSelector label="WET MODE" options={WET} value={params.wetMode} onChange={(v) => update('wetMode', v)} help={help} hint="four ways of treating time: reverb, delay, slip, weave."/>
              <div className="knob-row">
                <Knob id="time" title="TIME" dark sub={WHISPERS.time[params.wetMode]} value={params.time} params={params} onChange={update} onTouch={touch} modSource={modSource}
                  toggle={(params.wetMode === 1 || params.wetMode === 3) ? [{ id: 'sync', name: 'Sync wet to tempo', label: 'SYNC' }] : null}/>
                <Knob id="modWet" title="MODIFY" sub={WHISPERS.modWet[params.wetMode]} value={params.modWet} params={params} onChange={update} onTouch={touch} modSource={modSource} dark/>
              </div>
              <button type="button" className={`wet-pre ${params.wetPre ? 'on' : ''}`} role="switch" aria-checked={!!params.wetPre} onClick={() => update('wetPre', params.wetPre ? 0 : 1)}
                {...help('BEFORE THE LOOP', 'on: the wet side runs into the looper, so catches and overdubs keep it. off: the loop stays clean and the wet side plays over it.')}><i/>BEFORE THE LOOP</button>
              <Footswitch kind="wet" active={wetActive} state={wetStatus} running={running} onGesture={gesture}/>
            </div>
            <div className="channel-column global-column">
              <div className="column-title"><span className="pixel-icon globe-icon">◎</span><div><strong>THE IN-BETWEEN</strong><small>SHARED CONTROLS</small></div><span className="column-no">✦</span></div>
              <div className="knob-row">
                <Knob id="mix" title="MIX" sub={SHARED_WHISPER.mix} value={params.mix} params={params} onChange={update} onTouch={touch} modSource={modSource}/>
                <Knob id="clock" title="CLOCK" sub={params.smooth ? 'the heartbeat, breathing freely' : SHARED_WHISPER.clock} value={params.clock} params={params} onChange={update} onTouch={touch} modSource={modSource}/>
              </div>
              <ModeSelector label="ROUTING TO WET" options={ROUTE} value={params.routing} onChange={(v) => update('routing', v)} help={help} hint="what the wet side is allowed to hear: the input, the loop, or both. with PRE on it hears only the input." disabled={!!params.wetPre}/>
              <div className="between-card"><div className="between-art"><span>IN</span><span className="between-dash">···▶</span><Spark size={18}/><span className="between-dash">▶···</span><span>OUT</span></div><p>ONE CLOCK. TWO WORLDS.</p></div>
            </div>
            <div className="channel-column loop-column">
              <div className="column-title"><span className="pixel-icon">◉</span><div><strong>MICRO-LOOPER</strong><small>MEMORY &amp; MOTION</small></div><span className="column-no">B</span></div>
              <ModeSelector label="LOOP MODE" options={LOOP} value={params.loopMode} onChange={(v) => update('loopMode', v)} help={help} hint="three ways of holding a moment: envelope, tape, stretch."/>
              <div className="knob-row">
                <Knob id="length" title="LENGTH" sub={WHISPERS.length[params.loopMode]} value={params.length} params={params} onChange={update} onTouch={touch} modSource={modSource}
                  toggle={params.loopMode === 0 ? [{ id: 'envSync', name: 'Sync envelope to tempo', label: 'SYNC' }, { id: 'envSoft', name: 'Fade the envelope slices', label: 'SOFT' }] : null}/>
                <Knob id="modLoop" title="MODIFY" sub={WHISPERS.modLoop[params.loopMode]} value={params.modLoop} params={params} onChange={update} onTouch={touch} modSource={modSource}/>
              </div>
              <Footswitch kind="loop" active={loopActive} state={loopStatus} running={running} onGesture={gesture}/>
            </div>
          </div>
          <div className="capture-strip">
            <div className="capture-label"><span className="capture-icon">▧</span><div><strong>LOOP MEMORY</strong><small>{state?.lsrc ? `${timeText(state.winLen / state.clockHz)} CAPTURED` : 'ALWAYS LISTENING'}</small></div></div>
            <LoopWave wave={wave} state={state} params={params} onChange={update} onTouch={touch} modSource={modSource} help={help}/>
            <div className="capture-end"><Eyes/><span>LISTEN<br/>CLOSER.</span></div>
          </div>
          <TrimStrip params={params} onChange={update} help={help}/>
        </section>

        <aside className="side-panel" aria-label="Audio input and options">
          <div className="section-heading side-heading"><div><span className="heading-num">02</span><strong>THE SIGNAL</strong></div><span className="heading-end">●</span></div>
          <div className="side-inner">
            <div className="scope-head"><div><strong>HELLO, SOUND.</strong><small>YOUR SIGNAL, IN REAL TIME</small></div><span className={`scope-chip ${running ? 'live' : ''}`}>{running ? 'LIVE' : 'STANDBY'}</span><span className="scope-star"><Spark size={20}/></span></div>
            <Scope engineRef={engineRef} running={running}/>
            <div className="meter-row"><span>IN</span><div className="meter"><i style={{ width: running ? meter(state?.peakIn) : '0%' }}/></div><span>OUT</span><div className="meter"><i style={{ width: running ? meter(state?.peakOut) : '0%' }}/></div></div>
            <div className="side-rule"><span>INPUT SOURCE</span><span>01—03</span></div>
            <div className="source-buttons" role="group" aria-label="Input source">
              <button className={source === 'demo' ? 'selected' : ''} onClick={() => chooseSource('demo')}>DEMO</button>
              <button className={source === 'file' ? 'selected' : ''} onClick={() => chooseSource('file')}>FILE ↗</button>
              <button className={source === 'live' ? 'selected' : ''} onClick={() => chooseSource('live')}>LIVE IN</button>
            </div>
            <input ref={fileInput} type="file" accept="audio/*,.wav,.aif,.aiff,.flac" onChange={readFile} hidden />
            <div className="source-description">{source === 'demo' ? 'A little guitar phrase, made for exploring.' : source === 'file' ? (fileName || 'Choose an audio file to loop.') : 'Your microphone or audio interface. Use headphones.'}</div>
            <button className={`power-button ${running ? 'playing' : ''}`} onClick={startAudio} disabled={busy}><span>{busy ? '···' : running ? 'Ⅱ' : '▶'}</span>{busy ? 'ONE MOMENT' : running ? 'PAUSE AUDIO' : 'START AUDIO'}<span>↗</span></button>
            <label className="level-row"><span>INPUT LEVEL</span><input aria-label="Input level" type="range" min="0" max="1.5" step="0.01" value={inputLevel} onChange={(e) => { const v = Number(e.target.value); setInputLevel(v); engineRef.current.setInputLevel(v); }}/><b>{Math.round(inputLevel * 100)}%</b></label>
            {notice && <div className="notice" role="status">✳ {notice}</div>}
            <div className="side-rule dip-heading"><span>THE LITTLE SWITCHES</span><span>ON ↑</span></div>
            <div className="dips">
              {[['trails', 'TRAILS', 'the tails keep falling after you switch the wet side off.'],
              ['smooth', 'SMOOTH', 'the sample rate slides instead of stepping, so CLOCK bends pitch as it turns.'],
              ['classic', 'CLASSIC', 'the MK I in the room: a noisy clock, a loop that never sits still.']].map(([id, label, hint]) => <Dip key={id} label={label} checked={params[id]} onClick={() => update(id, params[id] ? 0 : 1)} help={help(label, hint)}/>)}
            </div>
            <label className={`level-row tempo-row ${params.sync || params.envSync || params.rampSync ? 'live' : ''}`}>
              <span>TEMPO {params.sync || params.envSync || params.rampSync ? '· SYNCED' : ''}</span>
              <input aria-label="Tempo in BPM" type="number" min="40" max="300" step="1" value={params.bpm}
                onChange={(e) => update('bpm', clamp(Math.round(Number(e.target.value) || 120), 40, 300))}/>
              <b>BPM</b>
            </label>
            <label className="level-row balance-row" data-mod-target="balance" {...help('WET / LOOP', 'a crossfade between the two worlds. hard right is the loop on its own.')}><span>WET / LOOP</span><input aria-label="Wet and loop balance" type="range" min="0" max="1" step="0.01" value={params.balance} onChange={(e) => update('balance', Number(e.target.value))}/><b>{Math.round(params.balance * 100)}</b></label>
            <ModPanel params={params} onChange={update} help={help} source={modSource} setSource={setModSource} state={state}/>
            <HoodPanel params={params} onChange={update} help={help}/>
            <div className="side-note"><Heart size={15}/><span>GOOD THINGS HAPPEN<br/>WHEN YOU GET LOST.</span><Spark size={14}/></div>
          </div>
        </aside>
      </div>
      <div className="window-footer"><span><span className="footer-led"/> {running ? 'AUDIO IS RUNNING' : 'READY WHEN YOU ARE'}</span><span className="footer-readout">{hover ? `${hover.label} : ${hover.text}` : `${touched === 'modWet' ? 'MODIFY / WET' : touched === 'modLoop' ? 'MODIFY / LOOP' : touched === 'loopEnd' ? 'END' : touched.toUpperCase()} : ${describe(touched, params)}`}</span><span>✳ &nbsp;MADE FOR GETTING LOST &nbsp; ✳</span></div>
    </main>

    <div className="below-window layout-width"><span>✦ &nbsp; UNOFFICIAL PEDAL-INSPIRED STUDY</span><span>MADE WITH CURIOSITY <Heart size={12}/></span><div className="below-actions"><button onClick={() => setGuideOpen(!guideOpen)}>{guideOpen ? 'CLOSE THE FIELD GUIDE ↑' : 'OPEN THE FIELD GUIDE ↓'}</button></div></div>
    {guideOpen && <section className="guide layout-width" aria-label="Field guide">
      <div className="guide-title"><span>✳</span><div><small>THE FIELD GUIDE / 001</small><h2>LET'S GET LOST.</h2></div><button onClick={() => setGuideOpen(false)} aria-label="Close guide">×</button></div>
      <div className="guide-grid"><div><b>01 / SEND SOME SOUND</b><p>Start the built-in demo, choose an audio file, or connect a live input. Use headphones with live input to avoid feedback.</p></div><div><b>02 / CATCH A MOMENT</b><p>The looper is always listening. Tap MICRO-LOOPER to catch the recent past; tap again to listen for something new. Hold while playing to overdub.</p></div><div><b>03 / MAKE SOME SPACE</b><p>Tap WET CHANNEL for reverb, delay, slip or weave. Hold it to freeze. Routing chooses whether the wet side hears the input, the loop, or both.</p></div><div><b>04 / TURN THE CLOCK</b><p>Lower CLOCK for longer, grainier loops and pitch changes. Try the switches, drag knobs, or use arrow keys. Double-click a knob to reset it.</p></div>
        <div><b>05 / KEEP THE BEAT</b><p>The SYNC switch under TIME makes the delay a musical division — 1/32 to a whole note, triplets and dotted notes included — instead of a length of time, and does the same for WEAVE's reach. It only appears in the modes that can use it: REVERB and SLIP ignore the beat, so the switch is not there at all. ENV has its own SYNC under LENGTH, and the modulation panel has a third. The plugin follows your DAW's tempo automatically; here you set it yourself, and the readout shows the division and what it lands on.</p></div></div>
      <div className="guide-whispers">
        <b>WHAT AM I TURNING?</b>
        <p className="guide-whispers-note">Every knob whispers rather than explains. These are the whispers for the modes you have selected; the readout beside each knob gives the exact value.</p>
        <dl>
          <div><dt>TIME <span>/ {WET[params.wetMode]}</span></dt><dd>{WHISPERS.time[params.wetMode]}</dd></div>
          <div><dt>MODIFY <span>/ {WET[params.wetMode]}</span></dt><dd>{WHISPERS.modWet[params.wetMode]}</dd></div>
          <div><dt>LENGTH <span>/ {LOOP[params.loopMode]}</span></dt><dd>{WHISPERS.length[params.loopMode]}</dd></div>
          <div><dt>MODIFY <span>/ {LOOP[params.loopMode]}</span></dt><dd>{WHISPERS.modLoop[params.loopMode]}</dd></div>
          <div><dt>MIX <span>/ shared</span></dt><dd>{SHARED_WHISPER.mix}</dd></div>
          <div><dt>CLOCK <span>/ shared</span></dt><dd>{SHARED_WHISPER.clock}</dd></div>
          <div><dt>ROUTING <span>/ shared</span></dt><dd>{SHARED_WHISPER.routing}</dd></div>
          <div><dt>MEMORY <span>/ loop trim</span></dt><dd>How much of the recent past the looper may keep. At the far left it waits for about a second, like the pedal; turn it clockwise and it holds up to half a minute. The plugin version works the same way, except that at PEDAL it follows the CLOCK like the hardware.</dd></div>
          <div><dt>START / END <span>/ the waveform</span></dt><dd>The two markers on the waveform. Drag either one, or press anywhere and the nearer marker jumps to you; moving START keeps the end where it is, and a double-click opens the whole take again. In TAPE the window is LENGTH's share of the newest audio, so END can only pull in that far. Everything trimmed away still shows, dimmed, so you can see what you let go of.</dd></div>
          <div><dt>MODULATION <span>/ two sources</span></dt><dd>Two sources, each with its own rate and shape. Drag one onto any knob or WET / LOOP to route it, and it moves that control around where the knob sits. Alt-drag a knob to set the selected source’s depth; a negative depth inverts it. <b>UNI</b> keeps the swing above the knob, <b>BOUNCE</b> reflects it so it never snaps back, <b>SWEEP</b> drops the shape and just climbs, <b>SYNC</b> makes a turn a division of the beat, and <b>HOLD</b> stops it where it is.</dd></div>
          <div><dt>SOFT <span>/ under LENGTH, in ENV</span></dt><dd>The slices fade in and out instead of cutting, and a new one takes over from the last, so nothing clicks. A rounder, less percussive stutter. Off is the sharp cut the pedal makes.</dd></div>
          <div><dt>BEFORE THE LOOP <span>/ wet channel</span></dt><dd>Puts the wet side before the looper: input, then wet, then loop. Catches and overdubs keep the wet sound, and it hears only the input, so ROUTING stands aside. Off, the loop stays clean and the wet side plays over it.</dd></div>
          <div><dt>UNDER THE HOOD <span>/ the options under the options</span></dt><dd>Five more controls and one switch, down at the bottom of the panel — the pedal keeps them behind both footswitches, but there is no reason for a menu here. <b>TONE</b> cuts the top off the wet side. <b>FADE</b> decides how much of the old loop survives each overdub pass — turn it right and the loop becomes whatever you just played. <b>WIDTH</b> moves the two speakers apart, or folds them to mono. <b>WET DIV</b> and <b>LOOP DIV</b> run each side on its own divided clock, so one can drift slower and lower than the other. <b>CLASSIC</b> brings back MK I manners: a noisy clock, a loop that never sits quite still, a reverb that rings.</dd></div>
        </dl>
      </div>
      <div className="guide-disclaimer">This is an independent approximation, not the original firmware and not affiliated with Chase Bliss. Some hardware features are not implemented.</div>
    </section>}
  </div>;
}

createRoot(document.getElementById('root')).render(<App/>);
