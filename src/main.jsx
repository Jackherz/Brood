import React, { useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AudioEngine } from './audio-engine';
import './style.css';

const clamp = (v, min = 0, max = 1) => Math.min(max, Math.max(min, v));
const clockSteps = [2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64];
const clockKhz = (v, smooth) => smooth ? 2 * Math.pow(32, v) : clockSteps[Math.round(v * 10)];
const expMap = (t, a, b) => a * Math.pow(b / a, t);
const percent = (v) => `${Math.round(v * 100)}%`;
const timeText = (v) => v < 1 ? `${Math.round(v * 1000)} ms` : `${v.toFixed(2)} s`;

const BASE = {
  time: 0.55, mix: 0.5, length: 0.72, modWet: 0.58, clock: 0.9, modLoop: 0.66,
  wetMode: 0, routing: 1, loopMode: 1,
  trails: 0, dryKill: 0, latch: 0, spread: 0, smooth: 0, balance: 0.5,
};
const PRESETS = [
  { name: 'A LITTLE DAYDREAM', subtitle: 'a place to begin', params: BASE },
  { name: 'SOFT FOCUS', subtitle: 'a warm, hazy reverb', params: { ...BASE, time: 0.73, mix: 0.56, modWet: 0.83, length: 0.65, clock: 0.8, wetMode: 0, routing: 1, loopMode: 2, modLoop: 0.62 } },
  { name: 'GLASS ECHO', subtitle: 'a pocket of repeats', params: { ...BASE, time: 0.46, mix: 0.48, modWet: 0.70, wetMode: 1, routing: 0, loopMode: 1, clock: 0.9 } },
  { name: 'LITTLE GHOST', subtitle: 'a strange harmony', params: { ...BASE, time: 0.45, mix: 0.54, modWet: 0.83, wetMode: 2, routing: 1, loopMode: 1, clock: 0.8 } },
  { name: 'MELT AWAY', subtitle: 'a slow-motion memory', params: { ...BASE, time: 0.67, mix: 0.58, length: 0.35, modWet: 0.75, clock: 0.7, modLoop: 0.57, wetMode: 0, routing: 2, loopMode: 2 } },
];
const KNOB_DEFAULTS = { time: 0.55, mix: 0.5, length: 0.72, modWet: 0.58, clock: 0.9, modLoop: 0.66 };
const WET = ['REVERB', 'DELAY', 'SLIP'];
const LOOP = ['ENV', 'TAPE', 'STRETCH'];
const ROUTE = ['IN', 'BOTH', 'LOOP'];
const PLUGIN_ZIP = 'Brood-Mood-macOS-arm64-Experimental.zip';
const PLUGIN_ZIP_URL = `${import.meta.env.BASE_URL}downloads/${PLUGIN_ZIP}`;
const DESCRIPTIONS = {
  time: ['DECAY / SIZE', 'DELAY TIME', 'SAMPLE SIZE'],
  modWet: ['SMEAR', 'FEEDBACK', 'SPEED / DIR'],
  length: ['SLICE SIZE', 'LOOP LENGTH', 'SLICE SIZE'],
  modLoop: ['SENSITIVITY', 'SPEED / DIR', 'STRETCH'],
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
    if (['wetMode', 'routing', 'loopMode'].includes(key)) out[key] = Math.round(clamp(input[key], 0, 2));
    else if (['trails', 'dryKill', 'latch', 'spread', 'smooth'].includes(key)) out[key] = input[key] ? 1 : 0;
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
    if (p.wetMode === 0) return v > 0.985 ? 'INFINITE' : timeText(expMap(v, 0.4, 25) / ratio);
    return timeText(expMap(v, p.wetMode === 1 ? 0.02 : 0.05, p.wetMode === 1 ? 2 : 1.5) / ratio);
  }
  if (id === 'length' && p.loopMode === 1) return timeText((64 / rate) * expMap(v, 0.02, 1));
  if (id === 'length') return timeText(expMap(v, 0.01, p.loopMode === 0 ? 1 : 0.5) / ratio);
  if (id === 'modWet' && p.wetMode === 2) {
    const magnitude = Math.pow(2, Math.round((-1 + 2 * Math.abs(v - 0.5) * 2) * 12) / 12);
    return `${v < 0.5 ? 'REV' : 'FWD'} ${magnitude.toFixed(2)}×`;
  }
  if (id === 'modLoop' && p.loopMode === 1) {
    const magnitude = Math.pow(2, Math.round(-1 + 3 * Math.abs(v - 0.5) * 2));
    return `${v < 0.5 ? 'REV' : 'FWD'} ${magnitude}×`;
  }
  if (id === 'modLoop' && p.loopMode === 2 && Math.abs(v - 0.5) < 0.03) return 'FROZEN';
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
    <div className="knob-caption"><span>{sub}</span><b>{describe(id, params)}</b></div>
  </div>;
}

function ModeSelector({ label, options, value, onChange }) {
  return <div className="mode-selector">
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
      const peaks = wave.peaks, start = state.winStart / state.lsrc;
      g.fillStyle = '#e2e1d9'; g.fillRect(0, 0, start * w, h);
      for (let i = 0; i < peaks.length; i++) {
        const x = Math.floor(i / peaks.length * w), amplitude = Math.max(1, Math.min(1, peaks[i] * 2) * (h / 2 - 3));
        g.fillStyle = i / peaks.length < start ? '#a6a69f' : '#151512';
        g.fillRect(x, Math.floor(h / 2 - amplitude), 2, Math.max(2, Math.floor(amplitude * 2)));
      }
      const playhead = clamp((state.winStart + state.pos) / state.lsrc) * w;
      g.fillStyle = '#151512'; g.fillRect(playhead, 0, 2, h);
    } else {
      g.fillStyle = '#9a9a92';
      for (let x = 6; x < w; x += 12) g.fillRect(x, h / 2, 2, 2);
    }
  }, [wave, state]);
  return <canvas ref={canvas} width="620" height="46" aria-label={wave ? 'Captured loop waveform' : 'No loop captured yet'} />;
}

function Dip({ label, checked, onClick }) {
  return <button type="button" className="dip" role="switch" aria-checked={!!checked} aria-label={label} onClick={onClick}>
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
  const [downloadAvailable, setDownloadAvailable] = useState(false);
  const [downloading, setDownloading] = useState(false);
  const [touched, setTouched] = useState('clock');
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
  // Some deployments don't include a native binary; only offer the button
  // when an actual ZIP is available (Vite may otherwise return its HTML shell).
  useEffect(() => {
    let alive = true;
    fetch(PLUGIN_ZIP_URL, { method: 'HEAD' })
      .then(r => r.ok && (r.headers.get('content-type') || '').includes('application/zip'))
      .then(available => { if (alive) setDownloadAvailable(available); })
      .catch(() => {});
    return () => { alive = false; };
  }, []);
  useEffect(() => { engineRef.current.setParams(params); safeWrite('brood-mood-params-v1', params); }, [params]);
  useEffect(() => { const engine = engineRef.current; return () => { engine.dispose(); }; }, []);
  const update = React.useCallback((id, value) => setParams((previous) => ({ ...previous, [id]: value })), []);
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
    setPreset(n); setParams({ ...patches[n].params }); setNotice(`Loaded ${patches[n].name.toLowerCase()}.`);
  };
  const savePatch = () => {
    const saved = sanitize(params);
    setUserPatch(saved); safeWrite('brood-mood-user-patch-v1', saved); setPreset(PRESETS.length); setNotice('Your patch is saved on this device.');
  };
  const downloadPlugin = async () => {
    if (downloading) return;
    setDownloading(true);
    try {
      // Fetch through the same preview origin, then download a local blob. This
      // avoids opening the sandbox's protected raw URL in a separate tab.
      const response = await fetch(PLUGIN_ZIP_URL);
      if (!response.ok || !(response.headers.get('content-type') || '').includes('application/zip')) throw new Error('Download is unavailable.');
      const url = URL.createObjectURL(await response.blob());
      const anchor = document.createElement('a');
      anchor.href = url; anchor.download = PLUGIN_ZIP;
      document.body.appendChild(anchor); anchor.click(); anchor.remove();
      window.setTimeout(() => URL.revokeObjectURL(url), 60000);
    } catch (_) {
      setNotice('Download unavailable here. Use the ZIP in Arena’s file viewer.');
    } finally { setDownloading(false); }
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
        {downloadAvailable && <button className="utility-download" type="button" onClick={downloadPlugin} disabled={downloading}>{downloading ? 'PREPARING…' : '↓ MAC VST3'}</button>}
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
              <Knob id="time" title="TIME" sub={DESCRIPTIONS.time[params.wetMode]} value={params.time} params={params} onChange={update} onTouch={touch}/>
              <ModeSelector label="WET MODE" options={WET} value={params.wetMode} onChange={(v) => update('wetMode', v)}/>
              <Knob id="modWet" title="MODIFY" sub={DESCRIPTIONS.modWet[params.wetMode]} value={params.modWet} params={params} onChange={update} onTouch={touch}/>
              <Footswitch kind="wet" active={wetActive} state={wetStatus} running={running} onGesture={gesture}/>
            </div>
            <div className="channel-column global-column">
              <div className="column-title"><span className="pixel-icon globe-icon">◎</span><div><strong>THE IN-BETWEEN</strong><small>SHARED CONTROLS</small></div><span className="column-no">✦</span></div>
              <Knob id="mix" title="MIX" sub="DRY / EFFECT" value={params.mix} params={params} onChange={update} onTouch={touch}/>
              <ModeSelector label="ROUTING TO WET" options={ROUTE} value={params.routing} onChange={(v) => update('routing', v)}/>
              <Knob id="clock" title="CLOCK" sub={params.smooth ? 'SMOOTH RATE' : 'STEPPED RATE'} value={params.clock} params={params} onChange={update} onTouch={touch} dark/>
              <div className="between-card"><div className="between-art"><span>IN</span><span className="between-dash">···▶</span><Spark size={18}/><span className="between-dash">▶···</span><span>OUT</span></div><p>ONE CLOCK. TWO WORLDS.</p></div>
            </div>
            <div className="channel-column loop-column">
              <div className="column-title"><span className="pixel-icon">◉</span><div><strong>MICRO-LOOPER</strong><small>MEMORY &amp; MOTION</small></div><span className="column-no">B</span></div>
              <Knob id="length" title="LENGTH" sub={DESCRIPTIONS.length[params.loopMode]} value={params.length} params={params} onChange={update} onTouch={touch}/>
              <ModeSelector label="LOOP MODE" options={LOOP} value={params.loopMode} onChange={(v) => update('loopMode', v)}/>
              <Knob id="modLoop" title="MODIFY" sub={DESCRIPTIONS.modLoop[params.loopMode]} value={params.modLoop} params={params} onChange={update} onTouch={touch}/>
              <Footswitch kind="loop" active={loopActive} state={loopStatus} running={running} onGesture={gesture}/>
            </div>
          </div>
          <div className="capture-strip">
            <div className="capture-label"><span className="capture-icon">▧</span><div><strong>LOOP MEMORY</strong><small>{state?.lsrc ? `${timeText(state.winLen / state.clockHz)} CAPTURED` : 'ALWAYS LISTENING'}</small></div></div>
            <div className="loop-wave"><LoopWave wave={wave} state={state}/>{!wave && <span>YOUR LITTLE MOMENTS LIVE HERE_</span>}</div>
            <div className="capture-end"><Eyes/><span>LISTEN<br/>CLOSER.</span></div>
          </div>
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
              {[['trails', 'TRAILS'], ['spread', 'SPREAD'], ['latch', 'LATCH'], ['dryKill', 'DRY KILL'], ['smooth', 'SMOOTH']].map(([id, label]) => <Dip key={id} label={label} checked={params[id]} onClick={() => update(id, params[id] ? 0 : 1)}/>)}
            </div>
            <label className="level-row balance-row"><span>WET / LOOP</span><input aria-label="Wet and loop balance" type="range" min="0" max="1" step="0.01" value={params.balance} onChange={(e) => update('balance', Number(e.target.value))}/><b>{Math.round(params.balance * 100)}</b></label>
            <div className="side-note"><Heart size={15}/><span>GOOD THINGS HAPPEN<br/>WHEN YOU GET LOST.</span><Spark size={14}/></div>
          </div>
        </aside>
      </div>
      <div className="window-footer"><span><span className="footer-led"/> {running ? 'AUDIO IS RUNNING' : 'READY WHEN YOU ARE'}</span><span className="footer-readout">{touched === 'modWet' ? 'MODIFY / WET' : touched === 'modLoop' ? 'MODIFY / LOOP' : touched.toUpperCase()} : {describe(touched, params)}</span><span>✳ &nbsp;MADE FOR GETTING LOST &nbsp; ✳</span></div>
    </main>

    <div className="below-window layout-width"><span>✦ &nbsp; UNOFFICIAL MOOD MKII–INSPIRED STUDY</span><span>MADE WITH CURIOSITY <Heart size={12}/></span><button onClick={() => setGuideOpen(!guideOpen)}>{guideOpen ? 'CLOSE THE FIELD GUIDE ↑' : 'OPEN THE FIELD GUIDE ↓'}</button></div>
    {guideOpen && <section className="guide layout-width" aria-label="Field guide">
      <div className="guide-title"><span>✳</span><div><small>THE FIELD GUIDE / 001</small><h2>LET'S GET LOST.</h2></div><button onClick={() => setGuideOpen(false)} aria-label="Close guide">×</button></div>
      <div className="guide-grid"><div><b>01 / SEND SOME SOUND</b><p>Start the built-in demo, choose an audio file, or connect a live input. Use headphones with live input to avoid feedback.</p></div><div><b>02 / CATCH A MOMENT</b><p>The looper is always listening. Tap MICRO-LOOPER to catch the recent past; tap again to listen for something new. Hold while playing to overdub.</p></div><div><b>03 / MAKE SOME SPACE</b><p>Tap WET CHANNEL for reverb, delay or slip. Hold it to freeze. Routing chooses whether the wet side hears the input, the loop, or both.</p></div><div><b>04 / TURN THE CLOCK</b><p>Lower CLOCK for longer, grainier loops and pitch changes. Try the switches, drag knobs, or use arrow keys. Double-click a knob to reset it.</p></div></div>
      <div className="guide-disclaimer">This is an independent approximation, not the original firmware and not affiliated with Chase Bliss. Some hardware features are not implemented. <a href="https://www.chasebliss.com/mood-mkii" target="_blank" rel="noreferrer">About the original pedal ↗</a></div>
    </section>}
  </div>;
}

createRoot(document.getElementById('root')).render(<App/>);
