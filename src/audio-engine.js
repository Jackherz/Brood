import workletUrl from './brood-worklet.js?url';

// A small, local demo phrase. No sample download or external audio is needed.
function makeDemo(context) {
  const fs = context.sampleRate;
  const length = Math.floor(8 * fs);
  const samples = new Float32Array(length);
  let seed = 1234567;
  const random = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 4294967296; };
  const pluck = (time, note, amplitude, decay) => {
    const frequency = 440 * Math.pow(2, (note - 69) / 12);
    const size = Math.max(2, Math.round(fs / frequency));
    const delay = new Float32Array(size);
    let low = 0;
    for (let i = 0; i < size; i++) { low += 0.55 * (random() * 2 - 1 - low); delay[i] = low; }
    const loss = Math.pow(0.001, 1 / (frequency * decay));
    let index = 0;
    const first = Math.floor(time * fs);
    for (let i = 0; i < Math.floor(decay * fs); i++) {
      const next = (index + 1) % size;
      samples[(first + i) % length] += delay[index] * amplitude;
      delay[index] = loss * 0.5 * (delay[index] + delay[next]);
      index = next;
    }
  };
  const chords = [[40, 47, 55, 62, 66, 59], [48, 55, 59, 64, 67, 64], [43, 50, 59, 64, 67, 62], [42, 50, 57, 66]];
  chords.forEach((notes, chord) => notes.forEach((note, i) => pluck(chord * 2 + i / 3, note, i === 0 ? 0.32 : 0.24, i === 0 ? 3.5 : 2.2)));
  let peak = 0;
  for (let i = 0; i < length; i++) peak = Math.max(peak, Math.abs(samples[i]));
  if (peak) for (let i = 0; i < length; i++) samples[i] *= 0.5 / peak;
  const buffer = context.createBuffer(1, length, fs);
  buffer.copyToChannel(samples, 0);
  return buffer;
}

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
    this.demoBuffer = null;
    this.sourceVersion = 0;
    this.inputLevel = 0.8;
    this.sentParams = {};
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
    this.stopSource();
    if (!this.context || this.context.state !== 'running') return;
    if (kind === 'live') {
      if (!navigator.mediaDevices?.getUserMedia) throw new Error('Live input is unavailable here. Try a secure browser or choose the demo.');
      const stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
      if (version !== this.sourceVersion || !this.running) { stream.getTracks().forEach((track) => track.stop()); return; }
      this.stream = stream;
      this.source = this.context.createMediaStreamSource(stream);
      this.source.connect(this.gain);
      return;
    }
    const buffer = kind === 'file' ? this.fileBuffer : (this.demoBuffer ||= makeDemo(this.context));
    if (!buffer) throw new Error('Choose an audio file first.');
    const source = this.context.createBufferSource();
    source.buffer = buffer;
    source.loop = true;
    source.connect(this.gain);
    source.start();
    this.source = source;
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
    if (this.context && this.context.state !== 'closed') await this.context.close();
    this.context = null;
    this.node = null;
  }
}
