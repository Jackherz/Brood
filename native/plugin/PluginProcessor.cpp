#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"

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

struct FloatDef { const char* id; const char* name; float def; };
const FloatDef kKnobs[] = {
    {"time", "Time", 0.55f}, {"mix", "Mix", 0.5f}, {"length", "Length", 0.72f},
    {"modWet", "Modify (wet)", 0.58f}, {"clock", "Clock", 0.9f}, {"modLoop", "Modify (loop)", 0.66f},
    {"balance", "Balance (wet/loop)", 0.5f},
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
                  juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                      [this, id](float v, int) { return describe(id, v); })));
          }
          layout.add(std::make_unique<APC>(juce::ParameterID("wetMode", 1), "Wet mode", juce::StringArray{"Reverb", "Delay", "Slip", "Weave"}, 0));
          layout.add(std::make_unique<APC>(juce::ParameterID("routing", 1), "Routing", juce::StringArray{"In", "In + Loop", "Loop"}, 1));
          layout.add(std::make_unique<APC>(juce::ParameterID("loopMode", 1), "Loop mode", juce::StringArray{"Env", "Tape", "Stretch"}, 1));
          layout.add(std::make_unique<APB>(juce::ParameterID("trails", 1), "Dip: Trails", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("smooth", 1), "Dip: Smooth clock", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("sync", 1), "Dip: Tempo sync", false));
          auto loopParam = [this](const juce::String& id, const juce::String& name, float def) {
              // The capture gets its own name: reusing "id" here shadows the
              // parameter of the same name (clang's -Wshadow-uncaptured-local).
              const juce::String paramId = id;
              return std::make_unique<APF>(juce::ParameterID(id, 1), name, juce::NormalisableRange<float>(0.f, 1.f), def,
                  juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                      [this, paramId](float v, int) { return describe(paramId, v); }));
          };
          layout.add(std::make_unique<APF>(juce::ParameterID("loopDb", 1), "Loop gain", juce::NormalisableRange<float>(-24.f, 12.f), 0.f,
              juce::AudioParameterFloatAttributes().withLabel("dB")));
          layout.add(loopParam("memory", "Loop memory", 0.f));
          layout.add(loopParam("loopEnd", "Loop end", 1.f));
          layout.add(loopParam("start", "Loop start", 0.f));
          // Hidden options: on the pedal these live behind holding both footswitches.
          layout.add(loopParam("tone", "Wet tone (hi-cut)", 1.f));
          layout.add(loopParam("fade", "Overdub fade", 0.f));
          layout.add(loopParam("width", "Stereo width", 0.5f));
          layout.add(loopParam("wetDiv", "Wet clock division", 0.f));
          layout.add(loopParam("loopDiv", "Loop clock division", 0.f));
          layout.add(std::make_unique<APB>(juce::ParameterID("classic", 1), "Classic (MK I quirks)", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("wetPre", 1), "Wet before the loop", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("hiQ", 1), "High quality conversion", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("freeze", 1), "Freeze (hold the wet side)", false));
          // The ramp engine: two generators, one per channel.
          // The two modulation sources: a rate, a shape and five switches each, and a
          // depth for each control it can move (0 is no route).
          for (int n = 1; n <= 2; ++n) {
              const juce::String m = "mod" + juce::String(n), name = "Mod " + juce::String(n) + " ";
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "On", 1), name + "on", true));
              layout.add(loopParam(m + "Rate", name + "rate", 0.45f));
              layout.add(loopParam(m + "Wave", name + "shape", 0.f));
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "Uni", 1), name + "unipolar", false));
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "Bounce", 1), name + "bounce", false));
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "Sweep", 1), name + "sweep", false));
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "Sync", 1), name + "follows the tempo", false));
              layout.add(std::make_unique<APB>(juce::ParameterID(m + "Hold", 1), name + "held", false));
              for (int t = 0; t < brood::kModTargets; ++t)
                  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(m + "_" + brood::kModTargetIds[t], 1),
                      name + "to " + juce::String(brood::kModTargetNames[t]).toLowerCase(), juce::NormalisableRange<float>(-1.f, 1.f), 0.f));
          }
          layout.add(std::make_unique<APB>(juce::ParameterID("envSync", 1), "ENV slice follows the tempo", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("envSoft", 1), "ENV slices fade", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("wetSwitch", 1), "Wet footswitch (hold = freeze)", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("loopSwitch", 1), "Loop footswitch (tap/hold)", false));
          return layout;
      }()) {
    formats_.registerBasicFormats();
    // The standalone starts on the built-in phrase so it makes sound with nothing plugged in;
    // in a DAW the plugin processes the track.
    if (wrapperType == wrapperType_Standalone) source = Demo;
    pullParams();
    core_.prepare(48000.0);
    coreRight_.p = core_.p;
    coreRight_.prepare(48000.0);
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
        if (wetMode == 0) return v > 0.985f ? juce::String("decay infinite") : "decay " + fmtTime(brood::expMap(v, 0.4, 25) / c);
        if (wetMode == 1) {
            const auto* sy = apvts.getRawParameterValue("sync");
            if (sy != nullptr && sy->load() > 0.5f)
                return juce::String(brood::syncLabel(v)) + " · " + fmtTime(brood::syncSeconds(hostBpm_.load(), v));
            return "delay " + fmtTime(brood::expMap(v, 0.02, 2) / c);
        }
        if (wetMode == 3) {
            const auto* sy = apvts.getRawParameterValue("sync");
            if (sy != nullptr && sy->load() > 0.5f)
                return juce::String(brood::syncLabel(v)) + " back · " + fmtTime(brood::syncSecondsRaw(hostBpm_.load(), v));
            return "memory " + fmtTime(0.2 + v * 2.6);
        }
        return "refresh " + fmtTime(brood::expMap(v, 0.05, 1.5) / c);
    }
    if (id == "mix") return "dry " + pct(v > 0.995f ? 0.0 : std::min(1.0, 2.0 * (1 - v))) + " / fx " + pct(std::min(1.0, 2.0 * v));
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
        return "speed " + fmtSpeed(brood::slipSpeedMap(v));
    }
    if (id == "clock") {
        return "sample rate " + juce::String(brood::clockHz(v, smooth) / 1000, 1) + " kHz";
    }
    if (id == "modLoop") {
        if (loopMode == 0) return "sensitivity " + pct(v);
        if (loopMode == 1) return "tape " + fmtSpeed(brood::speedMap(v, 0.04));
        return "stretch " + fmtSpeed(brood::stretchMap(v)) + ", same pitch";
    }
    if (id == "balance") return "wet " + pct(std::min(1.0, 2.0 * (1 - v))) + " / loop " + pct(std::min(1.0, 2.0 * v));
    if (id == "memory") {
        if (v <= 0.001f) return "pedal " + fmtTime(brood::kLoopSamples / hz);
        return "up to " + fmtTime(brood::expMap(v, 1, 30)) + " of " + fmtTime(30.0);
    }
    if (id == "loopEnd") return v >= 0.999f ? "to the end" : "plays to " + pct(v);
    if (id == "start") {
        // ENV and STRETCH play from the oldest audio, so START slides towards the
        // present; TAPE plays the newest, so START reaches back. Same knob, two
        // honest readouts. A percentage, because the room it moves through depends
        // on LENGTH and END and the take's length is not known here.
        if (v <= 0.005f) return loopMode == 1 ? "the newest" : "the oldest";
        return (loopMode == 1 ? "reaches back " : "slides on ") + pct(v);
    }
    if (id == "freeze") return v > 0.5f ? "held, ringing" : "moving";
    if (id == "tone") return v > 0.995f ? "open" : "hi-cut " + pct(1 - v);
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

juce::String BroodProcessor::describe(const juce::String& id) const {
    const auto* p = apvts.getRawParameterValue(id);
    return p ? describe(id, p->load()) : juce::String();
}

void BroodProcessor::makeDemo(double sr) {
    // Karplus-Strong fingerpicking, 8 s seamless loop (same phrase as the web version).
    const int len = static_cast<int>(8 * sr);
    demo_.setSize(1, len);
    demo_.clear();
    float* out = demo_.getWritePointer(0);
    juce::Random rnd(1234);
    auto pluck = [&](double t0, int note, float amp, double decay) {
        const double f = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const int N = std::max(2, juce::roundToInt(sr / f));
        std::vector<float> buf(static_cast<size_t>(N));
        float lp = 0;
        for (auto& b : buf) { lp += 0.55f * (rnd.nextFloat() * 2 - 1 - lp); b = lp; }
        const float rho = static_cast<float>(std::pow(0.001, 1.0 / (f * decay)));
        const int start = static_cast<int>(t0 * sr), n = static_cast<int>(decay * sr);
        int idx = 0;
        for (int k = 0; k < n; ++k) {
            const int nx = idx + 1 == N ? 0 : idx + 1;
            const float a = buf[static_cast<size_t>(idx)], b = buf[static_cast<size_t>(nx)];
            out[(start + k) % len] += a * amp;
            buf[static_cast<size_t>(idx)] = rho * 0.5f * (a + b);
            idx = nx;
        }
    };
    const std::vector<std::vector<int>> chords = {   // Em9, Cmaj7, G6, D/F#
        {40, 47, 55, 62, 66, 59}, {48, 55, 59, 64, 67, 64}, {43, 50, 59, 64, 67, 62}, {42, 50, 57, 66}};
    for (size_t c = 0; c < chords.size(); ++c)
        for (size_t i = 0; i < chords[c].size(); ++i)
            pluck(c * 2.0 + i / 3.0, chords[c][i], i == 0 ? 0.32f : 0.22f + 0.04f * rnd.nextFloat(), i == 0 ? 3.5 : 2.2);
    const float pk = demo_.getMagnitude(0, 0, len);
    if (pk > 0) demo_.applyGain(0.5f / pk);
    demoPos_ = 0;
}

void BroodProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    // DAWs may re-prepare for a sample-rate or block-size change. Preserve the
    // wet switch while resetting the DSP buffers, unless state is pending.
    if (restoreWet_.load() < 0 && ui.wetOn.load()) restoreWet_ = 1;
    pullParams();
    core_.prepare(sampleRate);
    coreRight_.p = core_.p;
    coreRight_.prepare(sampleRate);
    lastWetSw_ = lastLoopSw_ = false;
    const auto capacity = static_cast<size_t>(std::max(samplesPerBlock, 32));
    inputLeft_.assign(capacity, 0.f); inputRight_.assign(capacity, 0.f);
    scratchLeft_.assign(capacity, 0.f); scratchRight_.assign(capacity, 0.f);
    if (demo_.getNumSamples() != static_cast<int>(8 * sampleRate)) makeDemo(sampleRate);
}

void BroodProcessor::pullParams() {
    auto& p = core_.p;
    p.time = param("time"); p.mix = param("mix"); p.length = param("length");
    p.modWet = param("modWet"); p.clock = param("clock"); p.modLoop = param("modLoop");
    p.balance = param("balance");
    p.wetMode = juce::roundToInt(param("wetMode"));
    p.routing = juce::roundToInt(param("routing"));
    p.loopMode = juce::roundToInt(param("loopMode"));
    p.trails = param("trails") > 0.5f; p.smooth = param("smooth") > 0.5f;
    p.sync = param("sync") > 0.5f;
    p.memory = param("memory"); p.loopEnd = param("loopEnd");
    p.start = param("start");
    p.tone = param("tone"); p.fade = param("fade"); p.width = param("width");
    p.wetDiv = param("wetDiv"); p.loopDiv = param("loopDiv");
    p.classic = param("classic") > 0.5f;
    p.wetPre = param("wetPre") > 0.5f;
    p.loopDb = param("loopDb");
    p.hiQ = param("hiQ") > 0.5f;
    p.envSoft = param("envSoft") > 0.5f;
    p.freezeLatch = param("freeze") > 0.5f ? 1.0 : 0.0;
    for (int k = 0; k < 2; ++k) {
        const juce::String n = "mod" + juce::String(k + 1);
        auto& m = p.mods[k];
        const auto value = [this, &n](const char* f) { return apvts.getRawParameterValue(n + f)->load(); };
        m.on = value("On") > 0.5f; m.rate = value("Rate"); m.wave = value("Wave");
        m.uni = value("Uni") > 0.5f; m.bounce = value("Bounce") > 0.5f; m.sweep = value("Sweep") > 0.5f;
        m.sync = value("Sync") > 0.5f; m.hold = value("Hold") > 0.5f;
        for (int t = 0; t < brood::kModTargets; ++t) m.depth[t] = apvts.getRawParameterValue(n + "_" + brood::kModTargetIds[t])->load();
    }
    p.envSync = param("envSync") > 0.5f;
    p.bpm = hostBpm_.load();
    coreRight_.p = p;
}

bool BroodProcessor::loadFile(const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> r(formats_.createReaderFor(f));
    if (!r || r->lengthInSamples <= 0 || r->lengthInSamples > 60 * 60 * 192000) return false;
    juce::AudioBuffer<float> buf(static_cast<int>(r->numChannels), static_cast<int>(r->lengthInSamples));
    r->read(&buf, 0, buf.getNumSamples(), 0, true, true);
    const juce::SpinLock::ScopedLockType lock(fileLock_);   // the audio thread only tries this lock
    file_ = std::move(buf); fileRate_ = r->sampleRate; filePos_ = 0; fileName_ = f.getFileName();
    return true;
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
        (i == 0 ? core_ : coreRight_).restoreLoop(ready.pcm.data(), static_cast<int>(ready.pcm.size()), ready.play);
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
    if (auto* head = getPlayHead()) {   // not "playHead": that shadows AudioProcessor's own
        if (const auto position = head->getPosition()) {
            if (const auto sig = position->getTimeSignature(); sig.hasValue() && sig->denominator > 0)
                hostBarQuarters_.store(static_cast<float>(sig->numerator) * 4.f / static_cast<float>(sig->denominator));
            if (const auto ppq = position->getPpqPosition(); ppq.hasValue() && position->getPpqPositionOfLastBarStart().hasValue()) {
                hostPlaying = position->getIsPlaying();
                hostPpq = *ppq;
                hostBarStart = *position->getPpqPositionOfLastBarStart();
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
    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        if (!message.isController()) continue;
        const int cc = message.getControllerNumber(), value = message.getControllerValue();
        const char* id = nullptr;
        if (cc >= 14 && cc <= 19) {
            static const char* const knobs[] = {"time", "mix", "length", "modWet", "clock", "modLoop"};
            id = knobs[cc - 14];
            if (auto* parameter = apvts.getParameter(id)) parameter->setValueNotifyingHost(value / 127.f);
        } else if (cc >= 21 && cc <= 23) {
            static const char* const modes[] = {"wetMode", "routing", "loopMode"};
            id = modes[cc - 21];
            const float choice = value <= 1 ? 0.f : value == 2 ? 0.5f : 1.f;
            if (auto* parameter = apvts.getParameter(id)) parameter->setValueNotifyingHost(choice);
        } else {
            if (cc == 75) id = "trails"; else if (cc == 78) id = "smooth";   // SPREAD / DRY KILL / LATCH retired
            else if (cc == 102) midiLoop = value != 0;
            else if (cc == 103) midiWet = value != 0;
            else if (cc == 20) { for (const char* m : {"mod1Rate", "mod2Rate"}) if (auto* parameter = apvts.getParameter(m)) parameter->setValueNotifyingHost(value / 127.f); }
            else if (cc == 66) { for (const char* m : {"mod1Bounce", "mod2Bounce"}) if (auto* parameter = apvts.getParameter(m)) parameter->setValueNotifyingHost(value != 0 ? 1.f : 0.f); }
            else if (cc == 67) { for (const char* m : {"mod1Sweep", "mod2Sweep"}) if (auto* parameter = apvts.getParameter(m)) parameter->setValueNotifyingHost(value != 0 ? 1.f : 0.f); }
            else if (cc == 53) { if (auto* parameter = apvts.getParameter("wetDiv")) parameter->setValueNotifyingHost(value / 127.f); }
            else if (cc == 54) { if (auto* parameter = apvts.getParameter("loopDiv")) parameter->setValueNotifyingHost(value / 127.f); }
            else if (cc == 52) { for (const char* m : {"mod1Hold", "mod2Hold"}) if (auto* parameter = apvts.getParameter(m)) parameter->setValueNotifyingHost(value != 0 ? 1.f : 0.f); }
            else if (cc == 71) id = "classic";
            else if (cc == 105) id = "freeze";
            if (id != nullptr) if (auto* parameter = apvts.getParameter(id)) parameter->setValueNotifyingHost(value != 0 ? 1.f : 0.f);
        }
    }
    applyPendingTakes();
    pullParams();
    auto tap = [&](int which) {
        core_.footswitch(which, true); coreRight_.footswitch(which, true);
        core_.footswitch(which, false); coreRight_.footswitch(which, false);
    };
    const int restore = restoreWet_.exchange(-1);
    if (restore >= 0 && core_.state().wetOn != (restore != 0)) tap(0);
    if (midiWet >= 0 && core_.state().wetOn != (midiWet != 0)) tap(0);
    if (midiLoop >= 0 && (core_.state().loop != brood::LoopLed::Off) != (midiLoop != 0)) tap(1);
    const bool wetSw = param("wetSwitch") > 0.5f, loopSw = param("loopSwitch") > 0.5f;
    if (wetSw != lastWetSw_) { lastWetSw_ = wetSw; core_.footswitch(0, wetSw); coreRight_.footswitch(0, wetSw); }
    if (loopSw != lastLoopSw_) { lastLoopSw_ = loopSw; core_.footswitch(1, loopSw); coreRight_.footswitch(1, loopSw); }

    // A well-behaved host does not exceed its promised block size. Grow only if it does.
    const auto needed = static_cast<size_t>(n);
    if (inputLeft_.size() < needed) { inputLeft_.resize(needed); inputRight_.resize(needed); scratchLeft_.resize(needed); scratchRight_.resize(needed); }
    float* xL = inputLeft_.data(); float* xR = inputRight_.data();
    const float gain = inputGain.load();
    const int src = source.load();
    if (src == Demo && demo_.getNumSamples() > 0) {
        const float* d = demo_.getReadPointer(0);
        const int len = demo_.getNumSamples();
        int pos = static_cast<int>(demoPos_);
        for (int i = 0; i < n; ++i) { xL[i] = xR[i] = d[pos] * gain; if (++pos >= len) pos = 0; }
        demoPos_ = pos;
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
                xL[i] = (left[i0] + (left[i1] - left[i0]) * fr) * gain;
                xR[i] = (right[i0] + (right[i1] - right[i0]) * fr) * gain;
                filePos_ += ratio; while (filePos_ >= len) filePos_ -= len;
            }
        }
    } else {
        const float* left = nIn > 0 ? buffer.getReadPointer(0) : nullptr;
        const float* right = nIn > 1 ? buffer.getReadPointer(1) : left;
        for (int i = 0; i < n; ++i) { xL[i] = left ? left[i] * gain : 0.f; xR[i] = right ? right[i] * gain : 0.f; }
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
    ui.wetOn = st.wetOn; ui.freeze = st.freeze; ui.loopLed = static_cast<int>(st.loop);
    ui.lsrc = st.lsrc; ui.winStart = st.winStart; ui.winLen = st.winLen; ui.winSpan = core_.loopSpan(); ui.pos = static_cast<float>(st.pos);
    ui.clockHz = static_cast<float>(st.clockHz); ui.histCycles = st.histCycles;
    ui.peakIn = std::max(st.peakIn, rightState.peakIn); ui.peakOut = std::max(st.peakOut, rightState.peakOut);

    const bool leftDirty = core_.takeLoopWaveDirty(), rightDirty = coreRight_.takeLoopWaveDirty();
    bool wantWave = leftDirty || rightDirty || pendingWave_;
    if (st.loop == brood::LoopLed::Dub && ++dubBlocks_ % 40 == 0) wantWave = true;
    if (wantWave) {
        const juce::SpinLock::ScopedTryLockType lock(waveLock_);
        if (lock.isLocked()) {
            core_.loopPeaks(wave_.data(), kWaveBins); coreRight_.loopPeaks(waveRight_.data(), kWaveBins);
            for (size_t i = 0; i < wave_.size(); ++i) wave_[i] = std::max(wave_[i], waveRight_[i]);
            ui.waveVersion.fetch_add(1); pendingWave_ = false;
        } else pendingWave_ = true;
    }
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
    take.setProperty("pcm", juce::MemoryBlock(raw.getData(), raw.getDataSize()).toBase64Encoding(), nullptr);
    parent.appendChild(take, nullptr);
}

bool BroodProcessor::takeMoment(const juce::File& file) {
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 64 || reader->sampleRate <= 0) return false;
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
    out.applyGain(juce::Decibels::decibelsToGain(apvts.getRawParameterValue("loopDb")->load()));   // sent as you hear it
    sampleRate = static_cast<double>(ui.clockHz.load());
    return true;
}

void BroodProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    state.setProperty("source", source.load(), nullptr);
    state.setProperty("inputGain", inputGain.load(), nullptr);
    state.setProperty("presetName", presetName, nullptr);
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
    // Hosts can re-save a restored state before processing the first block.
    const int pendingWet = restoreWet_.load();
    state.setProperty("wetEnabled", pendingWet >= 0 ? pendingWet == 1 : ui.wetOn.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void BroodProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        // Sets saved before the rename carry the old tag; their parameters are
        // the same, so they load under the new one.
        if (!xml->hasTagName(apvts.state.getType()) && !xml->hasTagName(kLegacyStateTag)) return;
        xml->setTagName(apvts.state.getType().toString());
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.hasProperty("source")) { const int saved = static_cast<int>(state["source"]); source = saved == File ? Live : juce::jlimit(0, 1, saved); }
        if (state.hasProperty("inputGain")) inputGain = static_cast<float>(state["inputGain"]);
        if (state.hasProperty("presetName")) presetName = state["presetName"].toString();
        if (state.hasProperty("wetEnabled")) restoreWet_ = static_cast<bool>(state["wetEnabled"]) ? 1 : 0;
        // Unpack any take the state carries; the audio thread swaps it in at its
        // next block, so a render that starts from a saved state hears the loop.
        for (int c = 0; c < state.getNumChildren(); ++c) {
            const auto takes = state.getChild(c);
            if (!takes.hasType("TAKES")) continue;
            for (int i = 0; i < takes.getNumChildren(); ++i) {
                const auto take = takes.getChild(i);
                const int slot = take["id"].toString() == "right" ? 1 : 0;
                const int samples = static_cast<int>(take["samples"]);
                const auto encoded = take["pcm"].toString();
                if (samples < 64 || encoded.isEmpty()) continue;
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
                const juce::SpinLock::ScopedLockType lock(takeLock_);
                pendingTake_[slot] = std::move(ready);
            }
        }
        // The takes are ours, not parameters: keep them out of the parameter tree
        // (they were decoded above into the pending slots).
        for (auto takeTree = state.getChildWithName("TAKES"); takeTree.isValid(); takeTree = state.getChildWithName("TAKES"))
            state.removeChild(takeTree, nullptr);
        apvts.replaceState(state);
        // footswitches are momentary: never restore one as held down
        for (auto* id : {"wetSwitch", "loopSwitch"})
            if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(0.f);
    }
}

juce::AudioProcessorEditor* BroodProcessor::createEditor() { return new BroodEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BroodProcessor(); }
