import workletUrl from './brood-worklet.js?url';

// The demo loops: Jack Herz's recordings, cut to whole bars, in assets/demos (fetched by
// scripts/fetch-demos.sh, never committed). Each is fetched only when it is first played, so
// the page stays light. The order is the plug-in's (kDemos in native/plugin/PluginProcessor.cpp).
export const DEMOS = [
  { id: 'ambiotica', name: 'Ambiotica', detail: 'ambient pad, 128 bpm', bpm: 128 },
  { id: 'feedback-ambience', name: 'Feedback ambience', detail: 'drifting feedback, 128 bpm', bpm: 128 },
  { id: 'tela-tape-loop', name: 'Tela tape loop', detail: 'D dorian tape loop, 97 bpm', bpm: 97 },
  { id: 'habit-and-keys-1', name: 'Habit and keys 1', detail: 'keys, 64 bpm', bpm: 64 },
  { id: 'habit-and-keys-2', name: 'Habit and keys 2', detail: 'keys, 64 bpm', bpm: 64 },
  { id: 'chroma-rhodes', name: 'Chroma Rhodes', detail: 'chill house chords, 60 bpm', bpm: 60 },
  { id: 'nylon-tines', name: 'Nylon tines', detail: 'A flat minor, 70 bpm', bpm: 70 },
];
const demoUrl = (id) => new URL(`../assets/demos/${id}.flac`, import.meta.url).href;

export class AudioEngine {
  constructor(onMessage, onError) {
    this.onMessage = onMessage;
    this.onError = onError;
    this.context = null;
    this.node = null;
    this.source = null;
    this.stream = null;
    this.sourceKind = 'demo';
    this.fileBuffer = null;
    this.demoId = DEMOS[0].id;
    this.demoBuffers = new Map();   // id -> Promise<AudioBuffer>
    this.sourceVersion = 0;
    this.inputLevel = 0.8;
    this.sentParams = {};
    // Songs a host page offers (setSongs): played by streaming through one audio element, never
    // decoded whole, so a long song costs no more memory than a short one.
    this.songs = [];
    this.songEl = null;
    this.songNode = null;
    this.songCue = null;   // where a song starts when it next plays, in seconds
    this.fade = null;      // every source passes through this, so changes fade instead of click
  }

  setSongs(list) { this.songs = Array.isArray(list) ? list : []; }
  songFor(id) { return this.songs.find((song) => song.id === id) || null; }
  songElement() {
    if (!this.songEl) {
      const el = new Audio();
      el.preload = 'auto';
      el.loop = true;
      this.songEl = el;
    }
    return this.songEl;
  }
  // Where the chosen song is, for the song bar.
  songState() {
    const song = this.songFor(this.demoId);
    if (!song) return null;
    const el = this.songEl, here = el && el.dataset.id === song.id;
    return {
      id: song.id,
      time: this.songCue != null ? this.songCue : here ? el.currentTime : 0,
      duration: here && Number.isFinite(el.duration) ? el.duration : song.duration || 0,
      playing: !!here && !el.paused,
    };
  }
  // Jump in the song: a quick fade either side, so the jump doesn't click.
  async seekSong(seconds) {
    const song = this.songFor(this.demoId), el = this.songEl;
    if (!song) return;
    if (!el || el.dataset.id !== song.id || el.paused || !this.running) { this.songCue = Math.max(0, seconds); return; }
    await this.fadeOut();
    el.currentTime = Math.max(0, Math.min(seconds, (el.duration || seconds + 1) - 0.05));
    this.fadeIn();
  }
  async fadeOut() {
    if (!this.fade || !this.source || !this.running) return;
    const g = this.fade.gain, now = this.context.currentTime;
    g.cancelScheduledValues(now); g.setValueAtTime(g.value, now); g.linearRampToValueAtTime(0, now + 0.012);
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  fadeIn() {
    if (!this.fade || !this.context) return;
    const g = this.fade.gain, now = this.context.currentTime;
    g.cancelScheduledValues(now); g.setValueAtTime(0, now); g.linearRampToValueAtTime(1, now + 0.012);
  }

  get running() { return this.context?.state === 'running'; }

  async create(params) {
    const Context = window.AudioContext || window.webkitAudioContext;
    if (!Context) throw new Error('Web Audio is not available in this browser.');
    const context = new Context({ latencyHint: 'interactive' });
    try {
      if (!context.audioWorklet) throw new Error('AudioWorklet needs a secure browser context (HTTPS or localhost).');
      await context.audioWorklet.addModule(workletUrl);
      const node = new AudioWorkletNode(context, 'brood-processor', {
        numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
        channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'speakers',
      });
      const gain = context.createGain();
      const fade = context.createGain();
      fade.connect(gain);
      this.fade = fade;
      const analyser = context.createAnalyser();
      const output = context.createGain();
      gain.gain.value = this.inputLevel;
      analyser.fftSize = 512;
      output.gain.value = 0.8;
      gain.connect(node);
      node.connect(analyser);
      analyser.connect(output);
      output.connect(context.destination);
      node.port.onmessage = (event) => this.onMessage?.(event.data);
      node.onprocessorerror = () => this.onError?.('Audio processing stopped. Refresh the page and try again.');
      this.context = context;
      this.node = node;
      this.sentParams = {};
      this.gain = gain;
      this.analyser = analyser;
      this.output = output;
      this.setParams(params);
      // On this page a playing loop is heard alone: the worklet fades the dry side out under
      // it (30 ms) and back in when it stops. Done there, after the looper has heard the
      // input, so Brood keeps listening and the next catch still has something to keep.
      node.port.postMessage({ type: 'soloLoop', value: 1 });
    } catch (error) {
      await context.close().catch(() => {});
      throw error;
    }
  }

  async start(params) {
    if (!this.context) await this.create(params);
    this.setParams(params);
    await this.context.resume();
    await this.connectSource(this.sourceKind);
  }

  async pause() {
    if (!this.context) return;
    this.sourceVersion++;
    await this.fadeOut();
    this.stopSource(); // release the microphone while paused
    await this.context.suspend();
  }

  setParams(params) {
    if (!this.node) return;
    for (const [name, value] of Object.entries(params)) {
      if (this.sentParams[name] !== value) {
        this.node.port.postMessage({ type: 'param', name, value });
        this.sentParams[name] = value;
      }
    }
  }
  footswitch(which, action) { this.node?.port.postMessage({ type: 'fs', which, action }); }
  // OVERDUB latches (it isn't one of the saved settings); CLEAR and UNDO act once.
  overdub(on) { this.node?.port.postMessage({ type: 'param', name: 'overdubLatch', value: on ? 1 : 0 }); }
  clearLoop() { this.node?.port.postMessage({ type: 'clear' }); }
  undoLoop() { this.node?.port.postMessage({ type: 'undo' }); }
  // RESET: the wet side off, the take cleared with nothing left to undo. Safe before audio starts.
  resetAll() { this.node?.port.postMessage({ type: 'reset' }); }
  setInputLevel(value) {
    this.inputLevel = value;
    if (this.gain && this.context) this.gain.gain.setTargetAtTime(value, this.context.currentTime, 0.01);
  }

  stopSource() {
    if (this.source) {
      try { this.source.stop?.(); } catch (_) { /* media stream source, or already stopped */ }
      try { this.source.disconnect(); } catch (_) { /* already disconnected */ }
      this.source = null;
    }
    if (this.stream) { this.stream.getTracks().forEach((track) => track.stop()); this.stream = null; }
  }

  async connectSource(kind) {
    this.sourceKind = kind;
    const version = ++this.sourceVersion;
    await this.fadeOut();   // the old source fades out first: no click at the change
    if (version !== this.sourceVersion) return;
    this.stopSource();
    if (!this.context || this.context.state !== 'running') return;
    const into = this.fade || this.gain;
    if (kind === 'live') {
      if (!navigator.mediaDevices?.getUserMedia) throw new Error('Live input is unavailable here. Try a secure browser or choose the demo.');
      const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
      if (version !== this.sourceVersion || !this.running) { stream.getTracks().forEach((track) => track.stop()); return; }
      this.stream = stream;
      this.source = this.context.createMediaStreamSource(stream);
      this.source.connect(into);
      this.fadeIn();
      return;
    }
    const song = kind === 'demo' ? this.songFor(this.demoId) : null;
    if (song) {
      const el = this.songElement();
      if (!this.songNode) this.songNode = this.context.createMediaElementSource(el);
      if (el.dataset.id !== song.id) { el.dataset.id = song.id; el.src = song.src; }
      if (el.readyState < 1) {
        await new Promise((resolve) => {
          const done = () => { el.removeEventListener('loadedmetadata', done); el.removeEventListener('error', done); resolve(); };
          el.addEventListener('loadedmetadata', done); el.addEventListener('error', done);
          setTimeout(done, 8000);
        });
      }
      if (version !== this.sourceVersion || !this.running) return;
      if (el.error) throw new Error('Could not load that song. Check your connection and try again.');
      if (this.songCue != null) { el.currentTime = this.songCue; this.songCue = null; }
      this.songNode.connect(into);
      try { await el.play(); }
      catch (_) { this.songNode.disconnect(); throw new Error('The song could not start here. Press START AUDIO to play it.'); }
      if (version !== this.sourceVersion) { el.pause(); this.songNode.disconnect(); return; }
      this.source = { stop: () => el.pause(), disconnect: () => this.songNode.disconnect() };
      this.fadeIn();
      return;
    }
    let buffer = this.fileBuffer;
    if (kind === 'demo' && !DEMOS.some((demo) => demo.id === this.demoId)) this.demoId = DEMOS[0].id;   // a song no longer offered
    if (kind === 'demo') {
      buffer = await this.loadDemo(this.demoId);
      if (version !== this.sourceVersion || !this.running) return;
    }
    if (!buffer) throw new Error('Choose an audio file first.');
    const source = this.context.createBufferSource();
    source.buffer = buffer;
    source.loop = true;
    source.connect(into);
    source.start();
    this.source = source;
    this.fadeIn();
  }

  loadDemo(id) {
    if (!this.demoBuffers.has(id)) {
      const decoder = this.context || new OfflineAudioContext(2, 1, 44100);
      const loading = fetch(demoUrl(id))
        .then((response) => { if (!response.ok) throw new Error(); return response.arrayBuffer(); })
        .then((data) => decoder.decodeAudioData(data))
        .catch(() => { this.demoBuffers.delete(id); throw new Error('Could not load that demo. Check your connection and try again.'); });
      this.demoBuffers.set(id, loading);
    }
    return this.demoBuffers.get(id);
  }

  // Picking a demo while audio runs swaps it straight away; otherwise it plays on START AUDIO.
  async chooseDemo(id) {
    if (!DEMOS.some((demo) => demo.id === id) && !this.songFor(id)) return;
    this.demoId = id;
    if (this.running) await this.connectSource('demo');
  }

  async chooseFile(file) {
    if (!file) return;
    if (file.size > 100 * 1024 * 1024) throw new Error('Please choose an audio file under 100 MB.');
    const data = await file.arrayBuffer();
    // Decode without starting audio; the user may choose a file before pressing play.
    const decoder = this.context || new OfflineAudioContext(2, 1, 44100);
    try { this.fileBuffer = await decoder.decodeAudioData(data); }
    catch (_) { throw new Error('Could not decode this file. Try WAV, AIFF, MP3 or another supported format.'); }
    await this.connectSource('file');
  }

  async dispose() {
    this.sourceVersion++;
    this.stopSource();
    if (this.songEl) { this.songEl.pause(); this.songEl.removeAttribute('src'); }
    if (this.context && this.context.state !== 'closed') await this.context.close();
    this.context = null;
    this.node = null;
  }
}
