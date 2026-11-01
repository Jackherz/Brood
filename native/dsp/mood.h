// Mood emulator DSP core, C++ port of src/mood-worklet.js (the reference).
//
// Header-only, C++17, no exceptions/RTTI. All buffers are allocated in prepare();
// process() never allocates, locks, or throws, so it is safe on an audio thread.
// Scalars are double and buffers float, matching the JS (Number / Float32Array),
// so the two implementations agree to float precision (see tests/parity).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mood {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 2.0 * kPi;
constexpr double kHoldSec = 0.35;          // footswitch hold threshold
constexpr int kLoopSamples = 64000;        // pedal capacity: CLOCK sets loop length (1 s @ 64 kHz … 32 s @ 2 kHz)
// The buffer is allocated for the longest loop the panel allows; MEMORY caps how
// much of it a capture may use, so the pedal default still applies at 0. Two
// cores (stereo) therefore hold about 33 MB of loop memory.
constexpr int kMaxLoopSeconds = 32;
constexpr int kMaxLoopSamples = kLoopSamples * kMaxLoopSeconds;
constexpr double kClockMinHz = 2000.0;
constexpr double kClockMaxHz = 64000.0;
constexpr double kCoreFs = 64000.0;
// TONE is a hi-cut on the wet bus: a fixed 2.6 kHz one-pole, blended in by amount.
constexpr double kToneHz = 2600.0;
constexpr double kDelayMaxSec = 2.1;
constexpr double kSlipBufSec = 5.5;
constexpr double kWeaveBufSec = 3.0;   // Fieldtone Weaver captures a couple of seconds; three gives the threads room
constexpr double kWeavePitch[] = {0, 7, -5, 12, -12, 4, -17, 19};   // root-sympathetic intervals, semitones


inline double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double expMap(double t, double a, double b) { return a * std::pow(b / a, t); }
inline double clockHz(double knob, bool smooth = false) {
    knob = clampd(knob, 0, 1);
    static constexpr double steps[] = {2000, 3000, 4000, 6000, 8000, 12000, 16000, 24000, 32000, 48000, 64000};
    return smooth ? expMap(knob, kClockMinHz, kClockMaxHz)
                  : steps[static_cast<int>(std::floor(knob * 10 + 0.5))];
}
inline double sgn(double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0; }
inline double jsRound(double x) { return std::floor(x + 0.5); }

// Hidden options (on the pedal you hold both footswitches to reach these). The two
// sides can run on different clock divisions, so the wet side and the loop can
// drift apart in time and pitch. Mirrors divMap in src/mood-worklet.js.
inline int divMap(double knob) {
    static const int steps[3] = {1, 2, 4};
    return steps[static_cast<size_t>(jsRound(clampd(knob, 0, 1) * (3 - 1)))];
}

// ───── RAMPS ─────
// Each channel has a function generator that can drive one control. The first
// position is off; MODIFY means this channel's own MODIFY knob. Mirrors the
// RAMP_* helpers in src/mood-worklet.js.
constexpr int kRampTargetCount = 7;
constexpr int kRampWaveCount = 5;
inline const char* const kRampTargets[kRampTargetCount] = {"OFF", "TIME", "MIX", "LENGTH", "MODIFY", "CLOCK", "BALANCE"};
inline const char* const kRampWaves[kRampWaveCount] = {"TRI", "SQR", "SIN", "RND", "SMTH"};
inline int rampTargetIndex(double k) { return static_cast<int>(jsRound(clampd(k, 0, 1) * (kRampTargetCount - 1))); }
inline int rampWaveIndex(double k) { return static_cast<int>(jsRound(clampd(k, 0, 1) * (kRampWaveCount - 1))); }
inline double rampHertz(double k) { return expMap(clampd(k, 0, 1), 0.05, 8); }   // one cycle every 20 s … eight a second
constexpr double kRampDepth = 0.5;
inline double jsMod(double a, double n) { return std::fmod(a, n); }   // JS % on doubles

// Tempo sync for time-based effects. The knob becomes a musical division
// instead of a free time; the host supplies the tempo in the plugin and the
// front panel supplies it in the browser. Mirrors SYNC_DIVISIONS in the JS.
struct SyncDivision { double beats; const char* label; };
constexpr SyncDivision kSyncDivisions[] = {
    {1.0 / 8, "1/32"}, {1.0 / 6, "1/16T"}, {1.0 / 4, "1/16"}, {1.0 / 3, "1/8T"}, {1.0 / 2, "1/8"},
    {3.0 / 4, "1/8D"}, {1.0, "1/4"}, {3.0 / 2, "1/4D"}, {2.0, "1/2"}, {4.0, "1/1"},
};
constexpr int kSyncCount = static_cast<int>(sizeof(kSyncDivisions) / sizeof(kSyncDivisions[0]));
inline int syncIndex(double knob) { return static_cast<int>(jsRound(clampd(knob, 0, 1) * (kSyncCount - 1))); }
inline const char* syncLabel(double knob) { return kSyncDivisions[syncIndex(knob)].label; }
inline double syncSeconds(double bpm, double knob) {
    return clampd(kSyncDivisions[syncIndex(knob)].beats * 60.0 / clampd(bpm, 20, 300), 0.01, kDelayMaxSec * 0.95);
}

// Tape follows octave steps, reverse on the left / forward on the right.
inline double speedMap(double m, double /* legacy snap parameter */ = 0) {
    const double distance = std::min(1.0, std::fabs(m - 0.5) * 2);
    return (m < 0.5 ? -1.0 : 1.0) * std::pow(2.0, jsRound(-1 + 3 * distance));
}

inline double slipSpeedMap(double m) {
    const double distance = std::min(1.0, std::fabs(m - 0.5) * 2);
    return (m < 0.5 ? -1.0 : 1.0) * std::pow(2.0, jsRound((-1 + 2 * distance) * 12) / 12);
}

inline double stretchMap(double m) {
    const double d = m - 0.5;
    if (std::fabs(d) < 0.03) return 0.0;
    const double t = (std::fabs(d) - 0.03) / 0.47;
    return sgn(d) * std::pow(2.0, -3.0 + 3.0 * t);
}

inline double softClip(double x) {
    const double ax = std::fabs(x);
    if (ax < 0.9) return x;
    return sgn(x) * (0.9 + 0.1 * std::tanh((ax - 0.9) / 0.1));
}

struct LowPass {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(double fc, double fs, double q) {
        const double w = (kTau * clampd(fc, 10, fs * 0.49)) / fs;
        const double cw = std::cos(w), alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
        b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0; b2 = b0;
        a1 = (-2 * cw) / a0; a2 = (1 - alpha) / a0;
    }
    double run(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct LowPass4 {
    LowPass a, b;
    void set(double fc, double fs) { a.set(fc, fs, 0.5412); b.set(fc, fs, 1.3066); }
    double run(double x) { return b.run(a.run(x)); }
};

struct Allpass {
    std::vector<float> buf; int i = 0; double g = 0;
    void init(int len, double gain) { buf.assign(static_cast<size_t>(std::max(1, len)), 0.f); i = 0; g = gain; }
    double run(double x) {
        const double d = buf[static_cast<size_t>(i)];
        const double v = x + g * d;
        buf[static_cast<size_t>(i)] = static_cast<float>(v);
        if (++i >= static_cast<int>(buf.size())) i = 0;
        return d - g * v;
    }
};

inline double readFrac(const std::vector<float>& buf, double pos) {
    const double n = static_cast<double>(buf.size());
    double p = jsMod(pos, n); if (p < 0) p += n;
    const int i0 = static_cast<int>(std::floor(p)); const double f = p - i0;
    const int i1 = i0 + 1 == static_cast<int>(buf.size()) ? 0 : i0 + 1;
    const double a = buf[static_cast<size_t>(i0)], b = buf[static_cast<size_t>(i1)];
    return a + (b - a) * f;
}

// ───────────────────────── Wet channel: REVERB ─────────────────────────
struct Reverb {
    std::vector<float> lines[8];
    int len[8] = {}, w[8] = {};
    float lp[8] = {}, x[8] = {}, g[8] = {};
    double modPh[8] = {}, modRate[8] = {};
    Allpass diff[4];
    double fs = 48000, t60 = -1, size = 1, out[2] = {0, 0};

    void init(double sr) {
        fs = sr; t60 = -1; size = 1;
        const double k = fs / 48000.0;
        const int base[8] = {1031, 1327, 1523, 1789, 2053, 2311, 2617, 2963};
        const double rates[8] = {0.31, 0.43, 0.53, 0.61, 0.37, 0.47, 0.57, 0.67};
        for (int i = 0; i < 8; ++i) {
            len[i] = static_cast<int>(jsRound(base[i] * k));
            lines[i].assign(static_cast<size_t>(len[i] + 64), 0.f);
            w[i] = 0; lp[i] = 0; x[i] = 0; g[i] = 0;
            modPh[i] = i * 0.77; modRate[i] = (kTau * rates[i]) / fs;
        }
        const int dl[4] = {142, 107, 379, 277};
        for (int i = 0; i < 4; ++i) diff[i].init(static_cast<int>(jsRound(dl[i] * k)), 0.7);
    }
    void setDecay(double t, double roomSize) {
        if (std::fabs(t - t60) < 1e-4 && std::fabs(roomSize - size) < 1e-4) return;
        t60 = t; size = roomSize;
        for (int i = 0; i < 8; ++i) g[i] = static_cast<float>(std::pow(10.0, (-3.0 * len[i] * size) / (t * fs)));
    }
    // modify = smear: CCW clear and still, CW washed out
    const double* tick(double input, double modify, bool freeze, bool infinite) {
        double d = input * 0.35;
        const double dg = 0.45 + 0.3 * modify;
        for (auto& a : diff) { a.g = dg; d = a.run(d); }
        const double damp = freeze ? 1.0 : 1.0 - std::exp((-kTau * expMap(modify, 9000, 3500)) / fs);
        const double depth = 1 + modify * modify * 18 * (fs / 48000.0);
        double sum = 0;
        for (int i = 0; i < 8; ++i) {
            modPh[i] += modRate[i];
            if (modPh[i] > kTau) modPh[i] -= kTau;
            const double delay = len[i] * size - depth * (1 + std::sin(modPh[i]));
            const double v = readFrac(lines[i], w[i] - delay);
            lp[i] = static_cast<float>(lp[i] + damp * (v - lp[i]));
            const double gg = freeze || infinite ? 1.0 : g[i];
            x[i] = static_cast<float>(lp[i] * gg);
            sum += x[i];
        }
        const double h = sum * 0.25;   // Householder reflection
        double l = 0, r = 0;
        for (int i = 0; i < 8; ++i) {
            const double y = x[i] - h;
            lines[i][static_cast<size_t>(w[i])] = static_cast<float>(y + (freeze ? 0.0 : d));
            if (++w[i] >= static_cast<int>(lines[i].size())) w[i] = 0;
            if (i & 1) r += (i & 2) ? -x[i] : x[i];
            else l += (i & 2) ? -x[i] : x[i];
        }
        out[0] = l * 0.6; out[1] = r * 0.6;
        return out;
    }
};

// ───────────────────────── Wet channel: DELAY ─────────────────────────
struct Delay {
    std::vector<float> buf; int w = 0;
    double fs = 48000, d = -1, nextD = -1, fade = 1, lp = 0, hp = 0, lpA = 0, hpA = 0;
    void init(double sr) {
        fs = sr;
        buf.assign(static_cast<size_t>(std::ceil(kDelayMaxSec * fs) + 8), 0.f);
        w = 0; d = -1; nextD = -1; fade = 1; lp = hp = 0;
        lpA = 1 - std::exp((-kTau * 6500) / fs);
        hpA = 1 - std::exp((-kTau * 70) / fs);
    }
    double tick(double input, double delaySamples, double modify, bool freeze) {
        if (d < 0) d = delaySamples;
        // Crossfade between fixed heads; changing TIME does not tape-glide pitch.
        if (fade >= 1 && std::fabs(delaySamples - d) > fs * 0.0005) {
            nextD = delaySamples; fade = 0;
        }
        double y = readFrac(buf, w - d);
        if (fade < 1) {
            const double next = readFrac(buf, w - nextD);
            y += (next - y) * fade;
            fade += 1 / (fs * 0.03);
            if (fade >= 1) { d = nextD; fade = 1; }
        }
        lp += lpA * (y - lp);
        hp += hpA * (lp - hp);
        const double fbSig = freeze || modify > 0.985 ? y : lp - hp;
        const double fb = freeze || modify > 0.985 ? 1.0 : modify;
        double v = (freeze ? 0.0 : input) + fbSig * fb;
        if (v > 1 || v < -1) v = std::tanh(v);
        buf[static_cast<size_t>(w)] = static_cast<float>(v);
        if (++w >= static_cast<int>(buf.size())) w = 0;
        return y;
    }
};

// ───────────────────────── Wet channel: SLIP ──────────────────────────
struct Slip {
    struct Head { double pos = 0, g = 0; int dir = 0; };
    std::vector<float> buf; int w = 0;
    Head heads[2]; int cur = 0;
    double fs = 48000, count = 1e9, lastStart = 0, speed = 1;
    void init(double sr) {
        fs = sr;
        buf.assign(static_cast<size_t>(std::ceil(kSlipBufSec * fs)), 0.f);
        w = 0; heads[0] = heads[1] = Head{}; cur = 0; count = 1e9; lastStart = 0; speed = 1;
    }
    double tick(double input, double sliceLen, double speedTarget, bool freeze) {
        const int n = static_cast<int>(buf.size());
        if (!freeze) { buf[static_cast<size_t>(w)] = static_cast<float>(input); if (++w >= n) w = 0; }
        speed += (speedTarget - speed) * 0.002;
        const double s = speed;
        const double xf = std::max(32.0, std::min(0.25 * sliceLen, 0.03 * fs));
        if (++count >= sliceLen) {
            count = 0;
            const double margin = xf + 64;
            double start;
            if (freeze) start = lastStart;
            else { start = w - (margin + std::max(0.0, s - 1) * (sliceLen + xf)); lastStart = start; }
            cur ^= 1;
            heads[cur].pos = start; heads[cur].g = 0; heads[cur].dir = 1;
            heads[cur ^ 1].dir = -1;
        }
        const double step = 1 / xf;
        double out = 0;
        for (auto& h : heads) {
            if (h.dir > 0) h.g = std::min(1.0, h.g + step);
            else if (h.dir < 0) h.g = std::max(0.0, h.g - step);
            if (h.g > 0) { out += readFrac(buf, h.pos) * std::sin(h.g * kPi * 0.5); h.pos += s; }
        }
        return out;
    }
};


// ──────────────────────────── Wet channel: WEAVE ───────────────────────
// Re-sampler in the spirit of the Fieldtone Weaver: keep a few seconds of what
// just happened, then let three threads read it back at once. Each thread has
// its own pitch, direction, grain size and dropout, all rolled from one seeded
// pattern. TIME chooses how far back the threads may reach; MODIFY walks the
// pattern from long overlapping cloth (texture) into short clipped stitches
// (rhythm). The pattern is re-rolled only when the integer position of the knobs
// changes, so the same knob position always draws the same fabric — chance, but
// repeatable, and no undo. Mirrors the JS Weave class in src/mood-worklet.js.
struct Weave {
    struct Thread { double pos = 0, ph = 0, rate = 1, pan = 0, amp = 1; int dir = 1; };
    std::vector<float> buf; int w = 0;
    uint32_t rng = 0x5eed; int seedKey = -1;
    Thread th[3];
    double fs = 48000;
    void init(double sr) {
        fs = sr;
        buf.assign(static_cast<size_t>(std::ceil(kWeaveBufSec * fs)), 0.f);
        w = 0; rng = 0x5eed; seedKey = -1;
        static constexpr double pan[3] = {-0.7, 0, 0.7};
        for (int i = 0; i < 3; ++i) th[i] = Thread{-fs * (0.25 + 0.3 * i), i / 3.0, 1, pan[i], 1, 1};
    }
    double rand01() { rng = rng * 1103515245u + 12345u; return rng / 4294967296.0; }
    void reroll(int key, double reachSec) {
        if (key == seedKey) return;
        seedKey = key;
        for (auto& t : th) {
            const int semis = static_cast<int>(kWeavePitch[static_cast<int>(rand01() * 8)]);
            t.rate = std::pow(2.0, semis / 12.0);
            t.dir = rand01() < 0.3 ? -1 : 1;
            t.amp = rand01() < 0.85 ? 1.0 : 0.45;
            t.pos = w - fs * (0.15 + rand01() * 0.85 * std::max(0.2, reachSec));
        }
    }
    // out[0], out[1] are left/right.
    void tick(double input, double reachSec, double modify, bool frozen, double* out) {
        const int n = static_cast<int>(buf.size());
        if (!frozen) { buf[static_cast<size_t>(w)] = static_cast<float>(input); if (++w >= n) w = 0; }
        const int key = static_cast<int>(std::floor(clampd(reachSec, 0, 3) * 8)) * 16 +
                        static_cast<int>(std::floor(clampd(modify, 0, 1) * 16));
        reroll(key, reachSec);

        const double grain = std::max(64.0, expMap(1 - clampd(modify, 0, 1), 0.02, 0.55) * fs);
        const double chance = 0.55 * clampd(modify, 0, 1);
        double l = 0, r = 0;
        for (auto& t : th) {
            t.ph += 1 / grain;
            if (t.ph >= 1) {
                t.ph -= 1;
                // A new stitch: maybe move somewhere else in the cloth, maybe not.
                if (rand01() < 0.45) t.pos = w - fs * (0.12 + rand01() * std::max(0.2, reachSec));
                // Dropouts are partial, so a thread thins out instead of vanishing.
                t.amp = rand01() < chance * 0.35 ? 0.0 : 0.45 + 0.55 * rand01();
            }
            const double win = std::sin(t.ph * kPi);
            const double v = readFrac(buf, t.pos) * win * win * t.amp;
            t.pos += t.rate * t.dir;
            const double a = (t.pan + 1) * kPi / 4;
            l += v * std::cos(a);
            r += v * std::sin(a);
        }
        out[0] = l * 0.7; out[1] = r * 0.7;
    }
};

// ─────────────────────────────── Core ─────────────────────────────────
enum class LoopState { Off, Play };
enum class LoopLed { Off, Play, Rec, Dub };

struct Params {
    double time = 0.5, mix = 0.5, length = 0.75, modWet = 0.5, clock = 1, modLoop = 0.5;
    int wetMode = 0, routing = 0, loopMode = 0;   // reverb/delay/slip/weave, in/in+loop/loop, env/tape/stretch
    bool trails = false, dryKill = false, latch = false, spread = false, smooth = false;
    bool sync = false;
    double balance = 0.5, bpm = 120.0;
    double loopEnd = 1, memory = 0;                 // playback trim + capture ceiling
    double tone = 1, fade = 0, width = 0.5;          // hidden: wet hi-cut, overdub decay, stereo width
    double wetDiv = 0, loopDiv = 0;                  // hidden: per-side clock division
    bool classic = false;                            // hidden: MK I quirks
    double rampWet = 0, rampLoop = 0;                // RAMPS destination per channel
    double rampSpeed = 0.45, rampWave = 0;           // rate and shape
    bool rampUni = false, bounce = false, sweep = false;
};

struct State {
    bool wetOn, freeze; LoopLed loop;
    int lsrc, winStart, winLen; double pos, clockHz; uint32_t histCycles;
    float peakIn, peakOut;
};

class Core {
public:
    Params p;

    void prepare(double sampleRate) {
        fs = sampleRate;
        s = Smoothed{p.time, p.mix, p.length, p.modWet, p.clock, p.modLoop, p.balance, p.tone, p.fade, p.width, p.rampSpeed};
        e = Eff{p.time, p.mix, p.length, p.modWet, p.clock, p.modLoop, p.balance};
        hostCount = 0; phase = 0; clockRatio = 1; filterClock = -1;
        aa = reconL = reconR = LowPass4{};
        prev[0] = prev[1] = cur[0] = cur[1] = 0;
        reverb.init(kCoreFs); delay.init(kCoreFs); slip.init(kCoreFs); weave.init(kCoreFs);
        wetOn = freeze = wetMomentary = false;
        wetIn = wetOut = 0; modeGain[0] = 1; modeGain[1] = modeGain[2] = modeGain[3] = 0;
        decor[0].init(static_cast<int>(jsRound(113 * kCoreFs / 48000)), 0.6);
        decor[1].init(static_cast<int>(jsRound(337 * kCoreFs / 48000)), 0.6);
        loopDirect = 1;
        hist.assign(kMaxLoopSamples, 0.f); loopBuf.assign(kMaxLoopSamples, 0.f);
        histW = histCount = 0; histCycles = 0; lsrc = 0;
        loopState = LoopState::Off; overdub = false;
        loopGain = 0; loopPos = 0; tapeSpeed = 1; stretchSpeed = 0;
        env = 0; envGate = false; envHold = 0; envMix = 0; sliceStart = slicePh = 0;
        grain[0] = {0, 0}; grain[1] = {0.5, 0};
        rng = 22222;
        toneL = toneR = 0; wowPh = 0; toneC = 0;
        fsWet = FsWet{}; fsLoop = FsLoop{};
        engaged = 0; peakIn = peakOut = 0;
        loopWaveDirty = false;
    }

    // which: 0 = wet, 1 = loop. Mirrors MoodCore.footswitch.
    void footswitch(int which, bool down) {
        const int64_t now = hostCount;
        if (which == 0) {
            if (down) {
                fsWet = FsWet{true, now, false};
                if (!wetOn) { wetOn = true; wetMomentary = true; } else wetMomentary = false;
            } else if (fsWet.active) {
                const bool held = fsWet.held;
                fsWet.active = false;
                if (held) {
                    if (!p.latch) { freeze = false; if (wetMomentary) wetOn = false; }
                } else if (!wetMomentary) { wetOn = false; freeze = false; }
                wetMomentary = false;
            }
            return;
        }
        if (down) {
            const bool playing = loopState == LoopState::Play;
            fsLoop = FsLoop{true, now, false, histW, histCycles, playing};
        } else if (fsLoop.active) {
            const FsLoop d = fsLoop;
            fsLoop.active = false;
            if (d.playing) {
                overdub = false;
                if (!d.held) loopState = LoopState::Off;
                else loopWaveDirty = true;
            } else if (d.held) {
                const auto cycles = static_cast<uint32_t>(histCycles - d.histCycle);
                const int64_t written = static_cast<int64_t>(cycles) * static_cast<int64_t>(hist.size()) + histW - d.histIdx;
                capture(static_cast<int>(std::min<int64_t>(static_cast<int64_t>(hist.size()) - 1, std::max<int64_t>(0, written))));
            } else capture(histCount);
        }
    }

    // Explicit freeze for MIDI CC; footswitch gestures remain momentary/latching.
    void setFreeze(bool active) { freeze = active && wetOn; }

    // in may be null (silence). Mirrors MoodCore.process.
    void process(const float* in, float* outL, float* outR, int n) {
        const double a = 1 - std::exp(-1 / (0.03 * fs));
        const double aClock = 1 - std::exp(-1 / ((p.smooth ? 1.5 : 0.08) * fs));
        const double holdN = kHoldSec * fs;
        if (fsWet.active && !fsWet.held && hostCount - fsWet.t >= holdN) {
            fsWet.held = true;
            freeze = p.latch ? !freeze : true;
        }
        if (fsLoop.active && !fsLoop.held && hostCount - fsLoop.t >= holdN) { fsLoop.held = true; if (fsLoop.playing) overdub = true; }

        double pkIn = 0, pkOut = 0;
        for (int i = 0; i < n; ++i) {
            s.time += (p.time - s.time) * a;
            s.mix += (p.mix - s.mix) * a;
            s.length += (p.length - s.length) * a;
            s.modWet += (p.modWet - s.modWet) * a;
            s.modLoop += (p.modLoop - s.modLoop) * a;
            s.balance += (p.balance - s.balance) * a;
            s.clock += (p.clock - s.clock) * aClock;
            s.rampSpeed += (p.rampSpeed - s.rampSpeed) * a;
            rampTick();
            applyRamps(a);
            s.tone += (p.tone - s.tone) * a;
            s.fade += (p.fade - s.fade) * a;
            s.width += (p.width - s.width) * a;
            if ((i & 31) == 0) {
                const double c = clockHz(e.clock, p.smooth) / fs;
                clockRatio = c;
                if (std::fabs(c - filterClock) > 0.002 * c) {
                    filterClock = c;
                    const double fc = std::min({0.45 * c * fs, 0.45 * fs, 20000.0});
                    aa.set(fc, fs); reconL.set(fc, fs); reconR.set(fc, fs);
                }
            }
            const double dry = in ? in[i] : 0.0;
            pkIn = std::max(pkIn, std::fabs(dry));
            const double xa = aa.run(dry);
            phase += clockRatio;
            while (phase >= 1) { phase -= 1; coreTick(xa); }
            const double f = phase;
            double el = reconL.run(prev[0] + (cur[0] - prev[0]) * f);
            double er = reconR.run(prev[1] + (cur[1] - prev[1]) * f);
            // STEREO WIDTH: mid/side on the effect bus only — the dry signal must
            // not move. 0.5 is the image as built, so that setting is untouched.
            if (std::fabs(s.width - 0.5) > 0.002) {
                const double mid = (el + er) * 0.5, side = (el - er) * 0.5 * (s.width * 2);
                el = mid + side; er = mid - side;
            }
            const double engagedT = wetOn || loopState == LoopState::Play ? 1.0 : 0.0;
            engaged += (engagedT - engaged) * 0.001;
            const double dg = p.dryKill ? 0.0 : 1 - engaged * (1 - std::min(1.0, 2 * (1 - e.mix)));
            const double wg = std::min(1.0, 2 * e.mix);
            double l = dry * dg + el * wg;
            double r = dry * dg + er * wg;
            const bool fullyBypassed = !p.dryKill && !p.trails && !wetOn && loopState == LoopState::Off && engaged < 1e-4;
            if (!fullyBypassed) { l = softClip(l); r = softClip(r); }
            outL[i] = static_cast<float>(l); outR[i] = static_cast<float>(r);
            pkOut = std::max(pkOut, std::max(std::fabs(l), std::fabs(r)));
            ++hostCount;
        }
        peakIn = std::max(pkIn, peakIn * 0.9);
        peakOut = std::max(pkOut, peakOut * 0.9);
    }

    State state() const {
        LoopLed led = loopState == LoopState::Play ? LoopLed::Play : LoopLed::Off;
        if (fsLoop.active && fsLoop.held && !fsLoop.playing) led = LoopLed::Rec;
        else if (overdub && loopState == LoopState::Play) led = LoopLed::Dub;
        const Window win = lsrc > 0 ? loopWindow() : Window{0, 0};
        return State{wetOn, freeze, led, lsrc, win.start, win.W, loopPos, clockRatio * fs, histCycles,
                     static_cast<float>(peakIn), static_cast<float>(peakOut)};
    }

    // Peak envelope of the captured loop, for drawing. Not real-time safe to call
    // concurrently with process(); the plugin copies under its own lock.
    void loopPeaks(float* out, int bins) const {
        for (int b = 0; b < bins; ++b) out[b] = 0;
        if (!lsrc) return;
        for (int b = 0; b < bins; ++b) {
            const int a0 = static_cast<int>((static_cast<int64_t>(b) * lsrc) / bins);
            const int z = std::max(a0 + 1, static_cast<int>((static_cast<int64_t>(b + 1) * lsrc) / bins));
            float pk = 0;
            for (int i = a0; i < z; i += 4) pk = std::max(pk, std::fabs(loopBuf[static_cast<size_t>(i)]));
            out[b] = pk;
        }
    }

    bool takeLoopWaveDirty() { const bool d = loopWaveDirty; loopWaveDirty = false; return d; }
    // Raw generator output (0..1), for the UI and the ramp tests.
    double rampValue(int which) const { return rampRaw[which & 1]; }
    // What the DSP is using right now (knob + any ramp), in RAMP_KEYS order:
    // 0 time, 1 mix, 2 length, 3 wet modify, 4 clock, 5 loop modify, 6 balance.
    double effective(int which) const {
        switch (which) {
            case 0: return e.time; case 1: return e.mix; case 2: return e.length;
            case 3: return e.modWet; case 4: return e.clock; case 5: return e.modLoop;
            default: return e.balance;
        }
    }
    bool envGateOpen() const { return envGate; }
    int capturedLength() const { return lsrc; }

private:
    struct Smoothed { double time, mix, length, modWet, clock, modLoop, balance, tone, fade, width, rampSpeed; };
    // What the DSP reads: the smoothed knobs plus whatever a ramp is moving.
    struct Eff { double time, mix, length, modWet, clock, modLoop, balance; };
    struct Ramp { double ph = 0, dir = 1, prev = 0.5, next = 0.5; uint32_t rng = 0; };
    struct FsWet { bool active = false; int64_t t = 0; bool held = false; };
    struct FsLoop { bool active = false; int64_t t = 0; bool held = false; int histIdx = 0; uint32_t histCycle = 0; bool playing = false; };
    struct Grain { double ph, start; };
    struct Window { int W, start; };

    double fs = 48000;
    Smoothed s{};
    Eff e{};
    int64_t hostCount = 0;
    double phase = 0, clockRatio = 1, filterClock = -1;
    LowPass4 aa, reconL, reconR;
    double prev[2] = {0, 0}, cur[2] = {0, 0};

    Reverb reverb; Delay delay; Slip slip; Weave weave;
    bool wetOn = false, freeze = false, wetMomentary = false;
    double wetIn = 0, wetOut = 0, modeGain[4] = {1, 0, 0, 0};
    Allpass decor[2];
    double loopDirect = 1;

    std::vector<float> hist, loopBuf;
    int histW = 0, histCount = 0, lsrc = 0;
    uint32_t histCycles = 0;
    LoopState loopState = LoopState::Off;
    bool overdub = false;
    double loopGain = 0, loopPos = 0, tapeSpeed = 1, stretchSpeed = 0;
    double env = 0, envHold = 0, envMix = 0, sliceStart = 0, slicePh = 0;
    bool envGate = false;
    Grain grain[2] = {{0, 0}, {0.5, 0}};
    uint32_t rng = 22222;
    FsWet fsWet; FsLoop fsLoop;
    double engaged = 0, peakIn = 0, peakOut = 0;
    bool loopWaveDirty = false;
    double toneL = 0, toneR = 0, wowPh = 0, toneC = 0;   // hidden-option state

    // Two generators, each with its own phase and its own seeded stream (never the
    // shared rng: STRETCH grains and CLASSIC hiss draw from that one).
    Ramp ramp[2] = {{0, 1, 0.5, 0.5, 0x9e3779b9u}, {0.5, 1, 0.5, 0.5, 0x5bd1e995u}};
    double rampRaw[2] = {0, 0};     // per host sample
    double rampVal[2] = {0, 0};     // smoothed to audio rate

    double rand01() { rng = rng * 1103515245u + 12345u; return rng / 4294967296.0; }

    // A second, private random stream per generator: the shared rng belongs to the
    // STRETCH grains and CLASSIC hiss, and drawing from it here would move them.
    double rampRand(Ramp& g) {
        g.rng = g.rng * 1664525u + 1013904223u;
        return g.rng / 4294967296.0;
    }

    // Value of the chosen shape at the current phase, 0..1.
    double rampShape(const Ramp& g, int wave) const {
        const double ph = g.ph;
        if (p.sweep) return ph;                                   // SWEEP: it only ever climbs
        if (wave == 1) return ph < 0.5 ? 1.0 : 0.0;               // square
        if (wave == 2) return 0.5 + 0.5 * std::sin(kTau * ph);
        if (wave == 3) return g.next;                             // sample and hold
        if (wave == 4) {                                          // smooth random
            const double t = ph * ph * (3 - 2 * ph);
            return g.prev + (g.next - g.prev) * t;
        }
        return 1 - std::fabs(2 * ph - 1);                         // triangle
    }

    // Advance both generators. Once per host sample, not per core tick: the ramp is
    // a time function, so turning CLOCK down must not slow it down. BOUNCE
    // ping-pongs the phase instead of letting it reset.
    void rampTick() {
        const double rate = rampHertz(s.rampSpeed) / fs;
        const int wave = rampWaveIndex(p.rampWave);
        for (int k = 0; k < 2; ++k) {
            Ramp& g = ramp[k];
            if (rampTargetIndex(k == 0 ? p.rampWet : p.rampLoop) == 0) { rampRaw[k] = 0; continue; }
            g.ph += rate * g.dir;
            if (g.ph >= 1) {
                if (p.bounce) { g.ph = 2 - g.ph; g.dir = -1; } else { g.ph -= 1; }
                g.prev = g.next; g.next = rampRand(g);
            } else if (g.ph <= 0) {
                if (p.bounce) { g.ph = -g.ph; g.dir = 1; } else { g.ph += 1; }
                g.prev = g.next; g.next = rampRand(g);
            }
            rampRaw[k] = rampShape(g, wave);
        }
    }

    // Rebuild the effective controls from the smoothed knobs, then let the ramps
    // push whichever control they are pointed at.
    void applyRamps(double a) {
        rampVal[0] += (rampRaw[0] - rampVal[0]) * a;
        rampVal[1] += (rampRaw[1] - rampVal[1]) * a;
        e.time = s.time; e.mix = s.mix; e.length = s.length; e.modWet = s.modWet;
        e.clock = s.clock; e.modLoop = s.modLoop; e.balance = s.balance;
        for (int k = 0; k < 2; ++k) {
            const int idx = rampTargetIndex(k == 0 ? p.rampWet : p.rampLoop);
            if (idx == 0) continue;
            const double v = rampVal[k];
            const double off = (p.rampUni ? v : v * 2 - 1) * kRampDepth;
            double* field = idx == 1 ? &e.time : idx == 2 ? &e.mix : idx == 3 ? &e.length
                            : idx == 4 ? (k == 1 ? &e.modLoop : &e.modWet) : idx == 5 ? &e.clock : &e.balance;
            *field = clampd(*field + off, 0, 1);
        }
    }

    // One-pole coefficient for the TONE hi-cut; the blend amount is the caller's.
    double toneCoeff() {
        if (toneC == 0) toneC = 1 - std::exp((-kTau * kToneHz) / kCoreFs);
        return toneC;
    }

    // How much history a capture may use, in core samples. MEMORY 0 keeps the
    // pedal's clock-derived capacity; above that it is 1–30 s of real time.
    int maxCaptureSamples() const {
        const double clock = clockHz(e.clock, p.smooth);
        const double user = p.memory > 0.001 ? expMap(clampd(p.memory, 0, 1), 1, 30) * clock : kLoopSamples;
        return static_cast<int>(std::min<double>(kMaxLoopSamples - 1, std::max(64.0, std::min(user, kMaxLoopSeconds * clock))));
    }

    void capture(int n) {
        n = std::min({n, static_cast<int>(hist.size()) - 1, histCount, maxCaptureSamples()});
        if (n < 64) return;
        const int N = static_cast<int>(hist.size());
        int r = histW - n; if (r < 0) r += N;
        for (int i = 0; i < n; ++i) { loopBuf[static_cast<size_t>(i)] = hist[static_cast<size_t>(r)]; if (++r >= N) r = 0; }
        lsrc = n; loopPos = 0;
        tapeSpeed = speedMap(e.modLoop, 0.04);
        stretchSpeed = stretchMap(e.modLoop);
        grain[0] = {0, 0}; grain[1] = {0.5, 0};
        loopState = LoopState::Play;
        loopWaveDirty = true;
    }

    // Playback region of the captured loop. LENGTH decides where the loop sits
    // (TAPE takes the tail of the capture, ENV and STRETCH play all of it); END
    // then decides how much of that window sounds before it wraps, so END only
    // moves the loop's end point. Mirrors loopWindow() in src/mood-worklet.js.
    Window loopWindow() const {
        const int len = std::max(64, lsrc);
        const double trim = clampd(p.loopEnd, 0.02, 1);
        if (p.loopMode != 1)
            return {std::max(64, static_cast<int>(std::floor(len * trim))), 0};
        const int W0 = std::max(64, static_cast<int>(std::floor(len * expMap(e.length, 0.02, 1))));
        return {std::max(64, static_cast<int>(std::floor(W0 * trim))), len - W0};
    }

    double loopTick(double x) {
        const double ax = std::fabs(x);
        env += (ax - env) * (ax > env ? 0.02 : 0.0005);

        const double target = loopState == LoopState::Play ? 1.0 : 0.0;
        loopGain += (target - loopGain) * 0.003;
        if (loopState == LoopState::Off && loopGain < 1e-4) { loopGain = 0; return 0; }
        if (lsrc < 64) return 0;

        const Window win = loopWindow();
        const int W = win.W, start = win.start;
        const double Wd = W;
        const int mode = p.loopMode;
        const double m = e.modLoop;
        // LOOP CLOCK DIVISION: the loop reads at 1/div of the clock, so a divided
        // loop plays back slower and lower and takes div times as long to come round.
        const double ldivd = divMap(p.loopDiv);
        const double lstep = 1 / ldivd;
        double pos = jsMod(loopPos, Wd); if (pos < 0) pos += Wd;
        auto rd = [&](double q0) {
            double q = jsMod(q0, Wd); if (q < 0) q += Wd;
            const int i0 = static_cast<int>(std::floor(q)); const double f = q - i0;
            const int i1 = i0 + 1 >= W ? 0 : i0 + 1;
            const double a = loopBuf[static_cast<size_t>(start + i0)], b = loopBuf[static_cast<size_t>(start + i1)];
            return a + (b - a) * f;
        };
        const double F = std::min(0.004 * kCoreFs, Wd / 4);
        auto edgeAt = [&](double q0) { double q = jsMod(q0, Wd); if (q < 0) q += Wd; return std::min({1.0, q / F, (Wd - q) / F}); };
        double y;

        if (mode == 2) {
            const double G = std::min(expMap(e.length, 0.01 * kCoreFs, 0.5 * kCoreFs), Wd);
            // A divided loop stretches each grain by div as well, or the grains
            // would still be read at full speed.
            const double Gs = G * ldivd;
            const double dir = stretchSpeed < 0 ? -1.0 : 1.0;
            y = 0;
            for (auto& g : grain) {
                g.ph += 1 / Gs;
                if (g.ph >= 1) { g.ph -= 1; g.start = pos + (rand01() - 0.5) * 0.2 * G; }
                const double q = g.start + dir * g.ph * G;
                const double sw = std::sin(kPi * g.ph);
                y += rd(q) * sw * sw * edgeAt(q);
            }
            const double normal = clampd((m - 0.97) / 0.03, 0, 1);
            y = y * (1 - normal) + rd(pos) * edgeAt(pos) * normal;
            stretchSpeed += (stretchMap(m) - stretchSpeed) * 0.001;
            pos += stretchSpeed * lstep;
        } else if (mode == 1) {
            y = rd(pos) * edgeAt(pos);
            tapeSpeed += (speedMap(m, 0.04) - tapeSpeed) * 0.00025;
            pos += tapeSpeed * lstep;
        } else {
            const double thr = expMap(1 - m, 0.008, 0.35);
            if (env > thr) {
                if (!envGate) { envGate = true; sliceStart = pos; slicePh = 0; }
                envHold = 0.08 * kCoreFs;
            } else if (envGate && --envHold <= 0) envGate = false;
            envMix += ((envGate ? 1.0 : 0.0) - envMix) * 0.005;
            const double mainV = rd(pos) * edgeAt(pos);
            double sl = 0;
            if (envMix > 1e-4) {
                const double S = std::min(expMap(e.length, 0.01 * kCoreFs, 1.0 * kCoreFs), Wd);
                if (slicePh >= S) slicePh -= S;
                const double F2 = std::min(0.004 * kCoreFs, S / 4);
                sl = rd(sliceStart + slicePh) * std::min({1.0, slicePh / F2, (S - slicePh) / F2});
                slicePh += lstep;
            }
            y = mainV * (1 - envMix) + sl * envMix;
            pos += lstep;
        }

        // CLASSIC: the MK I looper never sat still — a slow wow with a little flutter.
        if (p.classic) {
            wowPh += (kTau * 0.9) / kCoreFs;
            if (wowPh > kTau) wowPh -= kTau;
            pos += 0.006 * std::sin(wowPh) + 0.002 * std::sin(wowPh * 5.7);
        }

        if (overdub) {
            // FADE: how much of the old loop survives each overdub pass. 0 keeps
            // the pedal's slow decay; 1 lets the new layer replace it completely.
            const double keep = 0.97 * (1 - clampd(s.fade, 0, 1));
            int q = static_cast<int>(jsMod(std::floor(mode == 1 ? loopPos : pos), Wd)); if (q < 0) q += W;
            float& cell = loopBuf[static_cast<size_t>(start + q)];
            cell = static_cast<float>(cell * keep + x);
        }
        loopPos = jsMod(jsMod(pos, Wd) + Wd, Wd);
        return y * loopGain;
    }

    void coreTick(double x) {
        const double loopOut = loopTick(x);
        const double wetSrc = loopState != LoopState::Play ? x : p.routing == 0 ? x : p.routing == 1 ? x + loopOut : loopOut;
        const double inT = wetOn ? 1.0 : 0.0;
        const double outT = wetOn || p.trails ? 1.0 : 0.0;
        wetIn += (inT - wetIn) * 0.003;
        wetOut += (outT - wetOut) * 0.003;
        for (int k = 0; k < 4; ++k) modeGain[k] += ((p.wetMode == k ? 1.0 : 0.0) - modeGain[k]) * 0.002;

        // WET CLOCK DIVISION: the wet side runs at 1/div of the clock, so every
        // time constant on that side is div times longer.
        const double wdiv = divMap(p.wetDiv);
        // CLASSIC: the MK I clock was noisy, most of all when it was slow.
        const double hiss = p.classic ? (rand01() * 2 - 1) * (0.0006 + 0.01 * (1 - clockRatio)) : 0.0;
        const double wIn = wetSrc * wetIn + hiss;
        double wl = 0, wr = 0;
        if (modeGain[0] > 1e-4) {
            // CLASSIC: the MK I reverb rings far longer than the MK II's.
            reverb.setDecay(expMap(e.time, 0.4, p.classic ? 32 : 25) * wdiv, expMap(e.time, 0.35, 1));
            const double* o = reverb.tick(wIn, e.modWet, freeze, e.time > 0.985);
            wl += o[0] * modeGain[0]; wr += o[1] * modeGain[0];
        }
        double mono = 0;
        if (modeGain[1] > 1e-4) {
            // Synced: the division is real seconds, converted into core ticks at the
            // current clock so the echoed note stays on the beat at any clock setting.
            double delayTicks = expMap(e.time, 0.02 * kCoreFs, 2 * kCoreFs);
            if (p.sync) delayTicks = clampd(syncSeconds(p.bpm, e.time) * clockRatio * fs, 1.0, kDelayMaxSec * kCoreFs * 0.95);
            delayTicks = clampd(delayTicks * wdiv, 1.0, kDelayMaxSec * kCoreFs * 0.95);
            mono += delay.tick(wIn, delayTicks, e.modWet, freeze) * modeGain[1];
        }
        if (modeGain[2] > 1e-4) mono += slip.tick(wIn, expMap(e.time, 0.05 * kCoreFs, 1.5 * kCoreFs) * wdiv, slipSpeedMap(e.modWet), freeze) * modeGain[2];
        if (modeGain[3] > 1e-4) {
            double o[2];
            weave.tick(wIn, std::min(2.8, (0.2 + e.time * 2.6) * wdiv), e.modWet, freeze, o);
            wl += o[0] * modeGain[3]; wr += o[1] * modeGain[3];
        }
        wl += mono;
        wr += p.spread ? decor[1].run(decor[0].run(mono)) : mono;
        if (p.spread) {
            const double mid = (wl + wr) * 0.5, side = (wl - wr) * 0.5 * 1.6;
            wl = mid + side; wr = mid - side;
        }
        // TONE: hi-cut on the wet bus. A blend, so fully open is bit-transparent.
        const double toneAmt = 1 - clampd(s.tone, 0, 1);
        if (toneAmt > 1e-4) {
            const double c = toneCoeff();
            toneL += c * (wl - toneL); toneR += c * (wr - toneR);
            wl += (toneL - wl) * toneAmt;
            wr += (toneR - wr) * toneAmt;
        }
        wl *= wetOut; wr *= wetOut;

        // Catch wet effects while listening; overdubs still use clean input only.
        if (loopState == LoopState::Off) {
            hist[static_cast<size_t>(histW)] = static_cast<float>(softClip(x + (wl + wr) * 0.5));
            if (++histW >= static_cast<int>(hist.size())) { histW = 0; ++histCycles; }
            if (histCount < static_cast<int>(hist.size())) ++histCount;
        }

        // The loop is heard directly, except to the extent it is ALSO arriving
        // through an active wet channel — that would double it. The ducking follows
        // the wet level, so turning the balance to LOOP always restores the loop in
        // full instead of silencing both sides.
        const double bal = e.balance;
        const double wetLvl = std::min(1.0, 2 * (1 - bal)), loopLvl = std::min(1.0, 2 * bal);
        const double directT = p.routing != 0 && wetOn ? 1.0 - wetLvl : 1.0;
        loopDirect += (directT - loopDirect) * 0.003;
        const double ld = loopOut * loopDirect * loopLvl;
        prev[0] = cur[0]; prev[1] = cur[1];
        cur[0] = wl * wetLvl + ld;
        cur[1] = wr * wetLvl + ld;
    }
};

}  // namespace mood
