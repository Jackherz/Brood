// Property tests for the C++ core. Behavioural equivalence with the JS reference
// is covered separately by tests/parity (bit-for-bit within float rounding).
#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>
#include <functional>
#include <vector>
#include "dsp/brood.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

constexpr double FS = 48000;
constexpr int B = 128;
struct Out { std::vector<float> L, R; };

static Out render(brood::Core& c, double secs, const std::function<double(double)>& src,
                  const std::function<void(brood::Core&, long)>& at = nullptr) {
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
static auto tone(double f, double dur) { return [=](double t) { return t < dur ? 0.4 * std::sin(2 * brood::kPi * f * t) : 0.0; }; }
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
static void tap(brood::Core& c, int which) { c.footswitch(which, true); c.footswitch(which, false); }

int main() {
    {   // bypass is transparent
        brood::Core c; c.prepare(FS);
        auto src = tone(220, 1);
        auto o = render(c, 0.5, src);
        double worst = 0;
        for (size_t i = 1000; i < 20000; ++i) worst = std::max(worst, std::fabs(o.L[i] - src(static_cast<double>(i) / FS)));
        CHECK(worst < 1e-6, "bypass error %g", worst);
    }
    {   // delay echo lands at TIME (0.2 s at the default knob)
        brood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.modWet = 0.3; c.prepare(FS);
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
            brood::Core c; c.p.clock = knob; c.prepare(FS);
            const double secs = knob == 1.0 ? 1.0 : 32.0;
            render(c, secs + 0.5, tone(330, 100), [&](brood::Core& cc, long b) { if (b == blk(secs + 0.4)) tap(cc, 1); });
            // Capacity is in core samples, so 64 000 of them is 1 s at 64 kHz and 32 s at 2 kHz.
            const double got = c.capturedLength() / brood::clockHz(knob);
            CHECK(std::fabs(got - (knob == 1.0 ? 1.0 : 32.0)) < 0.01, "clock %.0f: captured %.3f s", knob, got);
        }
    }
    {   // hold records a fresh loop of the held length
        brood::Core c; c.prepare(FS);
        render(c, 1.5, tone(330, 3), [](brood::Core& cc, long b) {
            if (b == blk(0.5)) cc.footswitch(1, true);
            if (b == blk(1.3)) cc.footswitch(1, false);
        });
        CHECK(std::fabs(c.capturedLength() / brood::kCoreFs - 0.8) < 0.01, "loop %.3f s", c.capturedLength() / brood::kCoreFs);
    }
    {   // every mode/routing combination stays finite and bounded
        for (int lm = 0; lm < 3; ++lm) for (int r = 0; r < 3; ++r) for (int wm = 0; wm < 4; ++wm) {
            brood::Core c; c.p.loopMode = lm; c.p.routing = r; c.p.wetMode = wm; c.p.modLoop = 0.1; c.p.modWet = 1; c.p.clock = 0.3;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 2, [](double t) { return 0.3 * std::sin(2 * brood::kPi * 196 * t) * std::exp(-3 * std::fmod(t, 0.5)); },
                            [](brood::Core& cc, long b) { if (b == blk(1.0)) tap(cc, 1); });
            bool bounded = true; for (float v : o.L) if (std::fabs(v) > 1.0f) bounded = false;
            CHECK(finite(o.L) && finite(o.R) && bounded, "loop %d routing %d wet %d", lm, r, wm);
            CHECK(rms(o.L, 1.2, 2.0) > 1e-3, "silent: loop %d routing %d wet %d", lm, r, wm);
        }
    }
    {   // WEAVE: audible, repeatable at a fixed knob position, and it answers the knobs
        auto weaveRun = [](double time, double modify) {
            brood::Core c; c.p.wetMode = 3; c.p.mix = 1; c.p.time = time; c.p.modWet = modify;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 3, [](double t) {
                const double ph = std::fmod(t, 1.0);
                return ph < 0.75 ? 0.35 * std::sin(2 * brood::kPi * 330 * t) : 0.0;   // gated tone keeps the buffer fresh
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
        brood::Core c; c.p.wetMode = 3; c.p.mix = 1; c.p.modWet = 1; c.p.time = 1; c.p.clock = 0.0; c.p.width = 1;
        c.prepare(FS); tap(c, 0);
        auto o = render(c, 2, tone(196, 5));
        bool bounded = true; for (float v : o.L) if (std::fabs(v) > 1.0f) bounded = false;
        CHECK(finite(o.L) && finite(o.R) && bounded, "weave blew up");
    }
    {   // MEMORY raises the capture ceiling; 0 keeps the pedal's clock-derived rule
        auto capturedSeconds = [](double memory, double clockKnob) {
            brood::Core c; c.p.memory = memory; c.p.clock = clockKnob; c.prepare(FS);
            render(c, 40, tone(330, 100), [&](brood::Core& cc, long b) { if (b == blk(39.0)) tap(cc, 1); });
            return c.capturedLength() / brood::clockHz(clockKnob);
        };
        CHECK(std::fabs(capturedSeconds(0, 1) - 1.0) < 0.02, "pedal rule @64k = %.2f s", capturedSeconds(0, 1));
        CHECK(std::fabs(capturedSeconds(0, 0) - 32.0) < 0.1, "pedal rule @2k = %.2f s", capturedSeconds(0, 0));
        CHECK(std::fabs(capturedSeconds(1, 1) - 30.0) < 0.05, "memory max = %.2f s", capturedSeconds(1, 1));
        CHECK(capturedSeconds(0.5, 1) > 4.0 && capturedSeconds(0.5, 1) < 7.0, "memory mid = %.2f s", capturedSeconds(0.5, 1));
    }
    {   // END trims the end of the loop and never moves where it starts
        for (int mode = 0; mode < 3; ++mode) {
            brood::Core c; c.p.loopMode = mode; c.p.length = 0.72; c.prepare(FS); tap(c, 1);
            render(c, 3, tone(330, 10), [&](brood::Core& cc, long b) { if (b == blk(2.0)) tap(cc, 1); });
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
        brood::Core c; c.p.loopMode = 1; c.p.length = 1.0; c.prepare(FS); tap(c, 1);
        render(c, 3, tone(330, 10), [&](brood::Core& cc, long b) { if (b == blk(2.0)) tap(cc, 1); });
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
            brood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.modWet = 0.5;
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
        CHECK(std::string(brood::syncLabel(2.0 / 3)) == "1/4", "sync label");
        // A whole bar must clear the delay's 2 s ceiling, or nothing slow is reachable.
        CHECK(brood::syncSecondsRaw(40, 1.0) > brood::kDelayMaxSec * 0.95, "raw divisions are unclamped");
    }
    {   // modulation spans: one cycle is a musical division, not a free rate
        CHECK(brood::kRampDivCount == 8, "eight spans");
        CHECK(std::string(brood::rampDivLabel(0.0)) == "8 BARS", "slowest span");
        CHECK(std::string(brood::rampDivLabel(1.0)) == "1/16", "fastest span");
        CHECK(std::fabs(brood::rampCycleSeconds(120, 0.0) - 16.0) < 1e-9, "8 bars @120 = %.3f s", brood::rampCycleSeconds(120, 0.0));
        CHECK(std::fabs(brood::rampCycleSeconds(60, 1.0) - 0.25) < 1e-9, "1/16 @60 = %.3f s", brood::rampCycleSeconds(60, 1.0));
        // Every span must round-trip through the stepper's index conversion.
        for (int i = 0; i < brood::kRampDivCount; ++i) {
            const double v = static_cast<double>(i) / (brood::kRampDivCount - 1);
            CHECK(brood::rampDivIndex(brood::rampValueOfIndex(i, brood::kRampDivCount)) == i, "span %d round-trips", i);
            CHECK(brood::rampDivIndex(v) == i, "span %d reads back", i);
        }
    }
    {   // END moves the end marker and nothing else: START stays where it was put
        for (int mode = 0; mode < 3; ++mode) {
            brood::Core c; c.p.loopMode = mode; c.p.start = 0.15; c.p.memory = 0; c.p.clock = 1;
            c.prepare(FS); tap(c, 0);
            render(c, 1.2, tone(220, 5), [&](brood::Core& cc, long b) { if (b == blk(1.0)) tap(cc, 1); });
            c.p.loopEnd = 0.7;
            const int startFull = c.state().winStart, lenFull = c.state().winLen;
            c.p.loopEnd = 0.5;
            const int startTrim = c.state().winStart, lenTrim = c.state().winLen;
            CHECK(startTrim == startFull, "mode %d: END moved START (%d -> %d)", mode, startFull, startTrim);
            CHECK(lenTrim <= lenFull, "mode %d: END did not shorten the window", mode);
            // ... and END still does something: a shorter window is a shorter window.
            CHECK(lenTrim < lenFull, "mode %d: END left the window alone", mode);
        }
    }
    {   // ENV has its own switch: the wet side's SYNC does not reach the slice
        auto hold = [&](bool envSync, bool sync) {
            brood::Core c; c.p.loopMode = 0; c.p.wetMode = 0; c.p.mix = 1; c.p.modWet = 0.65;
            c.p.length = 0.5; c.p.envSync = envSync; c.p.sync = sync; c.p.bpm = 120;
            c.p.memory = 0; c.p.clock = 1; c.p.routing = 1;
            c.prepare(FS); tap(c, 0);
            render(c, 1.2, tone(220, 5), [&](brood::Core& cc, long b) { if (b == blk(1.0)) tap(cc, 1); });
            return render(c, 1.5, [](double) { return 0.0; });
        };
        const auto free = hold(false, false), synced = hold(true, false), wetOnly = hold(false, true);
        double d1 = 0, d2 = 0;
        for (size_t i = 0; i < free.L.size(); ++i) {
            d1 += std::fabs(free.L[i] - synced.L[i]);
            d2 += std::fabs(free.L[i] - wetOnly.L[i]);
        }
        CHECK(d1 > 1.0, "ENV SYNC did nothing (%.3f)", d1);
        CHECK(d2 < 1e-6, "the wet SYNC leaked into ENV (%.6f)", d2);
    }

    {   // balance fully to LOOP always returns the loop, whatever the routing
        for (int r = 0; r < 3; ++r) {
            brood::Core c; c.p.routing = r; c.p.balance = 1; c.p.mix = 1; c.p.loopMode = 1;
            c.prepare(FS); tap(c, 0);
            render(c, 1.2, tone(220, 5), [&](brood::Core& cc, long b) { if (b == blk(1.0)) tap(cc, 1); });
            auto o = render(c, 1, [](double) { return 0.0; });
            const double level = rms(o.L, 0.2, 0.9);
            CHECK(level > 0.05, "routing %d: loop silent at balance 100%% (rms %.4f)", r, level);
        }
    }

    // ───────────────────────── hidden options ─────────────────────────
    // These use only the public API (a tap on the wet footswitch engages the wet
    // channel), like the rest of this file.
    {   // divisions are stepped
        CHECK(brood::divMap(0) == 1 && brood::divMap(0.5) == 2 && brood::divMap(1) == 4, "divMap steps");
    }
    {   // TONE is a hi-cut on the wet bus
        auto hf = [](double value) {
            brood::Core c; c.p.wetMode = 0; c.p.mix = 1; c.p.time = 0.7; c.p.tone = value;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 3, [](double t) { return 0.4 * std::sin(2 * brood::kPi * 220 * t) + 0.2 * std::sin(2 * brood::kPi * 7000 * t); });
            return hfEnergy(o.L, 1.5, 3);
        };
        const double open = hf(1), shut = hf(0);
        CHECK(shut < open * 0.8, "tone hi-cut: %.4f vs %.4f", shut, open);
    }
    {   // STEREO WIDTH: 0 mono, 0.5 the built image, 1 wider
        auto image = [](double width) {
            brood::Core c; c.p.wetMode = 0; c.p.mix = 1; c.p.time = 0.7; c.p.width = width;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 2.5, [](double t) { return 0.3 * std::sin(2 * brood::kPi * 220 * t) + 0.1 * std::sin(2 * brood::kPi * 3100 * t); });
            return sideEnergy(o.L, o.R);
        };
        const double mid = image(0.5), mono = image(0), wide = image(1);
        CHECK(mid > 0.005, "reverb is stereo (%.4f)", mid);
        CHECK(mono < mid * 0.05, "width 0 collapses the sides (%.5f vs %.4f)", mono, mid);
        CHECK(wide > mid * 1.6, "width 1 widens (%.4f vs %.4f)", wide, mid);
    }
    {   // WET CLOCK DIVISION stretches the wet side only
        auto gap = [](double wetDiv) {
            brood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.modWet = 0.3;
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
    {   // The input is read at each core tick's own time, so a captured loop is smooth.
        // Handing every tick the latest host sample repeated one in four at 48 kHz.
        brood::Core c; c.p.loopMode = 1; c.p.mix = 1; c.p.loopDiv = 1; c.p.clock = 1; c.p.modLoop = 0.75;
        c.prepare(FS);
        auto o = render(c, 3, [](double t) { return 0.35 * std::sin(2 * brood::kPi * 220 * t); },
                        [](brood::Core& cc, long b) { if (b == blk(0.1)) cc.footswitch(1, true); else if (b == blk(0.6)) cc.footswitch(1, false); });
        // The median over 10 ms chunks, so the fade where the window wraps does not count.
        const size_t chunk = static_cast<size_t>(0.01 * FS);
        std::vector<double> rough;
        for (size_t s = static_cast<size_t>(1 * FS); s + chunk < static_cast<size_t>(2.8 * FS); s += chunk) {
            double d2 = 0, d0 = 0;
            for (size_t i = s; i < s + chunk; ++i) { d2 += std::fabs(o.L[i + 1] - 2 * o.L[i] + o.L[i - 1]); d0 += std::fabs(o.L[i]); }
            rough.push_back(d2 / d0);
        }
        std::sort(rough.begin(), rough.end());
        const double median = rough[rough.size() / 2], ideal = std::pow(2 * brood::kPi * crossings(o.L, 1, 2.8) / FS, 2);
        CHECK(median < 2 * ideal, "loop roughness %.2e vs a clean sine's %.2e", median, ideal);
    }
    {   // LOOP CLOCK DIVISION drops the loop pitch
        auto pitch = [](double loopDiv) {
            brood::Core c; c.p.loopMode = 0; c.p.mix = 1; c.p.loopDiv = loopDiv; c.p.clock = 1; c.p.time = 0.5;
            c.prepare(FS);
            auto o = render(c, 6, [](double t) { return 0.35 * std::sin(2 * brood::kPi * 220 * t); },
                            [](brood::Core& cc, long b) { if (b == blk(0.1)) cc.footswitch(1, true); else if (b == blk(0.3)) cc.footswitch(1, false); });
            return crossings(o.L, 3, 5);
        };
        const double p1 = pitch(0), p2 = pitch(0.5), p4 = pitch(1);
        CHECK(std::fabs(p1 - 220) < 12, "loop pitch 1x = %.1f", p1);
        CHECK(std::fabs(p2 - 110) < 8, "loop pitch 2x = %.1f", p2);
        CHECK(std::fabs(p4 - 55) < 6, "loop pitch 4x = %.1f", p4);
    }
    {   // FADE: a silent overdub can replace the old loop
        auto level = [](double fade) {
            brood::Core c; c.p.loopMode = 1; c.p.mix = 0.6; c.p.clock = 1; c.p.fade = fade;
            c.prepare(FS);
            render(c, 0.4, tone(200, 5), [](brood::Core& cc, long b) { if (b == blk(0.1)) cc.footswitch(1, true); else if (b == blk(0.3)) cc.footswitch(1, false); });
            render(c, 4.5, [](double) { return 0.0; }, [](brood::Core& cc, long b) { if (b == blk(0.1)) cc.footswitch(1, true); });
            auto o = render(c, 0.4, [](double) { return 0.0; });
            return rms(o.L, 0.2, 0.4);
        };
        const double keep = level(0), gone = level(1);
        CHECK(keep > 0.005, "fade 0 keeps the loop (%.4f)", keep);
        CHECK(gone < keep * 0.05, "fade 1 wipes it (%.5f vs %.4f)", gone, keep);
    }
    {   // CLASSIC: the slow clock is noisy
        auto noise = [](bool classic, double level = 1) {
            brood::Core c; c.p.wetMode = 0; c.p.mix = 1; c.p.time = 0.6; c.p.clock = 0; c.p.classic = classic; c.p.wearNoise = level;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 2, [](double) { return 0.0; });
            return rms(o.L, 1, 2);
        };
        const double clean = noise(false), noisy = noise(true);
        CHECK(clean < 1e-6, "silence stays silent (%.6f)", clean);
        CHECK(noisy > 1e-4, "classic clock noise (%.5f)", noisy);
        const double half = noise(true, 0.5), none = noise(true, 0);
        CHECK(half > noisy * 0.4 && half < noisy * 0.6, "NOISE at half halves it (%.5f of %.5f)", half, noisy);
        CHECK(none < 1e-6, "NOISE at 0 leaves worn silent (%.6f)", none);
    }
    {   // WORN after the loop: the take is free of the worn hiss, even with the wet side ahead of the looper
        auto kept = [](bool post) {
            brood::Core c; c.p.wetMode = 0; c.p.mix = 1; c.p.time = 0.6; c.p.clock = 0; c.p.classic = true; c.p.wearPost = post;
            c.p.wetPre = true; c.p.balance = 1; c.p.loopMode = 1; c.prepare(FS); tap(c, 0); tap(c, 1);
            render(c, 3, [](double) { return 0.0; }, [&](brood::Core& cc, long b) { if (b == blk(2.0)) tap(cc, 1); });
            c.p.classic = false; tap(c, 0);   // wear off and wet off: only the loop is left to hear
            auto o = render(c, 3, [](double) { return 0.0; });
            return rms(o.L, 1, 3);
        };
        const double inLoop = kept(false), after = kept(true);
        CHECK(inLoop > 1e-5, "worn in the loop records its hiss (%.6f)", inLoop);
        CHECK(after < inLoop * 0.05, "worn after the loop keeps the take clean (%.6f of %.6f)", after, inLoop);
        // And while it's on, it's still heard as worn: the hiss is in the output.
        brood::Core c; c.p.wetMode = 0; c.p.mix = 1; c.p.time = 0.6; c.p.clock = 0; c.p.classic = true; c.p.wearPost = true; c.p.wetPre = true;
        c.prepare(FS); tap(c, 0);
        auto o = render(c, 2, [](double) { return 0.0; });
        CHECK(rms(o.L, 1, 2) > 1e-4, "worn after the loop is still heard (%.5f)", rms(o.L, 1, 2));
    }

    {   // The wet side doesn't click: a steady two-note drone through every mode, knobs moving and FREEZE
        // going on and off. A click is a jump far over what's around it.
        const auto clicks = [](int mode, double time, double modify, const std::function<void(brood::Core&, long)>& move) {
            brood::Core c; c.p.wetMode = mode; c.p.time = time; c.p.modWet = modify; c.p.mix = 1; c.p.clock = 1;
            c.prepare(FS); tap(c, 0);
            auto o = render(c, 8, [](double t) { return 0.2 * std::sin(2 * brood::kPi * 196 * t) + 0.15 * std::sin(2 * brood::kPi * 247 * t); }, move);
            const int n = static_cast<int>(o.L.size());
            std::vector<double> bend(static_cast<size_t>(n), 0.0);
            for (int i = 2; i < n; ++i) bend[static_cast<size_t>(i)] = std::fabs(o.L[static_cast<size_t>(i)] - 2 * o.L[static_cast<size_t>(i - 1)] + o.L[static_cast<size_t>(i - 2)]);
            int found = 0, last = -100000;
            std::vector<double> around;
            for (int s = static_cast<int>(FS) + 2048; s < n - 2048; s += 64) {
                around.assign(bend.begin() + s - 2048, bend.begin() + s + 2048);
                std::nth_element(around.begin(), around.begin() + 2048, around.end());
                for (int i = s; i < s + 64; ++i)
                    if (bend[static_cast<size_t>(i)] > 0.002 && bend[static_cast<size_t>(i)] > 12 * (around[2048] + 1e-6)) {
                        if (i - last > 240) ++found;
                        last = i;
                    }
            }
            return found;
        };
        const char* names[4] = {"reverb", "delay", "slip", "weave"};
        const auto every = [](double seconds) { return static_cast<long>(seconds * FS / 128); };
        for (int mode = 0; mode < 4; ++mode) {
            const long swing = every(1.33);
            int n = clicks(mode, 0.5, 0.5, [=](brood::Core& c, long b) { c.p.freezeLatch = (b / swing) % 2 ? 1 : 0; });
            CHECK(n == 0, "%s: FREEZE on and off makes %d clicks", names[mode], n);
            n = clicks(mode, 0.5, 0.5, [](brood::Core& c, long b) { c.p.modWet = 0.5 + 0.45 * std::sin(b * 128 / FS * 2 * brood::kPi * 0.25); });
            CHECK(n == 0, "%s: moving MODIFY makes %d clicks", names[mode], n);
            n = clicks(mode, 0.5, 0.5, [](brood::Core& c, long b) { c.p.time = 0.5 + 0.45 * std::sin(b * 128 / FS * 2 * brood::kPi * 0.25); });
            CHECK(n == 0, "%s: moving TIME makes %d clicks", names[mode], n);
        }
        const int held = clicks(1, 0.8, 0.9, nullptr);
        CHECK(held == 0, "delay: high feedback at the limiter makes %d clicks", held);
    }
    {   // SLIP keeps sounding while frozen, instead of running out of audio and going quiet
        brood::Core c; c.p.wetMode = 2; c.p.time = 0.5; c.p.modWet = 0.5; c.p.mix = 1; c.p.clock = 1;
        c.prepare(FS); tap(c, 0);
        render(c, 2, tone(220, 10));
        c.p.freezeLatch = 1;
        auto o = render(c, 2, [](double) { return 0.0; });
        double quietest = 1;
        for (double t = 0.5; t < 1.9; t += 0.05) quietest = std::min(quietest, rms(o.L, t, t + 0.05));
        CHECK(quietest > 0.02, "frozen slip stays up (quietest %.4f)", quietest);
    }

    // ─────────────────────────── the ramp engine ───────────────────────────
    {   // with every ramp off, the audio is exactly what it was before the engine
        auto run = [](bool withRamps) {
            brood::Core c; c.p.wetMode = 0; c.p.mix = 0.8; c.p.time = 0.5;
            if (withRamps) { c.p.mods[0].rate = 1; c.p.mods[1].rate = 1; }
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
            brood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.modWet = 0.55;
            c.p.time = 0.4; c.p.mods[0].depth[0] = 0.5; c.p.mods[0].rate = 0.35;
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
            brood::Core c; c.p.mods[0].depth[0] = 0.5; c.p.mods[0].wave = wave; c.p.time = 0.5; c.p.mods[0].rate = 0.5;
            c.prepare(FS);
            std::vector<double> v;
            render(c, 6, [](double) { return 0.0; }, [&](brood::Core& cc, long) { v.push_back(cc.rampValue(0)); });
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
            brood::Core c; c.p.mods[0].depth[0] = 0.5; c.p.mods[0].sweep = true; c.p.mods[0].bounce = bouncy; c.p.mods[0].rate = 0.5;
            c.prepare(FS);
            std::vector<double> v;
            render(c, 8, [](double) { return 0.0; }, [&](brood::Core& cc, long) { v.push_back(cc.rampValue(0)); });
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
    {   // HOLD: the generator stops where it is, then carries on from the same place
        brood::Core c; c.p.mods[0].depth[0] = 0.5; c.p.mods[0].rate = 0.5; c.prepare(FS);
        render(c, 0.7, [](double) { return 0.0; });
        const double before = c.rampValue(0);
        c.p.mods[0].hold = true;
        render(c, 0.7, [](double) { return 0.0; });
        CHECK(c.rampValue(0) == before, "HOLD stops the ramp (%.4f, %.4f)", before, c.rampValue(0));
        c.p.mods[0].hold = false;
        render(c, 0.1, [](double) { return 0.0; });
        CHECK(c.rampValue(0) != before, "and it runs again when released");
    }
    {   // POLARITY: unipolar never dips under the knob
        auto range = [](bool uni) {
            brood::Core c; c.p.mods[0].depth[0] = 0.5; c.p.mods[0].uni = uni; c.p.time = 0.5; c.p.mods[0].rate = 0.5;
            c.prepare(FS);
            double lo = 2, hi = -1;
            render(c, 6, [](double) { return 0.0; }, [&](brood::Core& cc, long) { const double v = cc.effective(0); lo = std::min(lo, v); hi = std::max(hi, v); });
            return std::pair<double, double>(lo, hi);
        };
        const auto uni = range(true), bi = range(false);
        CHECK(uni.first > 0.49, "unipolar never dips under the knob (%.2f)", uni.first);
        CHECK(bi.first < 0.1, "bipolar goes all the way down (%.2f)", bi.first);
        CHECK(uni.second > 0.9 && bi.second > 0.95, "both reach the top");
    }
    {   // speed maps hit musical anchors
        CHECK(brood::speedMap(0.5) == 0.5 && brood::speedMap(1) == 4 && brood::speedMap(0) == -4, "speedMap");
        CHECK(brood::stretchMap(0.5) == 0 && brood::stretchMap(1) == 1 && brood::stretchMap(0) == -1, "stretchMap");
    }
    {   // LENGTH reaches the very start of a 30 s capture, and travels evenly
        brood::Core c; c.p.loopMode = 1; c.p.memory = 1; c.p.loopEnd = 1; c.prepare(FS);
        render(c, 30.5, tone(220, 1));
        c.footswitch(1, true); c.footswitch(1, false);
        const int len = c.state().lsrc;
        CHECK(len > static_cast<int>(29 * FS), "the take is about 30 s (%.2f s)", len / FS);
        const double want[] = {0.0, 0.049, 0.245, 0.49, 0.735};
        const double knob[] = {1.0, 0.95, 0.75, 0.5, 0.25};
        for (int i = 0; i < 5; ++i) {
            c.p.length = knob[i];
            // Let the knob settle: in TAPE the window follows LENGTH at up to four times
            // playback speed, so on a 30 s take a turn of a quarter takes a few seconds.
            render(c, 4, [](double) { return 0.0; });
            const auto st = c.state();
            const double start = static_cast<double>(st.winStart) / len;
            CHECK(std::fabs(start - want[i]) < 0.02, "LENGTH %.2f starts at %.1f %% (want %.1f %%)", knob[i], start * 100, want[i] * 100);
        }
        c.p.length = 1;
        render(c, 8, [](double) { return 0.0; });
        const auto all = c.state();
        CHECK(all.winStart == 0 && all.winLen > static_cast<int>(29 * FS), "LENGTH 1 is the whole take from the start");
    }
    {   // Re-taking a loop many times must not splice a click into it
        const double slope = 0.4 * 2 * brood::kPi * 100 / FS;
        for (int taps : {1, 2, 4, 8}) {
            brood::Core c; c.p.clock = 1; c.p.memory = 0.5; c.p.loopMode = 1; c.p.mix = 1;
            c.p.wetMode = 0; c.p.modLoop = 0.5; c.p.length = 0.8; c.prepare(FS);
            long t = 0;
            auto play = [&](double secs) {
                Out o;
                const long n = std::lround(secs * FS / B);
                o.L.resize(static_cast<size_t>(n * B)); o.R.resize(static_cast<size_t>(n * B));
                for (long b = 0; b < n; ++b) {
                    float x[B];
                    for (int i = 0; i < B; ++i, ++t) x[i] = static_cast<float>(0.4 * std::sin(2 * brood::kPi * 100 * (static_cast<double>(t) / FS)));
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
            brood::Core c; c.p.loopMode = 2; c.p.memory = memory; c.p.mix = 0.7; c.p.modWet = 0.6; c.prepare(FS);
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
            brood::Core c; c.p.wetMode = 0; c.p.time = 0.7; c.p.mix = 1; c.p.modWet = 0.8; c.p.clock = 0.9; c.prepare(FS);
            c.footswitch(0, true); c.footswitch(0, false);
            render(c, 3, tone(300, 99));
            c.p.freezeLatch = freezeSwitch ? 1.0 : 0.0;
            const Out tail = render(c, 5, [](double) { return 0.0; });
            return std::pair<double, brood::State>(rms(tail.L, 3.5, 4.9), c.state());
        };
        const auto off = ring(false), on = ring(true);
        CHECK(on.first > 20 * off.first, "FREEZE keeps ringing (%.4f vs %.4f)", on.first, off.first);
        CHECK(on.second.freeze && on.second.wetOn, "the switch alone says the wet side is held");
        CHECK(!off.second.freeze, "and it is not held when the switch is up");
        brood::Core c; c.p.wetMode = 0; c.p.time = 0.7; c.p.mix = 1; c.p.modWet = 0.8; c.p.clock = 0.9; c.prepare(FS);
        c.footswitch(0, true); c.footswitch(0, false);
        render(c, 3, tone(300, 99));
        c.p.freezeLatch = 1;
        const Out held = render(c, 2, [](double) { return 0.0; });
        c.p.freezeLatch = 0;
        const Out freed = render(c, 4, [](double) { return 0.0; });
        CHECK(rms(held.L, 1.5, 1.9) > 5 * rms(freed.L, 3.5, 3.9), "releasing FREEZE lets the tail decay");
    }
    {   // WET/LOOP blends two worlds: no mode may swallow the loop in the middle
        // Broadband, so a mode's phase cannot flatter or ruin the measurement.
        auto broad = [](double t) {
            return 0.15 * (std::sin(2 * brood::kPi * 220 * t) + std::sin(2 * brood::kPi * 331 * t)
                         + std::sin(2 * brood::kPi * 517 * t) + std::sin(2 * brood::kPi * 733 * t));
        };
        auto level = [&](int wetMode, double balance) {
            brood::Core c; c.p.loopMode = 0; c.p.clock = 0.6; c.p.memory = 0; c.p.mix = 0.6;
            c.p.wetMode = wetMode; c.p.routing = 1; c.p.modWet = 0.5; c.p.time = 0.5; c.p.balance = balance;
            c.prepare(FS);
            render(c, 2, broad);
            c.footswitch(1, true); c.footswitch(1, false);
            render(c, 0.4, [](double) { return 0.0; });
            if (balance < 1) { c.footswitch(0, true); c.footswitch(0, false); }
            render(c, 1.2, [](double) { return 0.0; });
            const Out o = render(c, 1, [](double) { return 0.0; });
            return rms(o.L, 0.2, 0.9);
        };
        static const char* names[] = {"REVERB", "DELAY", "SLIP", "WEAVE"};
        for (int mode = 0; mode < 4; ++mode) {
            const double loopOnly = level(mode, 1), middle = level(mode, 0.5), mostlyWet = level(mode, 0.2);
            CHECK(middle > 0.9 * loopOnly, "%s: the loop keeps its level in the middle (%.2fx)", names[mode], middle / loopOnly);
            CHECK(middle < 2.2 * loopOnly, "%s: without doubling it (%.2fx)", names[mode], middle / loopOnly);
            CHECK(middle > 1.15 * mostlyWet, "%s: the middle is not just the wet side (%.2fx)", names[mode], middle / mostlyWet);
        }
    }
    {   // Holding LOOP overdubs, and FADE decides what the old take keeps
        auto take = [&](double fade, double& before, double& after, brood::LoopLed& during) {
            brood::Core c; c.p.loopMode = 0; c.p.clock = 0.6; c.p.memory = 0; c.p.mix = 0.5; c.p.fade = fade;
            c.prepare(FS);
            render(c, 2, tone(220, 9));
            c.footswitch(1, true); c.footswitch(1, false);
            before = c.loopWindowRms();
            render(c, 0.3, [](double) { return 0.0; });
            c.footswitch(1, true);
            render(c, 0.6, [](double) { return 0.0; });
            during = c.state().loop;
            render(c, 1.6, [](double) { return 0.0; });
            c.footswitch(1, false);
            render(c, 0.2, [](double) { return 0.0; });
            after = c.loopWindowRms();
        };
        double keepBefore = 0, keepAfter = 0, eraseBefore = 0, eraseAfter = 0;
        brood::LoopLed keepLed = brood::LoopLed::Off, eraseLed = brood::LoopLed::Off;
        take(0, keepBefore, keepAfter, keepLed);
        take(1, eraseBefore, eraseAfter, eraseLed);
        CHECK(keepLed == brood::LoopLed::Dub, "holding LOOP overdubs");
        CHECK(keepBefore > 0.1, "there was something to keep (%.3f)", keepBefore);
        CHECK(keepAfter > 0.05, "FADE 0 leaves the old take there (%.3f)", keepAfter);
        CHECK(eraseAfter < keepAfter * 0.5, "FADE 1 replaces it (%.3f vs %.3f)", eraseAfter, keepAfter);
    }
    {   // WIDTH does what SPREAD used to: 0 is mono and the top opens the sides
        auto runWidth = [&](double width) {
            brood::Core c; c.p.wetMode = 1; c.p.mix = 1; c.p.modWet = 0.6; c.p.time = 0.35; c.p.clock = 1; c.p.width = width;
            c.prepare(FS); c.footswitch(0, true); c.footswitch(0, false);
            const Out o = render(c, 1.5, tone(440, 9));
            double side = 0, mid = 0;
            for (size_t i = static_cast<size_t>(0.3 * FS); i < o.L.size(); ++i) {
                mid += std::fabs(static_cast<double>(o.L[i]) + o.R[i]);
                side += std::fabs(static_cast<double>(o.L[i]) - o.R[i]);
            }
            return std::pair<double, double>(side, mid);
        };
        const auto mono = runWidth(0), centre = runWidth(0.5), wide = runWidth(1);
        CHECK(mono.first < 1e-9, "WIDTH 0 is mono (%.2e)", mono.first);
        CHECK(centre.first <= mono.first + 1e-9, "the middle is the built image (%.2e)", centre.first);
        CHECK(wide.first > 1e-3, "WIDTH 1 opens the sides (%.2e)", wide.first);
        CHECK(wide.second > 0.2 * centre.second, "and keeps the middle of the signal");
    }
    {   // MIX alone takes the dry away, so DRY KILL is not needed
        auto dryLeft = [&](double mix) {
            brood::Core c; c.p.wetMode = 0; c.p.mix = mix; c.p.modWet = 0.5; c.p.time = 0.5;
            c.prepare(FS); c.footswitch(0, true); c.footswitch(0, false);
            render(c, 0.5, [](double) { return 0.0; });
            const Out o = render(c, 0.4, [](double) { return 0.5; });
            return static_cast<double>(o.L.back());
        };
        CHECK(dryLeft(0.5) > 0.4, "half way the dry is still there (%.3f)", dryLeft(0.5));
        CHECK(std::fabs(dryLeft(1)) < 0.05, "fully wet the dry is gone (%.3f)", dryLeft(1));
    }
    {   // There is no LATCH any more: a held foot is momentary, the switch latches
        brood::Core c; c.p.wetMode = 0; c.p.time = 0.8; c.p.modWet = 0.9; c.p.mix = 1;
        c.prepare(FS);
        c.footswitch(0, true); c.footswitch(0, false);
        render(c, 1, tone(300, 9));
        c.footswitch(0, true);
        render(c, 0.6, [](double) { return 0.0; });
        CHECK(c.state().freeze, "holding freezes");
        c.footswitch(0, false);
        render(c, 0.1, [](double) { return 0.0; });
        CHECK(!c.state().freeze, "letting go lets go");
    }
    {   // A re-prepare must keep the take: hosts re-prepare for a device change and
        // before an offline render, and wiping the loop there is what came back as
        // silence when bouncing.
        brood::Core c; c.p.loopMode = 0; c.p.clock = 0.6; c.prepare(FS);
        render(c, 2, tone(220, 9));
        c.footswitch(1, true); c.footswitch(1, false);
        const int before = c.capturedLength();
        const double level = c.loopWindowRms();
        CHECK(before > 64, "there is a take to keep (%d)", before);
        c.prepare(FS);                       // what a host does before a render
        CHECK(c.capturedLength() == before, "prepare keeps the take (%d -> %d)", before, c.capturedLength());
        CHECK(std::fabs(c.loopWindowRms() - level) < 1e-9, "and its contents");
        c.prepare(96000.0);                  // and a sample-rate change
        CHECK(c.capturedLength() == before, "a sample-rate change keeps it too");
    }
    {   // The take survives being written into the state and read back
        brood::Core c; c.p.loopMode = 0; c.p.clock = 0.6; c.prepare(FS);
        render(c, 2, tone(220, 9));
        c.footswitch(1, true); c.footswitch(1, false);
        const int n = c.capturedLength();
        std::vector<float> pcm(static_cast<size_t>(n));
        CHECK(c.capturedLoop(pcm.data(), n) == n, "the take can be read out");
        std::vector<int16_t> packed;
        brood::Core::packLoop(pcm.data(), n, packed);
        std::vector<float> back(static_cast<size_t>(n));
        brood::Core::unpackLoop(packed.data(), n, back.data());
        double worst = 0;
        for (int i = 0; i < n; ++i) worst = std::max(worst, std::fabs(static_cast<double>(back[static_cast<size_t>(i)] - pcm[static_cast<size_t>(i)])));
        CHECK(worst < 1e-4, "int16 packing is transparent enough (%.2e)", worst);
        brood::Core d; d.p.loopMode = 0; d.p.clock = 0.6; d.prepare(FS);
        d.restoreLoop(back.data(), n, true);
        CHECK(d.capturedLength() == n, "restored into a fresh engine");
        CHECK(d.state().loop == brood::LoopLed::Play, "and it plays");
        const Out o = render(d, 1, [](double) { return 0.0; });
        CHECK(finite(o.L) && rms(o.L, 0.1, 0.9) > 0.01, "a restored take makes sound (%.3f)", rms(o.L, 0.1, 0.9));
    }
    {   // START moves the loop window inside the take, and never clicks when it does.
        auto captured = [](int mode, double length) {
            brood::Core c; c.p.loopMode = mode; c.p.length = length; c.prepare(FS);
            c.p.loopEnd = 1;
            render(c, 4, [](double) { return 0.2; });
            tap(c, 1);
            return c;
        };
        for (int mode : {0, 2}) {
            brood::Core c = captured(mode, 0.72);
            c.p.loopEnd = 0.5;
            render(c, 0.1, [](double) { return 0.0; });
            const auto base = c.state();
            CHECK(base.winStart == 0, "mode %d: START 0 opens on the oldest audio", mode);
            const int room = c.capturedLength() - base.winLen;
            c.p.start = 1;
            render(c, 0.1, [](double) { return 0.0; });
            CHECK(c.state().winLen == base.winLen, "mode %d: START leaves the length alone", mode);
            CHECK(std::abs(c.state().winStart - room) <= 1, "mode %d: START 1 slides the window to the end (%d of %d)", mode, c.state().winStart, room);
        }
        {
            brood::Core c = captured(1, 0.72);
            render(c, 0.1, [](double) { return 0.0; });
            const int tail = c.state().winStart;
            c.p.start = 1;
            render(c, 0.1, [](double) { return 0.0; });
            CHECK(c.state().winStart == 0, "TAPE: START 1 reaches all the way back");
            c.p.start = 0;
            render(c, 0.1, [](double) { return 0.0; });
            CHECK(c.state().winStart == tail, "TAPE: and 0 is the tail, as it always was");
        }
        {
            brood::Core c = captured(0, 1.0);
            c.p.start = 1;
            render(c, 0.1, [](double) { return 0.0; });
            CHECK(c.state().winStart == 0, "a full-length window cannot move");
        }
        // A jump in the read head would be a click; the crossfade must hide it.
        brood::Core c; c.p.loopMode = 2; c.p.length = 0.5; c.p.loopEnd = 0.4; c.p.mix = 1; c.prepare(FS);
        render(c, 3, [](double t) { return 0.35 * std::sin(2 * brood::kPi * 140 * t) + 0.2 * std::sin(2 * brood::kPi * 523 * t); });
        tap(c, 1);
        c.p.balance = 1;
        auto step = [](const std::vector<float>& a, double from, double to) {
            double m = 0;
            for (long i = std::lround(from * FS) + 1; i < std::lround(to * FS); ++i) m = std::max(m, (double) std::fabs(a[(size_t) i] - a[(size_t) (i - 1)]));
            return m;
        };
        const Out still = render(c, 0.5, [](double) { return 0.0; });
        const double quiet = step(still.L, 0.1, 0.5);
        const long sweepBlocks = blk(0.5);
        const Out sweep = render(c, 0.6, [](double) { return 0.0; }, [sweepBlocks](brood::Core& cc, long b) {
            cc.p.start = std::min(1.0, (double) b / (double) sweepBlocks);
        });
        const double moving = step(sweep.L, 0.02, 0.6);
        CHECK(moving < std::max(0.02, quiet * 4), "sweeping START stays smooth (worst step %.4f vs %.4f still)", moving, quiet);
        c.p.start = 0;
        render(c, 0.1, [](double) { return 0.0; });
        const Out jump = render(c, 0.4, [](double) { return 0.0; }, [](brood::Core& cc, long b) { if (b == blk(0.1)) cc.p.start = 1; });
        const double worst = step(jump.L, 0.05, 0.4);
        CHECK(worst < std::max(0.02, quiet * 4), "one big move stays smooth (worst step %.4f vs %.4f still)", worst, quiet);
    }
    {   // The ramp cells' index <-> value conversion. The editor used to write the
        // index straight into a 0..1 parameter, so only "off" and the first entry
        // were reachable and the engine was pointed at the last knob instead. Every
        // stop must survive the round trip, and the values must run 0..1 in order.
        for (int count : {brood::kRampWaveCount}) {
            for (int i = 0; i < count; ++i) {
                const double v = brood::rampValueOfIndex(i, count);
                CHECK(brood::rampIndexOfValue(v, count) == i, "count %d: index %d survives the round trip (v=%.3f)", count, i, v);
                CHECK(v >= 0.0 && v <= 1.0, "count %d: index %d stays inside 0..1", count, i);
                CHECK(i == 0 || v > brood::rampValueOfIndex(i - 1, count), "count %d: index %d is above index %d", count, i, i - 1);
            }
            CHECK(brood::rampValueOfIndex(0.0, count) == 0.0, "count %d: the first stop is 0", count);
            CHECK(brood::rampValueOfIndex(count - 1, count) == 1.0, "count %d: the last stop is 1", count);
            CHECK(brood::rampIndexOfValue(0.0, count) == 0, "count %d: 0 reads as the first stop", count);
            CHECK(brood::rampIndexOfValue(1.0, count) == count - 1, "count %d: 1 reads as the last stop", count);
            CHECK(brood::rampIndexOfValue(-3.0, count) == 0, "count %d: below 0 clamps to the first stop", count);
            CHECK(brood::rampIndexOfValue(9.0, count) == count - 1, "count %d: above 1 clamps to the last stop", count);
        }
        CHECK(brood::rampValueOfIndex(6, 1) == 0.0, "a single-entry list has one value");
        CHECK(brood::rampIndexOfValue(brood::rampValueOfIndex(4, brood::kRampWaveCount), brood::kRampWaveCount) == 4, "all five shapes are reachable");
    }
    // SMOOTH runs through the same rates as the steps, so switching it doesn't move the clock.
    {
        for (int k = 0; k <= 10; ++k)
            CHECK(std::fabs(brood::clockHz(k / 10.0, true) - brood::clockHz(k / 10.0, false)) < 1e-6, "step %d is the same rate with SMOOTH", k);
        for (double a = 0; a < 1; a += 0.01)
            CHECK(brood::clockHz(a + 0.01, true) > brood::clockHz(a, true), "SMOOTH only ever rises, at %f", a);
        const auto after = [](double knob, double secondsAfter) {
            static brood::Core c;
            c.prepare(48000);
            c.p.smooth = false;
            c.p.clock = knob;
            std::vector<float> in(256, 0.f), l(256), r(256);
            const auto run = [&](double s) { for (int b = 0; b < static_cast<int>(s * 48000 / 256); ++b) c.process(in.data(), l.data(), r.data(), 256); };
            run(1.0);
            const double before = c.state().clockHz;
            c.p.smooth = true;
            c.p.clock = brood::clockTarget(knob, false);   // what the panel does on the click
            run(secondsAfter);
            return c.state().clockHz / before;
        };
        for (const double knob : {0.9, 0.87, 0.5, 0.13}) {
            CHECK(std::fabs(after(knob, 0.02) - 1) < 1e-3, "SMOOTH on leaves the clock alone at once (knob %f)", knob);
            CHECK(std::fabs(after(knob, 3.0) - 1) < 1e-3, "and it stays there (knob %f)", knob);
        }
    }
    {   // TAPE: deep, fast LENGTH modulation keeps the playhead moving through the window. The
        // playhead keeps its share of the window, and the window follows LENGTH at a capped
        // speed, so it neither jumps back (as it did, every cycle) nor zips through the take.
        brood::Core c; c.p.loopMode = 1; c.p.clock = 1; c.p.memory = 0.6; c.p.length = 0.5; c.p.mix = 1; c.p.balance = 1; c.p.routing = 2; c.p.modLoop = 0.66;
        c.prepare(FS);
        render(c, 4, [](double t) { return 0.4 * std::sin(2 * brood::kPi * 220 * t); });
        tap(c, 1);
        c.p.mods[0].on = true; c.p.mods[0].rate = 1; c.p.mods[0].wave = 0.3; c.p.mods[0].depth[2] = 1;   // square, 8 Hz, full depth
        double maxStep = 0, last = -1; int back = 0; float prev = 0;
        render(c, 3, [](double) { return 0.0; }, [&](brood::Core& cc, long b) {
            const auto st = cc.state();
            const double f = st.winLen > 0 ? st.pos / st.winLen : 0;
            if (b > blk(1) && last >= 0 && f < last - 0.1 && last < 0.9) ++back;
            last = f;
        });
        auto o = render(c, 1, [](double) { return 0.0; });
        for (size_t i = 0; i < o.L.size(); ++i) { if (i > 0) maxStep = std::max(maxStep, static_cast<double>(std::fabs(o.L[i] - prev))); prev = o.L[i]; }
        CHECK(back == 0, "LENGTH modulation throws the playhead back %d times", back);
        // A 220 Hz sine at the loop's level steps by about 0.012 a sample; four times normal
        // speed at most is about 0.05. Jumping back stepped by 0.165.
        CHECK(maxStep < 0.07, "LENGTH modulation steps by %.3f", maxStep);
        CHECK(rms(o.L, 0, 1) > 0.1, "and the loop keeps sounding (%.3f)", rms(o.L, 0, 1));
    }
    std::printf(failures ? "%d failure(s)\n" : "all DSP tests passed\n", failures);
    return failures ? 1 : 0;
}
