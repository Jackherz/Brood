import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AudioEngine } from './audio-engine';
import './style.css';

const clamp = (v, min = 0, max = 1) => Math.min(max, Math.max(min, v));
const clockSteps = [2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64];
const clockKhz = (v, smooth) => smooth ? 2 * Math.pow(32, v) : clockSteps[Math.round(v * 10)];
const expMap = (t, a, b) => a * Math.pow(b / a, t);
const percent = (v) => `${Math.round(v * 100)}%`;

// Tempo-sync divisions. Duplicated from src/mood-worklet.js because an
// AudioWorklet script cannot import an ES module; test/dsp.test.js asserts the
// two copies agree, so they cannot drift apart silently.
const SYNC_DIVISIONS = [
  { beats: 1 / 8, label: '1/32' }, { beats: 1 / 6, label: '1/16T' }, { beats: 1 / 4, label: '1/16' },
  { beats: 1 / 3, label: '1/8T' }, { beats: 1 / 2, label: '1/8' }, { beats: 3 / 4, label: '1/8D' },
  { beats: 1, label: '1/4' }, { beats: 3 / 2, label: '1/4D' }, { beats: 2, label: '1/2' }, { beats: 4, label: '1/1' },
];
const syncIndex = (knob) => Math.round(clamp(knob) * (SYNC_DIVISIONS.length - 1));
const syncLabel = (knob) => SYNC_DIVISIONS[syncIndex(knob)].label;
const syncSeconds = (bpm, knob) => clamp(SYNC_DIVISIONS[syncIndex(knob)].beats * 60 / clamp(bpm, 20, 300), 0.01, 2.1 * 0.95);
const timeText = (v) => v < 1 ? `${Math.round(v * 1000)} ms` : `${v.toFixed(2)} s`;
// The loop trim, mirroring the worklet: a memory ceiling of one second at the
// far left (the pedal's own buffer) up to half a minute at the right.
const memoryText = (v) => v <= 0.001 ? 'PEDAL' : `${expMap(v, 1, 30).toFixed(1)}s`;
// The ramp engine, mirrored from src/mood-worklet.js.
const RAMP_TARGETS = ['STILL', 'TIME', 'MIX', 'LENGTH', 'MODIFY', 'CLOCK', 'BALANCE'];
const RAMP_WAVES = ['TRI', 'SQR', 'SIN', 'RND', 'SMTH'];
const rampIndex = (v, names) => Math.round(clamp(v) * (names.length - 1));
const rampValue = (index, names) => names.length < 2 ? 0 : index / (names.length - 1);
const rampHertz = (v) => expMap(clamp(v), 0.05, 8);
const rampHint = (v) => `${(1 / rampHertz(v)).toFixed(1)}s A TURN`;
// Hidden options mirror src/mood-worklet.js: the sides can run on divided clocks.
const DIV_STEPS = [1, 2, 4];
const divIndex = (v) => Math.round(clamp(v, 0, 1) * (DIV_STEPS.length - 1));

const BASE = {
  time: 0.55, mix: 0.5, length: 0.72, modWet: 0.58, clock: 0.9, modLoop: 0.66,
  wetMode: 0, routing: 1, loopMode: 1,
  trails: 0, dryKill: 0, latch: 0, spread: 0, smooth: 0, balance: 0.5,
  sync: 0, bpm: 120, memory: 0, loopEnd: 1,
  tone: 1, fade: 0, width: 0.5, wetDiv: 0, loopDiv: 0, classic: 0, freeze: 0,
  rampWet: 0, rampLoop: 0, rampSpeed: 0.45, rampWave: 0, rampUni: 0, bounce: 0, sweep: 0,
};
const PRESETS = [
  { name: 'A LITTLE DAYDREAM', subtitle: 'a place to begin', params: BASE },
  { name: 'SOFT FOCUS', subtitle: 'a warm, hazy reverb', params: { ...BASE, time: 0.73, mix: 0.56, modWet: 0.83, length: 0.65, clock: 0.8, wetMode: 0, routing: 1, loopMode: 2, modLoop: 0.62 } },
  { name: 'GLASS ECHO', subtitle: 'a pocket of repeats', params: { ...BASE, time: 0.46, mix: 0.48, modWet: 0.70, wetMode: 1, routing: 0, loopMode: 1, clock: 0.9 } },
  { name: 'LITTLE GHOST', subtitle: 'a strange harmony', params: { ...BASE, time: 0.45, mix: 0.54, modWet: 0.83, wetMode: 2, routing: 1, loopMode: 1, clock: 0.8 } },
  { name: 'MELT AWAY', subtitle: 'a slow-motion memory', params: { ...BASE, time: 0.67, mix: 0.58, length: 0.35, modWet: 0.75, clock: 0.7, modLoop: 0.57, wetMode: 0, routing: 2, loopMode: 2 } },
];
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
    else if (['trails', 'dryKill', 'latch', 'spread', 'smooth', 'sync', 'rampUni', 'bounce', 'sweep', 'freeze', 'classic'].includes(key)) out[key] = input[key] ? 1 : 0;
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
    if (p.wetMode === 3) return `${timeText(0.2 + v * 2.6)} BACK`;
    if (p.wetMode === 0) return v > 0.985 ? 'INFINITE' : timeText(expMap(v, 0.4, 25) / ratio);
    return timeText(expMap(v, p.wetMode === 1 ? 0.02 : 0.05, p.wetMode === 1 ? 2 : 1.5) / ratio);
  }
  if (id === 'length' && p.loopMode === 1) return timeText((64 / rate) * expMap(v, 0.02, 1));
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
  if (id === 'loopEnd') return v >= 0.999 ? 'THE WHOLE LOOP' : `PLAYS ${percent(v)}`;
  if (id === 'tone') return v > 0.995 ? 'OPEN' : `HI-CUT ${percent(1 - v)}`;
  if (id === 'fade') return v <= 0.001 ? 'PEDAL DECAY' : `KEEPS ${percent(1 - v)}`;
  if (id === 'width') return v < 0.49 ? `NARROW ${percent(v * 2)}` : v > 0.51 ? `WIDE ${percent((v - 0.5) * 2)}` : 'AS BUILT';
  if (id === 'wetDiv' || id === 'loopDiv') { const d = DIV_STEPS[divIndex(v)]; return d === 1 ? '1× CLOCK' : `${d}× SLOWER`; }
  if (id === 'rampWet' || id === 'rampLoop') return RAMP_TARGETS[rampIndex(v, RAMP_TARGETS)];
  if (id === 'rampWave') return RAMP_WAVES[rampIndex(v, RAMP_WAVES)];
  if (id === 'rampSpeed') return rampHint(v);
  if (id === 'rampUni' || id === 'bounce' || id === 'sweep') return v ? 'ON' : 'OFF';
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

function Knob({ id, title, sub, value, params, onChange, onTouch, dark = false }) {
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
  const down = (event) => {
    if (event.button !== 0) return;
    event.currentTarget.setPointerCapture(event.pointerId);
    drag.current = { y: event.clientY, x: event.clientX, value };
    onTouch(id);
    event.currentTarget.focus();
  };
  const move = (event) => {
    if (!drag.current) return;
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
  const ticks = Array.from({ length: 21 }, (_, i) => {
    const a = (-135 + 270 * i / 20) * Math.PI / 180;
    const r1 = i % 5 === 0 ? 48 : 50;
    const r2 = 54;
    return <line key={i} x1={60 + Math.sin(a) * r1} y1={60 - Math.cos(a) * r1} x2={60 + Math.sin(a) * r2} y2={60 - Math.cos(a) * r2} stroke="currentColor" strokeWidth={i % 5 === 0 ? 2.4 : 1.5} />;
  });
  return <div className="knob-block">
    <div className="knob-heading"><span>{title}</span><span className="knob-index">{id === 'time' ? '01' : id === 'mix' ? '02' : id === 'length' ? '03' : id === 'modWet' ? '04' : id === 'clock' ? '05' : '06'}</span></div>
    <div ref={element} className={`knob ${dark ? 'knob-dark' : ''}`} role="slider" tabIndex="0"
      aria-label={`${title}${id === 'modWet' ? ' wet' : id === 'modLoop' ? ' loop' : ''}`}
      aria-valuemin="0" aria-valuemax="100" aria-valuenow={Math.round(value * 100)} aria-valuetext={describe(id, params)}
      onPointerDown={down} onPointerMove={move} onPointerUp={() => { drag.current = null; }} onPointerCancel={() => { drag.current = null; }}
      onKeyDown={key} onFocus={() => onTouch(id)} onDoubleClick={() => onChange(id, KNOB_DEFAULTS[id])}>
      <svg viewBox="0 0 120 120" aria-hidden="true">
        <g>{ticks}</g>
        <path className="knob-shadow" d="M42 22H78V25H88V30H94V40H97V80H94V90H88V95H78V98H42V95H32V90H26V80H23V40H26V30H32V25H42Z" transform="translate(2 3)" />
        <path className="knob-outer" d="M42 22H78V25H88V30H94V40H97V80H94V90H88V95H78V98H42V95H32V90H26V80H23V40H26V30H32V25H42Z" />
        <path className="knob-inner" d="M43 29H77V32H85V37H90V44H92V76H90V83H85V88H77V91H43V88H35V83H30V76H28V44H30V37H35V32H43Z" />
        <g transform={`rotate(${-135 + value * 270} 60 60)`}><rect className="knob-pointer" x="57" y="32" width="6" height="22"/><rect className="knob-center" x="57" y="57" width="6" height="6"/></g>
      </svg>
    </div>
    <p className="knob-whisper">{sub}</p>
    <div className="knob-caption"><b>{describe(id, params)}</b></div>
  </div>;
}

function ModeSelector({ label, options, value, onChange, help, hint }) {
  return <div className="mode-selector" {...(help ? help(label, hint) : {})}>
    <div className="micro-label"><span>{label}</span><span className="micro-dots">•••</span></div>
    <div className="mode-buttons" role="radiogroup" aria-label={label}>
      {options.map((option, i) => <button key={option} type="button" role="radio" aria-checked={value === i} className={value === i ? 'selected' : ''} onClick={() => onChange(i)}>{option}</button>)}
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

function LoopWave({ wave, state }) {
  const canvas = useRef(null);
  useEffect(() => {
    const c = canvas.current, g = c.getContext('2d'), w = c.width, h = c.height;
    g.clearRect(0, 0, w, h);
    g.fillStyle = '#f8f7f0'; g.fillRect(0, 0, w, h);
    g.strokeStyle = '#d5d4cc'; g.lineWidth = 1;
    g.beginPath(); g.moveTo(0, h / 2 + .5); g.lineTo(w, h / 2 + .5); g.stroke();
    if (wave?.peaks && state?.lsrc) {
      const peaks = wave.peaks;
      const start = clamp(state.winStart / state.lsrc), end = clamp((state.winStart + state.winLen) / state.lsrc);
      // Trimmed-away audio stays visible but recedes; the two edges are drawn as markers.
      g.fillStyle = '#e6e5dd';
      g.fillRect(0, 0, start * w, h);
      g.fillRect(end * w, 0, w - end * w, h);
      for (let i = 0; i < peaks.length; i++) {
        const x = Math.floor(i / peaks.length * w), amplitude = Math.max(1, Math.min(1, peaks[i] * 2) * (h / 2 - 3));
        const inside = i / peaks.length >= start && i / peaks.length < end;
        g.fillStyle = inside ? '#151512' : '#b3b3ab';
        g.fillRect(x, Math.floor(h / 2 - amplitude), 2, Math.max(2, Math.floor(amplitude * 2)));
      }
      g.fillStyle = '#151512';
      g.fillRect(start * w, 0, 2, h);
      g.fillRect(Math.max(0, end * w - 2), 0, 2, h);
      const playhead = clamp((state.winStart + state.pos) / state.lsrc) * w;
      g.fillRect(playhead, 0, 2, h);
    } else {
      g.fillStyle = '#9a9a92';
      for (let x = 6; x < w; x += 12) g.fillRect(x, h / 2, 2, 2);
    }
  }, [wave, state]);
  return <canvas ref={canvas} width="620" height="46" aria-label={wave ? 'Captured loop waveform' : 'No loop captured yet'} />;
}

function TrimStrip({ params, onChange, help }) {
  const controls = [
    ['memory', 'MEMORY', memoryText(params.memory), 'how much of the recent past the looper may keep'],
    ['loopEnd', 'END', describe('loopEnd', params), 'how much of the loop plays before it wraps'],
  ];
  return <div className="trim-strip" role="group" aria-label="Loop trim">
    <span className="trim-title">TRIM<br/>THE<br/>MOMENT</span>
    <div className="trim-row">
      {controls.map(([id, label, value, hint]) => (
        <label key={id} className="trim-control" {...help(label, hint)}>
          <span>{label}</span>
          <input type="range" min="0" max="1" step="0.005" value={params[id]} aria-label={`${label}: ${hint}`} onChange={(event) => onChange(id, Number(event.target.value))}/>
          <b>{value}</b>
        </label>
      ))}
    </div>
  </div>;
}

// The options the pedal hides behind both footswitches. They used to live on a
// separate page of the browser version; they sit on the panel now, so nothing
// has to be opened to be reached.
function HoodPanel({ params, onChange, help }) {
  const rows = [
    ['tone', 'TONE', 'a hi-cut on the wet side. right is open; left is soft and far away.'],
    ['fade', 'FADE', 'what survives an overdub. left keeps the pedal\u2019s slow erosion; right lets the new layer win.'],
    ['width', 'WIDTH', 'how far apart the two speakers sit. the middle is the image as built.'],
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

// One ramp destination. Arrows step through the list so the cell stays big.
function Stepper({ label, names, value, onChange, hint, help }) {
  const index = rampIndex(value, names);
  const step = (d) => onChange(rampValue((index + d + names.length) % names.length, names));
  return <div className="stepper" role="group" aria-label={`${label}: ${hint}`} {...(help || {})}>
    <button type="button" aria-label={`previous ${label}`} onClick={() => step(-1)}>◀</button>
    <span className="stepper-cap">{label}</span>
    <b title={hint}>{names[index]}</b>
    <button type="button" aria-label={`next ${label}`} onClick={() => step(1)}>▶</button>
  </div>;
}

// The ramps: two tides, one shape, one rate, and the two dips that bend them.
function RampPanel({ params, onChange, help }) {
  return <div className="ramp-panel">
    <div className="side-rule ramp-heading"><span>THE RAMPS</span><span>⇅ SLOW TIDE</span></div>
    <div className="ramp-grid">
      <Stepper label="WET" names={RAMP_TARGETS} value={params.rampWet} onChange={(v) => onChange('rampWet', v)} hint="which part of the wet side drifts"
        help={help('RAMP / WET', 'which part of the wet side the first generator moves, on top of that knob.')}/>
      <Stepper label="LOOP" names={RAMP_TARGETS} value={params.rampLoop} onChange={(v) => onChange('rampLoop', v)} hint="which part of the loop drifts"
        help={help('RAMP / LOOP', 'which part of the loop the second generator moves. MODIFY means this side\u2019s own.')}/>
      <Stepper label="SHAPE" names={RAMP_WAVES} value={params.rampWave} onChange={(v) => onChange('rampWave', v)} hint="the shape of the tide"
        help={help('RAMP SHAPE', 'the shape of the tide: triangle, square, sine, random, smooth random.')}/>
      <label className="rate-row" {...help('RAMP RATE', 'how slowly the tide turns. both sides share it, so they stay in step.')}><span>RATE</span>
        <input aria-label="Ramp speed" type="range" min="0" max="1" step="0.005" value={params.rampSpeed} onChange={(event) => onChange('rampSpeed', Number(event.target.value))}/>
        <b>{rampHint(params.rampSpeed)}</b>
      </label>
    </div>
    <div className="dips ramp-dips">
      {[['rampUni', 'UNI', 'the swing sits above the knob instead of either side of it.'],
        ['bounce', 'BOUNCE', 'the swing reflects instead of snapping back, so nothing jumps.'],
        ['sweep', 'SWEEP', 'the shape is ignored and the knob just climbs, again and again.']].map(([id, label, hint]) => <Dip key={id} label={label} checked={params[id]} onClick={() => onChange(id, params[id] ? 0 : 1)} help={help(label, hint)}/>)}
    </div>
  </div>;
}

function Dip({ label, checked, onClick, help }) {
  return <button type="button" className="dip" role="switch" aria-checked={!!checked} aria-label={label} onClick={onClick} {...(help || {})}>
    <span className={`dip-track ${checked ? 'on' : ''}`}><span /></span><span>{label}</span>
  </button>;
}

function App() {
  const [params, setParams] = useState(() => sanitize(safeRead('brood-mood-params-v1')));
  const [userPatch, setUserPatch] = useState(() => { const saved = safeRead('brood-mood-user-patch-v1'); return saved ? sanitize(saved) : null; });
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
  useEffect(() => { engineRef.current.setParams(params); safeWrite('brood-mood-params-v1', params); }, [params]);
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
  const patches = userPatch ? [...PRESETS, { name: 'YOUR PATCH', subtitle: 'saved on this device', params: userPatch }] : PRESETS;
  const loadPatch = (index) => {
    const n = (index + patches.length) % patches.length;
    setPreset(n); setParams(sanitize(patches[n].params)); setNotice(`Loaded ${patches[n].name.toLowerCase()}.`);
  };
  const savePatch = () => {
    const saved = sanitize(params);
    setUserPatch(saved); safeWrite('brood-mood-user-patch-v1', saved); setPreset(PRESETS.length); setNotice('Your patch is saved on this device.');
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
        <div className="window-name"><span className="window-icon">✳</span> MOOD.EXE <span className="bar-divider">/</span> <span className="window-sub">A SOUND EXPERIMENT</span></div>
        <div className="window-actions"><button title="Reset controls" aria-label="Reset controls" onClick={() => { setParams({ ...BASE }); setPreset(0); setNotice('Controls reset.'); }}>↺</button><button title="Open guide" aria-label="Open guide" onClick={() => setGuideOpen(!guideOpen)}>?</button><span aria-hidden="true">×</span></div>
      </div>
      <header className="hero">
        <div className="hero-title">
          <div className="hero-kicker"><Spark size={12}/> MICRO-LOOPER <span>+</span> SPATIAL EFFECTS</div>
          <div className="wordmark">MOOD<span className="wordmark-dot">.</span></div>
          <div className="hero-subtitle">a little world of sound <span>&amp;</span> space</div>
        </div>
        <div className="hero-art" aria-hidden="true"><Orbit/><div className="art-caption">SOUND GOES IN<br/>SOMETHING NEW COMES OUT</div></div>
        <div className="patch-box">
          <div className="patch-top"><span>✦ &nbsp;PATCH SELECTOR</span><span>{String(preset + 1).padStart(2, '0')} / {String(patches.length).padStart(2, '0')}</span></div>
          <div className="patch-controls"><button aria-label="Previous patch" onClick={() => loadPatch(preset - 1)}><TinyArrow direction="left"/></button><label className="patch-select"><span className="sr-only">Choose patch</span><select value={Math.min(preset, patches.length - 1)} onChange={(event) => loadPatch(Number(event.target.value))}>{patches.map((patch, i) => <option value={i} key={patch.name}>{patch.name}</option>)}</select></label><button aria-label="Next patch" onClick={() => loadPatch(preset + 1)}><TinyArrow/></button></div>
          <div className="patch-bottom"><span>{patches[preset]?.subtitle || 'a place to begin'}</span><button onClick={savePatch}>[ SAVE PATCH ]</button></div>
        </div>
      </header>

      <div className="body-grid">
        <section className="control-panel" aria-label="Effect controls">
          <div className="section-heading"><div><span className="heading-num">01</span><strong>THE CONTROL ROOM</strong><span className="heading-detail">/ MAKE A LITTLE MAGIC</span></div><span className="heading-end">DRAG KNOBS TO ADJUST ↕</span></div>
          <div className="control-columns">
            <div className="channel-column wet-column">
              <div className="column-title"><span className="pixel-icon">✳</span><div><strong>WET CHANNEL</strong><small>SPACE &amp; TIME</small></div><span className="column-no">A</span></div>
              <ModeSelector label="WET MODE" options={WET} value={params.wetMode} onChange={(v) => update('wetMode', v)} help={help} hint="four ways of treating time: reverb, delay, slip, weave."/>
              <div className="knob-row">
                <Knob id="time" title="TIME" sub={WHISPERS.time[params.wetMode]} value={params.time} params={params} onChange={update} onTouch={touch}/>
                <Knob id="modWet" title="MODIFY" sub={WHISPERS.modWet[params.wetMode]} value={params.modWet} params={params} onChange={update} onTouch={touch}/>
              </div>
              <Footswitch kind="wet" active={wetActive} state={wetStatus} running={running} onGesture={gesture}/>
            </div>
            <div className="channel-column global-column">
              <div className="column-title"><span className="pixel-icon globe-icon">◎</span><div><strong>THE IN-BETWEEN</strong><small>SHARED CONTROLS</small></div><span className="column-no">✦</span></div>
              <div className="knob-row">
                <Knob id="mix" title="MIX" sub={SHARED_WHISPER.mix} value={params.mix} params={params} onChange={update} onTouch={touch}/>
                <Knob id="clock" title="CLOCK" sub={params.smooth ? 'the heartbeat, breathing freely' : SHARED_WHISPER.clock} value={params.clock} params={params} onChange={update} onTouch={touch} dark/>
              </div>
              <ModeSelector label="ROUTING TO WET" options={ROUTE} value={params.routing} onChange={(v) => update('routing', v)} help={help} hint="what the wet side is allowed to hear: the input, the loop, or both."/>
              <div className="between-card"><div className="between-art"><span>IN</span><span className="between-dash">···▶</span><Spark size={18}/><span className="between-dash">▶···</span><span>OUT</span></div><p>ONE CLOCK. TWO WORLDS.</p></div>
            </div>
            <div className="channel-column loop-column">
              <div className="column-title"><span className="pixel-icon">◉</span><div><strong>MICRO-LOOPER</strong><small>MEMORY &amp; MOTION</small></div><span className="column-no">B</span></div>
              <ModeSelector label="LOOP MODE" options={LOOP} value={params.loopMode} onChange={(v) => update('loopMode', v)} help={help} hint="three ways of holding a moment: envelope, tape, stretch."/>
              <div className="knob-row">
                <Knob id="length" title="LENGTH" sub={WHISPERS.length[params.loopMode]} value={params.length} params={params} onChange={update} onTouch={touch}/>
                <Knob id="modLoop" title="MODIFY" sub={WHISPERS.modLoop[params.loopMode]} value={params.modLoop} params={params} onChange={update} onTouch={touch}/>
              </div>
              <Footswitch kind="loop" active={loopActive} state={loopStatus} running={running} onGesture={gesture}/>
            </div>
          </div>
          <div className="capture-strip">
            <div className="capture-label"><span className="capture-icon">▧</span><div><strong>LOOP MEMORY</strong><small>{state?.lsrc ? `${timeText(state.winLen / state.clockHz)} CAPTURED` : 'ALWAYS LISTENING'}</small></div></div>
            <div className="loop-wave"><LoopWave wave={wave} state={state}/>{!wave && <span>YOUR LITTLE MOMENTS LIVE HERE_</span>}</div>
            <div className="capture-end"><Eyes/><span>LISTEN<br/>CLOSER.</span></div>
          </div>
          <TrimStrip params={params} onChange={update} help={help}/>
        </section>

        <aside className="side-panel" aria-label="Audio input and options">
          <div className="section-heading side-heading"><div><span className="heading-num">02</span><strong>THE SIGNAL</strong></div><span className="heading-end">●</span></div>
          <div className="side-inner">
            <div className="scope-head"><div><strong>HELLO, SOUND.</strong><small>YOUR SIGNAL, IN REAL TIME</small></div><span className="scope-star"><Spark size={20}/></span></div>
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
              ['spread', 'SPREAD', 'the two channels stop sharing. each one gets its own copy.'],
              ['latch', 'LATCH', 'a held footswitch stays held: tap once and it is still pressed.'],
              ['dryKill', 'DRY KILL', 'the dry signal steps aside. only what the pedal made comes out.'],
              ['smooth', 'SMOOTH', 'the sample rate slides instead of stepping, so CLOCK bends pitch as it turns.'],
              ['sync', 'SYNC', 'the delay stops being a length of time and becomes a division of the beat.'],
              ['classic', 'CLASSIC', 'the MK I in the room: a noisy clock, a loop that never sits still.']].map(([id, label, hint]) => <Dip key={id} label={label} checked={params[id]} onClick={() => update(id, params[id] ? 0 : 1)} help={help(label, hint)}/>)}
            </div>
            <label className={`level-row tempo-row ${params.sync ? 'live' : ''}`}>
              <span>TEMPO {params.sync ? '· SYNCED' : ''}</span>
              <input aria-label="Tempo in BPM" type="number" min="40" max="300" step="1" value={params.bpm}
                onChange={(e) => update('bpm', clamp(Math.round(Number(e.target.value) || 120), 40, 300))}/>
              <b>BPM</b>
            </label>
            <label className="level-row balance-row" {...help('WET / LOOP', 'a crossfade between the two worlds. hard right is the loop on its own.')}><span>WET / LOOP</span><input aria-label="Wet and loop balance" type="range" min="0" max="1" step="0.01" value={params.balance} onChange={(e) => update('balance', Number(e.target.value))}/><b>{Math.round(params.balance * 100)}</b></label>
            <RampPanel params={params} onChange={update} help={help}/>
            <HoodPanel params={params} onChange={update} help={help}/>
            <div className="side-note"><Heart size={15}/><span>GOOD THINGS HAPPEN<br/>WHEN YOU GET LOST.</span><Spark size={14}/></div>
          </div>
        </aside>
      </div>
      <div className="window-footer"><span><span className="footer-led"/> {running ? 'AUDIO IS RUNNING' : 'READY WHEN YOU ARE'}</span><span className="footer-readout">{hover ? `${hover.label} : ${hover.text}` : `${touched === 'modWet' ? 'MODIFY / WET' : touched === 'modLoop' ? 'MODIFY / LOOP' : touched.toUpperCase()} : ${describe(touched, params)}`}</span><span>✳ &nbsp;MADE FOR GETTING LOST &nbsp; ✳</span></div>
    </main>

    <div className="below-window layout-width"><span>✦ &nbsp; UNOFFICIAL MOOD MKII–INSPIRED STUDY</span><span>MADE WITH CURIOSITY <Heart size={12}/></span><div className="below-actions"><button onClick={() => setGuideOpen(!guideOpen)}>{guideOpen ? 'CLOSE THE FIELD GUIDE ↑' : 'OPEN THE FIELD GUIDE ↓'}</button></div></div>
    {guideOpen && <section className="guide layout-width" aria-label="Field guide">
      <div className="guide-title"><span>✳</span><div><small>THE FIELD GUIDE / 001</small><h2>LET'S GET LOST.</h2></div><button onClick={() => setGuideOpen(false)} aria-label="Close guide">×</button></div>
      <div className="guide-grid"><div><b>01 / SEND SOME SOUND</b><p>Start the built-in demo, choose an audio file, or connect a live input. Use headphones with live input to avoid feedback.</p></div><div><b>02 / CATCH A MOMENT</b><p>The looper is always listening. Tap MICRO-LOOPER to catch the recent past; tap again to listen for something new. Hold while playing to overdub.</p></div><div><b>03 / MAKE SOME SPACE</b><p>Tap WET CHANNEL for reverb, delay, slip or weave. Hold it to freeze. Routing chooses whether the wet side hears the input, the loop, or both.</p></div><div><b>04 / TURN THE CLOCK</b><p>Lower CLOCK for longer, grainier loops and pitch changes. Try the switches, drag knobs, or use arrow keys. Double-click a knob to reset it.</p></div>
        <div><b>05 / KEEP THE BEAT</b><p>Switch SYNC on and the delay stops being a length of time and becomes a musical division — 1/32 to a whole note, triplets and dotted notes included. The plugin follows your DAW's tempo automatically; here you set it yourself. The knob's readout shows the division and the milliseconds it lands on.</p></div></div>
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
          <div><dt>END <span>/ loop trim</span></dt><dd>How much of the captured loop plays before it wraps. Where the loop <em>starts</em> is the LENGTH knob's business — in TAPE the window is the most recent part of what you caught, so turning LENGTH moves the start and pulling END back only moves the end. Everything trimmed away still shows in the waveform, dimmed, so you can see what you let go of.</dd></div>
          <div><dt>THE RAMPS <span>/ both channels</span></dt><dd>Two function generators, one per side. Each steps through what it drives — TIME, MIX, LENGTH, MODIFY, CLOCK or BALANCE — and writes into it on top of where that knob sits, so the knob still sets the middle of the swing. SHAPE picks the tide (triangle, square, sine, random, smooth random), RATE sets how slowly it turns, and UNI keeps it above the knob instead of either side. <b>BOUNCE</b> reflects the swing so it never snaps back; <b>SWEEP</b> drops the shape and just climbs, over and over. Two generators can run against each other: the wet side drifting up while the loop drifts down is most of what this pedal is for.</dd></div>
          <div><dt>UNDER THE HOOD <span>/ the options under the options</span></dt><dd>Five more controls and one switch, down at the bottom of the panel — the pedal keeps them behind both footswitches, but there is no reason for a menu here. <b>TONE</b> cuts the top off the wet side. <b>FADE</b> decides how much of the old loop survives each overdub pass — turn it right and the loop becomes whatever you just played. <b>WIDTH</b> moves the two speakers apart, or folds them to mono. <b>WET DIV</b> and <b>LOOP DIV</b> run each side on its own divided clock, so one can drift slower and lower than the other. <b>CLASSIC</b> brings back MK I manners: a noisy clock, a loop that never sits quite still, a reverb that rings.</dd></div>
        </dl>
      </div>
      <div className="guide-disclaimer">This is an independent approximation, not the original firmware and not affiliated with Chase Bliss. Some hardware features are not implemented. <a href="https://www.chasebliss.com/mood-mkii" target="_blank" rel="noreferrer">About the original pedal ↗</a></div>
    </section>}
  </div>;
}

createRoot(document.getElementById('root')).render(<App/>);
