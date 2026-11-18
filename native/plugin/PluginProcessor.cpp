#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#if BROOD_DEMOS
#include "BroodDemos.h"
#endif

namespace {
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;

juce::String fmtTime(double s) { return s < 1 ? juce::String(juce::roundToInt(s * 1000)) + " ms" : juce::String(s, 2) + " s"; }
juce::String fmtSpeed(double s) {
    if (std::fabs(s) < 1e-9) return "frozen";
    return (s < 0 ? "reverse " : "") + juce::String(std::fabs(s), 2) + "x";
}
// The state tag every set saved before the plug-in was renamed. ASCII codes so the
// old product name is not spelled anywhere in the source.
const char kLegacyStateTag[] = { 0x4d, 0x4f, 0x4f, 0x44, 0 };
juce::String pct(double x) { return juce::String(juce::roundToInt(x * 100)) + "%"; }
// SLIP's RATE: how fast and which way the slices play, signed and with no unit ("+0.63").
juce::String signedRate(double s) { return (s < 0 ? "-" : "+") + juce::String(std::fabs(s), 2); }
// WET MIX in both words: how loud the routed source is unprocessed, and WET (each full in the middle).
int wetMixDry(double v) { return juce::roundToInt(std::min(1.0, 2.0 * (1 - v)) * 100); }
int wetMixWet(double v) { return juce::roundToInt(std::min(1.0, 2.0 * v) * 100); }
// A level fader as dB ("-6.0 dB", "+3.5 dB", "off"); topDb is the fader's top.
juce::String levelText(double v, double topDb) {
    if (v <= 0.0005) return "off";
    const double db = -48.0 + v * (topDb + 48.0);
    return (db > 0.04 ? "+" : "") + juce::String(db, 1) + " dB";
}

// Host value strings: a number and its unit, at most 8 characters, the way a controller's
// display shows its own parameters (35%, -3.1 dB, 1708 Hz). The editor keeps the prose.
juce::String shortTime(double s) {
    if (s < 1) return juce::String(juce::roundToInt(s * 1000)) + " ms";
    if (s < 10) return juce::String(s, 2) + " s";
    if (s < 100) return juce::String(s, 1) + " s";
    return juce::String(juce::roundToInt(s)) + " s";
}
juce::String shortSpeed(double s) {
    if (std::fabs(s) < 1e-9) return "Stop";
    return juce::String(s, std::fabs(s) < 10 ? 2 : 1) + "x";
}
juce::String signedPct(double x) {
    const int n = juce::roundToInt(x * 100);
    return (n > 0 ? "+" : "") + juce::String(n) + "%";
}
juce::String titleCase(juce::String s) { s = s.toLowerCase(); return s.substring(0, 1).toUpperCase() + s.substring(1); }
// The number at the front of what someone typed ("350 ms", "-6 dB", "40%"), and its unit.
double leadingNumber(const juce::String& text) { return text.retainCharacters("0123456789.-+").getDoubleValue(); }
double inverseExp(double x, double a, double b) { return x <= a ? 0.0 : x >= b ? 1.0 : std::log(x / a) / std::log(b / a); }

struct FloatDef { const char* id; const char* name; float def; };
const FloatDef kKnobs[] = {
    {"time", "Time", 0.55f}, {"wetMix", "Wet Mix", 0.f}, {"length", "Length", 0.72f},
    {"modWet", "Modify / Wet", 0.58f}, {"clock", "Clock", 0.9f}, {"modLoop", "Modify / Loop", 0.66f},
};
}  // namespace

BroodProcessor::BroodProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "BROOD", [this] {
          juce::AudioProcessorValueTreeState::ParameterLayout layout;
          for (const auto& k : kKnobs) {
              const juce::String id = k.id;
              layout.add(std::make_unique<APF>(juce::ParameterID(id, 1), k.name, juce::NormalisableRange<float>(0.f, 1.f), k.def,
                  juce::AudioParameterFloatAttributes()
                      .withStringFromValueFunction([this, id](float v, int) { return hostValue(id, v); })
                      .withValueFromStringFunction([this, id](const juce::String& t) { return hostValueFromText(id, t); })));
          }
          layout.add(std::make_unique<APC>(juce::ParameterID("wetMode", 1), "Wet Mode", juce::StringArray{"Reverb", "Delay", "Slip", "Weave"}, 0));
          layout.add(std::make_unique<APC>(juce::ParameterID("routing", 1), "Routing", juce::StringArray{"In", "In+Loop", "Loop", "Off"}, 1));
          // INPUT MONITOR: whether the live input is heard (see brood::Params::monitor).
          layout.add(std::make_unique<APC>(juce::ParameterID("monitor", 1), "Monitor", juce::StringArray{"Auto", "On", "Off"}, 0));
          layout.add(std::make_unique<APC>(juce::ParameterID("loopMode", 1), "Loop Mode", juce::StringArray{"Env", "Tape", "Stretch"}, 1));
          layout.add(std::make_unique<APB>(juce::ParameterID("trails", 1), "Trails", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("smooth", 1), "Smooth", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("sync", 1), "Wet Sync", false));
          auto loopParam = [this](const juce::String& id, const juce::String& name, float def) {
              // The capture gets its own name: reusing "id" here shadows the
              // parameter of the same name (clang's -Wshadow-uncaptured-local).
              const juce::String paramId = id;
              return std::make_unique<APF>(juce::ParameterID(id, 1), name, juce::NormalisableRange<float>(0.f, 1.f), def,
                  juce::AudioParameterFloatAttributes()
                      .withStringFromValueFunction([this, paramId](float v, int) { return hostValue(paramId, v); })
                      .withValueFromStringFunction([this, paramId](const juce::String& t) { return hostValueFromText(paramId, t); }));
          };
          // The two levels, as faders (brood::levelGain): LOOP LEVEL 0 dB at 0.8, INPUT LEVEL at 2/3.
          layout.add(loopParam("loopLevel", "Loop Level", 0.8f));
          layout.add(loopParam("inputLevel", "Input Level", static_cast<float>(brood::inputLevelOfDb(0.0))));
          layout.add(loopParam("memory", "Memory", 0.f));
          layout.add(loopParam("loopEnd", "Loop End", 1.f));
          // 1 is the newest audio in every mode: a fresh TAPE loop plays the tail, as it always has.
          layout.add(loopParam("start", "Loop Start", 1.f));
          // Hidden options: on the pedal these live behind holding both footswitches.
          layout.add(loopParam("tone", "Tone", 1.f));
          layout.add(loopParam("fade", "Fade", 0.f));
          layout.add(loopParam("width", "Width", 0.5f));
          layout.add(loopParam("wetDiv", "Wet Div", 0.f));
          layout.add(loopParam("loopDiv", "Loop Div", 0.f));
          layout.add(loopParam("character", "Wear", 0.5f));
          layout.add(loopParam("wearNoise", "Noise", 1.f));
          // Where WORN's wear sits: PRE, before the loop, so it's recorded; POST, after it, so the loop keeps a clean take.
          layout.add(std::make_unique<APB>(juce::ParameterID("wearPost", 1), "Wear Post", false,
              juce::AudioParameterBoolAttributes()
                  .withStringFromValueFunction([](bool post, int) { return juce::String(post ? "Post" : "Pre"); })
                  .withValueFromStringFunction([](const juce::String& t) { return t.trim().toLowerCase().startsWith("post"); })));
          layout.add(std::make_unique<APB>(juce::ParameterID("tapeDelay", 1), "Tape Echo", false));
          // RECORD: the loop switch records what's played after it, for FREE or a number of bars.
          layout.add(std::make_unique<APC>(juce::ParameterID("recMode", 1), "Rec Mode", juce::StringArray{"Capture", "Record"}, 0));
          layout.add(loopParam("recBars", "Rec Bars", 0.f));
          layout.add(std::make_unique<APB>(juce::ParameterID("recAtTap", 1), "At Tap", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("freeze", 1), "Freeze", false));
          // The ramp engine: two generators, one per channel.
          // The two modulation sources: a rate, a shape and four switches each, and a
          // depth for each control it can move (0 is no route).
          for (int n = 1; n <= 2; ++n) {
              // Short enough for a controller's display: the names a host shows are these.
              static const char* const shortTarget[brood::kModTargets] = {"Time", "Wet Mix", "Length", "Mod Wet", "Clock", "Mod Loop", "Loop Lvl"};
              const juce::String m = "mod" + juce::String(n), name = "M" + juce::String(n) + " ";
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "On", 1), name + "On", true));
              layout.add(loopParam(m + "Rate", name + "Rate", 0.45f));
              layout.add(loopParam(m + "Wave", name + "Shape", 0.f));
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "Uni", 1), name + "Uni", false));
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "Sync", 1), name + "Sync", false));
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "Hold", 1), name + "Hold", false));
              for (int t = 0; t < brood::kModTargets; ++t)
                  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(m + "_" + brood::kModTargetIds[t], 1),
                      "M" + juce::String(n) + ">" + shortTarget[t], juce::NormalisableRange<float>(-1.f, 1.f), 0.f,
                      juce::AudioParameterFloatAttributes()
                          .withStringFromValueFunction([](float v, int) { return signedPct(v); })
                          .withValueFromStringFunction([](const juce::String& t) { return static_cast<float>(juce::jlimit(-1.0, 1.0, leadingNumber(t) / 100.0)); })));
          }
          layout.add(std::make_unique<APB>(juce::ParameterID("envSync", 1), "Env Sync", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("envSoft", 1), "Env Soft", false));
          // The FREEZE key, as a footswitch: hold freezes while held, a tap flips FREEZE.
          layout.add(std::make_unique<APB>(juce::ParameterID("freezeKey", 1), "Freeze Key", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("loopSwitch", 1), "Loop Switch", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("overdub", 1), "Overdub", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("clear", 1), "Clear", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("undo", 1), "Undo", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("redo", 1), "Redo", false));
          // RESAMPLE: a press starts one (a loop playing), a press while it runs ends it there.
          layout.add(std::make_unique<APB>(juce::ParameterID("resample", 1), "Resample", false));
          return layout;
      }()) {
    formats_.registerBasicFormats();
    // The standalone starts on a demo loop so it makes sound with nothing plugged in; in a DAW
    // the plugin processes the track.
    if (wrapperType == wrapperType_Standalone && loadDemo(0)) source = Demo;
    pullParams();
    core_.prepare(48000.0);
    coreRight_.p = core_.p;
    coreRight_.prepare(48000.0);
    coreRight_.setSide(1);   // RESAMPLE records each engine's own side of WET
    history = std::make_unique<BroodHistory>(apvts,
        juce::StringArray{"freezeKey", "loopSwitch", "overdub", "clear", "undo", "redo", "resample", "freeze"},
        [this](int id) { sendCommand(0, id); }, [this](int id) { sendCommand(1, id); });
    history->onChange = [this] { ui.undoDepth = history->undoDepth(); ui.redoDepth = history->redoDepth(); };
}

// What a control Brood moves by itself is called, and its value as the host has it (0..1).
static const char* autoParamId(brood::AutoParam a) {
    switch (a) {
        case brood::AutoParam::Start: return "start"; case brood::AutoParam::LoopEnd: return "loopEnd";
        case brood::AutoParam::Length: return "length"; case brood::AutoParam::LoopMode: return "loopMode";
        case brood::AutoParam::ModLoop: return "modLoop"; case brood::AutoParam::LoopDiv: return "loopDiv";
        case brood::AutoParam::LoopLevel: return "loopLevel"; case brood::AutoParam::Routing: return "routing";
        case brood::AutoParam::Freeze: return "freeze"; case brood::AutoParam::Mod1On: return "mod1On";
        default: return "mod2On";
    }
}
static float autoParamNormalised(brood::AutoParam a, double v) {
    if (a == brood::AutoParam::LoopMode) return static_cast<float>(v / 2.0);   // a choice of three
    if (a == brood::AutoParam::Routing) return static_cast<float>(v / 3.0);    // a choice of four
    return static_cast<float>(v);
}
static void applyAuto(brood::Params& p, brood::AutoParam a, double v) {
    switch (a) {
        case brood::AutoParam::Start: p.start = v; break; case brood::AutoParam::LoopEnd: p.loopEnd = v; break;
        case brood::AutoParam::Length: p.length = v; break; case brood::AutoParam::LoopMode: p.loopMode = static_cast<int>(v); break;
        case brood::AutoParam::ModLoop: p.modLoop = v; break; case brood::AutoParam::LoopDiv: p.loopDiv = v; break;
        case brood::AutoParam::LoopLevel: p.loopLevel = v; break; case brood::AutoParam::Routing: p.routing = static_cast<int>(v); break;
        case brood::AutoParam::Freeze: p.freezeLatch = v; break; case brood::AutoParam::Mod1On: p.mods[0].on = v > 0.5; break;
        default: p.mods[1].on = v > 0.5; break;
    }
}
void BroodProcessor::holdAuto(const brood::AutoChange& change) {
    for (auto& o : overrides_) if (o.active && o.which == change.which) { o.value = change.value; o.samplesLeft = static_cast<int>(getSampleRate() * 0.5); return; }
    for (auto& o : overrides_) if (!o.active) { o = Override{true, change.which, change.value, static_cast<int>(getSampleRate() * 0.5)}; return; }
}
void BroodProcessor::sendCommand(int type, int id) {
    const auto scope = commandFifo_.write(1);
    if (scope.blockSize1 > 0) commandQueue_[static_cast<size_t>(scope.startIndex1)] = Command{type, id};
}

juce::String BroodProcessor::describe(const juce::String& id, float v) const {
    const auto* wm = apvts.getRawParameterValue("wetMode");
    const auto* lm = apvts.getRawParameterValue("loopMode");
    const auto* ck = apvts.getRawParameterValue("clock");
    if (wm == nullptr || lm == nullptr || ck == nullptr) return juce::String(v, 3);   // still constructing
    const int wetMode = juce::roundToInt(wm->load()), loopMode = juce::roundToInt(lm->load());
    const auto* sm = apvts.getRawParameterValue("smooth");
    const bool smooth = sm != nullptr && sm->load() > 0.5f;
    const double hz = brood::clockHz(ck->load(), smooth);
    const double c = hz / brood::kCoreFs;
    if (id == "time") {
        // WET DIV runs the wet side slower, so every time on it is that many times longer.
        const auto* wd = apvts.getRawParameterValue("wetDiv");
        const int div = brood::divMap(wd != nullptr ? wd->load() : 0.f);
        const juce::String times = div > 1 ? " x" + juce::String(div) : juce::String();
        const double delayMost = brood::kDelayMaxSec * 0.95;   // the delay line's length
        if (wetMode == 0) return v > 0.985f ? juce::String("decay infinite") : "decay " + fmtTime(brood::expMap(v, 0.4, 25) * div / c);
        if (wetMode == 1) {
            const auto* sy = apvts.getRawParameterValue("sync");
            if (sy != nullptr && sy->load() > 0.5f)
                return juce::String(brood::syncLabel(v)) + times + " · " + fmtTime(std::min(brood::syncSeconds(hostBpm_.load(), v) * div, delayMost * brood::kCoreFs / hz));
            return "delay " + fmtTime(std::min(brood::expMap(v, 0.02, 2) * div, delayMost) / c);
        }
        if (wetMode == 3) {
            const auto* sy = apvts.getRawParameterValue("sync");
            if (sy != nullptr && sy->load() > 0.5f)
                return juce::String(brood::syncLabel(v)) + times + " back · " + fmtTime(std::min(brood::syncSecondsRaw(hostBpm_.load(), v) * div, brood::kWeaveBufSec * 0.85));
            return "memory " + fmtTime(std::min(2.8, (0.2 + v * 2.6) * div));
        }
        return "refresh " + fmtTime(brood::expMap(v, 0.05, 1.5) * div / c);
    }
    if (id == "wetMix") return "dry " + juce::String(wetMixDry(v)) + " / wet " + juce::String(wetMixWet(v));
    if (id == "loopLevel") return levelText(v, 12.0);
    if (id == "inputLevel") { const double db = -48.0 + v * 72.0; return levelText(v, 24.0) + (v > 0.0005 && db > 0.04 ? ", into the tape" : ""); }
    if (id == "monitor") { const int m = juce::roundToInt(v); return m == 1 ? "on" : m == 2 ? "off" : "auto"; }
    if (id == "length") {
        const double full = brood::kLoopSamples / hz;
        if (loopMode == 1) return "loop " + fmtTime(full * brood::expMap(v, 0.02, 1)) + " of " + fmtTime(full);
        if (loopMode == 0) {
            // ENV slices to a length, so a synced LENGTH is a division of the beat.
            // ENV has its own switch: the wet side's SYNC does not reach it.
            const auto* sy = apvts.getRawParameterValue("envSync");
            if (sy != nullptr && sy->load() > 0.5f)
                return juce::String(brood::syncLabel(v)) + " slice · " + fmtTime(brood::syncSecondsRaw(hostBpm_.load(), v));
        }
        return "slice " + fmtTime((loopMode == 0 ? brood::expMap(v, 0.01, 1) : brood::expMap(v, 0.01, 0.5)) / c);
    }
    if (id == "modWet") {
        if (wetMode == 0) return "smear " + pct(v);
        if (wetMode == 1) return "feedback " + pct(v);
        if (wetMode == 3) return v < 0.34 ? "long cloth " + pct(v) : "short stitches " + pct(v);
        return "rate " + signedRate(brood::slipSpeedMap(v));
    }
    if (id == "clock") {
        return "sample rate " + juce::String(brood::clockHz(v, smooth) / 1000, 1) + " kHz";
    }
    if (id == "modLoop") {
        if (loopMode == 0) return "sensitivity " + pct(v);
        if (loopMode == 1) return "tape " + fmtSpeed(brood::speedMap(v, 0.04));
        return "stretch " + fmtSpeed(brood::stretchMap(v)) + ", same pitch";
    }
    if (id == "wearPost") return v > 0.5f ? "post loop: heard worn, recorded clean" : "pre loop: recorded worn";
    if (id == "memory") {
        if (v <= 0.001f) return "pedal " + fmtTime(brood::kLoopSamples / hz);
        return "up to " + fmtTime(brood::expMap(v, 1, 30)) + " of " + fmtTime(30.0);
    }
    if (id == "loopEnd") return v >= 0.999f ? "to the end" : "plays to " + pct(v);
    if (id == "start") {
        // One meaning in every mode: 0 is the oldest audio, 1 the newest.
        if (v <= 0.005f) return "the oldest";
        if (v >= 0.995f) return "the newest";
        return pct(v) + " in";
    }
    if (id == "freeze") return v > 0.5f ? "held, ringing" : "moving";
    if (id == "tone") return v > 0.995f ? "open" : "hi-cut " + pct(1 - v);
    if (id == "recBars") { const int bars = recBarsFor(v); return bars == 0 ? juce::String("free") : juce::String(bars) + (bars == 1 ? " bar" : " bars"); }
    if (id == "wearNoise") return v <= 0.005f ? juce::String("worn hiss off") : "worn hiss " + pct(v);
    if (id == "fade") return v <= 0.001f ? "pedal decay" : "overdub keeps " + pct(1 - v);
    // WIDTH is where the removed SPREAD lives now: hard left is mono.
    if (id == "width") return v <= 0.005f ? "mono" : v < 0.49f ? "narrow " + pct(v * 2) :
                        v > 0.51f ? "wide " + pct((v - 0.5f) * 2) : "as built";
    if (id.startsWith("mod1") || id.startsWith("mod2")) {
        const auto field = id.substring(4);
        const juce::String rateOwner = id.substring(0, 4);
        if (field == "Wave") return juce::String(brood::kRampWaves[brood::rampWaveIndex(v)]).toLowerCase();
        if (field == "Rate") {
            const auto* rs = apvts.getRawParameterValue(rateOwner + "Sync");
            if (rs != nullptr && rs->load() > 0.5f)
                return juce::String(brood::rampDivLabel(v)) + " · " + fmtTime(brood::rampCycleSeconds(hostBpm_.load(), v)) + " a turn";
            return fmtTime(1.0 / brood::rampHertz(v)) + " a turn";
        }
        if (field == "Sync") return v > 0.5f ? "follows the tempo" : "free rate";
        if (field == "Hold") return v > 0.5f ? "held where it is" : "running";
        if (field.startsWith("_")) return (v > 0 ? "+" : "") + juce::String(juce::roundToInt(v * 100.f)) + "%";
    }
    if (id == "envSync") return v > 0.5f ? "slice follows the tempo" : "free slice";
    if (id == "wetDiv" || id == "loopDiv") {
        const int div = brood::divMap(v);
        return div == 1 ? juce::String("1x clock") : juce::String(div) + "x slower";
    }
    return juce::String(v, 3);
}

// What a host shows for a parameter's value: at most 8 characters, a number and its unit.
juce::String BroodProcessor::hostValue(const juce::String& id, float v) const {
    const auto* wm = apvts.getRawParameterValue("wetMode");
    const auto* lm = apvts.getRawParameterValue("loopMode");
    const auto* ck = apvts.getRawParameterValue("clock");
    if (wm == nullptr || lm == nullptr || ck == nullptr) return juce::String(v, 2);   // still constructing
    const int wetMode = juce::roundToInt(wm->load()), loopMode = juce::roundToInt(lm->load());
    const auto on = [this](const char* p) { const auto* r = apvts.getRawParameterValue(p); return r != nullptr && r->load() > 0.5f; };
    const bool smooth = on("smooth");
    const double hz = brood::clockHz(ck->load(), smooth);
    const double c = hz / brood::kCoreFs;
    if (id == "time") {
        const auto* wd = apvts.getRawParameterValue("wetDiv");
        const int div = brood::divMap(wd != nullptr ? wd->load() : 0.f);   // WET DIV: times that many times longer
        if (wetMode == 0) return v > 0.985f ? juce::String("Inf") : shortTime(brood::expMap(v, 0.4, 25) * div / c);
        if ((wetMode == 1 || wetMode == 3) && on("sync")) return juce::String(brood::syncLabel(v)) + (div > 1 ? "x" + juce::String(div) : juce::String());
        if (wetMode == 1) return shortTime(std::min(brood::expMap(v, 0.02, 2) * div, brood::kDelayMaxSec * 0.95) / c);
        if (wetMode == 3) return shortTime(std::min(2.8, (0.2 + v * 2.6) * div));
        return shortTime(brood::expMap(v, 0.05, 1.5) * div / c);
    }
    if (id == "length") {
        if (loopMode == 1) return shortTime(brood::kLoopSamples / hz * brood::expMap(v, 0.02, 1));
        if (loopMode == 0) return on("envSync") ? juce::String(brood::syncLabel(v)) : shortTime(brood::expMap(v, 0.01, 1) / c);
        return shortTime(brood::expMap(v, 0.01, 0.5) / c);
    }
    if (id == "modWet") return wetMode == 2 ? signedRate(brood::slipSpeedMap(v)) : pct(v);
    if (id == "modLoop") return loopMode == 0 ? pct(v) : loopMode == 1 ? shortSpeed(brood::speedMap(v, 0.04)) : shortSpeed(brood::stretchMap(v));
    if (id == "clock") return juce::String(brood::clockHz(v, smooth) / 1000, 1) + " kHz";
    if (id == "wetMix" || id == "loopEnd") return pct(v);
    if (id == "loopLevel") { const auto t = levelText(v, 12.0); return t == "off" ? juce::String("Off") : t; }
    if (id == "inputLevel") { const auto t = levelText(v, 24.0); return t == "off" ? juce::String("Off") : t; }
    if (id == "memory") return v <= 0.001f ? juce::String("Pedal") : shortTime(brood::expMap(v, 1, 30));
    if (id == "start") return v <= 0.005f ? juce::String("Oldest") : v >= 0.995f ? juce::String("Newest") : pct(v);
    if (id == "tone") return v > 0.995f ? juce::String("Open") : "Cut " + pct(1 - v);
    if (id == "fade") return v <= 0.001f ? juce::String("Pedal") : "Keep " + pct(1 - v);
    if (id == "width") return v <= 0.005f ? juce::String("Mono") : std::fabs(v - 0.5f) <= 0.01f ? juce::String("As built") : signedPct((v - 0.5) * 2);
    if (id == "wetDiv" || id == "loopDiv") { const int div = brood::divMap(v); return div == 1 ? juce::String("1x") : juce::String(div) + "x slow"; }
    if (id == "character") return v < 0.25f ? "Clean" : v > 0.75f ? "Worn" : "Pedal";
    if (id == "wearNoise") return v <= 0.005f ? juce::String("Off") : pct(v);
    if (id == "recBars") { const int bars = recBarsFor(v); return bars == 0 ? juce::String("Free") : juce::String(bars) + (bars == 1 ? " bar" : " bars"); }
    if (id.startsWith("mod1") || id.startsWith("mod2")) {
        const auto field = id.substring(4);
        if (field == "Wave") return titleCase(brood::kRampWaves[brood::rampWaveIndex(v)]);
        if (field == "Rate") return on((id.substring(0, 4) + "Sync").toRawUTF8()) ? titleCase(brood::rampDivLabel(v)) : juce::String(brood::rampHertz(v), 2) + " Hz";
    }
    return juce::String(v, 2);
}

// A value typed into a host's field, back into the parameter. Numbers with the units hostValue()
// shows are read for the plain ones; a value whose meaning depends on the mode in a way that
// can't be undone from the text (a speed, a division, a stepped CLOCK) keeps its current value
// rather than jumping to the top of its range.
float BroodProcessor::hostValueFromText(const juce::String& id, const juce::String& text) const {
    const auto* now = apvts.getRawParameterValue(id);
    const float current = now != nullptr ? now->load() : 0.f;
    const auto t = text.trim().toLowerCase();
    const double x = leadingNumber(t);
    const bool ms = t.endsWith("ms"), sec = !ms && t.endsWith("s");
    const double seconds = ms ? x / 1000.0 : x;
    const auto* wm = apvts.getRawParameterValue("wetMode");
    const auto* lm = apvts.getRawParameterValue("loopMode");
    const auto* ck = apvts.getRawParameterValue("clock");
    const int wetMode = wm != nullptr ? juce::roundToInt(wm->load()) : 0, loopMode = lm != nullptr ? juce::roundToInt(lm->load()) : 1;
    const double c = brood::clockHz(ck != nullptr ? ck->load() : 0.9f, false) / brood::kCoreFs;
    // The levels, in dB (or "off").
    if (id == "loopLevel" || id == "inputLevel") {
        if (t.startsWith("off") || t.startsWith("-inf")) return 0.f;
        if (t.endsWith("db") || t.containsAnyOf("0123456789"))
            return static_cast<float>(id == "loopLevel" ? brood::loopLevelOfDb(x) : brood::inputLevelOfDb(x));
    }
    if (t.endsWith("%")) {
        if (id == "tone" || id == "fade") return static_cast<float>(juce::jlimit(0.0, 1.0, 1.0 - x / 100.0));
        if (id == "width") return static_cast<float>(juce::jlimit(0.0, 1.0, 0.5 + x / 200.0));
        return static_cast<float>(juce::jlimit(0.0, 1.0, x / 100.0));
    }
    if ((ms || sec) && id == "time") {
        if (wetMode == 0) return static_cast<float>(inverseExp(seconds * c, 0.4, 25));
        if (wetMode == 1) return static_cast<float>(inverseExp(seconds * c, 0.02, 2));
        if (wetMode == 3) return static_cast<float>(juce::jlimit(0.0, 1.0, (seconds - 0.2) / 2.6));
        return static_cast<float>(inverseExp(seconds * c, 0.05, 1.5));
    }
    if ((ms || sec) && id == "length") {
        if (loopMode == 0) return static_cast<float>(inverseExp(seconds * c, 0.01, 1));
        if (loopMode == 2) return static_cast<float>(inverseExp(seconds * c, 0.01, 0.5));
    }
    if (sec && id == "memory") return static_cast<float>(inverseExp(x, 1, 30));
    if (t.endsWith("hz") && !t.endsWith("khz") && id.endsWith("Rate")) return static_cast<float>(inverseExp(x, 0.05, 8));
    return current;
}

juce::String BroodProcessor::describe(const juce::String& id) const {
    const auto* p = apvts.getRawParameterValue(id);
    return p ? describe(id, p->load()) : juce::String();
}

bool BroodProcessor::loadDemo(int index) {
#if BROOD_DEMOS
    const auto& demo = kDemos[juce::jlimit(0, static_cast<int>(std::size(kDemos)) - 1, index)];
    for (int i = 0; i < BroodDemos::namedResourceListSize; ++i) {
        if (juce::String(BroodDemos::originalFilenames[i]) != demo.file) continue;
        int size = 0;
        const char* data = BroodDemos::getNamedResource(BroodDemos::namedResourceList[i], size);
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader(flac.createReaderFor(new juce::MemoryInputStream(data, static_cast<size_t>(size), false), true));
        if (reader == nullptr || reader->lengthInSamples < 2) return false;
        juce::AudioBuffer<float> loop(2, static_cast<int>(reader->lengthInSamples));
        reader->read(&loop, 0, loop.getNumSamples(), 0, true, true);
        {
            const juce::SpinLock::ScopedLockType lock(demoLock_);
            demo_ = std::move(loop);
            demoRate_ = reader->sampleRate;
            demoPos_ = 0;
        }
        hostBpm_.store(demo.bpm);   // a host that reports a tempo overrides this every block
        ui.bpm.store(demo.bpm);
        return true;
    }
#else
    juce::ignoreUnused(index);
#endif
    return false;
}

void BroodProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    tape_.prepare(sampleRate);
    pullParams();
    core_.prepare(sampleRate);
    coreRight_.p = core_.p;
    coreRight_.prepare(sampleRate);
    lastWetSw_ = lastLoopSw_ = false;
    const auto capacity = static_cast<size_t>(std::max(samplesPerBlock, 32));
    inputLeft_.assign(capacity, 0.f); inputRight_.assign(capacity, 0.f);
    scratchLeft_.assign(capacity, 0.f); scratchRight_.assign(capacity, 0.f);
}

void BroodProcessor::pullParams() {
    auto& p = core_.p;
    p.time = param("time"); p.wetMix = param("wetMix"); p.length = param("length");
    p.modWet = param("modWet"); p.clock = param("clock"); p.modLoop = param("modLoop");
    p.monitor = juce::roundToInt(param("monitor"));
    p.wetMode = juce::roundToInt(param("wetMode"));
    p.routing = juce::roundToInt(param("routing"));
    p.loopMode = juce::roundToInt(param("loopMode"));
    p.trails = param("trails") > 0.5f; p.smooth = param("smooth") > 0.5f;
    p.sync = param("sync") > 0.5f;
    p.memory = param("memory"); p.loopEnd = param("loopEnd");
    p.start = param("start");
    p.tone = param("tone"); p.fade = param("fade"); p.width = param("width");
    p.wetDiv = param("wetDiv"); p.loopDiv = param("loopDiv");
    // WEAR is one line from clean to worn: HI-Q conversion, the pedal, the MK I's noisy manners.
    p.classic = param("character") > 0.75f;
    p.hiQ = param("character") < 0.25f;
    p.wearNoise = param("wearNoise");
    p.wearPost = param("wearPost") > 0.5f;
    p.overdubLatch = param("overdub") > 0.5f;
    p.loopLevel = param("loopLevel");
    p.envSoft = param("envSoft") > 0.5f;
    p.freezeLatch = param("freeze") > 0.5f ? 1.0 : 0.0;
    for (int k = 0; k < 2; ++k) {
        const juce::String n = "mod" + juce::String(k + 1);
        auto& m = p.mods[k];
        const auto value = [this, &n](const char* f) { return apvts.getRawParameterValue(n + f)->load(); };
        m.on = value("On") > 0.5f; m.rate = value("Rate"); m.wave = value("Wave");
        m.uni = value("Uni") > 0.5f;
        m.sync = value("Sync") > 0.5f; m.hold = value("Hold") > 0.5f;
        for (int t = 0; t < brood::kModTargets; ++t) m.depth[t] = apvts.getRawParameterValue(n + "_" + brood::kModTargetIds[t])->load();
    }
    p.envSync = param("envSync") > 0.5f;
    p.bpm = hostBpm_.load();
    p.tapeDelay = param("tapeDelay") > 0.5f;
    p.recMode = juce::roundToInt(param("recMode"));
    p.recBars = recBarsFor(param("recBars"));
    p.recAtTap = param("recAtTap") > 0.5f;
    p.barQuarters = hostBarQuarters_.load();
    p.hostPlaying = hostPlaying_; p.ppq = hostPpq_; p.barStartPpq = hostBarStart_;
    // A new take fitted the window (START, END, and LENGTH in TAPE, to the whole take). Until
    // the message thread has moved those knobs, keep the cores on the fitted values, or the
    // old knob positions would trim the new take for a block or two.
    // Half a second at most: automation holding a knob elsewhere wins after that.
    for (auto& o : overrides_) {
        if (!o.active) continue;
        const float now = apvts.getParameter(autoParamId(o.which))->getValue();
        if (std::fabs(now - autoParamNormalised(o.which, o.value)) < 1e-3f || (o.samplesLeft -= getBlockSize()) <= 0) o.active = false;
        else applyAuto(p, o.which, o.value);
    }
    coreRight_.p = p;
}

bool BroodProcessor::loadFile(const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> r(formats_.createReaderFor(f));
    // A file's header is outside data: refuse silly rates, read two channels at most and
    // twenty minutes at most, so a strange or crafted file can't take gigabytes.
    if (!r || r->numChannels < 1 || r->lengthInSamples <= 0 || !(r->sampleRate >= 1000.0 && r->sampleRate <= 768000.0)) return false;
    const auto frames = static_cast<int>(juce::jmin<juce::int64>(r->lengthInSamples, static_cast<juce::int64>(20.0 * 60.0 * r->sampleRate)));
    juce::AudioBuffer<float> buf(juce::jmin(2, static_cast<int>(r->numChannels)), frames);
    r->read(&buf, 0, frames, 0, true, true);
    const juce::SpinLock::ScopedLockType lock(fileLock_);   // the audio thread only tries this lock
    file_ = std::move(buf); fileRate_ = r->sampleRate; filePos_ = 0; fileName_ = f.getFileName();
    return true;
}

// Audio thread, after each block: the columns of the history written since last time, the
// louder of the two sides, for the take strip's live view.
void BroodProcessor::publishLive() {
    const int64_t total = core_.histWritten();
    const int N = brood::kMaxLoopSamples, C = brood::kLiveCols;
    const int pos = core_.histWritePos();
    const float* l = core_.liveColumns();
    const float* r = coreRight_.liveColumns();
    auto copy = [&](int c) { live_[static_cast<size_t>(c)].store(std::max(l[c], r[c]), std::memory_order_relaxed); };
    if (liveSeen_ < 0 || total < liveSeen_ || total - liveSeen_ >= N) {
        for (int c = 0; c < C; ++c) copy(c);
    } else {
        // From the column the last block ended in to the one being written now.
        const int was = static_cast<int>(((pos - (total - liveSeen_)) % N + N) % N);
        for (int c = was / brood::kLiveColTicks, k = 0; k <= C; c = (c + 1) % C, ++k) {
            copy(c);
            if (c == pos / brood::kLiveColTicks) break;
        }
    }
    liveSeen_ = total;
    ui.histPos = pos;
    ui.memTicks = core_.memoryTicks();
    ui.takeSoFar = core_.takeSoFar();
}

void BroodProcessor::handleAsyncUpdate() {
    const auto off = [this](const char* id, std::atomic<bool>& flag) {
        if (!flag.exchange(false)) return;
        if (auto* prm = apvts.getParameter(id); prm != nullptr && prm->getValue() > 0.5f) prm->setValueNotifyingHost(0.f);
    };
    off("clear", resetClear_);
    off("undo", resetUndo_);
    off("overdub", resetOverdub_);
    if (flipFreeze_.exchange(false))
        if (auto* prm = apvts.getParameter("freeze")) {
            prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->getValue() > 0.5f ? 0.f : 1.f); prm->endChangeGesture();
        }
    // A new take: START and END around all of it, and LENGTH full in TAPE.
    off("redo", resetRedo_);
    off("resample", resetResample_);
    // The UNDO history: the cores' versions (with the controls each action moved), MIDI moves,
    // and the presses of UNDO and REDO, in that order.
    {
        const auto scope = versionFifo_.read(versionFifo_.getNumReady());
        auto take = [&](int start, int count) {
            for (int i = 0; i < count; ++i)
                history->versionEvent(versionQueue_[static_cast<size_t>(start + i)], [](brood::AutoParam a) { return juce::String(autoParamId(a)); }, autoParamNormalised);
        };
        take(scope.startIndex1, scope.blockSize1); take(scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = midiFifo_.read(midiFifo_.getNumReady());
        auto params = getParameters();
        auto take = [&](int start, int count) {
            for (int i = 0; i < count; ++i) {
                const auto& m = midiQueue_[static_cast<size_t>(start + i)];
                if (auto* prm = dynamic_cast<juce::RangedAudioParameter*>(params[m.index])) history->midiMoved(m.cc, prm->getParameterID(), m.before, m.after);
            }
        };
        take(scope.startIndex1, scope.blockSize1); take(scope.startIndex2, scope.blockSize2);
    }
    if (undoRequest_.exchange(false)) history->undo();
    if (redoRequest_.exchange(false)) history->redo();
}

bool BroodProcessor::copyWave(std::array<float, kWaveBins>& out) const {
    const juce::SpinLock::ScopedTryLockType lock(waveLock_);
    if (!lock.isLocked()) return false;
    out = wave_;
    return true;
}

// Swap a state-carried take into the engines. Message thread fills it, audio
// thread takes it, so nothing is allocated or locked for long on the audio side.
void BroodProcessor::applyPendingTakes() {
    for (int i = 0; i < 2; ++i) {
        PendingTake ready;
        {
            const juce::SpinLock::ScopedTryLockType lock(takeLock_);
            if (!lock.isLocked()) return;      // try again next block
            if (!pendingTake_[i].has) continue;
            ready = std::move(pendingTake_[i]);
            pendingTake_[i] = PendingTake{};
        }
        auto& core = i == 0 ? core_ : coreRight_;
        core.restoreLoop(ready.pcm.data(), static_cast<int>(ready.pcm.size()), ready.play, ready.follows, ready.anchor);
    }
}

void BroodProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    if (buffer.getNumChannels() < 1 || n == 0 || nOut < 1) { buffer.clear(); return; }

    // DAW tempo for the sync dip. Static tempo, tempo ramps and offline renders
    // all reduce to "the bpm at this block"; if the host offers none, the last
    // known value stands (120 BPM until told otherwise).
    bool hostPlaying = false;
    double hostPpq = 0, hostBarStart = 0;
    hostPlaying_ = false;   // stopped unless this block's position says otherwise
    if (auto* head = getPlayHead()) {   // not "playHead": that shadows AudioProcessor's own
        if (const auto position = head->getPosition()) {
            if (const auto sig = position->getTimeSignature(); sig.hasValue() && sig->denominator > 0)
                hostBarQuarters_.store(static_cast<float>(sig->numerator) * 4.f / static_cast<float>(sig->denominator));
            if (const auto ppq = position->getPpqPosition(); ppq.hasValue() && position->getPpqPositionOfLastBarStart().hasValue()) {
                hostPlaying = position->getIsPlaying();
                hostPpq = *ppq;
                hostBarStart = *position->getPpqPositionOfLastBarStart();
                hostPlaying_ = hostPlaying; hostPpq_ = hostPpq; hostBarStart_ = hostBarStart;
            }
            if (const auto bpm = position->getBpm()) {
                if (std::isfinite(*bpm) && *bpm > 5.0 && *bpm < 400.0) {
                    hostBpm_.store(static_cast<float>(*bpm));
                    ui.bpm.store(static_cast<float>(*bpm));
                }
            }
        }
    }

    // A useful subset of the pedal's CC map. Changes are applied at block
    // boundaries; hosts can also automate every front-panel parameter directly.
    int midiWet = -1, midiLoop = -1;
    // What a controller moves, the history records (a burst of one controller is one action).
    int ccNow = -1;
    const auto setParam = [this, &ccNow](const char* pid, float v) {
        auto* parameter = apvts.getParameter(pid);
        if (parameter == nullptr) return;
        const float before = parameter->getValue();
        parameter->setValueNotifyingHost(v);
        if (ccNow >= 0 && std::fabs(before - parameter->getValue()) > 1e-6f) {
            const auto scope = midiFifo_.write(1);
            if (scope.blockSize1 > 0) midiQueue_[static_cast<size_t>(scope.startIndex1)] = MidiMove{ccNow, parameter->getParameterIndex(), before, parameter->getValue()};
        }
    };
    // A press on a momentary CC (anything above 0) acts once, like the panel's key.
    const auto press = [&](const char* pid, int value) { if (value != 0) setParam(pid, 1.f); };
    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        if (!message.isController()) continue;
        const int cc = message.getControllerNumber(), value = message.getControllerValue();
        ccNow = cc;
        const char* id = nullptr;
        if (cc >= 14 && cc <= 19) {
            static const char* const knobs[] = {"time", "wetMix", "length", "modWet", "clock", "modLoop"};
            id = knobs[cc - 14];
            setParam(id, value / 127.f);
        } else if (cc >= 21 && cc <= 23) {
            static const char* const modes[] = {"wetMode", "routing", "loopMode"};
            id = modes[cc - 21];
            // The pedal's three-way values (0 or 1, 2, 3); ROUTING's fourth, OFF, is 4 and up.
            const float choice = cc == 22 ? (value <= 1 ? 0.f : value == 2 ? 1.f / 3 : value == 3 ? 2.f / 3 : 1.f)
                                          : (value <= 1 ? 0.f : value == 2 ? 0.5f : 1.f);
            setParam(id, choice);
        } else {
            if (cc == 75) id = "trails"; else if (cc == 78) id = "smooth";   // SPREAD / DRY KILL / LATCH retired
            else if (cc == 102) midiLoop = value != 0;
            else if (cc == 103) midiWet = value != 0;
            else if (cc == 20) { for (const char* m : {"mod1Rate", "mod2Rate"}) setParam(m, value / 127.f); }
            else if (cc == 53) setParam("wetDiv", value / 127.f);
            else if (cc == 54) setParam("loopDiv", value / 127.f);
            else if (cc == 52) { for (const char* m : {"mod1Hold", "mod2Hold"}) setParam(m, value != 0 ? 1.f : 0.f); }
            // WEAR: 0 is the pedal as built, anything above it worn (the pedal's CLASSIC switch).
            else if (cc == 71) setParam("character", value != 0 ? 1.f : 0.5f);
            else if (cc == 28) setParam("loopLevel", value / 127.f);   // the pedal's LEVEL BALANCE, now LOOP LEVEL
            else if (cc == 29) setParam("inputLevel", value / 127.f);
            else if (cc == 31) setParam("monitor", value <= 1 ? 0.f : value == 2 ? 0.5f : 1.f);   // AUTO, ON, OFF
            else if (cc == 106) setParam("overdub", value != 0 ? 1.f : 0.f);
            else if (cc == 108) press("undo", value);
            else if (cc == 110) press("clear", value);
            else if (cc == 107) press("resample", value);
            else if (cc == 109) press("redo", value);
            else if (cc == 105) id = "freeze";
            if (id != nullptr) setParam(id, value != 0 ? 1.f : 0.f);
        }
    }
    applyPendingTakes();
    pullParams();
    auto tap = [&](int which) {
        core_.footswitch(which, true); coreRight_.footswitch(which, true);
        core_.footswitch(which, false); coreRight_.footswitch(which, false);
    };
    // CC 103, the pedal's WET switch: ROUTING OFF, or back to the routing it had before.
    if (midiWet >= 0) {
        const int routingNow = juce::roundToInt(param("routing"));
        if (midiWet == 0 && routingNow != 3) { lastRouting_ = routingNow; setParam("routing", 1.f); }
        else if (midiWet != 0 && routingNow == 3) setParam("routing", static_cast<float>(lastRouting_) / 3.f);
        pullParams();
    }
    if (midiLoop >= 0 && (core_.state().loop != brood::LoopLed::Off) != (midiLoop != 0)) tap(1);
    const bool wetSw = param("freezeKey") > 0.5f, loopSw = param("loopSwitch") > 0.5f;
    if (wetSw != lastWetSw_) { lastWetSw_ = wetSw; core_.footswitch(0, wetSw); coreRight_.footswitch(0, wetSw); }
    if (loopSw != lastLoopSw_) { lastLoopSw_ = loopSw; core_.footswitch(1, loopSw); coreRight_.footswitch(1, loopSw); }
    // The history's swaps and releases, to both engines alike.
    {
        const auto scope = commandFifo_.read(commandFifo_.getNumReady());
        auto run = [&](int start, int count) {
            for (int i = 0; i < count; ++i) {
                const auto& c = commandQueue_[static_cast<size_t>(start + i)];
                if (c.type == 0) { core_.swapVersion(c.id); coreRight_.swapVersion(c.id); }
                else { core_.releaseVersion(c.id); coreRight_.releaseVersion(c.id); }
            }
        };
        run(scope.startIndex1, scope.blockSize1); run(scope.startIndex2, scope.blockSize2);
    }
    const bool clearNow = param("clear") > 0.5f, undoNow = param("undo") > 0.5f, redoNow = param("redo") > 0.5f, resampleNow = param("resample") > 0.5f;
    if (clearNow && !lastClear_) { core_.clearLoop(); coreRight_.clearLoop(); }
    // UNDO stands a RECORD take or a RESAMPLE down if one is under way; otherwise it's the history's.
    if (undoNow && !lastUndo_) {
        const bool left = core_.cancelActive(), right = coreRight_.cancelActive();
        if (!left && !right) undoRequest_ = true;
    }
    if (redoNow && !lastRedo_) redoRequest_ = true;
    if (resampleNow && !lastResample_) { core_.resampleKey(); coreRight_.resampleKey(); }
    lastClear_ = clearNow; lastUndo_ = undoNow; lastRedo_ = redoNow; lastResample_ = resampleNow;
    if (clearNow) resetClear_ = true;
    if (undoNow) resetUndo_ = true;
    if (redoNow) resetRedo_ = true;
    if (resampleNow) resetResample_ = true;

    // A well-behaved host does not exceed its promised block size. Grow only if it does.
    const auto needed = static_cast<size_t>(n);
    if (inputLeft_.size() < needed) { inputLeft_.resize(needed); inputRight_.resize(needed); scratchLeft_.resize(needed); scratchRight_.resize(needed); }
    float* xL = inputLeft_.data(); float* xR = inputRight_.data();
    const int src = source.load();
    if (src == Demo) {
        // The loop at its own rate, read across to the host's.
        std::fill(xL, xL + n, 0.f); std::fill(xR, xR + n, 0.f);
        const juce::SpinLock::ScopedTryLockType lock(demoLock_);
        if (lock.isLocked() && demo_.getNumSamples() > 1 && demoRate_ > 0) {
            const double ratio = demoRate_ / getSampleRate();
            const int len = demo_.getNumSamples();
            const float* left = demo_.getReadPointer(0);
            const float* right = demo_.getReadPointer(1);
            for (int i = 0; i < n; ++i) {
                const int i0 = static_cast<int>(demoPos_), i1 = (i0 + 1) % len;
                const float fr = static_cast<float>(demoPos_ - i0);
                xL[i] = left[i0] + (left[i1] - left[i0]) * fr;
                xR[i] = right[i0] + (right[i1] - right[i0]) * fr;
                demoPos_ += ratio; while (demoPos_ >= len) demoPos_ -= len;
            }
        }
    } else if (src == File) {
        std::fill(xL, xL + n, 0.f); std::fill(xR, xR + n, 0.f);
        const juce::SpinLock::ScopedTryLockType lock(fileLock_);
        if (lock.isLocked() && file_.getNumSamples() > 1 && fileRate_ > 0) {
            const double ratio = fileRate_ / getSampleRate();
            const int len = file_.getNumSamples(), nc = file_.getNumChannels();
            const float* left = file_.getReadPointer(0);
            const float* right = file_.getReadPointer(nc > 1 ? 1 : 0);
            for (int i = 0; i < n; ++i) {
                const int i0 = static_cast<int>(filePos_), i1 = (i0 + 1) % len;
                const float fr = static_cast<float>(filePos_ - i0);
                xL[i] = (left[i0] + (left[i1] - left[i0]) * fr);
                xR[i] = (right[i0] + (right[i1] - right[i0]) * fr);
                filePos_ += ratio; while (filePos_ >= len) filePos_ -= len;
            }
        }
    } else {
        const float* left = nIn > 0 ? buffer.getReadPointer(0) : nullptr;
        const float* right = nIn > 1 ? buffer.getReadPointer(1) : left;
        for (int i = 0; i < n; ++i) { xL[i] = left ? left[i] : 0.f; xR[i] = right ? right[i] : 0.f; }
    }

    // The input level, and past 0 dB the tape: everything after it hears the driven sound.
    const float gain = static_cast<float>(brood::inputLevelGain(param("inputLevel")));
    if (gain != 1.f || inputGainNow_ != 1.f) {
        const float step = (gain - inputGainNow_) / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            const float g = inputGainNow_ + step * static_cast<float>(i + 1);
            xL[i] *= g; xR[i] *= g;
        }
        tape_.process(xL, xR, n, juce::Decibels::gainToDecibels(inputGainNow_, -100.f), juce::Decibels::gainToDecibels(gain, -100.f));
        inputGainNow_ = gain;
    }

    // Each engine is mono in, stereo out. The left engine supplies the left
    // channel and the right engine the right one: taking the *left* output of both
    // (as this used to) threw away every engine's right side, so on a mono track —
    // where the two engines are identical — the reverb's stereo, WEAVE's panning
    // and WIDTH all collapsed to dual mono.
    float* outL = buffer.getWritePointer(0);
    float* outR = nOut > 1 ? buffer.getWritePointer(1) : scratchRight_.data();
    core_.process(xL, outL, scratchLeft_.data(), n);
    coreRight_.process(xR, scratchLeft_.data(), outR, n);
    if (nOut == 1)   // a mono output hears both sides rather than only the left one
        for (int i = 0; i < n; ++i) outL[i] = 0.5f * (outL[i] + outR[i]);
    for (int c = nOut; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);

    // The Bench's bars: the exact output, from the next bar line.
    outputRecorder_.process(outL, nOut > 1 ? outR : outL, n, hostPlaying, hostPpq, hostBarStart, hostBarQuarters_.load(), hostBpm_.load());

    const auto st = core_.state(), rightState = coreRight_.state();
    ui.envGate = core_.envGateOpen();
    for (int t = 0; t < brood::kModTargets; ++t) ui.effective[static_cast<size_t>(t)] = static_cast<float>(core_.effective(t));
    for (int k = 0; k < 2; ++k) ui.modValue[static_cast<size_t>(k)] = static_cast<float>(core_.rampValue(k));
    ui.wetFed = st.wetFed; ui.inputHeard = st.inputHeard; ui.routingNow = st.routingNow; ui.freeze = st.freeze; ui.loopLed = static_cast<int>(st.loop); ui.recArmed = st.recArmed;
    ui.lsrc = st.lsrc; ui.winStart = st.winStart; ui.winLen = st.winLen; ui.winSpan = core_.loopSpan(); ui.pos = static_cast<float>(st.pos);
    for (size_t k = 0; k < 2; ++k) { ui.head[k] = static_cast<float>(st.head[k]); ui.headLevel[k] = st.headLevel[k]; }
    ui.clockHz = static_cast<float>(st.clockHz); ui.histCycles = st.histCycles;
    ui.peakIn = std::max(st.peakIn, rightState.peakIn); ui.peakOut = std::max(st.peakOut, rightState.peakOut);

    const bool leftDirty = core_.takeLoopWaveDirty(), rightDirty = coreRight_.takeLoopWaveDirty();
    const bool wantWave = leftDirty || rightDirty || pendingWave_;
    // An overdub redraws only the bins it wrote, every block, so the strip keeps up with it.
    int dubLo = 0, dubHi = -1, lo = 0, hi = 0;
    if (core_.takeDubDirty(lo, hi)) { dubLo = lo; dubHi = hi; }
    if (coreRight_.takeDubDirty(lo, hi)) { dubLo = dubHi < 0 ? lo : std::min(dubLo, lo); dubHi = std::max(dubHi, hi); }
    if (wantWave || (dubHi >= 0 && st.lsrc > 0)) {
        const juce::SpinLock::ScopedTryLockType lock(waveLock_);
        if (!lock.isLocked()) pendingWave_ = true;   // the dirty cells are lost, so redraw the lot
        else if (wantWave) {
            core_.loopPeaks(wave_.data(), kWaveBins); coreRight_.loopPeaks(waveRight_.data(), kWaveBins);
            for (size_t i = 0; i < wave_.size(); ++i) wave_[i] = std::max(wave_[i], waveRight_[i]);
            ui.waveVersion.fetch_add(1); pendingWave_ = false;
        } else {
            const auto bin = [&](int cell) { return static_cast<int>(static_cast<int64_t>(cell) * kWaveBins / st.lsrc); };
            const int b0 = std::max(0, bin(dubLo)), b1 = std::min(kWaveBins - 1, bin(dubHi));
            core_.loopPeaksRange(wave_.data(), kWaveBins, b0, b1); coreRight_.loopPeaksRange(waveRight_.data(), kWaveBins, b0, b1);
            for (int i = b0; i <= b1; ++i) wave_[static_cast<size_t>(i)] = std::max(wave_[static_cast<size_t>(i)], waveRight_[static_cast<size_t>(i)]);
            ui.waveVersion.fetch_add(1);
        }
    }
    publishLive();
    {
        // The left engine's versions go to the history; the right one keeps step and says the same.
        bool lengthToo = false;
        core_.takeFitted(lengthToo); coreRight_.takeFitted(lengthToo);
        brood::VersionEvent ev;
        bool any = false;
        while (core_.popVersionEvent(ev)) {
            for (int i = 0; i < ev.nAuto; ++i) holdAuto(ev.autos[i]);
            const auto scope = versionFifo_.write(1);
            if (scope.blockSize1 > 0) versionQueue_[static_cast<size_t>(scope.startIndex1)] = ev;
            any = true;
        }
        while (coreRight_.popVersionEvent(ev)) {}
        if (any) triggerAsyncUpdate();
    }
    ui.canOverdub = st.canOverdub; ui.dubLatched = st.dubLatched;
    ui.resampling = st.resampling; ui.canResample = st.canResample; ui.resampleLimitHit = st.resampleLimitHit;
    ui.resampleSecs = static_cast<float>(st.resampleSecs); ui.resampleNeed = static_cast<float>(st.resampleNeed);
    ui.follows = st.follows; ui.parked = st.parked;
    // OVERDUB on as the core saw it, but not latched: no loop to layer into, or the loop
    // stopped. Set it back so a controller shows the truth.
    if (core_.p.overdubLatch && !core_.overdubLatched()) resetOverdub_ = true;
    // A tap on the FREEZE key flips the FREEZE switch, on the message thread so the host hears it.
    if (core_.takeFreezeTap()) flipFreeze_ = true;
    coreRight_.takeFreezeTap();
    if (resetClear_.load() || resetUndo_.load() || resetOverdub_.load() || flipFreeze_.load() || resetRedo_.load() || resetResample_.load()
        || undoRequest_.load() || redoRequest_.load() || midiFifo_.getNumReady() > 0) triggerAsyncUpdate();
    const double secs = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
    ui.load = static_cast<float>(100.0 * secs / (n / getSampleRate()));
}

// The captured take, as data the state can carry. Two engines (left and right)
// each hold a copy of what they caught.
static void writeTake(juce::ValueTree& parent, const brood::Core& core, const char* name) {
    const int n = core.capturedLength();
    if (n < 64) return;
    std::vector<float> pcm(static_cast<size_t>(n));
    const int got = core.capturedLoop(pcm.data(), n);
    if (got < 64) return;
    std::vector<int16_t> packed;
    brood::Core::packLoop(pcm.data(), got, packed);          // float → int16, half the bytes
    juce::MemoryOutputStream raw;
    {
        juce::GZIPCompressorOutputStream gz(raw);           // the state is base64'd, so compress first
        gz.write(packed.data(), packed.size() * sizeof(int16_t));
        gz.flush();
    }
    juce::ValueTree take("TAKE");
    take.setProperty("id", juce::String(name), nullptr);
    take.setProperty("samples", got, nullptr);
    take.setProperty("play", core.loopLedState() != brood::LoopState::Off, nullptr);
    // A transport take reopens waiting for play, locked to the song from where it began.
    if (core.takeFollowsTransport()) {
        take.setProperty("follows", true, nullptr);
        take.setProperty("anchor", core.takeAnchor(), nullptr);
    }
    take.setProperty("pcm", juce::MemoryBlock(raw.getData(), raw.getDataSize()).toBase64Encoding(), nullptr);
    parent.appendChild(take, nullptr);
}

bool BroodProcessor::takeMoment(const juce::File& file) {
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr || reader->numChannels < 1 || reader->lengthInSamples < 64 || !(reader->sampleRate >= 1000.0 && reader->sampleRate <= 768000.0)) return false;
    const auto frames = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(300.0 * reader->sampleRate)));
    juce::AudioBuffer<float> source(2, frames);
    reader->read(&source, 0, frames, 0, true, true);
    // Resampled to the clock the looper runs at now, so it plays back at its own pitch.
    const double ratio = reader->sampleRate / juce::jmax(1.f, ui.clockHz.load());
    const int n = juce::jlimit(64, static_cast<int>(brood::kMaxLoopSamples), static_cast<int>(frames / ratio));
    const juce::SpinLock::ScopedLockType lock(takeLock_);
    for (int c = 0; c < 2; ++c) {
        const float* in = source.getReadPointer(c);
        PendingTake ready;
        ready.pcm.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double at = i * ratio;
            const int i0 = juce::jmin(frames - 1, static_cast<int>(at));
            const double f = at - i0;
            const auto sample = [&](int j) { return static_cast<double>(in[juce::jlimit(0, frames - 1, j)]); };
            ready.pcm[static_cast<size_t>(i)] = static_cast<float>(brood::hermite(sample(i0 - 1), sample(i0), sample(i0 + 1), sample(i0 + 2), f));
        }
        ready.play = true;
        ready.has = true;
        pendingTake_[c] = std::move(ready);
    }
    return true;
}

bool BroodProcessor::copyMoment(juce::AudioBuffer<float>& out, double& sampleRate) const {
    const int n = juce::jmin(core_.capturedLength(), coreRight_.capturedLength());
    if (n < 64) return false;
    std::vector<float> left(static_cast<size_t>(n)), right(static_cast<size_t>(n));
    core_.capturedLoop(left.data(), n);
    coreRight_.capturedLoop(right.data(), n);
    const int start = juce::jlimit(0, n - 1, ui.winStart.load());
    const int len = juce::jlimit(1, n - start, ui.winLen.load());
    out.setSize(2, len);
    out.copyFrom(0, 0, left.data() + start, len);
    out.copyFrom(1, 0, right.data() + start, len);
    out.applyGain(static_cast<float>(brood::loopLevelGain(apvts.getRawParameterValue("loopLevel")->load())));   // sent as you hear it
    sampleRate = static_cast<double>(ui.clockHz.load());
    return true;
}

void BroodProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    state.setProperty("source", source.load(), nullptr);
    state.setProperty("presetName", presetName, nullptr);
    state.setProperty("stateVersion", kStateVersion, nullptr);
    // A host may render offline in a fresh instance, and a reloaded set starts
    // empty; without the take in here a bounce comes back as silence or as
    // something other than what was playing.
    // A restored state hands its TAKES child back through apvts.copyState(), so
    // strip the old one first: otherwise every save after a load would stack
    // another copy of the audio and the set file would grow on each round trip.
    for (auto stale = state.getChildWithName("TAKES"); stale.isValid(); stale = state.getChildWithName("TAKES"))
        state.removeChild(stale, nullptr);
    juce::ValueTree takes("TAKES");
    writeTake(takes, core_, "left");
    writeTake(takes, coreRight_, "right");
    if (takes.getNumChildren() > 0) state.appendChild(takes, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void BroodProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        // Sets saved before the rename carry the old tag; their parameters are
        // the same, so they load under the new one.
        if (!xml->hasTagName(apvts.state.getType()) && !xml->hasTagName(kLegacyStateTag)) return;
        xml->setTagName(apvts.state.getType().toString());
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.hasProperty("source") && wrapperType == wrapperType_Standalone) { const int saved = static_cast<int>(state["source"]); source = saved == File ? Live : juce::jlimit(0, 1, saved); }
        if (state.hasProperty("presetName")) presetName = state["presetName"].toString();
        if (history != nullptr) history->clear();   // a reopened set has no undo
        // Unpack any take the state carries; the audio thread swaps it in at its
        // next block, so a render that starts from a saved state hears the loop.
        for (int c = 0; c < state.getNumChildren(); ++c) {
            const auto takes = state.getChild(c);
            if (!takes.hasType("TAKES")) continue;
            for (int i = 0; i < takes.getNumChildren(); ++i) {
                const auto take = takes.getChild(i);
                const int slot = take["id"].toString() == "right" ? 1 : 0;
                // A set file is outside data: a damaged or crafted one must not make us
                // allocate more than the longest take the core can hold (restoreLoop
                // refuses anything longer anyway).
                const double claimed = static_cast<double>(take["samples"]);
                if (!(claimed >= 64.0 && claimed <= static_cast<double>(brood::kMaxLoopSamples))) continue;
                const int samples = static_cast<int>(claimed);
                const auto encoded = take["pcm"].toString();
                if (encoded.isEmpty()) continue;
                juce::MemoryBlock block;
                if (!block.fromBase64Encoding(encoded) || block.getSize() < 2) continue;
                juce::MemoryInputStream raw(block, false);
                juce::GZIPDecompressorInputStream gz(raw);
                std::vector<int16_t> pcm(static_cast<size_t>(samples));
                gz.read(pcm.data(), samples * static_cast<int>(sizeof(int16_t)));
                PendingTake ready;
                ready.pcm.resize(static_cast<size_t>(samples));
                brood::Core::unpackLoop(pcm.data(), samples, ready.pcm.data());
                ready.has = true;
                ready.play = static_cast<bool>(take["play"]);
                ready.follows = static_cast<bool>(take.getProperty("follows", false));   // older sets: free
                ready.anchor = static_cast<double>(take.getProperty("anchor", 0.0));
                const juce::SpinLock::ScopedLockType lock(takeLock_);
                pendingTake_[slot] = std::move(ready);
            }
        }
        // Sets from before START meant the same in every mode stored TAPE's START counted
        // back from the newest audio: put the window back where it was.
        if (static_cast<int>(state.getProperty("stateVersion", 1)) < 2) {
            int takeLength = brood::kLoopSamples;
            if (const auto takes = state.getChildWithName("TAKES"); takes.isValid() && takes.getNumChildren() > 0)
                takeLength = juce::jmax(64, static_cast<int>(takes.getChild(0)["samples"]));
            migrateStart(state, takeLength);
        }
        // Sets from before version 3 had PRE-LOOP, and WET/LOOP ran the other way.
        if (static_cast<int>(state.getProperty("stateVersion", 1)) < 3) {
            auto param = [&state](const char* id) {
                for (auto child : state) if (child.hasType("PARAM") && child["id"].toString() == id) return child;
                return juce::ValueTree();
            };
            migrateRouting(
                [&](const char* id) { const auto c = param(id); return c.isValid() ? c["value"] : juce::var(); },
                [&](const char* id, double v) {
                    auto c = param(id);
                    if (!c.isValid()) { c = juce::ValueTree("PARAM"); c.setProperty("id", juce::String(id), nullptr); state.appendChild(c, nullptr); }
                    c.setProperty("value", v, nullptr);
                },
                [&](const char* id) { if (auto c = param(id); c.isValid()) state.removeChild(c, nullptr); },
                true);
        }
        // Sets from before version 4 had five shapes, SWEEP and BOUNCE.
        if (static_cast<int>(state.getProperty("stateVersion", 1)) < 4) {
            auto param = [&state](const char* id) {
                for (auto child : state) if (child.hasType("PARAM") && child["id"].toString() == id) return child;
                return juce::ValueTree();
            };
            migrateShapes(
                [&](const char* id) { const auto c = param(id); return c.isValid() ? c["value"] : juce::var(); },
                [&](const char* id, double v) {
                    auto c = param(id);
                    if (!c.isValid()) { c = juce::ValueTree("PARAM"); c.setProperty("id", juce::String(id), nullptr); state.appendChild(c, nullptr); }
                    c.setProperty("value", v, nullptr);
                },
                [&](const char* id) { if (auto c = param(id); c.isValid()) state.removeChild(c, nullptr); });
        }
        // Sets from before version 5 had MIX, BALANCE, GAIN, the WET switch and LEVEL outside the
        // parameters. Carried over as nearly as the new controls allow (see migrateLevels).
        if (static_cast<int>(state.getProperty("stateVersion", 1)) < 5) {
            auto param = [&state](const char* id) {
                for (auto child : state) if (child.hasType("PARAM") && child["id"].toString() == id) return child;
                return juce::ValueTree();
            };
            const bool wetWasOn = static_cast<bool>(state.getProperty("wetEnabled", true));   // no such property: leave ROUTING be
            const double gainWas = static_cast<double>(state.getProperty("inputGain", 1.0));
            migrateLevels(
                [&](const char* id) { const auto c = param(id); return c.isValid() ? c["value"] : juce::var(); },
                [&](const char* id, double v) {
                    auto c = param(id);
                    if (!c.isValid()) { c = juce::ValueTree("PARAM"); c.setProperty("id", juce::String(id), nullptr); state.appendChild(c, nullptr); }
                    c.setProperty("value", v, nullptr);
                },
                [&](const char* id) { if (auto c = param(id); c.isValid()) state.removeChild(c, nullptr); },
                true, wetWasOn, gainWas);
            state.removeProperty("wetEnabled", nullptr); state.removeProperty("inputGain", nullptr);
        }
        // The takes are ours, not parameters: keep them out of the parameter tree
        // (they were decoded above into the pending slots).
        for (auto takeTree = state.getChildWithName("TAKES"); takeTree.isValid(); takeTree = state.getChildWithName("TAKES"))
            state.removeChild(takeTree, nullptr);
        apvts.replaceState(state);
        // Footswitches are momentary and OVERDUB, CLEAR and UNDO are gestures: never restore
        // one as on (a saved CLEAR would clear the take it was saved with).
        for (auto* id : {"freezeKey", "loopSwitch", "overdub", "clear", "undo", "redo", "resample"})
            if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(0.f);
    }
}

// A saved TAPE START, from before START meant the same in every mode, rewritten in place.
void BroodProcessor::migrateStart(juce::ValueTree& state, int takeLength) {
    auto param = [&state](const char* id) {
        for (auto child : state) if (child.hasType("PARAM") && child["id"].toString() == id) return child;
        return juce::ValueTree();
    };
    auto mode = param("loopMode"), start = param("start"), length = param("length");
    // A set with no START at all gets the new default, 1, which is the old default's place.
    if (!mode.isValid() || !start.isValid() || juce::roundToInt(static_cast<float>(mode["value"])) != 1) return;
    const double len = length.isValid() ? static_cast<double>(length["value"]) : 0.72;
    start.setProperty("value", brood::migrateTapeStart(static_cast<double>(start["value"]), len, takeLength), nullptr);
}

void BroodProcessor::migrateRouting(const std::function<juce::var(const char*)>& get,
                                    const std::function<void(const char*, double)>& set,
                                    const std::function<void(const char*)>& drop, bool raw) {
    // PRE-LOOP on: the wet side was recorded and heard only the input, which is ROUTING IN now.
    // Off: the loop kept only the input, which only ROUTING LOOP does now.
    const juce::var pre = get("wetPre");
    const bool wasPre = !pre.isVoid() && static_cast<double>(pre) > 0.5;
    set("routing", wasPre ? 0.0 : (raw ? 2.0 : 2.0 / 3.0));   // LOOP: 2 raw, 2/3 as 0..1 (a choice of four now)
    drop("wetPre");
    // WET/LOOP counted the loop; it counts the wet side now. Its middle stays put.
    if (const juce::var b = get("balance"); !b.isVoid()) set("balance", 1.0 - static_cast<double>(b));
    // A modulation of WET/LOOP pushed it the other way: turn it round too (0..1 when not raw).
    for (const char* id : {"mod1_balance", "mod2_balance"})
        if (const juce::var d = get(id); !d.isVoid()) set(id, raw ? -static_cast<double>(d) : 1.0 - static_cast<double>(d));
}

void BroodProcessor::migrateLevels(const std::function<juce::var(const char*)>& get,
                                   const std::function<void(const char*, double)>& set,
                                   const std::function<void(const char*)>& drop, bool raw, bool wetWasOn, double inputGainWas) {
    // MIX blended the dry sound against Brood; WET MIX blends a source against WET. The nearest
    // reading: WET MIX takes BALANCE's wet share where it was set (that was the loop against WET),
    // otherwise MIX's value; WET switched off becomes ROUTING OFF.
    if (const juce::var b = get("balance"); !b.isVoid()) set("wetMix", static_cast<double>(b));
    else if (const juce::var m = get("mix"); !m.isVoid()) set("wetMix", static_cast<double>(m));
    drop("mix");
    for (const char* m : {"mod1", "mod2"}) {
        const juce::String id(m);
        if (const juce::var d = get((id + "_mix").toRawUTF8()); !d.isVoid()) set((id + "_wetMix").toRawUTF8(), static_cast<double>(d));
        drop((id + "_mix").toRawUTF8()); drop((id + "_balance").toRawUTF8());
    }
    drop("balance");
    // GAIN was dB; LOOP LEVEL is a fader.
    if (const juce::var g = get("loopDb"); !g.isVoid()) {
        const double db = raw ? static_cast<double>(g) : -24.0 + 36.0 * static_cast<double>(g);   // a preset stores 0..1 of -24..+12
        set("loopLevel", brood::loopLevelOfDb(db));
    }
    drop("loopDb");
    if (raw && !wetWasOn) set("routing", 3.0);
    if (raw) set("inputLevel", brood::inputLevelOfDb(juce::Decibels::gainToDecibels(inputGainWas, -48.0)));
    drop("wetSwitch");
}
void BroodProcessor::migrateShapes(const std::function<juce::var(const char*)>& get,
                                   const std::function<void(const char*, double)>& set,
                                   const std::function<void(const char*)>& drop) {
    for (const char* m : {"mod1", "mod2"}) {
        const juce::String id(m);
        auto num = [&](const juce::String& field, double fallback) {
            const juce::var v = get((id + field).toRawUTF8());
            return v.isVoid() ? fallback : static_cast<double>(v);
        };
        const double wave = num("Wave", 0), rate = num("Rate", 0.45);
        const auto to = brood::migrateShape(wave, num("Sweep", 0) > 0.5, num("Bounce", 0) > 0.5, num("Sync", 0) > 0.5, rate);
        if (to.wave != wave) set((id + "Wave").toRawUTF8(), to.wave);
        if (to.rate != rate) set((id + "Rate").toRawUTF8(), to.rate);
        drop((id + "Sweep").toRawUTF8());
        drop((id + "Bounce").toRawUTF8());
    }
}

juce::AudioProcessorEditor* BroodProcessor::createEditor() { return new BroodEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BroodProcessor(); }
