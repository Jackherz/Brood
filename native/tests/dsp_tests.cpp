// Property tests for the C++ core. Behavioural equivalence with the JS reference
// is covered separately by tests/parity (bit-for-bit within float rounding).
#include <cmath>
#include <string>
#include <cstdio>
#include <functional>
#include <vector>
#include "dsp/mood.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

constexpr double FS = 48000;
constexpr int B = 128;
struct Out { std::vector<float> L, R; };

static Out render(mood::Core& c, double secs, const std::function<double(double)>& src,
                  const std::function<void(mood::Core&, long)>& at = nullptr) {
    const long n = static_cast<long>(std::ceil(secs * FS / B));
    Out o; o.L.resize(static_cast<size_t>(n * B)); o.R.resize(static_cast<size_t>(n * B));
    float x[B];
    for (long b = 0; b < n; ++b) {
        if (at) at(c, b);
        for (int i = 0; i < B; ++i) x[i] = static_cast<float>(src((b * B + i) / FS));
        c.process(x, o.L.data() + b * B, o.R.data() + b * B, B);
    }
    return o;
}
static long blk(double s) { return std::lround(s * FS / B); }
static double rms(const std::vector<float>& a, double s, double e) {
    double acc = 0; const size_t i0 = static_cast<size_t>(s * FS), i1 = static_cast<size_t>(e * FS);
    for (size_t i = i0; i < i1; ++i) acc += a[i] * a[i];
    return std::sqrt(acc / static_cast<double>(i1 - i0));
}
static bool finite(const std::vector<float>& a) { for (float v : a) if (!std::isfinite(v)) return false; return true; }
static auto tone(double f, double dur) { return [=](double t) { return t < dur ? 0.4 * std::sin(2 * mood::kPi * f * t) : 0.0; }; }
static double hfEnergy(const std::vector<float>& a, double s, double e) {
    double acc = 0;
    for (size_t i = static_cast<size_t>(s * FS) + 1; i < static_cast<size_t>(e * FS); ++i) { const double d = a[i] - a[i - 1]; acc += d * d; }
    return std::sqrt(acc / static_cast<double>(static_cast<size_t>((e - s) * FS)));
}
static double sideEnergy(const std::vector<float>& L, const std::vector<float>& R) {
    double acc = 0;
    for (size_t i = 0; i < L.size(); ++i) { const double d = (L[i] - R[i]) * 0.5; acc += d * d; }
    return std::sqrt(acc / static_cast<double>(L.size()));
}
static double crossings(const std::vector<float>& a, double s, double e) {
    long n = 0;
    for (size_t i = static_cast<size_t>(s * FS) + 1; i < static_cast<size_t>(e * FS); ++i)
        if ((a[i - 1] <= 0) != (a[i] <= 0)) ++n;
    return n / 2.0 / (e - s);
}
static void tap(mood::Core& c, int which) { c.footswitch(which, true); c.footswitch(which, false); }

int main() {
    {   // bypass is transparent
        mood::Core c; c.prepare(FS);
        auto src = tone(220, 1);
        auto o = render(c, 0.5, src);
        double worst = 0;
        for (size_t i = 1000; i < 20000; ++i) worst = std::max(worst, std::fabs(o.L[i] - src(static_cast<double>(i) / FS)));
        CHECK(worst < 1e-6, "bypass error %g", worst);
    }
    {   // delay echo lands at TIME (0.2 s at the default knob)
        mood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.dryKill = true; c.p.modWet = 0.3; c.prepare(FS);
        tap(c, 0);
        // start in delay mode: let the reverb→delay mode crossfade finish before the click
        auto o = render(c, 1.4, [](double t) { return t > 0.5 && t < 0.502 ? 0.8 : 0.0; });
        size_t at = 0; float best = 0;
        for (size_t i = static_cast<size_t>(0.52 * FS); i < static_cast<size_t>(1.3 * FS); ++i) if (std::fabs(o.L[i]) > best) { best = std::fabs(o.L[i]); at = i; }
        const double echo = at / FS - 0.5;
        CHECK(std::fabs(echo - 0.2) < 0.01, "echo at %.4f s", echo);
    }
    {   // loop buffer length is set by the clock
        for (double knob : {1.0, 0.0}) {
            mood::Core c; c.p.clock = knob; c.prepare(FS);
            const double secs = knob == 1.0 ? 1.0 : 32.0;
            render(c, secs + 0.5, tone(330, 100), [&](mood::Core& cc, long b) { if (b == blk(secs + 0.4)) tap(cc, 1); });
            // Capacity is in core samples, so 64 000 of them is 1 s at 64 kHz and 32 s at 2 kHz.
            const double got = c.capturedLength() / mood::clockHz(knob);
            CHECK(std::fabs(got - (knob == 1.0 ? 1.0 : 32.0)) < 0.01, "clock %.0f: captured %.3f s", knob, got);
        }
    }
    {   // hold records a fresh loop of the held length
        mood::Core c; c.prepare(FS);
        render(c, 1.5, tone(330, 3), [](mood::Core& cc, long b) {
            if (b == blk(0.5)) cc.footswitch(1, true);
            if (b == blk(1.3)) cc.footswitch(1, false);
        });
        CHECK(std::fabs(c.capturedLength() / mood::kCoreFs - 0.8) < 0.01, "loop %.3f s", c.capturedLength() / mood::kCoreFs);
    }
    {   // every mode/routing combination stays finite and bounded
        for (int lm = 0; lm < 3; ++lm) for (int r = 0; r < 3; ++r) for (int wm = 0; wm < 4; ++wm) {
            mood::Core c; c.p.loopMode = lm; c.p.routing = r; c.p.wetMode = wm; c.p.modLoop = 0.1; c.p.modWet = 1; c.p.clock = 0.3;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 2, [](double t) { return 0.3 * std::sin(2 * mood::kPi * 196 * t) * std::exp(-3 * std::fmod(t, 0.5)); },
                            [](mood::Core& cc, long b) { if (b == blk(1.0)) tap(cc, 1); });
            bool bounded = true; for (float v : o.L) if (std::fabs(v) > 1.0f) bounded = false;
            CHECK(finite(o.L) && finite(o.R) && bounded, "loop %d routing %d wet %d", lm, r, wm);
            CHECK(rms(o.L, 1.2, 2.0) > 1e-3, "silent: loop %d routing %d wet %d", lm, r, wm);
        }
    }
    {   // WEAVE: audible, repeatable at a fixed knob position, and it answers the knobs
        auto weaveRun = [](double time, double modify) {
            mood::Core c; c.p.wetMode = 3; c.p.mix = 1; c.p.dryKill = true; c.p.time = time; c.p.modWet = modify;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 3, [](double t) {
                const double ph = std::fmod(t, 1.0);
                return ph < 0.75 ? 0.35 * std::sin(2 * mood::kPi * 330 * t) : 0.0;   // gated tone keeps the buffer fresh
            });
            return rms(o.L, 1.5, 3.0);
        };
        const double texture = weaveRun(0.5, 0.3), again = weaveRun(0.5, 0.3);
        const double rhythm = weaveRun(0.5, 0.85), deep = weaveRun(0.95, 0.3);
        CHECK(texture > 5e-3, "weave silent: %g", texture);
        CHECK(std::fabs(texture - again) < 1e-9, "weave not repeatable: %g vs %g", texture, again);
        CHECK(std::fabs(texture - rhythm) > 1e-4, "weave ignores MODIFY");
        CHECK(std::fabs(texture - deep) > 1e-4, "weave ignores TIME");
    }
    {   // WEAVE stays bounded with the nastiest settings
        mood::Core c; c.p.wetMode = 3; c.p.mix = 1; c.p.modWet = 1; c.p.time = 1; c.p.clock = 0.0; c.p.spread = true;
        c.prepare(FS); tap(c, 0);
        auto o = render(c, 2, tone(196, 5));
        bool bounded = true; for (float v : o.L) if (std::fabs(v) > 1.0f) bounded = false;
        CHECK(finite(o.L) && finite(o.R) && bounded, "weave blew up");
    }
    {   // MEMORY raises the capture ceiling; 0 keeps the pedal's clock-derived rule
        auto capturedSeconds = [](double memory, double clockKnob) {
            mood::Core c; c.p.memory = memory; c.p.clock = clockKnob; c.prepare(FS);
            render(c, 40, tone(330, 100), [&](mood::Core& cc, long b) { if (b == blk(39.0)) tap(cc, 1); });
            return c.capturedLength() / mood::clockHz(clockKnob);
        };
        CHECK(std::fabs(capturedSeconds(0, 1) - 1.0) < 0.02, "pedal rule @64k = %.2f s", capturedSeconds(0, 1));
        CHECK(std::fabs(capturedSeconds(0, 0) - 32.0) < 0.1, "pedal rule @2k = %.2f s", capturedSeconds(0, 0));
        CHECK(std::fabs(capturedSeconds(1, 1) - 30.0) < 0.05, "memory max = %.2f s", capturedSeconds(1, 1));
        CHECK(capturedSeconds(0.5, 1) > 4.0 && capturedSeconds(0.5, 1) < 7.0, "memory mid = %.2f s", capturedSeconds(0.5, 1));
    }
    {   // END trims the end of the loop and never moves where it starts
        for (int mode = 0; mode < 3; ++mode) {
            mood::Core c; c.p.loopMode = mode; c.p.length = 0.72; c.prepare(FS); tap(c, 1);
            render(c, 3, tone(330, 10), [&](mood::Core& cc, long b) { if (b == blk(2.0)) tap(cc, 1); });
            const auto full = c.state();
            c.p.loopEnd = 0.5;
            const auto half = c.state();
            CHECK(half.winStart == full.winStart, "mode %d: END leaves the start alone (%d vs %d)", mode, half.winStart, full.winStart);
            CHECK(std::abs(half.winLen - full.winLen / 2) <= 1, "mode %d: END halves what plays (%d of %d)", mode, half.winLen, full.winLen);
            c.p.loopEnd = 0.02;
            CHECK(c.state().winLen >= 64, "mode %d: the trim has a floor", mode);
            CHECK(c.state().winStart == full.winStart, "mode %d: still no movement at the extreme", mode);
        }
    }
    {   // LENGTH is what moves a TAPE loop's start point, from the tail of the capture
        mood::Core c; c.p.loopMode = 1; c.p.length = 1.0; c.prepare(FS); tap(c, 1);
        render(c, 3, tone(330, 10), [&](mood::Core& cc, long b) { if (b == blk(2.0)) tap(cc, 1); });
        const auto longLoop = c.state();
        c.p.length = 0.4;
        render(c, 0.2, tone(330, 10));   // the smoothed knob only moves in process()
        const auto shortLoop = c.state();
        CHECK(shortLoop.winLen < longLoop.winLen, "a shorter LENGTH is a shorter loop");
        CHECK(shortLoop.winStart > longLoop.winStart, "and it starts later in the capture");
        CHECK(std::abs(shortLoop.winStart - (c.capturedLength() - shortLoop.winLen)) <= 1, "the window ends at the capture end");
    }
    {   // tempo sync puts the echo on the divisiuon, independent of the clock
        auto echoAt = [](double time, double bpm, double clock) {
            mood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.dryKill = true; c.p.modWet = 0.5;
            c.p.sync = true; c.p.bpm = bpm; c.p.time = time; c.p.clock = clock;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 3, [](double t) { return std::fabs(t - 1.0) < 1e-5 ? 0.8 : 0.0; });
            size_t at = 0; float best = 0;
            for (size_t i = static_cast<size_t>(FS) + 64; i < o.L.size(); ++i)
                if (std::fabs(o.L[i]) > best) { best = std::fabs(o.L[i]); at = i; }
            return at / FS - 1.0;
        };
        CHECK(std::fabs(echoAt(2.0 / 3, 120, 1) - 0.5) < 0.01, "sync 1/4 @120 = %.3f s", echoAt(2.0 / 3, 120, 1));
        CHECK(std::fabs(echoAt(2.0 / 3, 90, 1) - 2.0 / 3) < 0.01, "sync 1/4 @90 = %.3f s", echoAt(2.0 / 3, 90, 1));
        CHECK(std::fabs(echoAt(1.0 / 3, 120, 1) - 1.0 / 6) < 0.01, "sync 1/8T @120 = %.3f s", echoAt(1.0 / 3, 120, 1));
        CHECK(std::fabs(echoAt(2.0 / 3, 120, 0.4) - 0.5) < 0.01, "sync ignores the clock");
        CHECK(std::string(mood::syncLabel(2.0 / 3)) == "1/4", "sync label");
    }
    {   // balance fully to LOOP always returns the loop, whatever the routing
        for (int r = 0; r < 3; ++r) {
            mood::Core c; c.p.routing = r; c.p.balance = 1; c.p.mix = 1; c.p.loopMode = 1;
            c.prepare(FS); tap(c, 0);
            render(c, 1.2, tone(220, 5), [&](mood::Core& cc, long b) { if (b == blk(1.0)) tap(cc, 1); });
            auto o = render(c, 1, [](double) { return 0.0; });
            const double level = rms(o.L, 0.2, 0.9);
            CHECK(level > 0.05, "routing %d: loop silent at balance 100%% (rms %.4f)", r, level);
        }
    }

    // ───────────────────────── hidden options ─────────────────────────
    // These use only the public API (a tap on the wet footswitch engages the wet
    // channel), like the rest of this file.
    {   // divisions are stepped
        CHECK(mood::divMap(0) == 1 && mood::divMap(0.5) == 2 && mood::divMap(1) == 4, "divMap steps");
    }
    {   // TONE is a hi-cut on the wet bus
        auto hf = [](double value) {
            mood::Core c; c.p.wetMode = 0; c.p.mix = 1; c.p.time = 0.7; c.p.tone = value;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 3, [](double t) { return 0.4 * std::sin(2 * mood::kPi * 220 * t) + 0.2 * std::sin(2 * mood::kPi * 7000 * t); });
            return hfEnergy(o.L, 1.5, 3);
        };
        const double open = hf(1), shut = hf(0);
        CHECK(shut < open * 0.8, "tone hi-cut: %.4f vs %.4f", shut, open);
    }
    {   // STEREO WIDTH: 0 mono, 0.5 the built image, 1 wider
        auto image = [](double width) {
            mood::Core c; c.p.wetMode = 0; c.p.mix = 1; c.p.time = 0.7; c.p.width = width;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 2.5, [](double t) { return 0.3 * std::sin(2 * mood::kPi * 220 * t) + 0.1 * std::sin(2 * mood::kPi * 3100 * t); });
            return sideEnergy(o.L, o.R);
        };
        const double mid = image(0.5), mono = image(0), wide = image(1);
        CHECK(mid > 0.005, "reverb is stereo (%.4f)", mid);
        CHECK(mono < mid * 0.05, "width 0 collapses the sides (%.5f vs %.4f)", mono, mid);
        CHECK(wide > mid * 1.6, "width 1 widens (%.4f vs %.4f)", wide, mid);
    }
    {   // WET CLOCK DIVISION stretches the wet side only
        auto gap = [](double wetDiv) {
            mood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.dryKill = true; c.p.modWet = 0.3;
            c.p.time = 0.3; c.p.wetDiv = wetDiv;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 6, [](double t) { return t > 1.0 && t < 1.002 ? 0.8 : 0.0; });
            size_t at = 0; float best = 0;
            for (size_t i = static_cast<size_t>(1.02 * FS); i < o.L.size(); ++i)
                if (std::fabs(o.L[i]) > best) { best = std::fabs(o.L[i]); at = i; }
            return at / FS - 1.0;
        };
        const double one = gap(0), two = gap(0.5), four = gap(1);
        CHECK(std::fabs(two / one - 2) < 0.1, "wet div 2x: %.3f -> %.3f", one, two);
        CHECK(std::fabs(four / one - 4) < 0.2, "wet div 4x: %.3f -> %.3f", one, four);
    }
    {   // LOOP CLOCK DIVISION drops the loop pitch
        auto pitch = [](double loopDiv) {
            mood::Core c; c.p.loopMode = 0; c.p.mix = 1; c.p.loopDiv = loopDiv; c.p.clock = 1; c.p.time = 0.5;
            c.prepare(FS);
            auto o = render(c, 6, [](double t) { return 0.35 * std::sin(2 * mood::kPi * 220 * t); },
                            [](mood::Core& cc, long b) { if (b == blk(0.1)) cc.footswitch(1, true); else if (b == blk(0.3)) cc.footswitch(1, false); });
            return crossings(o.L, 3, 5);
        };
        const double p1 = pitch(0), p2 = pitch(0.5), p4 = pitch(1);
        CHECK(std::fabs(p1 - 220) < 12, "loop pitch 1x = %.1f", p1);
        CHECK(std::fabs(p2 - 110) < 8, "loop pitch 2x = %.1f", p2);
        CHECK(std::fabs(p4 - 55) < 6, "loop pitch 4x = %.1f", p4);
    }
    {   // FADE: a silent overdub can replace the old loop
        auto level = [](double fade) {
            mood::Core c; c.p.loopMode = 1; c.p.mix = 0.6; c.p.clock = 1; c.p.fade = fade;
            c.prepare(FS);
            render(c, 0.4, tone(200, 5), [](mood::Core& cc, long b) { if (b == blk(0.1)) cc.footswitch(1, true); else if (b == blk(0.3)) cc.footswitch(1, false); });
            render(c, 4.5, [](double) { return 0.0; }, [](mood::Core& cc, long b) { if (b == blk(0.1)) cc.footswitch(1, true); });
            auto o = render(c, 0.4, [](double) { return 0.0; });
            return rms(o.L, 0.2, 0.4);
        };
        const double keep = level(0), gone = level(1);
        CHECK(keep > 0.005, "fade 0 keeps the loop (%.4f)", keep);
        CHECK(gone < keep * 0.05, "fade 1 wipes it (%.5f vs %.4f)", gone, keep);
    }
    {   // CLASSIC: the slow clock is noisy
        auto noise = [](bool classic) {
            mood::Core c; c.p.wetMode = 0; c.p.mix = 1; c.p.time = 0.6; c.p.clock = 0; c.p.classic = classic;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 2, [](double) { return 0.0; });
            return rms(o.L, 1, 2);
        };
        const double clean = noise(false), noisy = noise(true);
        CHECK(clean < 1e-6, "silence stays silent (%.6f)", clean);
        CHECK(noisy > 1e-4, "classic clock noise (%.5f)", noisy);
    }

    // ─────────────────────────── the ramp engine ───────────────────────────
    {   // with every ramp off, the audio is exactly what it was before the engine
        auto run = [](bool withRamps) {
            mood::Core c; c.p.wetMode = 0; c.p.mix = 0.8; c.p.time = 0.5;
            if (withRamps) { c.p.rampWet = 0; c.p.rampLoop = 0; c.p.rampSpeed = 1; }
            c.prepare(FS); tap(c, 0);
            return render(c, 1, tone(220, 1));
        };
        const auto a = run(false), b = run(true);
        double worst = 0;
        for (size_t i = 0; i < a.L.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a.L[i] - b.L[i])));
        CHECK(worst == 0.0, "ramps off must not change a sample (%g)", worst);
    }
    {   // a TIME ramp moves the echo
        auto echoPair = [] {
            mood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.dryKill = true; c.p.modWet = 0.55;
            c.p.time = 0.4; c.p.rampWet = 0.2; c.p.rampSpeed = 0.35;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 5, [](double t) { return std::fabs(t - 1.0) < 1e-5 ? 0.8 : 0.0; });
            auto peakBetween = [&](double s0, double s1) {
                size_t at = 0; float best = 0;
                for (size_t i = static_cast<size_t>(s0 * FS); i < std::min(o.L.size(), static_cast<size_t>(s1 * FS)); ++i)
                    if (std::fabs(o.L[i]) > best) { best = std::fabs(o.L[i]); at = i; }
                return at / FS;
            };
            return std::pair<double, double>(peakBetween(1.02, 2.2), peakBetween(3.0, 5.0));
        };
        const auto p = echoPair();
        CHECK(std::fabs(p.first - p.second) > 0.01, "the ramp moves the echo (%.3f then %.3f)", p.first, p.second);
    }
    {   // shapes: square sits on the ends, triangle and sine pass through the middle
        auto collect = [](double wave) {
            mood::Core c; c.p.rampWet = 0.2; c.p.rampWave = wave; c.p.time = 0.5; c.p.rampSpeed = 0.5;
            c.prepare(FS);
            std::vector<double> v;
            render(c, 6, [](double) { return 0.0; }, [&](mood::Core& cc, long) { v.push_back(cc.rampValue(0)); });
            return v;
        };
        auto ends = [](const std::vector<double>& v) {
            size_t n = 0; for (double x : v) if (x < 0.15 || x > 0.85) ++n; return double(n) / double(v.size());
        };
        auto middle = [](const std::vector<double>& v) {
            size_t n = 0; for (double x : v) if (std::fabs(x - 0.5) < 0.15) ++n; return double(n) / double(v.size());
        };
        const auto sq = collect(0.25), tri = collect(0), sin = collect(0.5);
        CHECK(ends(sq) > 0.9, "square is two levels (%.2f)", ends(sq));
        CHECK(ends(sin) > ends(tri), "sine hangs on the ends more than the triangle");
        CHECK(middle(tri) > middle(sin), "the triangle passes through the middle more");
        CHECK(middle(sq) < 0.05, "the square skips the middle");
    }
    {   // SWEEP climbs and restarts; BOUNCE takes the snap out
        auto collect = [](bool bouncy) {
            mood::Core c; c.p.rampWet = 0.2; c.p.sweep = true; c.p.bounce = bouncy; c.p.rampSpeed = 0.5;
            c.prepare(FS);
            std::vector<double> v;
            render(c, 8, [](double) { return 0.0; }, [&](mood::Core& cc, long) { v.push_back(cc.rampValue(0)); });
            return v;
        };
        auto resets = [](const std::vector<double>& v) { size_t n = 0; for (size_t i = 1; i < v.size(); ++i) if (v[i] < v[i - 1] - 0.5) ++n; return n; };
        auto rising = [](const std::vector<double>& v) { size_t n = 0; for (size_t i = 1; i < v.size(); ++i) if (v[i] > v[i - 1]) ++n; return double(n) / double(v.size() - 1); };
        const auto saw = collect(false), bouncy = collect(true);
        CHECK(resets(saw) >= 2, "the sweep restarts (%zu)", resets(saw));
        CHECK(rising(saw) > 0.9, "the sweep only climbs (%.2f)", rising(saw));
        CHECK(resets(bouncy) == 0, "BOUNCE removes the snap-back (%zu)", resets(bouncy));
        CHECK(rising(bouncy) > 0.3 && rising(bouncy) < 0.7, "and turns it into a tide (%.2f)", rising(bouncy));
    }
    {   // POLARITY: unipolar never dips under the knob
        auto range = [](bool uni) {
            mood::Core c; c.p.rampWet = 0.2; c.p.rampUni = uni; c.p.time = 0.5; c.p.rampSpeed = 0.5;
            c.prepare(FS);
            double lo = 2, hi = -1;
            render(c, 6, [](double) { return 0.0; }, [&](mood::Core& cc, long) { const double v = cc.effective(0); lo = std::min(lo, v); hi = std::max(hi, v); });
            return std::pair<double, double>(lo, hi);
        };
        const auto uni = range(true), bi = range(false);
        CHECK(uni.first > 0.49, "unipolar never dips under the knob (%.2f)", uni.first);
        CHECK(bi.first < 0.1, "bipolar goes all the way down (%.2f)", bi.first);
        CHECK(uni.second > 0.9 && bi.second > 0.95, "both reach the top");
    }
    {   // speed maps hit musical anchors
        CHECK(mood::speedMap(0.5) == 0.5 && mood::speedMap(1) == 4 && mood::speedMap(0) == -4, "speedMap");
        CHECK(mood::stretchMap(0.5) == 0 && mood::stretchMap(1) == 1 && mood::stretchMap(0) == -1, "stretchMap");
    }
    {   // LENGTH reaches the very start of a 30 s capture, and travels evenly
        mood::Core c; c.p.loopMode = 1; c.p.memory = 1; c.p.loopEnd = 1; c.prepare(FS);
        render(c, 30.5, tone(220, 1));
        c.footswitch(1, true); c.footswitch(1, false);
        const int len = c.state().lsrc;
        CHECK(len > static_cast<int>(29 * FS), "the take is about 30 s (%.2f s)", len / FS);
        const double want[] = {0.0, 0.049, 0.245, 0.49, 0.735};
        const double knob[] = {1.0, 0.95, 0.75, 0.5, 0.25};
        for (int i = 0; i < 5; ++i) {
            c.p.length = knob[i];
            render(c, 0.6, [](double) { return 0.0; });   // let the smoothed knob settle
            const auto st = c.state();
            const double start = static_cast<double>(st.winStart) / len;
            CHECK(std::fabs(start - want[i]) < 0.02, "LENGTH %.2f starts at %.1f %% (want %.1f %%)", knob[i], start * 100, want[i] * 100);
        }
        c.p.length = 1;
        render(c, 0.6, [](double) { return 0.0; });
        const auto all = c.state();
        CHECK(all.winStart == 0 && all.winLen > static_cast<int>(29 * FS), "LENGTH 1 is the whole take from the start");
    }
    {   // Re-taking a loop many times must not splice a click into it
        const double slope = 0.4 * 2 * mood::kPi * 100 / FS;
        for (int taps : {1, 2, 4, 8}) {
            mood::Core c; c.p.clock = 1; c.p.memory = 0.5; c.p.loopMode = 1; c.p.mix = 1;
            c.p.wetMode = 0; c.p.modLoop = 0.5; c.p.length = 0.8; c.prepare(FS);
            long t = 0;
            auto play = [&](double secs) {
                Out o;
                const long n = std::lround(secs * FS / B);
                o.L.resize(static_cast<size_t>(n * B)); o.R.resize(static_cast<size_t>(n * B));
                for (long b = 0; b < n; ++b) {
                    float x[B];
                    for (int i = 0; i < B; ++i, ++t) x[i] = static_cast<float>(0.4 * std::sin(2 * mood::kPi * 100 * (static_cast<double>(t) / FS)));
                    c.process(x, o.L.data() + b * B, o.R.data() + b * B, B);
                }
                return o;
            };
            play(4);
            for (int k = 0; k < taps; ++k) {
                c.footswitch(1, true); c.footswitch(1, false); play(1.5);
                c.footswitch(1, true); c.footswitch(1, false); play(1.0);
            }
            c.footswitch(1, true); c.footswitch(1, false);
            const Out o = play(0.2);
            CHECK(finite(o.L), "%d tap(s): the loop stays finite", taps);
            CHECK(c.capturedLength() > 64, "%d tap(s): the capture has audio", taps);
            double worst = 0;
            for (size_t i = static_cast<size_t>(4 * FS) + 1; i < o.L.size(); ++i) worst = std::max(worst, std::fabs(static_cast<double>(o.L[i] - o.L[i - 1])));
            CHECK(worst < slope * 1.4, "%d tap(s): the worst step in the loop is the tone's own slope (%.2fx)", taps, worst / slope);
        }
    }
    {   // The MEMORY window must be whole samples: a fractional one poisoned the loop with NaN
        for (double memory : {0.15, 0.4, 0.5, 0.85}) {
            mood::Core c; c.p.loopMode = 2; c.p.memory = memory; c.p.mix = 0.7; c.p.modWet = 0.6; c.prepare(FS);
            render(c, 9, tone(120, 99));
            c.footswitch(1, true); c.footswitch(1, false);
            const Out o = render(c, 1, [](double) { return 0.0; });
            CHECK(finite(o.L), "MEMORY %.2f: no NaN after the window elapses", memory);
            // MAX_LOOP_SECONDS bounds the capture; whole samples keeps lsrc sane.
            CHECK(c.capturedLength() > 64 && c.state().winLen > 0, "MEMORY %.2f: the capture is usable", memory);
        }
    }
    {   // FREEZE holds the wet tail without a held footswitch, and lets go again
        auto ring = [&](bool freezeSwitch) {
            mood::Core c; c.p.wetMode = 0; c.p.time = 0.7; c.p.mix = 1; c.p.modWet = 0.8; c.p.clock = 0.9; c.prepare(FS);
            c.footswitch(0, true); c.footswitch(0, false);
            render(c, 3, tone(300, 99));
            c.p.freezeLatch = freezeSwitch ? 1.0 : 0.0;
            const Out tail = render(c, 5, [](double) { return 0.0; });
            return std::pair<double, mood::State>(rms(tail.L, 3.5, 4.9), c.state());
        };
        const auto off = ring(false), on = ring(true);
        CHECK(on.first > 20 * off.first, "FREEZE keeps ringing (%.4f vs %.4f)", on.first, off.first);
        CHECK(on.second.freeze && on.second.wetOn, "the switch alone says the wet side is held");
        CHECK(!off.second.freeze, "and it is not held when the switch is up");
        mood::Core c; c.p.wetMode = 0; c.p.time = 0.7; c.p.mix = 1; c.p.modWet = 0.8; c.p.clock = 0.9; c.prepare(FS);
        c.footswitch(0, true); c.footswitch(0, false);
        render(c, 3, tone(300, 99));
        c.p.freezeLatch = 1;
        const Out held = render(c, 2, [](double) { return 0.0; });
        c.p.freezeLatch = 0;
        const Out freed = render(c, 4, [](double) { return 0.0; });
        CHECK(rms(held.L, 1.5, 1.9) > 5 * rms(freed.L, 3.5, 3.9), "releasing FREEZE lets the tail decay");
    }
    std::printf(failures ? "%d failure(s)\n" : "all DSP tests passed\n", failures);
    return failures ? 1 : 0;
}
