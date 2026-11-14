// Brood DSP core, C++ port of src/brood-worklet.js (the reference).
//
// Header-only, C++17, no exceptions/RTTI. All buffers are allocated in prepare();
// process() never allocates, locks, or throws, so it is safe on an audio thread.
// Scalars are double and buffers float, matching the JS (Number / Float32Array),
// so the two implementations agree to float precision (see tests/parity).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace brood {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 2.0 * kPi;
constexpr double kHoldSec = 0.35;          // footswitch hold threshold
constexpr int kLoopSamples = 64000;        // pedal capacity: CLOCK sets loop length (1 s @ 64 kHz … 32 s @ 2 kHz)
// The buffer is allocated for the longest loop the panel allows; MEMORY caps how
// much of it a capture may use, so the pedal default still applies at 0. Two
// cores (stereo) therefore hold about 33 MB of loop memory.
constexpr int kMaxLoopSeconds = 32;
constexpr int kMaxLoopSamples = kLoopSamples * kMaxLoopSeconds;
// The take strip's live view: a peak for every this many ticks of history, kept as it's written.
constexpr int kLiveColTicks = 128;
constexpr int kLiveCols = kMaxLoopSamples / kLiveColTicks;
constexpr double kClockMinHz = 2000.0;
constexpr double kClockMaxHz = 64000.0;
constexpr double kCoreFs = 64000.0;
constexpr double kLengthFollow = 3.0;      // TAPE window samples a LENGTH move may shift per tick
// TONE is a hi-cut on the wet bus: a fixed 2.6 kHz one-pole, blended in by amount.
constexpr double kToneHz = 2600.0;
constexpr double kDelayMaxSec = 2.1;
constexpr double kSlipBufSec = 5.5;
constexpr double kWetHoldSec = 0.01;   // how fast FREEZE takes hold of a reverb or delay
constexpr double kWeaveClear = 2048;   // how far a stitch keeps from the write point, in samples
constexpr double kWeaveBufSec = 3.0;   // Fieldtone Weaver captures a couple of seconds; three gives the threads room
constexpr double kWeavePitch[] = {0, 7, -5, 12, -12, 4, -17, 19};   // root-sympathetic intervals, semitones


inline double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double expMap(double t, double a, double b) { return a * std::pow(b / a, t); }
// Stepped, CLOCK lands on these. SMOOTH travels through the same rates, so the two agree at
// every step and switching SMOOTH doesn't change the sound.
inline double clockHz(double knob, bool smooth = false) {
    knob = clampd(knob, 0, 1);
    static constexpr double steps[] = {2000, 3000, 4000, 6000, 8000, 12000, 16000, 24000, 32000, 48000, 64000};
    if (!smooth) return steps[static_cast<int>(std::floor(knob * 10 + 0.5))];
    const int below = std::min(9, static_cast<int>(std::floor(knob * 10)));
    return expMap(knob * 10 - below, steps[below], steps[below + 1]);
}
// Where stepped CLOCK settles: on the step itself, so SMOOTH picks up from there.
inline double clockTarget(double knob, bool smooth) {
    return smooth ? knob : std::floor(clampd(knob, 0, 1) * 10 + 0.5) / 10;
}
inline double sgn(double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0; }
inline double jsRound(double x) { return std::floor(x + 0.5); }

// Hidden options (on the pedal you hold both footswitches to reach these). The two
// sides can run on different clock divisions, so the wet side and the loop can
// drift apart in time and pitch. Mirrors divMap in src/brood-worklet.js.
inline int divMap(double knob) {
    static const int steps[3] = {1, 2, 4};
    return steps[static_cast<size_t>(jsRound(clampd(knob, 0, 1) * (3 - 1)))];
}

// ───── RAMPS ─────
// Each channel has a function generator that can drive one control. The first
// position is off; MODIFY means this channel's own MODIFY knob. Mirrors the
// RAMP_* helpers in src/brood-worklet.js.
// What a modulation source can move, in the order of Core::effective().
constexpr int kModTargets = 7;
inline const char* const kModTargetIds[kModTargets] = {"time", "mix", "length", "modWet", "clock", "modLoop", "balance"};
inline const char* const kModTargetNames[kModTargets] = {"TIME", "MIX", "LENGTH", "MODIFY / WET", "CLOCK", "MODIFY / LOOP", "WET / LOOP"};
constexpr int kRampWaveCount = 5;
inline const char* const kRampWaves[kRampWaveCount] = {"TRI", "SQR", "SIN", "RND", "SMTH"};
// Index ↔ normalised value. The parameters are plain 0..1 floats with `count`
// stops, and this is the only place that conversion lives: the engine, the
// parameter readouts and the editor's stepper cells all go through it. Treating
// the index as the value (which the editor did until round 14) left only the first
// two entries reachable and pointed the engine at the wrong knob entirely.
inline int rampIndexOfValue(double v, int count) {
    return static_cast<int>(jsRound(clampd(v, 0, 1) * (count - 1)));
}
inline double rampValueOfIndex(int i, int count) {
    if (count < 2) return 0.0;
    return clampd(i, 0, count - 1) / static_cast<double>(count - 1);
}
inline int rampWaveIndex(double k) { return rampIndexOfValue(k, kRampWaveCount); }
inline double rampHertz(double k) { return expMap(clampd(k, 0, 1), 0.05, 8); }   // one cycle every 20 s … eight a second
inline double jsMod(double a, double n) { return std::fmod(a, n); }   // JS % on doubles

// Sets saved before START meant the same in every mode stored TAPE's START counted back
// from the newest audio. The value that puts the window in the same place now, for the
// LENGTH and take length that were saved (0 was the newest, so it becomes exactly 1).
inline double migrateTapeStart(double oldStart, double length, int takeLength) {
    const int len = std::max(64, takeLength);
    const int W0 = std::max(64, static_cast<int>(std::lround(len * std::min(1.0, 0.02 + 0.98 * std::clamp(length, 0.0, 1.0)))));
    const int room = std::max(0, len - W0);
    const long oldAt = std::lround((1.0 - std::clamp(oldStart, 0.0, 1.0)) * room);
    if (oldAt >= room) return 1.0;
    return std::clamp(static_cast<double>(oldAt) / std::max(1, len - 64), 0.0, 1.0);
}

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
// How long the division is at this tempo, before anything clamps it to a block's
// own limit. The delay clamps to its line, WEAVE to its buffer, ENV to the take.
inline double syncSecondsRaw(double bpm, double knob) {
    return kSyncDivisions[syncIndex(knob)].beats * 60.0 / clampd(bpm, 20, 300);
}
inline double syncSeconds(double bpm, double knob) {
    return clampd(syncSecondsRaw(bpm, knob), 0.01, kDelayMaxSec * 0.95);
}

// The modulation generators sync to lengths rather than rates: one cycle is a
// musical span, from eight bars down to a sixteenth. Written as beats per cycle so
// a slower tempo makes a longer cycle, exactly like the delay's divisions.
inline const char* const kRampDivLabels[] = {"8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8", "1/16"};
constexpr double kRampDivBeats[] = {32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25};
constexpr int kRampDivCount = static_cast<int>(sizeof(kRampDivBeats) / sizeof(kRampDivBeats[0]));
inline int rampDivIndex(double knob) { return rampIndexOfValue(knob, kRampDivCount); }
inline const char* rampDivLabel(double knob) { return kRampDivLabels[rampDivIndex(knob)]; }
inline double rampCycleSeconds(double bpm, double knob) {
    return kRampDivBeats[rampDivIndex(knob)] * 60.0 / clampd(bpm, 20, 300);
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

// The output stage's limiter: the same shape as softClip with a wider knee. It
// starts bending at 0.7 and reaches the same 1.0 ceiling, so a loud reverb or an
// overdub peak is rounded over rather than flattened in the last tenth.
inline double softLimit(double x) {
    const double ax = std::fabs(x);
    if (ax < 0.7) return x;
    return sgn(x) * (0.7 + 0.3 * std::tanh((ax - 0.7) / 0.3));
}

// Four-point, third-order Hermite: x0..x1 is the span, xm1 and x2 its neighbours.
// Linear interpolation dulls the top and leaves images of everything a read head
// slides over; this is the cheapest curve that is smooth through the points.
// HI-Q: a windowed sinc in place of the four-point curve, for the crossings into and out of
// the core clock. A table of Kaiser-windowed sincs, one row per position between samples, rows
// interpolated. It looks ahead by half its length, so the wet side runs that much later: eight
// ticks going out, which is 1 ms at an 8 kHz clock. The dry side doesn't wait.
constexpr int kSincPhases = 256;
inline double besselI0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 32; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
    return sum;
}
template <int Taps>
struct SincTable {
    double c[kSincPhases + 1][Taps];
    SincTable(double cutoff, double beta) {
        const double mid = Taps / 2 - 1;
        for (int ph = 0; ph <= kSincPhases; ++ph) {
            const double f = static_cast<double>(ph) / kSincPhases;
            double total = 0;
            for (int k = 0; k < Taps; ++k) {
                const double t = k - mid - f;
                const double x = kPi * cutoff * t;
                const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(x) / x;
                const double w = t / (Taps / 2.0);
                c[ph][k] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1 - w * w))) / besselI0(beta);
                total += c[ph][k];
            }
            for (int k = 0; k < Taps; ++k) c[ph][k] /= total;
        }
    }
    // `at(k)` gives sample k, oldest first; f is the position between samples mid and mid + 1.
    template <typename At>
    double read(const At& at, double f) const {
        const double pos = f * kSincPhases;
        const int i = std::min(kSincPhases - 1, static_cast<int>(pos));
        const double fr = pos - i;
        double y = 0;
        for (int k = 0; k < Taps; ++k) y += at(k) * (c[i][k] + (c[i + 1][k] - c[i][k]) * fr);
        return y;
    }
};
inline const SincTable<8>& sincIn() { static const SincTable<8> t(0.97, 7.0); return t; }
inline const SincTable<16>& sincOut() { static const SincTable<16> t(0.92, 8.0); return t; }

inline double hermite(double xm1, double x0, double x1, double x2, double f) {
    const double c1 = 0.5 * (x1 - xm1);
    const double c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2;
    const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
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

// Butterworth low-pass, 6th order (three biquads) or, for CLASSIC, the 4th order
// the clock filters used to be. The steeper one keeps far less of the images a
// low CLOCK leaves above its band; CLASSIC keeps that grit on purpose.
struct LowPassN {
    LowPass a, b, c;
    bool sixth = true;
    void set(double fc, double fs, bool sixthOrder) {
        sixth = sixthOrder;
        if (sixth) { a.set(fc, fs, 0.5176); b.set(fc, fs, 0.7071); c.set(fc, fs, 1.9319); }
        else { a.set(fc, fs, 0.5412); b.set(fc, fs, 1.3066); }
    }
    double run(double x) {
        const double y = b.run(a.run(x));
        return sixth ? c.run(y) : y;
    }
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

// Fractional read from a circular buffer, Hermite-interpolated.
// Writes into a buffer that FREEZE can stop. Writing again would put a step where the
// old audio ends, so the input slides in from the last sample held.
struct WetWriter {
    bool wasFrozen = false;
    double seam = 1, from = 0;
    void write(std::vector<float>& buf, int& w, double input, bool frozen) {
        if (!frozen) {
            const int n = static_cast<int>(buf.size());
            if (wasFrozen) { seam = 0; from = buf[static_cast<size_t>(w == 0 ? n - 1 : w - 1)]; }
            if (seam < 1) { seam = std::min(1.0, seam + 1.0 / 240); input = from + (input - from) * seam; }
            buf[static_cast<size_t>(w)] = static_cast<float>(input);
            if (++w >= n) w = 0;
        }
        wasFrozen = frozen;
    }
};

inline double readFrac(const std::vector<float>& buf, double pos) {
    const int N = static_cast<int>(buf.size());
    const double n = static_cast<double>(N);
    double p = jsMod(pos, n); if (p < 0) p += n;
    const int i0 = static_cast<int>(std::floor(p)); const double f = p - i0;
    const int im1 = i0 == 0 ? N - 1 : i0 - 1;
    const int i1 = i0 + 1 >= N ? i0 + 1 - N : i0 + 1;
    const int i2 = i0 + 2 >= N ? i0 + 2 - N : i0 + 2;
    return hermite(buf[static_cast<size_t>(im1)], buf[static_cast<size_t>(i0)],
                   buf[static_cast<size_t>(i1)], buf[static_cast<size_t>(i2)], f);
}

// ───────────────────────── Wet channel: REVERB ─────────────────────────
struct Reverb {
    std::vector<float> lines[8];
    int len[8] = {}, w[8] = {};
    float lp[8] = {}, x[8] = {}, g[8] = {};
    double modPh[8] = {}, modRate[8] = {};
    Allpass diff[4];
    double fs = 48000, t60 = -1, size = 1, out[2] = {0, 0};
    double hold = 0, ring = 0, holdA = 0;   // FREEZE and endless decay, glided so they don't step the tail

    void init(double sr) {
        fs = sr; t60 = -1; size = 1; hold = ring = 0;
        holdA = 1 - std::exp(-1 / (kWetHoldSec * fs));
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
        hold += ((freeze ? 1.0 : 0.0) - hold) * holdA;
        ring += ((freeze || infinite ? 1.0 : 0.0) - ring) * holdA;
        const double dampOpen = 1.0 - std::exp((-kTau * expMap(modify, 9000, 3500)) / fs);
        const double damp = dampOpen + (1.0 - dampOpen) * hold;
        const double depth = 1 + modify * modify * 18 * (fs / 48000.0);
        double sum = 0;
        for (int i = 0; i < 8; ++i) {
            modPh[i] += modRate[i];
            if (modPh[i] > kTau) modPh[i] -= kTau;
            const double delay = len[i] * size - depth * (1 + std::sin(modPh[i]));
            const double v = readFrac(lines[i], w[i] - delay);
            lp[i] = static_cast<float>(lp[i] + damp * (v - lp[i]));
            const double gg = g[i] + (1.0 - g[i]) * ring;
            x[i] = static_cast<float>(lp[i] * gg);
            sum += x[i];
        }
        const double h = sum * 0.25;   // Householder reflection
        double l = 0, r = 0;
        for (int i = 0; i < 8; ++i) {
            const double y = x[i] - h;
            lines[i][static_cast<size_t>(w[i])] = static_cast<float>(y + d * (1.0 - hold));
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
    double hold = 0, full = 0, holdA = 0;   // FREEZE, and repeating exactly (FREEZE or the top of MODIFY)
    // TAPE ECHO: one read head that glides to a new TIME, so the echo bends in pitch as a
    // tape echo's does, with a little wow and flutter and a gentle saturation in the loop.
    double headD = -1, headV = 0, frozenD = -1, wowPh = 0, flutPh = 0, tapeMix = 0;
    void init(double sr) {
        fs = sr; hold = full = 0;
        headD = -1; headV = 0; frozenD = -1; wowPh = flutPh = 0; tapeMix = 0;
        holdA = 1 - std::exp(-1 / (kWetHoldSec * fs));
        buf.assign(static_cast<size_t>(std::ceil(kDelayMaxSec * fs) + 8), 0.f);
        w = 0; d = -1; nextD = -1; fade = 1; lp = hp = 0;
        lpA = 1 - std::exp((-kTau * 6500) / fs);
        hpA = 1 - std::exp((-kTau * 70) / fs);
    }
    double tick(double input, double delaySamples, double modify, bool freeze, bool tape) {
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
        // TAPE ECHO, faded in and out over 30 ms so switching it under sound doesn't click.
        tapeMix = std::clamp(tapeMix + (tape ? 1.0 : -1.0) / (fs * 0.03), 0.0, 1.0);
        double tw = 0;   // how much of the tape head is heard
        if (tapeMix > 0) {
            // While frozen the line repeats exactly: TIME is ignored until FREEZE lets go.
            if (hold > 0.5) { if (frozenD < 0) frozenD = headD; } else frozenD = -1;
            const double target = frozenD >= 0 ? frozenD : delaySamples;
            if (headD < 0) headD = target;
            // The head moves toward the new time no faster than half its own speed again,
            // so pitch bends at most an octave down or a fifth up, and never past the write head.
            const double want = std::clamp((target - headD) / (0.1 * fs), -0.5, 0.5);
            headV += (want - headV) * 0.002;
            headD = std::clamp(headD + headV, 1.0, static_cast<double>(buf.size()) - 4);
            wowPh += kTau * 0.5 / fs; if (wowPh > kTau) wowPh -= kTau;
            flutPh += kTau * 6.0 / fs; if (flutPh > kTau) flutPh -= kTau;
            const double wobble = headD * (0.0015 * std::sin(wowPh) + 0.0005 * std::sin(flutPh)) * (1 - hold);
            // Repeating exactly, the head reads whole samples: a fractional read written back
            // every pass would dull the frozen sound a little more each time round.
            const double at = full > 0.999 ? std::round(headD) : headD + wobble;
            const double yTape = readFrac(buf, w - at);
            tw = 0.5 - 0.5 * std::cos(kPi * tapeMix);
            y += (yTape - y) * tw;
        } else headD = delaySamples;   // off, the head waits where the knob is, ready to take over
        lp += lpA * (y - lp);
        hp += hpA * (lp - hp);
        // Glided, as what's written here comes back on every repeat: a hard switch would
        // leave a seam in the line that clicks each time round.
        hold += ((freeze ? 1.0 : 0.0) - hold) * holdA;
        full += ((freeze || modify > 0.985 ? 1.0 : 0.0) - full) * holdA;
        double fb = (lp - hp) * modify;
        // Tape saturates gently in the loop, not when repeating exactly.
        if (tw > 0) fb += (std::tanh(1.3 * fb) / std::tanh(1.3) - fb) * tw * (1 - full);
        const double v = input * (1 - hold) + fb * (1 - full) + y * full;
        buf[static_cast<size_t>(w)] = static_cast<float>(softClip(v));
        if (++w >= static_cast<int>(buf.size())) w = 0;
        return y;
    }
};

// ───────────────────────── Wet channel: SLIP ──────────────────────────
struct Slip {
    struct Head { double pos = 0, g = 0, level = 1; int dir = 0; bool dying = false; };
    std::vector<float> buf; int w = 0;
    Head heads[2]; int cur = 0;
    double fs = 48000, count = 1e9, speed = 1;
    WetWriter writer;
    void init(double sr) {
        fs = sr;
        buf.assign(static_cast<size_t>(std::ceil(kSlipBufSec * fs)), 0.f);
        w = 0; heads[0] = heads[1] = Head{}; cur = 0; count = 1e9; speed = 1; writer = WetWriter{};
    }
    double tick(double input, double sliceLen, double speedTarget, bool freeze) {
        const int n = static_cast<int>(buf.size());
        const bool freezing = freeze && !writer.wasFrozen;
        writer.write(buf, w, input, freeze);
        speed += (speedTarget - speed) * 0.002;
        const double s = speed;
        const double xf = std::max(32.0, std::min(0.25 * sliceLen, 0.03 * fs));
        // Freezing stops the write point, so a head running just behind it would reach it
        // at once: start a slice now, from far enough back. The head being replaced has to
        // have finished fading, or its restart would be a step.
        if (freezing) count = 1e9;
        if (++count >= sliceLen && heads[cur ^ 1].g <= 0) {
            count = 0;
            const double margin = xf + 64;
            const double behind = margin + std::max(0.0, freeze ? s : s - 1) * (sliceLen + xf);
            const double start = w - std::min(behind, n - 2 * margin);
            cur ^= 1;
            heads[cur].pos = start; heads[cur].g = 0; heads[cur].dir = 1; heads[cur].level = 1; heads[cur].dying = false;
            heads[cur ^ 1].dir = -1;
        }
        const double step = 1 / xf;
        double out = 0;
        for (auto& h : heads) {
            if (h.dir > 0) h.g = std::min(1.0, h.g + step);
            else if (h.dir < 0) h.g = std::max(0.0, h.g - step);
            if (h.g > 0) {
                // A head that speeds up can catch the write point, where this moment's audio
                // meets the oldest, and that seam is a click. So it bows out before it gets
                // there: once it's close it fades for good, until the next slice replaces it.
                double ahead = jsMod(w - h.pos, static_cast<double>(n)); if (ahead < 0) ahead += n;
                const double closing = freeze ? s : s - 1;   // toward the write point; negative, away and round to it
                if (ahead < 64 + 240 * std::max(0.0, closing) || ahead > n - 64 + 240 * std::min(0.0, closing)) h.dying = true;
                if (h.dying) h.level = std::max(0.0, h.level - 1.0 / 240);
                out += readFrac(buf, h.pos) * std::sin(h.g * kPi * 0.5) * h.level;
                h.pos += s;
            }
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
// repeatable, and no undo. Mirrors the JS Weave class in src/brood-worklet.js.
struct Weave {
    struct Thread {
        double pos = 0, ph = 0, rate = 1, pan = 0, amp = 1; int dir = 1;
        double level = 1; bool dying = false;
        // A new roll waits for the next stitch, where the window is shut.
        bool rolled = false; double nextRate = 1, nextBack = 0; int nextDir = 1;
    };
    std::vector<float> buf; int w = 0;
    uint32_t rng = 0x5eed; int seedKey = -1;
    Thread th[3];
    WetWriter writer;
    double fs = 48000;
    void init(double sr) {
        fs = sr;
        buf.assign(static_cast<size_t>(std::ceil(kWeaveBufSec * fs)), 0.f);
        w = 0; rng = 0x5eed; seedKey = -1; writer = WetWriter{};
        static constexpr double pan[3] = {-0.7, 0, 0.7};
        for (int i = 0; i < 3; ++i) { th[i] = Thread{}; th[i].pos = -fs * (0.25 + 0.3 * i); th[i].ph = i / 3.0; th[i].pan = pan[i]; }
    }
    double rand01() { rng = rng * 1103515245u + 12345u; return rng / 4294967296.0; }
    void reroll(int key, double reachSec) {
        if (key == seedKey) return;
        seedKey = key;
        for (auto& t : th) {
            const int semis = static_cast<int>(kWeavePitch[static_cast<int>(rand01() * 8)]);
            t.nextRate = std::pow(2.0, semis / 12.0);
            t.nextDir = rand01() < 0.3 ? -1 : 1;
            t.nextBack = fs * (0.15 + rand01() * 0.85 * std::max(0.2, reachSec));
            t.rolled = true;
        }
    }
    // out[0], out[1] are left/right.
    void tick(double input, double reachSec, double modify, bool frozen, double* out) {
        const int n = static_cast<int>(buf.size());
        writer.write(buf, w, input, frozen);
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
                double back = -1;
                if (rand01() < 0.45) back = fs * (0.12 + rand01() * std::max(0.2, reachSec));
                if (t.rolled) { t.rate = t.nextRate; t.dir = t.nextDir; back = t.nextBack; t.rolled = false; }
                // The write point is where the newest audio meets the oldest. A thread drifts
                // toward or away from it by its speed, so it starts where a whole stitch stays clear.
                const double drift = t.rate * t.dir - (frozen ? 0 : 1);
                const double nearest = kWeaveClear + std::max(0.0, drift) * grain;
                const double farthest = std::max(nearest, n - kWeaveClear + std::min(0.0, drift) * grain);
                double ahead = jsMod(w - t.pos, static_cast<double>(n)); if (ahead < 0) ahead += n;
                t.pos = w - clampd(back >= 0 ? back : ahead, nearest, farthest);
                t.level = 1; t.dying = false;
                // Dropouts are partial, so a thread thins out instead of vanishing.
                t.amp = rand01() < chance * 0.35 ? 0.0 : 0.45 + 0.55 * rand01();
            }
            // MODIFY lengthening a stitch or FREEZE stopping the write point can still bring
            // it close: then it fades for good, until the next stitch.
            double ahead = jsMod(w - t.pos, static_cast<double>(n)); if (ahead < 0) ahead += n;
            if (ahead < kWeaveClear * 0.5 || ahead > n - kWeaveClear * 0.5) t.dying = true;
            if (t.dying) t.level = std::max(0.0, t.level - 1.0 / 240);
            const double win = std::sin(t.ph * kPi);
            const double v = readFrac(buf, t.pos) * win * win * t.amp * t.level;
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
    bool trails = false, smooth = false;
    bool sync = false;
    double balance = 0.5, bpm = 120.0;
    double loopEnd = 1, memory = 0;                 // playback trim + capture ceiling
    double start = 1;                                // where the loop's window sits in the take: 0 the oldest audio, 1 the newest
    double tone = 1, fade = 0, width = 0.5;          // hidden: wet hi-cut, overdub decay, stereo width
    double wetDiv = 0, loopDiv = 0;                  // hidden: per-side clock division
    bool classic = false;                            // hidden: MK I quirks
    double wearNoise = 1;                            // how much of the worn clock's hiss is let through, 0..1
    // WORN after the loop: the input is converted as the pedal's, overdubs don't follow the
    // wobble and the hiss stays out of what's recorded, so the take is as clean as PEDAL's
    // and only what's heard is worn.
    bool wearPost = false;
    bool tapeDelay = false;                          // TAPE ECHO: the delay glides and bends pitch
    // OVERDUB, latched: switching it on layers into a playing loop until it's switched off.
    // Switched on with no loop playing it does nothing (overdubLatched() says so).
    bool overdubLatch = false;
    // RECORD: the loop switch records what's played after it instead of keeping the recent
    // past. recBars 0 records until the next tap; otherwise that many bars, then plays.
    // Bars start at the next bar line while the host plays, unless recAtTap.
    int recMode = 0;                                 // 0 capture, 1 record
    int recBars = 0;
    bool recAtTap = false;
    double barQuarters = 4;                          // the host's bar, in quarter notes
    bool hostPlaying = false;                        // the host's transport, at the start of this block
    double ppq = 0, barStartPpq = 0;                 // and where it is: now, and where this bar began
    double freezeLatch = 0;                          // the FREEZE switch: wet up, held
    // The two modulation sources. Each can move any of the seven controls below by its
    // own depth (-1..1, 0 is no route), and runs at its own rate and shape.
    struct ModSource {
        bool on = true, uni = false, bounce = false, sweep = false, sync = false, hold = false;
        double rate = 0.45, wave = 0;
        double depth[kModTargets] = {0, 0, 0, 0, 0, 0, 0};
    };
    ModSource mods[2];
    bool envSync = false;                           // ENV slice follows the tempo
    bool envSoft = false;                           // ENV's slices fade in and out instead of cutting
    bool hiQ = false;                               // HI-Q: sinc conversion into and out of the clock
    double loopDb = 0;                              // GAIN on the loop, -24..+12 dB
    bool wetPre = false;                            // the wet side runs into the looper instead of beside it
};

struct State {
    bool wetOn, freeze; LoopLed loop;
    int lsrc, winStart, winLen; double pos, clockHz; uint32_t histCycles;
    float peakIn, peakOut;
    bool recArmed;   // RECORD: waiting for the bar line
    // OVERDUB: whether it can be switched on now (a loop is playing), and whether it's latched.
    bool canOverdub = false, dubLatched = false;
    // UNDO: what it would step back over next (0 nothing, 1 a take, 2 a clear, 3 a layer),
    // and whether that step has been undone already, so pressing it again redoes.
    int undoKind = 0;
    bool undoRedo = false;
    // The take follows the host's transport (it was caught while the host played), and is
    // parked: the host stopped it, and it waits for play.
    bool follows = false, parked = false;
};

class Core {
public:
    Params p;

    void prepare(double sampleRate) {
        fs = sampleRate;
        s = Smoothed{p.time, p.mix, p.length, p.modWet, p.clock, p.modLoop, p.balance, p.tone, p.fade, p.width, {p.mods[0].rate, p.mods[1].rate}};
        e = Eff{p.time, p.mix, p.length, p.modWet, p.clock, p.modLoop, p.balance};
        hostCount = 0; phase = 0; clockRatio = 1; filterClock = -1; filterClassic = -1;
        aa = reconL = reconR = aaHi[0] = aaHi[1] = LowPassN{};
        for (auto& ch : ticks) for (auto& t : ch) t = 0;
        for (auto& x : xin) x = 0;
        for (auto& x : xinHi) x = 0;
        for (auto& ch : ticksHi) for (auto& t : ch) t = 0;
        xinHiW = ticksHiW = 0; hiMix = 0;
        reverb.init(kCoreFs); delay.init(kCoreFs); slip.init(kCoreFs); weave.init(kCoreFs);
        wetOn = freeze = wetMomentary = false;
        wetIn = wetOut = 0; modeGain[0] = 1; modeGain[1] = modeGain[2] = modeGain[3] = 0;
        decor[0].init(static_cast<int>(jsRound(113 * kCoreFs / 48000)), 0.6);
        decor[1].init(static_cast<int>(jsRound(337 * kCoreFs / 48000)), 0.6);
        // The loop survives a re-prepare. A host calls prepareToPlay again for a
        // device or block-size change and before an offline render, and wiping the
        // take there is why a bounce could come back empty. The loop is measured in
        // core-clock ticks (kCoreFs), not host samples, so it stays valid as it is.
        if (hist.size() != static_cast<size_t>(kMaxLoopSamples)) {
            hist.assign(kMaxLoopSamples, 0.f); loopBuf.assign(kMaxLoopSamples, 0.f); undoBuf.assign(kMaxLoopSamples, 0.f);
            undoLen = 0; undoKind = UndoKind::None; undoRedo = false;
            liveCols.assign(kLiveCols, 0.f); histTotal = 0;
            histW = histCount = 0; lsrc = 0; loopPos = 0; loopState = LoopState::Off;
        }
        histCycles = 0;
        overdub = false;
        dubLatched = latchSeen = false; pendingOp = LoopOp::None; resumeAfterOp = false;
        pendingLock = false; lockGain = 1; lastHostPlaying = false; lastBlockN = 0;
        loopGain = 0; tapeSpeed = 1; stretchSpeed = 0;
        env = 0; envGate = false; envHold = 0; envMix = 0; sliceStart = slicePh = 0;
        softStart = softPh = softS = softOldFade = softOldS = 0;
        dubGain = dubPrev = 0; dubLast = -1;
        grain[0] = {0, 0, 1}; grain[1] = {0.5, 0, 1};
        rng = 22222;
        toneL = toneR = 0; wowPh = 0;
        warmIn[0] = warmIn[1] = warmOut[0] = warmOut[1] = 0;
        fsWet = FsWet{}; fsLoop = FsLoop{}; rec = Recording{};
        engaged = 0; peakIn = peakOut = 0;
        loopWaveDirty = false;
    }

    // which: 0 = wet, 1 = loop. Mirrors BroodCore.footswitch.
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
                    freeze = false; if (wetMomentary) wetOn = false;   // a hold freezes only while held
                } else if (!wetMomentary) { wetOn = false; freeze = false; }
                wetMomentary = false;
            }
            return;
        }
        settlePending();   // a CLEAR or UNDO still fading out happens now
        parked = false;    // a tap with a transport loop parked works as with any stopped loop
        if (down) {
            const bool playing = loopState == LoopState::Play;
            // RECORD, with nothing playing: a tap while recording (or armed) ends it there.
            if (!playing && rec.phase != RecPhase::Idle) {
                if (rec.phase == RecPhase::Recording) closeRecording();
                else rec.phase = RecPhase::Idle;   // armed: a second tap stands it down
                fsLoop = FsLoop{};
                return;
            }
            fsLoop = FsLoop{true, now, false, histW, histCycles, playing};
            if (!playing && p.recMode == 1) {
                // Recording starts here, or at the next bar line when bars are chosen and the
                // host is playing. A hold records from the press either way.
                rec.histIdx = histW; rec.histCycle = histCycles; rec.endAt = -1;
                if (p.recBars > 0 && !p.recAtTap && p.hostPlaying) {
                    const double into = (p.ppq - p.barStartPpq) / std::max(0.25, p.barQuarters);
                    rec.barTarget = p.barStartPpq + std::ceil(into - 1e-9) * std::max(0.25, p.barQuarters);
                    rec.phase = RecPhase::Armed;
                } else startRecording(false);
            }
        } else if (fsLoop.active) {
            const FsLoop d = fsLoop;
            fsLoop.active = false;
            if (!d.playing && p.recMode == 1 && rec.phase != RecPhase::Idle) {
                // RECORD: a hold ends on release; a tap leaves it running (to the next tap,
                // the bar count or the ceiling).
                if (d.held) closeRecording();
                return;
            }
            if (d.playing) {
                // A tap stops the loop, and a latched OVERDUB with it. Letting go of a hold
                // ends its layer, unless OVERDUB is latched.
                if (!d.held) { loopState = LoopState::Off; overdub = false; dubLatched = false; }
                else { overdub = dubLatched; loopWaveDirty = true; }
            } else if (d.held) {
                const auto cycles = static_cast<uint32_t>(histCycles - d.histCycle);
                const int64_t written = static_cast<int64_t>(cycles) * static_cast<int64_t>(hist.size()) + histW - d.histIdx;
                capture(static_cast<int>(std::min<int64_t>(static_cast<int64_t>(hist.size()) - 1, std::max<int64_t>(0, written))));
            } else capture(histCount);
        }
    }

    // CLEAR: the take goes, and UNDO brings it back. A playing loop fades out first, the way a
    // tap stops it, so nothing clicks. A recording, or a wait for the bar line, stands down.
    void clearLoop() {
        settlePending();
        rec.phase = RecPhase::Idle;
        if (lsrc < 64) return;
        overdub = false; dubLatched = false;
        if (loopState == LoopState::Play || loopGain > 0) { loopState = LoopState::Off; pendingOp = LoopOp::Clear; resumeAfterOp = false; return; }
        doClear();
    }
    // UNDO: one step back over the last CLEAR, new take or overdub layer; again, it redoes.
    // Undoing a layer keeps a playing loop playing (it dips while the take is swapped);
    // undoing a clear or a take leaves the loop stopped, ready for a tap.
    void undoLoop() {
        settlePending();
        if (undoKind == UndoKind::None || rec.phase != RecPhase::Idle) return;
        resumeAfterOp = loopState == LoopState::Play && undoKind == UndoKind::Dub;
        overdub = false; dubLatched = false;
        if (loopState == LoopState::Play || loopGain > 0) { loopState = LoopState::Off; pendingOp = LoopOp::Undo; return; }
        pendingOp = LoopOp::Undo;
        settlePending();
    }
    bool overdubLatched() const { return dubLatched; }
    // The take is a transport take, and the song position its first sample belongs to (for saving).
    bool takeFollowsTransport() const { return takeFollows && lsrc >= 64; }
    double takeAnchor() const { return anchorPpq; }
    // True once after a take arrived and fitted the window (see fitWindow), with TAPE's
    // LENGTH moved too when `lengthToo`.
    bool takeFitted(bool& lengthToo) {
        if (!fitted) return false;
        fitted = false; lengthToo = p.loopMode == 1;
        return true;
    }

    // Explicit freeze for MIDI CC; footswitch gestures remain momentary/latching.
    void setFreeze(bool active) { freeze = active && wetOn; }

    // in may be null (silence). Mirrors BroodCore.process.
    void process(const float* in, float* outL, float* outR, int n) {
        const double a = 1 - std::exp(-1 / (0.03 * fs));
        const double aClock = 1 - std::exp(-1 / ((p.smooth ? 1.5 : 0.08) * fs));
        const double holdN = kHoldSec * fs;
        if (fsWet.active && !fsWet.held && hostCount - fsWet.t >= holdN) {
            fsWet.held = true;
            freeze = true;
        }
        if (fsLoop.active && !fsLoop.held && hostCount - fsLoop.t >= holdN) {
            fsLoop.held = true;
            if (fsLoop.playing) beginLayer();
            // RECORD held: it records from the press for as long as it's held, bars or not.
            else if (rec.phase != RecPhase::Idle) { rec.histIdx = fsLoop.histIdx; rec.histCycle = fsLoop.histCycle; rec.phase = RecPhase::Recording; rec.endAt = -1; }
        }
        if (p.overdubLatch != latchSeen) {
            latchSeen = p.overdubLatch;
            if (latchSeen) { if (canOverdub()) { beginLayer(); dubLatched = true; } }
            else if (dubLatched) {
                dubLatched = false;
                if (!(fsLoop.active && fsLoop.held && fsLoop.playing)) overdub = false;   // a hold keeps its layer
            }
        }
        followTransport(n);
        const int64_t blockStart = hostCount;
        blockStartCount = hostCount;

        double pkIn = 0, pkOut = 0;
        for (int i = 0; i < n; ++i) {
            s.time += (p.time - s.time) * a;
            s.mix += (p.mix - s.mix) * a;
            s.length += (p.length - s.length) * a;
            s.modWet += (p.modWet - s.modWet) * a;
            s.modLoop += (p.modLoop - s.modLoop) * a;
            s.balance += (p.balance - s.balance) * a;
            s.clock += (clockTarget(p.clock, p.smooth) - s.clock) * aClock;
            for (int k = 0; k < 2; ++k) s.modRate[k] += (p.mods[k].rate - s.modRate[k]) * a;
            rampTick();
            applyRamps(a);
            s.tone += (p.tone - s.tone) * a;
            s.fade += (p.fade - s.fade) * a;
            s.width += (p.width - s.width) * a;
            if ((i & 31) == 0) {
                const double c = clockHz(e.clock, p.smooth) / fs;
                clockRatio = c;
                const int classicNow = p.classic ? (p.wearPost ? 2 : 1) : 0;
                const bool wornIn = p.classic && ! p.wearPost;   // the worn conversion on the way in is what a loop records
                if (std::fabs(c - filterClock) > 0.002 * c || classicNow != filterClassic) {
                    filterClock = c; filterClassic = classicNow;
                    const double fc = std::min({0.45 * c * fs, 0.45 * fs, 20000.0});
                    aa.set(fc, fs, !wornIn); reconL.set(fc, fs, !p.classic); reconR.set(fc, fs, !p.classic);
                    // HI-Q's input: twice the filter, a little higher up, so more of the top
                    // survives and less folds back.
                    const double fcHi = std::min({0.48 * c * fs, 0.45 * fs, 20000.0});
                    aaHi[0].set(fcHi, fs, true); aaHi[1].set(fcHi, fs, true);
                }
            }
            const double dry = in ? in[i] : 0.0;
            pkIn = std::max(pkIn, std::fabs(dry));
            const double xa = aa.run(dry);
            xin[0] = xin[1]; xin[1] = xin[2]; xin[2] = xin[3]; xin[3] = xa;
            hiMix += ((p.hiQ && !p.classic ? 1.0 : 0.0) - hiMix) * 0.002;
            const bool hi = hiMix > 1e-5;
            if (hi) { xinHi[xinHiW] = aaHi[1].run(aaHi[0].run(dry)); xinHiW = (xinHiW + 1) & 7; }
            phase += clockRatio;
            while (phase >= 1) {
                phase -= 1;
                // Each core tick falls somewhere between two host samples; read the
                // input there (one host sample late, so the curve has a neighbour on
                // both sides). Handing every tick the latest host sample instead
                // repeated one sample in four at 48 kHz, a stair-step jitter that was
                // recorded into every loop. CLASSIC keeps that hold as part of its grit.
                const double u = 1 - phase / clockRatio;
                double xt = p.classic && ! p.wearPost ? xa : hermite(xin[0], xin[1], xin[2], xin[3], u);
                if (hi) xt += (sincIn().read([this](int k) { return xinHi[(xinHiW + k) & 7]; }, u) - xt) * hiMix;
                coreTick(xt);
            }
            const double f = phase;
            double il, ir;
            if (p.classic) {
                il = ticks[0][2] + (ticks[0][3] - ticks[0][2]) * f;
                ir = ticks[1][2] + (ticks[1][3] - ticks[1][2]) * f;
            } else {
                il = hermite(ticks[0][0], ticks[0][1], ticks[0][2], ticks[0][3], f);
                ir = hermite(ticks[1][0], ticks[1][1], ticks[1][2], ticks[1][3], f);
            }
            const double sincL = hi ? sincOut().read([this](int k) { return ticksHi[0][(ticksHiW + k) & 15]; }, f) : 0.0;
            const double sincR = hi ? sincOut().read([this](int k) { return ticksHi[1][(ticksHiW + k) & 15]; }, f) : 0.0;
            double el = reconL.run(il);
            double er = reconR.run(ir);
            // HI-Q: the sinc already stops the images, so the droop of the filter isn't needed.
            if (hi) { el += (sincL - el) * hiMix; er += (sincR - er) * hiMix; }
            // STEREO WIDTH: mid/side on the effect bus only — the dry signal must
            // not move. 0.5 is the image as built, so that setting is untouched.
            if (std::fabs(s.width - 0.5) > 0.002) {
                const double mid = (el + er) * 0.5, side = (el - er) * 0.5 * (s.width * 2);
                el = mid + side; er = mid - side;
            }
            const bool latchFreeze = p.freezeLatch > 0.5;   // the FREEZE switch, per sample
            const double engagedT = wetOn || latchFreeze || loopState == LoopState::Play ? 1.0 : 0.0;
            engaged += (engagedT - engaged) * 0.001;
            const double dg = 1 - engaged * (1 - std::min(1.0, 2 * (1 - e.mix)));
            const double wg = std::min(1.0, 2 * e.mix);
            double l = dry * dg + el * wg;
            double r = dry * dg + er * wg;
            const bool fullyBypassed = !p.trails && !wetOn && !latchFreeze && loopState == LoopState::Off && engaged < 1e-4;
            if (!fullyBypassed) { l = softLimit(l); r = softLimit(r); }
            outL[i] = static_cast<float>(l); outR[i] = static_cast<float>(r);
            pkOut = std::max(pkOut, std::max(std::fabs(l), std::fabs(r)));
            ++hostCount;
            // RECORD: the bar line, and the bar count, in host time, so moving CLOCK can't
            // stretch them. Checked every sample, so both engines close on the same one.
            if (rec.phase == RecPhase::Armed) {
                const double now = p.ppq + static_cast<double>(hostCount - blockStart) * p.bpm / 60.0 / fs;
                if (!p.hostPlaying || now >= rec.barTarget) startRecording(true);
            } else if (rec.phase == RecPhase::Recording && rec.endAt >= 0 && hostCount >= rec.endAt) closeRecording();
        }
        peakIn = std::max(pkIn, peakIn * 0.9);
        peakOut = std::max(pkOut, peakOut * 0.9);
    }

    State state() const {
        LoopLed led = loopState == LoopState::Play ? LoopLed::Play : LoopLed::Off;
        if ((fsLoop.active && fsLoop.held && !fsLoop.playing) || rec.phase == RecPhase::Recording) led = LoopLed::Rec;
        else if (overdub && loopState == LoopState::Play) led = LoopLed::Dub;
        // TAPE plays a window that follows LENGTH at a capped speed: report that one, so the
        // playhead drawn is the playhead heard.
        const Window win = lsrc <= 0 ? Window{0, 0} : p.loopMode == 1 && lengthSlew >= 0 ? loopWindowAt(lengthSlew) : loopWindow();
        const bool heldNow = p.freezeLatch > 0.5;
        State st{wetOn || heldNow, freeze || heldNow, led, lsrc, win.start, win.W, loopPos, clockRatio * fs, histCycles,
                     static_cast<float>(peakIn), static_cast<float>(peakOut), rec.phase == RecPhase::Armed};
        st.canOverdub = canOverdub(); st.dubLatched = dubLatched;
        st.undoKind = static_cast<int>(undoKind); st.undoRedo = undoRedo;
        st.follows = takeFollows && lsrc >= 64; st.parked = parked;
        return st;
    }

    // Peak envelope of the captured loop, for drawing. Not real-time safe to call
    // concurrently with process(); the plugin copies under its own lock.
    void loopPeaks(float* out, int bins) const {
        for (int b = 0; b < bins; ++b) out[b] = 0;
        if (!lsrc) return;
        loopPeaksRange(out, bins, 0, bins - 1);
    }
    // The same for bins first..last only, so an overdub redraws just what it wrote.
    void loopPeaksRange(float* out, int bins, int first, int last) const {
        if (!lsrc) return;
        first = std::max(0, first); last = std::min(bins - 1, last);
        for (int b = first; b <= last; ++b) {
            const int a0 = static_cast<int>((static_cast<int64_t>(b) * lsrc) / bins);
            const int z = std::max(a0 + 1, static_cast<int>((static_cast<int64_t>(b + 1) * lsrc) / bins));
            float pk = 0;
            for (int i = a0; i < z; i += 4) pk = std::max(pk, std::fabs(loopBuf[static_cast<size_t>(i)]));
            out[b] = pk;
        }
    }

    // Level of what the loop is holding (its playable window only), for tests and
    // for anything that wants to know whether the take still has something in it.
    double loopWindowRms() const {
        if (lsrc <= 0 || overdub) return 0;
        const Window win = loopWindow();
        double acc = 0;
        for (int i = 0; i < win.W; ++i) { const double v = loopBuf[static_cast<size_t>(win.start + i)]; acc += v * v; }
        return std::sqrt(acc / static_cast<double>(std::max(1, win.W)));
    }

    // ── the take as data ──
    // A host that re-creates the plug-in for an offline render starts with empty
    // buffers, so a captured loop has to travel in the state as well as live in
    // memory. Copies of the playable window, in core-clock samples.
    int capturedLoop(float* out, int maxOut) const {
        const int n = std::min(lsrc, maxOut);
        for (int i = 0; i < n; ++i) out[i] = loopBuf[static_cast<size_t>(i)];
        return n;
    }
    // Put a take back. `n` must already be in core-clock samples.
    // `follows`: a transport take, with the song position its first sample belongs to. It
    // comes back parked and plays when the host does.
    void restoreLoop(const float* pcm, int n, bool play, bool follows = false, double anchor = 0) {
        if (pcm == nullptr || n < 64 || n > static_cast<int>(loopBuf.size())) return;
        loopBuf.assign(loopBuf.size(), 0.f);      // keep the allocation, clear the old take
        std::copy(pcm, pcm + n, loopBuf.begin());
        lsrc = n; loopPos = 0; loopGain = 1;
        loopState = play ? LoopState::Play : LoopState::Off;
        takeFollows = follows; anchorPpq = anchor; pendingLock = false; lockGain = 1;
        if (follows) { loopState = LoopState::Off; loopGain = 0; parked = true; lastHostPlaying = false; }
        else parked = false;
        loopWaveDirty = true;
        undoKind = UndoKind::None; undoRedo = false; undoLen = 0;   // a reopened set has no undo
        pendingOp = LoopOp::None; overdub = dubLatched = false;
    }
    LoopState loopLedState() const { return loopState; }
    // int16 is plenty for a take and half the bytes of float; the state is
    // base64'd afterwards, so every byte saved is a byte off the set file.
    static void packLoop(const float* src, int n, std::vector<int16_t>& out) {
        out.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const float v = static_cast<float>(clampd(static_cast<double>(src[i]), -1.0, 1.0));
            out[static_cast<size_t>(i)] = static_cast<int16_t>(jsRound(v * 32767.f));
        }
    }
    static void unpackLoop(const int16_t* src, int n, float* out) {
        for (int i = 0; i < n; ++i) out[i] = static_cast<float>(src[i]) / 32767.f;
    }

    bool takeLoopWaveDirty() { const bool d = loopWaveDirty; loopWaveDirty = false; return d; }
    // Cells of the take an overdub wrote since the last call; false if none.
    bool takeDubDirty(int& lo, int& hi) {
        if (dubHi < 0) return false;
        lo = dubLo; hi = dubHi; dubLo = std::numeric_limits<int>::max(); dubHi = -1;
        return true;
    }

    // ── the history, for the take strip's live view (read only) ──
    // Peaks of each kLiveColTicks of history, by position in the ring; histW / kLiveColTicks
    // is the column being written now.
    const float* liveColumns() const { return liveCols.data(); }
    // Ticks written since the buffer was made: what is new since a reader last looked.
    int64_t histWritten() const { return histTotal; }
    int histWritePos() const { return histW; }
    // The strip's window, in ticks. In CAPTURE, the most a tap keeps. In RECORD, the chosen
    // bars, so the whole take fits as it records; with FREE, 8 s, widening as the take grows.
    int memoryTicks() const {
        if (p.recMode != 1) return maxCaptureSamples();
        const double clock = clockHz(e.clock, p.smooth);
        double want = p.recBars > 0 ? p.recBars * std::max(0.25, p.barQuarters) * 60.0 / std::max(5.0, p.bpm) * clock : 8.0 * clock;
        if (p.recBars == 0) want = std::max(want, takeSoFar() + clock);
        return static_cast<int>(std::min<double>(maxCaptureSamples(), std::max(64.0, want)));
    }
    // Ticks kept so far by a RECORD take, or by a CAPTURE hold; -1 when nothing is.
    int takeSoFar() const {
        auto since = [&](int idx, uint32_t cycle) {
            const auto cycles = static_cast<uint32_t>(histCycles - cycle);
            const int64_t n = static_cast<int64_t>(cycles) * static_cast<int64_t>(hist.size()) + histW - idx;
            return static_cast<int>(std::min<int64_t>(maxCaptureSamples(), std::max<int64_t>(0, n)));
        };
        if (rec.phase == RecPhase::Recording) return since(rec.histIdx, rec.histCycle);
        if (fsLoop.active && fsLoop.held && !fsLoop.playing && p.recMode == 0) return since(fsLoop.histIdx, fsLoop.histCycle);
        return -1;
    }
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
    // The widest the playback window can be: the whole take in ENV and STRETCH, and
    // LENGTH's share of it in TAPE. END is a fraction of this, so the editor needs it
    // to turn a marker dragged on the waveform back into START and END.
    int loopSpan() const {
        const int len = std::max(64, lsrc);
        if (p.loopMode != 1) return len;
        return std::max(64, static_cast<int>(std::lround(len * std::min(1.0, 0.02 + 0.98 * clampd(e.length, 0, 1)))));
    }


private:
    struct Smoothed { double time, mix, length, modWet, clock, modLoop, balance, tone, fade, width, modRate[2]; };
    // What the DSP reads: the smoothed knobs plus whatever a ramp is moving.
    struct Eff { double time, mix, length, modWet, clock, modLoop, balance; };
    struct Ramp { double ph = 0, dir = 1, prev = 0.5, next = 0.5; uint32_t rng = 0; };
    struct FsWet { bool active = false; int64_t t = 0; bool held = false; };
    struct FsLoop { bool active = false; int64_t t = 0; bool held = false; int histIdx = 0; uint32_t histCycle = 0; bool playing = false; };
    enum class RecPhase { Idle, Armed, Recording };
    struct Recording { RecPhase phase = RecPhase::Idle; int histIdx = 0; uint32_t histCycle = 0; int64_t endAt = -1; double barTarget = 0; };
    Recording rec;
    struct Grain { double ph, start, dir; };
    struct Window { int W, start; };

    double fs = 48000;
    Smoothed s{};
    Eff e{};
    int64_t hostCount = 0;
    double phase = 0, clockRatio = 1, filterClock = -1;
    int filterClassic = -1;
    LowPassN aa, reconL, reconR, aaHi[2];
    // The last four core ticks per channel, oldest first. The host-rate output is
    // a Hermite curve through the middle two (one tick of latency), or for
    // CLASSIC the old straight line between the newest two.
    double ticks[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    double xin[4] = {0, 0, 0, 0};   // the last four host input samples, oldest first
    double xinHi[8] = {0, 0, 0, 0, 0, 0, 0, 0}, ticksHi[2][16] = {{0}, {0}};   // HI-Q's longer memory of both
    int xinHiW = 0, ticksHiW = 0;
    double hiMix = 0;               // HI-Q, faded in and out so switching it doesn't click

    Reverb reverb; Delay delay; Slip slip; Weave weave;
    bool wetOn = false, freeze = false, wetMomentary = false;
    double wetIn = 0, wetOut = 0, modeGain[4] = {1, 0, 0, 0};
    Allpass decor[2];

    std::vector<float> hist, loopBuf, liveCols;
    // UNDO's copy: the take before the last CLEAR, new take or overdub layer (or after it,
    // once undone). Swapped with loopBuf, never copied, except when a layer begins.
    std::vector<float> undoBuf;
    int undoLen = 0;
    enum class UndoKind { None = 0, Take = 1, Clear = 2, Dub = 3 };
    UndoKind undoKind = UndoKind::None;
    bool undoRedo = false;
    enum class LoopOp { None, Clear, Undo };
    LoopOp pendingOp = LoopOp::None;   // waiting for the loop to fade out
    bool resumeAfterOp = false;
    bool dubLatched = false, latchSeen = false;
    bool fitted = false;
    // ENV synced to a playing host: the grid line the last tick was in, at which slice length.
    bool sliceFresh = false;
    double gridS = -1, gridLine = 0;
    // Transport takes: caught while the host played, anchored at the song position of their
    // first sample. The undo copy keeps its own.
    bool takeFollows = false, undoFollows = false, parked = false;
    double anchorPpq = 0, undoAnchor = 0;
    bool lastHostPlaying = false;
    double lastPpq = 0;
    int lastBlockN = 0;
    int64_t blockStartCount = 0;
    bool pendingLock = false;
    double lockGain = 1;
    int64_t histTotal = 0;
    int histW = 0, histCount = 0, lsrc = 0;
    uint32_t histCycles = 0;
    LoopState loopState = LoopState::Off;
    bool overdub = false;
    double dubGain = 0, dubPrev = 0, dubPos = 0;
    int dubW = 64, dubStart = 0;
    double loopDbSeen = 0, loopLevelTarget = 1, loopLevel = 1;
    int dubLast = -1;
    int dubLo = std::numeric_limits<int>::max(), dubHi = -1;   // cells written since the strip looked
    double loopGain = 0, loopPos = 0, tapeSpeed = 1, stretchSpeed = 0;
    // Moving START moves the read head, and a jump is a click. These three carry a
    // short crossfade from where the loop was playing to where it now begins.
    int startA = 0, startB = 0;                // whole samples: never fractional
    double startFade = 1;
    int startLen = -1;
    // LENGTH in TAPE sizes the window. When it moves, the playhead keeps its place as a
    // share of the window (the way the Digitakt does it), and the window's start follows
    // without a crossfade, since nothing jumps. These say what the last tick played.
    int lastW = -1, lastWant = 0, lastMode = -1;
    double lengthSlew = -1;
    double lastTrim = -1, lastWhere = -1;
    double env = 0, envHold = 0, envMix = 0, sliceStart = 0, slicePh = 0;
    double softStart = 0, softPh = 0, softS = 0, softOldFade = 0, softOldS = 0;   // SOFT: the slice being let go
    bool envGate = false;
    Grain grain[2] = {{0, 0, 1}, {0.5, 0, 1}};
    uint32_t rng = 22222;
    FsWet fsWet; FsLoop fsLoop;
    double engaged = 0, peakIn = 0, peakOut = 0;
    bool loopWaveDirty = false;
    double warmIn[2] = {0, 0}, warmOut[2] = {0, 0};
    double toneL = 0, toneR = 0, wowPh = 0;   // hidden-option state

    // Two generators, each with its own phase and its own seeded stream (never the
    // shared rng: STRETCH grains and CLASSIC hiss draw from that one).
    Ramp ramp[2] = {{0, 1, 0.5, 0.5, 0x9e3779b9u}, {0.5, 1, 0.5, 0.5, 0x5bd1e995u}};
    double rampRaw[2] = {0, 0};     // per host sample
    double rampVal[2] = {0, 0};     // smoothed to audio rate
    double modGainS[2] = {1, 1};    // ON, smoothed
    double modDepthS[2][kModTargets] = {{0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}};   // depths, smoothed

    double rand01() { rng = rng * 1103515245u + 12345u; return rng / 4294967296.0; }

    // A second, private random stream per generator: the shared rng belongs to the
    // STRETCH grains and CLASSIC hiss, and drawing from it here would move them.
    double rampRand(Ramp& g) {
        g.rng = g.rng * 1664525u + 1013904223u;
        return g.rng / 4294967296.0;
    }

    // Value of the chosen shape at the current phase, 0..1.
    static double rampShape(const Ramp& g, int wave, bool sweep) {
        const double ph = g.ph;
        if (sweep) return ph;                                     // SWEEP: it only ever climbs
        if (wave == 1) return ph < 0.5 ? 1.0 : 0.0;               // square
        if (wave == 2) return 0.5 + 0.5 * std::sin(kTau * ph);
        if (wave == 3) return g.next;                             // sample and hold
        if (wave == 4) {                                          // smooth random
            const double t = ph * ph * (3 - 2 * ph);
            return g.prev + (g.next - g.prev) * t;
        }
        return 1 - std::fabs(2 * ph - 1);                         // triangle
    }

    // Advance both sources. Once per host sample, not per core tick: a source is a time
    // function, so turning CLOCK down must not slow it down. BOUNCE ping-pongs the phase
    // instead of letting it reset. HOLD stops the phase, so the knobs stay pushed
    // wherever it had got to and it picks up from there. A source with no routes still
    // runs, so its chip shows what it would do.
    void rampTick() {
        for (int k = 0; k < 2; ++k) {
            const Params::ModSource& m = p.mods[k];
            Ramp& g = ramp[k];
            const int wave = rampWaveIndex(m.wave);
            if (!m.hold) {
                // Free-running by default; synced, one cycle is a musical span instead.
                const double hz = m.sync ? 1.0 / rampCycleSeconds(p.bpm, s.modRate[k]) : rampHertz(s.modRate[k]);
                g.ph += hz / fs * g.dir;
                if (g.ph >= 1) {
                    if (m.bounce) { g.ph = 2 - g.ph; g.dir = -1; } else { g.ph -= 1; }
                    g.prev = g.next; g.next = rampRand(g);
                } else if (g.ph <= 0) {
                    if (m.bounce) { g.ph = -g.ph; g.dir = 1; } else { g.ph += 1; }
                    g.prev = g.next; g.next = rampRand(g);
                }
            }
            rampRaw[k] = rampShape(g, wave, m.sweep);
        }
    }

    // Rebuild the effective controls from the smoothed knobs, then let each source push
    // what it's routed to, around where the knob sits.
    void applyRamps(double a) {
        e.time = s.time; e.mix = s.mix; e.length = s.length; e.modWet = s.modWet;
        e.clock = s.clock; e.modLoop = s.modLoop; e.balance = s.balance;
        double* field[kModTargets] = {&e.time, &e.mix, &e.length, &e.modWet, &e.clock, &e.modLoop, &e.balance};
        for (int k = 0; k < 2; ++k) {
            const Params::ModSource& m = p.mods[k];
            rampVal[k] += (rampRaw[k] - rampVal[k]) * a;
            modGainS[k] += ((m.on ? 1.0 : 0.0) - modGainS[k]) * a;
            const double swing = (m.uni ? rampVal[k] : rampVal[k] * 2 - 1) * modGainS[k];
            for (int t = 0; t < kModTargets; ++t) {
                modDepthS[k][t] += (m.depth[t] - modDepthS[k][t]) * a;
                *field[t] += swing * modDepthS[k][t];
            }
        }
        for (auto* f : field) *f = clampd(*f, 0, 1);
    }

    // One-pole coefficient for the TONE hi-cut; the blend amount is the caller's.
    // It is built from constants only, so it is worked out once. (It used to be
    // cached behind a `toneC == 0` test, which is an equality test on a float:
    // a sentinel that only works by luck of rounding.)
    static double toneCoeff() {
        static const double c = 1 - std::exp((-kTau * kToneHz) / kCoreFs);
        return c;
    }

    // How much history a take may use, in core samples. In CAPTURE it's MEMORY: 0 keeps the
    // pedal's clock-derived capacity, above that 1–30 s of real time. RECORD ignores MEMORY:
    // the bars, or the second tap, decide, up to all the buffer holds (32 s).
    int maxCaptureSamples() const {
        const double clock = clockHz(e.clock, p.smooth);
        const double most = std::min<double>(kMaxLoopSamples - 1, kMaxLoopSeconds * clock);
        if (p.recMode == 1) return static_cast<int>(std::max(64.0, most));
        const double user = p.memory > 0.001 ? expMap(clampd(p.memory, 0, 1), 1, 30) * clock : kLoopSamples;
        return static_cast<int>(std::max(64.0, std::min(user, most)));
    }

    // RECORD: from here, for the chosen bars (from the next bar line, when `atBar`).
    void startRecording(bool atBar) {
        if (atBar) { rec.histIdx = histW; rec.histCycle = histCycles; }
        rec.phase = RecPhase::Recording;
        rec.endAt = p.recBars > 0 ? hostCount + static_cast<int64_t>(std::llround(p.recBars * std::max(0.25, p.barQuarters) * 60.0 / std::max(5.0, p.bpm) * fs)) : -1;
    }
    bool canOverdub() const { return loopState == LoopState::Play && lsrc >= 64 && pendingOp == LoopOp::None; }
    // A new overdub layer: the take as it stands becomes UNDO's copy first.
    void beginLayer() {
        if (overdub) return;
        std::copy(loopBuf.begin(), loopBuf.begin() + lsrc, undoBuf.begin());
        undoLen = lsrc; undoKind = UndoKind::Dub; undoRedo = false;
        undoFollows = takeFollows; undoAnchor = anchorPpq;
        overdub = true;
    }
    void doClear() {
        std::swap(loopBuf, undoBuf);
        undoFollows = takeFollows; undoAnchor = anchorPpq; takeFollows = false; parked = false;
        undoLen = lsrc; lsrc = 0; loopPos = 0;
        undoKind = UndoKind::Clear; undoRedo = false;
        loopWaveDirty = true;
    }
    void runOp(LoopOp op) {
        if (op == LoopOp::Clear) { doClear(); return; }
        std::swap(loopBuf, undoBuf); std::swap(lsrc, undoLen);
        std::swap(takeFollows, undoFollows); std::swap(anchorPpq, undoAnchor);   // each take keeps its kind
        undoRedo = !undoRedo;
        if (undoKind != UndoKind::Dub) {
            loopPos = 0; grain[0] = {0, 0, 1}; grain[1] = {0.5, 0, 1};
            if (lsrc >= 64) fitWindow();   // another take: it plays all of itself, as a new one does
        }
        loopWaveDirty = true;
    }
    // A CLEAR or UNDO waiting for the loop to fade out happens now; a layer's undo plays on.
    // An undone take plays, since a tap with the loop stopped would catch a new one instead;
    // a transport take with the host stopped parks and waits for play. Undoing a layer keeps
    // a playing loop playing and a stopped one stopped.
    void settlePending() {
        if (pendingOp == LoopOp::None) return;
        const LoopOp op = pendingOp;
        pendingOp = LoopOp::None;
        const bool layer = undoKind == UndoKind::Dub;
        runOp(op);
        parked = false;
        if (op == LoopOp::Undo && lsrc >= 64 && (!layer || resumeAfterOp)) {
            if (takeFollows && !p.hostPlaying) parked = true;
            else { loopState = LoopState::Play; if (takeFollows) { pendingLock = true; lockGain = 0; } }
        }
        resumeAfterOp = false;
    }

    // ── the host's transport ──
    // The song position now, in quarter notes, from this block's start and the host samples since.
    double songNow() const { return p.ppq + static_cast<double>(hostCount - blockStartCount) * p.bpm / (60.0 * fs); }
    // Where a transport take's window should be at this song position, in ticks into it.
    double songTicks(int winStart, double Wd) const {
        const double secs = (songNow() - anchorPpq) * 60.0 / std::max(5.0, p.bpm);
        double q = jsMod(secs * clockRatio * fs - winStart, Wd); if (q < 0) q += Wd;
        return q;
    }
    // Once a block: a transport take fades out and parks when the host stops, comes back
    // locked to the song when it plays, and is put back in time after a jump in song position.
    void followTransport(int n) {
        const bool playing = p.hostPlaying;
        if (takeFollows && lsrc >= 64) {
            if (lastHostPlaying && !playing && loopState == LoopState::Play) {
                loopState = LoopState::Off; overdub = false; dubLatched = false; parked = true;
            } else if (!lastHostPlaying && playing && parked) {
                parked = false; loopState = LoopState::Play; pendingLock = true; lockGain = 0;
            } else if (lastHostPlaying && playing && loopState == LoopState::Play && lastBlockN > 0) {
                const double expected = lastPpq + lastBlockN * p.bpm / (60.0 * fs);
                if (std::fabs(p.ppq - expected) > 0.05) pendingLock = true;   // a jump, not drift
            }
        }
        lastHostPlaying = playing; lastPpq = p.ppq; lastBlockN = n;
    }

    // Everything written since the recording began becomes the loop, which plays at once.
    void closeRecording() {
        const auto cycles = static_cast<uint32_t>(histCycles - rec.histCycle);
        const int64_t written = static_cast<int64_t>(cycles) * static_cast<int64_t>(hist.size()) + histW - rec.histIdx;
        rec.phase = RecPhase::Idle;
        capture(static_cast<int>(std::min<int64_t>(static_cast<int64_t>(hist.size()) - 1, std::max<int64_t>(0, written))));
    }

    void capture(int n) {
        n = std::min({n, static_cast<int>(hist.size()) - 1, histCount, maxCaptureSamples()});
        if (n < 64) return;
        const int N = static_cast<int>(hist.size());
        // The take this replaces becomes UNDO's copy: a swap, nothing copied.
        std::swap(loopBuf, undoBuf); undoLen = lsrc >= 64 ? lsrc : 0;
        undoFollows = takeFollows; undoAnchor = anchorPpq;
        undoKind = UndoKind::Take; undoRedo = false;
        int r = histW - n; if (r < 0) r += N;
        for (int i = 0; i < n; ++i) { loopBuf[static_cast<size_t>(i)] = hist[static_cast<size_t>(r)]; if (++r >= N) r = 0; }
        lsrc = n; loopPos = 0;
        tapeSpeed = speedMap(e.modLoop, 0.04);
        stretchSpeed = stretchMap(e.modLoop);
        grain[0] = {0, 0, 1}; grain[1] = {0.5, 0, 1};
        loopState = LoopState::Play;
        loopWaveDirty = true;
        fitWindow();
        // Caught while the host plays: it follows the transport, anchored at the song position
        // its first sample belongs to.
        takeFollows = p.hostPlaying; parked = false; pendingLock = false; lockGain = 1;
        anchorPpq = songNow() - n / std::max(1.0, clockRatio * fs) * p.bpm / 60.0;
    }

    // A new take plays all of itself: END at the end, START out of the way and, in TAPE,
    // LENGTH full, so 2 bars recorded is 2 bars heard. The core sets its own copy at once,
    // so not one tick plays the old window; takeFitted() tells the host to move its knobs.
    void fitWindow() {
        p.loopEnd = 1; p.start = 1;
        if (p.loopMode == 1) { p.length = 1; s.length = 1; }
        lengthSlew = -1; startLen = -1;   // the window starts afresh, not gliding from the old one
        fitted = true;
    }

    // Playback region of the captured loop. LENGTH decides how big the window is
    // (TAPE takes its from the tail of the capture, ENV and STRETCH from the whole
    // take), END decides how much of that window sounds before it wraps, and START
    // decides where the window sits inside the capture — 0 is where each mode has
    // always begun. Mirrors loopWindow() in src/brood-worklet.js.
    Window loopWindow() const { return loopWindowAt(e.length); }
    // The window for a given LENGTH. Overdub asks for the knob's own, ignoring modulation.
    Window loopWindowAt(double lengthNow) const {
        const int len = std::max(64, lsrc);
        const double trim = clampd(p.loopEnd, 0.02, 1);
        const double where = clampd(p.start, 0, 1);
        if (p.loopMode != 1) {
            const int W0 = std::max(64, static_cast<int>(std::floor(len * trim)));
            // ENV and STRETCH: END says how much of the take plays, START says where
            // that window sits in it. Zero is the oldest audio, which is where these
            // two modes have always played from, so nothing changes at the default.
            //
            // START is measured against the take, not against the room END happens to
            // leave, so dragging END moves the end marker and nothing else. It gives
            // ground only when the window would otherwise run past the end of the
            // take — and it cannot move at all while the window fills the take, since
            // there is nowhere to slide to.
            const int travel = std::max(0, static_cast<int>(std::lround(where * std::max(0, len - 64))));
            const int start = std::min(travel, std::max(0, len - W0));
            return {W0, start};
        }
        // Linear in the fraction of the capture, so at 1 the window is everything
        // and the start of a long recording is actually reachable; the exponential
        // curve spent 95% of the knob's travel on the last few seconds.
        // Rounded, not floored: at 1 the window is exactly the whole take.
        const int W0 = std::max(64, static_cast<int>(std::lround(len * std::min(1.0, 0.02 + 0.98 * clampd(lengthNow, 0, 1)))));
        // START means the same as in ENV and STRETCH: 0 is the oldest audio, 1 the newest.
        // At 1 the window sits against the end of the take whatever LENGTH is, so TAPE
        // still follows the newest audio as LENGTH turns; anywhere else START is a fixed
        // point in the take that LENGTH grows and shrinks the window from.
        const int travel = std::max(0, static_cast<int>(std::lround(where * std::max(0, len - 64))));
        return {std::max(64, static_cast<int>(std::floor(W0 * trim))), std::min(travel, std::max(0, len - W0))};
    }

    // `x` is what the looper listens to; `rec` is what an overdub lays down.
    double loopTick(double x, double rec) {
        const double ax = std::fabs(x);
        env += (ax - env) * (ax > env ? 0.02 : 0.0005);

        const double target = loopState == LoopState::Play ? 1.0 : 0.0;
        loopGain += (target - loopGain) * 0.003;
        if (loopState == LoopState::Off && loopGain < 1e-4) {
            loopGain = 0; dubGain = 0; dubLast = -1;
            if (pendingOp != LoopOp::None) settlePending();   // faded out: CLEAR or UNDO now
            return 0;
        }
        if (lsrc < 64) return 0;

        // TAPE: the window follows LENGTH at up to three samples a tick, so the playhead,
        // which keeps its share of the window, never runs faster than four times normal
        // (two octaves up). Faster, deep LENGTH modulation zipped through the take.
        const int len0 = std::max(64, lsrc);
        const double maxL = kLengthFollow / (0.98 * len0);
        if (lengthSlew < 0 || lsrc != startLen) lengthSlew = e.length;
        else lengthSlew += clampd(e.length - lengthSlew, -maxL, maxL);
        const Window win = p.loopMode == 1 ? loopWindowAt(lengthSlew) : loopWindow();
        const int W = win.W;
        const int len = std::max(64, lsrc);
        const double Wd = W;
        // The song lock: dip for 5 ms, move the playhead to where the song says, come back.
        if (pendingLock) {
            lockGain = std::max(0.0, lockGain - 1.0 / (0.005 * kCoreFs));
            if (lockGain <= 0) { loopPos = songTicks(win.start, Wd); pendingLock = false; }
        } else if (lockGain < 1) lockGain = std::min(1.0, lockGain + 1.0 / (0.005 * kCoreFs));
        const int mode = p.loopMode;
        const double trimNow = clampd(p.loopEnd, 0.02, 1), whereNow = clampd(p.start, 0, 1);
        // Only LENGTH moved the window: same mode, same END, same START, same take.
        const bool lengthMoved = mode == 1 && lastMode == 1 && lsrc == startLen && trimNow == lastTrim && whereNow == lastWhere && lastW > 0 && W != lastW;
        if (lengthMoved) loopPos = loopPos * Wd / lastW;
        // A change of loop mode keeps the playhead where it is in the take when the new
        // window holds it; otherwise it crossfades from there to the new window's start.
        const bool modeSwitched = lastMode >= 0 && mode != lastMode && lsrc == startLen && lastW > 0;
        int switchFrom = -1;
        if (modeSwitched) {
            const double absNow = lastWant + loopPos;
            const double inNew = absNow - win.start;
            if (inNew >= 0 && inNew < Wd) loopPos = inNew;
            else { loopPos = 0; switchFrom = static_cast<int>(std::floor(absNow)); }
        }
        const double m = e.modLoop;
        // LOOP CLOCK DIVISION: the loop reads at 1/div of the clock, so a divided
        // loop plays back slower and lower and takes div times as long to come round.
        const double ldivd = divMap(p.loopDiv);
        const double lstep = 1 / ldivd;
        double pos = jsMod(loopPos, Wd); if (pos < 0) pos += Wd;
        const double posAtStart = pos;

        // START moves the read head, and a jump in the read head is a click. So a
        // change of START is a crossfade from where the loop was playing to where it
        // now begins: 25 ms, weighted by a raised cosine so the fade has no corners
        // of its own either. A capture or a restore resets it — there is a gain ramp
        // around both, so landing instantly there cannot be heard.
        const int wantStart = win.start;   // ('target' is taken: the loop's gain target)
        if (lsrc != startLen) { startLen = lsrc; startA = startB = wantStart; startFade = 1; }
        if (lengthMoved && wantStart != lastWant) {
            // The window slid with LENGTH and the playhead slid with it: move both heads
            // the same way, so any fade under way carries on and none starts.
            startA += wantStart - lastWant;
            startB += wantStart - lastWant;
        }
        if (modeSwitched) {
            if (switchFrom >= 0) { startA = switchFrom; startB = wantStart; startFade = 0; }
            else { startA = startB = wantStart; startFade = 1; }
        }
        lastW = W; lastWant = wantStart; lastMode = mode; lastTrim = trimNow; lastWhere = whereNow;
        if (startFade >= 1) {
            startA = startB;                          // the last fade has landed
            if (wantStart != startA) { startB = wantStart; startFade = 0; }
        } else {
            // The head we are fading *to* stays put while it fades in. Moving it
            // mid-fade is the same jump in a smaller dose, which is what a first
            // attempt did: a knob still being dragged simply starts the next fade
            // as soon as this one lands.
            startFade = std::min(1.0, startFade + 1.0 / (kCoreFs * 0.025));
        }
        const double wB = 0.5 - 0.5 * std::cos(kPi * startFade);   // 0 → A, 1 → B
        auto at = [&](double base, double q0) {
            double q = jsMod(q0, Wd); if (q < 0) q += Wd;
            const double abs = base + q;
            int i0 = static_cast<int>(std::floor(abs));
            double f = abs - i0;
            if (i0 < 0) { i0 = 0; f = 0; }
            int i1 = i0 + 1;
            if (i1 >= len) { i1 = len - 1; if (i0 > i1) i0 = i1; f = 0; }
            // Hermite, with the neighbours held at the take's edges rather than
            // wrapped: the take is not circular, the window is.
            const int im1 = i0 > 0 ? i0 - 1 : 0;
            const int i2 = std::min(i1 + 1, len - 1);
            return hermite(loopBuf[static_cast<size_t>(im1)], loopBuf[static_cast<size_t>(i0)],
                           loopBuf[static_cast<size_t>(i1)], loopBuf[static_cast<size_t>(i2)], f);
        };
        auto rd = [&](double q0) {
            if (startFade >= 1) return at(startB, q0);
            if (startFade <= 0) return at(startA, q0);
            return at(startA, q0) * (1 - wB) + at(startB, q0) * wB;
        };
        const double F = std::min(0.004 * kCoreFs, Wd / 4);
        auto edgeAt = [&](double q0) { double q = jsMod(q0, Wd); if (q < 0) q += Wd; return std::min({1.0, q / F, (Wd - q) / F}); };
        double y;

        if (mode == 2) {
            const double G = std::min(expMap(e.length, 0.01 * kCoreFs, 0.5 * kCoreFs), Wd);
            // A divided loop stretches each grain by div as well, or the grains
            // would still be read at full speed.
            const double Gs = G * ldivd;
            // A grain keeps the direction it started with. Flipping grains already sounding
            // as MODIFY passes the middle jumped both read heads, and that was a click.
            const double dir = stretchSpeed < 0 ? -1.0 : 1.0;
            y = 0;
            for (auto& g : grain) {
                g.ph += 1 / Gs;
                if (g.ph >= 1) { g.ph -= 1; g.start = pos + (rand01() - 0.5) * 0.2 * G; g.dir = dir; }
                const double q = g.start + g.dir * g.ph * G;
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
            const bool soft = p.envSoft;
            if (env > thr) {
                if (!envGate) {
                    envGate = true;
                    // SOFT: a new slice while the last is still sounding would jump the
                    // read head, which is the click. The last one is let go over 20 ms.
                    if (soft && envMix > 1e-4) { softStart = sliceStart; softPh = slicePh; softOldS = softS; softOldFade = 1; }
                    sliceStart = pos; slicePh = 0; sliceFresh = true;
                }
                envHold = 0.08 * kCoreFs;
            } else if (envGate && --envHold <= 0) envGate = false;
            envMix += ((envGate ? 1.0 : 0.0) - envMix) * (soft ? 0.0008 : 0.005);
            const double softF = 0.02 * kCoreFs;
            const double mainF = std::min(soft ? softF : 0.004 * kCoreFs, Wd / 4);
            double mainEdge = edgeAt(pos);
            if (soft) {
                double q = jsMod(pos, Wd); if (q < 0) q += Wd;
                mainEdge = 0.5 - 0.5 * std::cos(kPi * std::min({1.0, q / mainF, (Wd - q) / mainF}));
            }
            const double mainV = rd(pos) * mainEdge;
            double sl = 0;
            if (envMix > 1e-4) {
                // ENV slices to a length, so synced LENGTH is a division of the beat,
                // converted into core ticks the same way the delay's is.
                const double S = p.envSync
                    ? std::min(Wd, std::max(64.0, syncSecondsRaw(p.bpm, e.length) * clockRatio * fs))
                    : std::min(expMap(e.length, 0.01 * kCoreFs, 1.0 * kCoreFs), Wd);
                // Synced with the host playing, slices land on the host's grid: a new one starts
                // on each grid line of the division, counted from the song position. A LENGTH
                // move changes the division, which re-bases the count without starting a slice,
                // so the new length takes over at its next grid line and nothing jumps.
                const bool grid = p.envSync && p.hostPlaying;
                if (grid) {
                    const double t = songNow() * 60.0 / std::max(5.0, p.bpm) * clockRatio * fs * lstep;   // the song, in slice ticks
                    const double line = std::floor(t / S);
                    const double into = t - line * S;
                    if (sliceFresh) slicePh = into;   // the envelope just opened: join the slice the grid is in
                    else if (S == gridS && line != gridLine) {
                        if (soft && envMix > 1e-4) { softStart = sliceStart; softPh = slicePh; softOldS = softS; softOldFade = 1; }
                        slicePh = into;
                    }
                    gridS = S; gridLine = line;
                    if (soft) softS = S;
                } else gridS = -1;
                sliceFresh = false;
                if (soft) {
                    // The slice length glides, so a moving LENGTH can't cut a fade short.
                    softS = softS <= 0 ? S : softS + (S - softS) * 0.0005;
                    if (slicePh >= softS) slicePh -= softS;
                    const double F3 = std::min(softF, softS / 4);
                    sl = rd(sliceStart + slicePh) * (0.5 - 0.5 * std::cos(kPi * std::min({1.0, slicePh / F3, (softS - slicePh) / F3})));
                    if (softOldFade > 0) {
                        const double F4 = std::min(softF, softOldS / 4);
                        const double shape = 0.5 - 0.5 * std::cos(kPi * std::clamp(std::min(softPh / F4, (softOldS - softPh) / F4), 0.0, 1.0));
                        sl += rd(softStart + softPh) * shape * (0.5 - 0.5 * std::cos(kPi * softOldFade));
                        softPh += lstep;
                        softOldFade = std::max(0.0, softOldFade - 1.0 / softF);
                    }
                } else {
                    if (!grid && slicePh >= S) slicePh -= S;   // on the grid, a slice cut short waits for its line
                    const double F2 = std::min(0.004 * kCoreFs, S / 4);
                    sl = rd(sliceStart + slicePh) * std::max(0.0, std::min({1.0, slicePh / F2, (S - slicePh) / F2}));
                }
                slicePh += lstep;
            }
            y = mainV * (1 - envMix) + sl * envMix;
            pos += lstep;
        }

        // CLASSIC: the MK I looper never sat still — a slow wow with a little flutter.
        double wow = 0;
        if (p.classic) {
            wowPh += (kTau * 0.9) / kCoreFs;
            if (wowPh > kTau) wowPh -= kTau;
            wow = 0.006 * std::sin(wowPh) + 0.002 * std::sin(wowPh * 5.7);
            pos += wow;
        }
        // WORN after the loop: the wobble is heard, not written. The overdub's head moves as
        // if the playhead were steady, so what it lays down isn't smeared.
        const double dubWow = p.wearPost ? wow : 0.0;

        // Overdub fades in and out over about 8 ms, because a hard start or stop is a step
        // recorded into the take, there on every pass from then on. Its write head keeps
        // its own place in the window it started in, moving at the read head's speed, so a
        // modulated LENGTH or START can't throw it around the take and leave seams: what's
        // laid down stays continuous, and the read head finds it as it comes round. It
        // writes every cell it crosses, once, and fades at that window's edges.
        dubGain += ((overdub ? 1.0 : 0.0) - dubGain) * 0.002;
        if (dubGain > 1e-4) {
            const bool starting = dubLast < 0;
            if (starting) {
                // The knob's window, not the modulated one, lined up with where the read
                // head is so an unmodulated loop layers exactly where you hear it.
                const Window base = loopWindowAt(s.length);
                dubW = base.W; dubStart = base.start;
                dubPos = startB + (mode == 1 ? loopPos : pos) - dubStart;
            }
            else dubPos += pos - posAtStart - dubWow;
            dubPos = jsMod(dubPos, dubW); if (dubPos < 0) dubPos += dubW;
            const int q = std::min(dubW - 1, static_cast<int>(std::floor(dubPos)));
            // FADE: how much of the old loop survives each overdub pass. 0 keeps
            // the pedal's slow decay; 1 lets the new layer replace it completely.
            const double loss = 1 - 0.97 * (1 - clampd(s.fade, 0, 1));
            const double dubF = std::min(0.004 * kCoreFs, dubW / 4.0);
            auto write = [&](int cellQ, double v) {
                const double amount = dubGain * std::min({1.0, cellQ / dubF, (dubW - cellQ) / dubF});
                const int wi = std::min(len - 1, std::max(0, dubStart + cellQ));
                dubLo = std::min(dubLo, wi); dubHi = std::max(dubHi, wi);   // for the strip's redraw
                float& cell = loopBuf[static_cast<size_t>(wi)];
                cell = static_cast<float>(cell * (1 - loss * amount) + v * amount);
            };
            if (starting) write(q, rec);
            else {
                int d = q - dubLast;
                if (d * 2 > dubW) d -= dubW; else if (d * 2 < -dubW) d += dubW;
                const int count = std::min(std::abs(d), 16), dir = d > 0 ? 1 : -1;
                for (int k = 1; k <= count; ++k)
                    write(((dubLast + dir * k) % dubW + dubW) % dubW, dubPrev + (rec - dubPrev) * k / count);
            }
            dubLast = q; dubPrev = rec;
        } else dubLast = -1;
        loopPos = jsMod(jsMod(pos, Wd) + Wd, Wd);
        // GAIN: the loop's own level, in dB, smoothed so a move doesn't click.
        if (p.loopDb != loopDbSeen) { loopDbSeen = p.loopDb; loopLevelTarget = std::pow(10.0, p.loopDb / 20.0); }
        loopLevel += (loopLevelTarget - loopLevel) * 0.003;
        return y * loopGain * loopLevel * lockGain;
    }

    void coreTick(double x) {
        // PRE: input, then wet, then loop, so the wet side is recorded into whatever the
        // looper keeps. It comes first, so it hears only the input and ROUTING stands
        // aside: feeding the loop to a wet side that feeds the loop would never settle.
        const bool pre = p.wetPre;
        double loopOut = pre ? 0.0 : loopTick(x, x);
        const double wetSrc = pre || loopState != LoopState::Play ? x : p.routing == 0 ? x : p.routing == 1 ? x + loopOut : loopOut;
        // The FREEZE switch is the hold gesture without the holding: it brings the
        // wet side up and freezes it, so a reverb can be left ringing on its own.
        const bool held = p.freezeLatch > 0.5;
        const bool wet = wetOn || held;
        const bool frozen = freeze || held;
        const double inT = wet ? 1.0 : 0.0;
        const double outT = wet || p.trails ? 1.0 : 0.0;
        wetIn += (inT - wetIn) * 0.003;
        wetOut += (outT - wetOut) * 0.003;
        for (int k = 0; k < 4; ++k) modeGain[k] += ((p.wetMode == k ? 1.0 : 0.0) - modeGain[k]) * 0.002;

        // WET CLOCK DIVISION: the wet side runs at 1/div of the clock, so every
        // time constant on that side is div times longer.
        const double wdiv = divMap(p.wetDiv);
        // CLASSIC: the MK I clock was noisy, most of all when it was slow.
        const double hiss = p.classic ? (rand01() * 2 - 1) * (0.0006 + 0.01 * (1 - clockRatio)) * clampd(p.wearNoise, 0, 1) : 0.0;
        // WORN after the loop, with the wet side ahead of the looper: hiss fed to the wet side
        // would be recorded with it, so it joins the output after the looper instead.
        const bool hissAfter = pre && p.wearPost;
        const double wIn = wetSrc * wetIn + (hissAfter ? 0.0 : hiss);
        double wl = 0, wr = 0;
        if (modeGain[0] > 1e-4) {
            // CLASSIC: the MK I reverb rings far longer than the MK II's.
            reverb.setDecay(expMap(e.time, 0.4, p.classic ? 32 : 25) * wdiv, expMap(e.time, 0.35, 1));
            const double* o = reverb.tick(wIn, e.modWet, frozen, e.time > 0.985);
            wl += o[0] * modeGain[0]; wr += o[1] * modeGain[0];
        }
        double mono = 0;
        if (modeGain[1] > 1e-4) {
            // Synced: the division is real seconds, converted into core ticks at the
            // current clock so the echoed note stays on the beat at any clock setting.
            double delayTicks = expMap(e.time, 0.02 * kCoreFs, 2 * kCoreFs);
            if (p.sync) delayTicks = clampd(syncSeconds(p.bpm, e.time) * clockRatio * fs, 1.0, kDelayMaxSec * kCoreFs * 0.95);
            delayTicks = clampd(delayTicks * wdiv, 1.0, kDelayMaxSec * kCoreFs * 0.95);
            mono += delay.tick(wIn, delayTicks, e.modWet, frozen, p.tapeDelay) * modeGain[1];
        }
        if (modeGain[2] > 1e-4) mono += slip.tick(wIn, expMap(e.time, 0.05 * kCoreFs, 1.5 * kCoreFs) * wdiv, slipSpeedMap(e.modWet), frozen) * modeGain[2];
        if (modeGain[3] > 1e-4) {
            double o[2];
            // Synced, TIME is a division of the beat: how far back the threads reach
            // becomes a musical distance, so a stitch lands on the grid.
            const double reach = p.sync
                ? std::min(kWeaveBufSec * 0.85, syncSecondsRaw(p.bpm, e.time) * wdiv)
                : std::min(2.8, (0.2 + e.time * 2.6) * wdiv);
            weave.tick(wIn, reach, e.modWet, frozen, o);
            wl += o[0] * modeGain[3]; wr += o[1] * modeGain[3];
        }
        wl += mono;
        wr += mono;
        // WIDTH is the whole stereo story now that SPREAD has gone: 0 is mono, 0.5
        // is the image as built, and above the middle the two sides stop sharing.
        const double wv = clampd(s.width, 0.0, 1.0);
        if (std::fabs(wv - 0.5) > 0.002) {
            const double mid = (wl + wr) * 0.5, side = (wl - wr) * 0.5 * (wv * 2);
            wl = mid + side; wr = mid - side;
        }
        if (wv > 0.5) {
            const double amt = (wv - 0.5) * 2;
            const double d = decor[1].run(decor[0].run((wl + wr) * 0.5));
            wl += d * 0.5 * amt; wr -= d * 0.5 * amt;
        }
        // TONE: hi-cut on the wet bus. A blend, so fully open is bit-transparent.
        const double toneAmt = 1 - clampd(s.tone, 0, 1);
        if (toneAmt > 1e-4) {
            // Closing it warms as well as darkens: the range's saturation, the same curve
            // Silt's filter drives, with a little second harmonic and its offset taken out.
            const auto warm = [](double x, double& in, double& out) {
                const double y = std::tanh(1.5 * x) / 1.5 + 0.03 * x * x / (1 + x * x);
                out = y - in + 0.9995 * out;
                in = y;
                return out;
            };
            const double warmL = warm(wl, warmIn[0], warmOut[0]), warmR = warm(wr, warmIn[1], warmOut[1]);
            const double c = toneCoeff();
            toneL += c * (warmL - toneL); toneR += c * (warmR - toneR);
            wl += (toneL - wl) * toneAmt;
            wr += (toneR - wr) * toneAmt;
        }
        wl *= wetOut; wr *= wetOut;
        if (pre) loopOut = loopTick(x, softClip(x + (wl + wr) * 0.5));

        // The history always advances, whether or not a loop is playing: pausing it
        // left a splice in the buffer and every extra tap stitched in another one,
        // which is what put clicks inside the next loop. It's clean unless PRE puts the
        // wet side ahead of the looper.
        const float kept = static_cast<float>(softClip(x + (pre ? (wl + wr) * 0.5 : 0)));
        hist[static_cast<size_t>(histW)] = kept;
        // The strip's picture of it: read only, never heard.
        float& col = liveCols[static_cast<size_t>(histW / kLiveColTicks)];
        if (histW % kLiveColTicks == 0) col = 0;
        col = std::max(col, std::fabs(kept));
        ++histTotal;
        if (++histW >= static_cast<int>(hist.size())) { histW = 0; ++histCycles; }
        if (histCount < static_cast<int>(hist.size())) ++histCount;
        // RECORD at the MEMORY ceiling: it closes, keeping what was played first.
        if (rec.phase == RecPhase::Recording) {
            const auto cycles = static_cast<uint32_t>(histCycles - rec.histCycle);
            const int64_t written = static_cast<int64_t>(cycles) * static_cast<int64_t>(hist.size()) + histW - rec.histIdx;
            if (written >= maxCaptureSamples()) closeRecording();
        }

        // WET / LOOP blends two complete worlds: the loop keeps its own level
        // across the whole knob and the wet side adds to it. It used to be ducked
        // against the wet level so the loop was not heard twice, but the wet side
        // always returns it late — and in SLIP and WEAVE it returns very little —
        // so the duck swallowed the loop in the middle of the travel. Mirrors
        // coreTick() in src/brood-worklet.js.
        const double bal = e.balance;
        const double wetLvl = std::min(1.0, 2 * (1 - bal)), loopLvl = std::min(1.0, 2 * bal);
        const double ld = loopOut * loopLvl;
        for (auto& ch : ticks) { ch[0] = ch[1]; ch[1] = ch[2]; ch[2] = ch[3]; }
        const double hissOut = hissAfter ? hiss * wetOut : 0.0;
        ticks[0][3] = (wl + hissOut) * wetLvl + ld;
        ticks[1][3] = (wr + hissOut) * wetLvl + ld;
        ticksHi[0][ticksHiW] = ticks[0][3]; ticksHi[1][ticksHiW] = ticks[1][3];
        ticksHiW = (ticksHiW + 1) & 15;
    }
};

}  // namespace brood
