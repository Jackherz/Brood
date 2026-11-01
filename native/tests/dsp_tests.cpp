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
    {   // START/END trim the captured loop; an inverted pair still leaves a region
        mood::Core c; c.prepare(FS); tap(c, 1);
        render(c, 3, tone(330, 10), [&](mood::Core& cc, long b) { if (b == blk(2.0)) tap(cc, 1); });
        const auto full = c.state();
        CHECK(full.winStart == 0 && full.winLen == c.capturedLength(), "untrimmed window is the whole loop");
        c.p.loopStart = 0.5; c.p.loopEnd = 1.0;
        const auto half = c.state();
        CHECK(std::abs(half.winStart - c.capturedLength() / 2) <= 1, "trim start %d of %d", half.winStart, c.capturedLength());
        CHECK(std::abs(half.winLen - c.capturedLength() / 2) <= 1, "trim length %d", half.winLen);
        c.p.loopStart = 0.9; c.p.loopEnd = 0.1;
        const auto swapped = c.state();
        CHECK(swapped.winLen > 0 && swapped.winLen < c.capturedLength(), "inverted trim yields a region");
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
    {   // speed maps hit musical anchors
        CHECK(mood::speedMap(0.5) == 0.5 && mood::speedMap(1) == 4 && mood::speedMap(0) == -4, "speedMap");
        CHECK(mood::stretchMap(0.5) == 0 && mood::stretchMap(1) == 1 && mood::stretchMap(0) == -1, "stretchMap");
    }
    std::printf(failures ? "%d failure(s)\n" : "all DSP tests passed\n", failures);
    return failures ? 1 : 0;
}
