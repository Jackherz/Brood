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
juce::String pct(double x) { return juce::String(juce::roundToInt(x * 100)) + "%"; }

struct FloatDef { const char* id; const char* name; float def; };
const FloatDef kKnobs[] = {
    {"time", "Time", 0.55f}, {"mix", "Mix", 0.5f}, {"length", "Length", 0.72f},
    {"modWet", "Modify (wet)", 0.58f}, {"clock", "Clock", 0.9f}, {"modLoop", "Modify (loop)", 0.66f},
    {"balance", "Balance (wet/loop)", 0.5f},
};
}  // namespace

MoodProcessor::MoodProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MOOD", [this] {
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
          layout.add(std::make_unique<APB>(juce::ParameterID("dryKill", 1), "Dip: Dry kill", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("latch", 1), "Dip: Latch", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("spread", 1), "Dip: Spread", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("smooth", 1), "Dip: Smooth clock", false));
          layout.add(std::make_unique<APB>(juce::ParameterID("sync", 1), "Dip: Tempo sync", false));
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

juce::String MoodProcessor::describe(const juce::String& id, float v) const {
    const auto* wm = apvts.getRawParameterValue("wetMode");
    const auto* lm = apvts.getRawParameterValue("loopMode");
    const auto* ck = apvts.getRawParameterValue("clock");
    if (wm == nullptr || lm == nullptr || ck == nullptr) return juce::String(v, 3);   // still constructing
    const int wetMode = juce::roundToInt(wm->load()), loopMode = juce::roundToInt(lm->load());
    const auto* sm = apvts.getRawParameterValue("smooth");
    const bool smooth = sm != nullptr && sm->load() > 0.5f;
    const double hz = mood::clockHz(ck->load(), smooth);
    const double c = hz / mood::kCoreFs;
    if (id == "time") {
        if (wetMode == 0) return v > 0.985f ? juce::String("decay infinite") : "decay " + fmtTime(mood::expMap(v, 0.4, 25) / c);
        if (wetMode == 1) {
            const auto* sy = apvts.getRawParameterValue("sync");
            if (sy != nullptr && sy->load() > 0.5f)
                return juce::String(mood::syncLabel(v)) + " · " + fmtTime(mood::syncSeconds(hostBpm_.load(), v));
            return "delay " + fmtTime(mood::expMap(v, 0.02, 2) / c);
        }
        if (wetMode == 3) return "memory " + fmtTime(0.2 + v * 2.6);
        return "refresh " + fmtTime(mood::expMap(v, 0.05, 1.5) / c);
    }
    if (id == "mix") {
        const auto* dkp = apvts.getRawParameterValue("dryKill");
        const bool dk = dkp != nullptr && dkp->load() > 0.5f;
        return "dry " + pct(dk ? 0 : std::min(1.0, 2.0 * (1 - v))) + " / fx " + pct(std::min(1.0, 2.0 * v));
    }
    if (id == "length") {
        const double full = mood::kLoopSamples / hz;
        if (loopMode == 1) return "loop " + fmtTime(full * mood::expMap(v, 0.02, 1)) + " of " + fmtTime(full);
        return "slice " + fmtTime((loopMode == 0 ? mood::expMap(v, 0.01, 1) : mood::expMap(v, 0.01, 0.5)) / c);
    }
    if (id == "modWet") {
        if (wetMode == 0) return "smear " + pct(v);
        if (wetMode == 1) return "feedback " + pct(v);
        if (wetMode == 3) return v < 0.34 ? "long cloth " + pct(v) : "short stitches " + pct(v);
        return "speed " + fmtSpeed(mood::slipSpeedMap(v));
    }
    if (id == "clock") {
        return "sample rate " + juce::String(mood::clockHz(v, smooth) / 1000, 1) + " kHz";
    }
    if (id == "modLoop") {
        if (loopMode == 0) return "sensitivity " + pct(v);
        if (loopMode == 1) return "tape " + fmtSpeed(mood::speedMap(v, 0.04));
        return "stretch " + fmtSpeed(mood::stretchMap(v)) + ", same pitch";
    }
    if (id == "balance") return "wet " + pct(std::min(1.0, 2.0 * (1 - v))) + " / loop " + pct(std::min(1.0, 2.0 * v));
    return juce::String(v, 3);
}

juce::String MoodProcessor::describe(const juce::String& id) const {
    const auto* p = apvts.getRawParameterValue(id);
    return p ? describe(id, p->load()) : juce::String();
}

void MoodProcessor::makeDemo(double sr) {
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

void MoodProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
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

void MoodProcessor::pullParams() {
    auto& p = core_.p;
    p.time = param("time"); p.mix = param("mix"); p.length = param("length");
    p.modWet = param("modWet"); p.clock = param("clock"); p.modLoop = param("modLoop");
    p.balance = param("balance");
    p.wetMode = juce::roundToInt(param("wetMode"));
    p.routing = juce::roundToInt(param("routing"));
    p.loopMode = juce::roundToInt(param("loopMode"));
    p.trails = param("trails") > 0.5f; p.dryKill = param("dryKill") > 0.5f; p.latch = param("latch") > 0.5f;
    p.spread = param("spread") > 0.5f; p.smooth = param("smooth") > 0.5f;
    p.sync = param("sync") > 0.5f;
    p.bpm = hostBpm_.load();
    coreRight_.p = p;
}

bool MoodProcessor::loadFile(const juce::File& f) {
    std::unique_ptr<juce::AudioFormatReader> r(formats_.createReaderFor(f));
    if (!r || r->lengthInSamples <= 0 || r->lengthInSamples > 60 * 60 * 192000) return false;
    juce::AudioBuffer<float> buf(static_cast<int>(r->numChannels), static_cast<int>(r->lengthInSamples));
    r->read(&buf, 0, buf.getNumSamples(), 0, true, true);
    const juce::SpinLock::ScopedLockType lock(fileLock_);   // the audio thread only tries this lock
    file_ = std::move(buf); fileRate_ = r->sampleRate; filePos_ = 0; fileName_ = f.getFileName();
    return true;
}

bool MoodProcessor::copyWave(std::array<float, kWaveBins>& out) const {
    const juce::SpinLock::ScopedTryLockType lock(waveLock_);
    if (!lock.isLocked()) return false;
    out = wave_;
    return true;
}

void MoodProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    if (buffer.getNumChannels() < 1 || n == 0 || nOut < 1) { buffer.clear(); return; }

    // DAW tempo for the sync dip. Static tempo, tempo ramps and offline renders
    // all reduce to "the bpm at this block"; if the host offers none, the last
    // known value stands (120 BPM until told otherwise).
    if (auto* playHead = getPlayHead()) {
        if (const auto position = playHead->getPosition()) {
            if (const auto bpm = position->getBpm()) {
                if (std::isfinite(*bpm) && *bpm > 5.0 && *bpm < 400.0) {
                    hostBpm_.store(static_cast<float>(*bpm));
                    ui.bpm.store(static_cast<float>(*bpm));
                }
            }
        }
    }

    // A useful subset of the MOOD MKII CC map. Changes are applied at block
    // boundaries; hosts can also automate every front-panel parameter directly.
    int midiWet = -1, midiLoop = -1, midiFreeze = -1;
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
            if (cc == 73) id = "spread"; else if (cc == 74) id = "dryKill";
            else if (cc == 75) id = "trails"; else if (cc == 76) id = "latch"; else if (cc == 78) id = "smooth";
            else if (cc == 102) midiLoop = value != 0;
            else if (cc == 103) midiWet = value != 0;
            else if (cc == 105) midiFreeze = value != 0;
            if (id != nullptr) if (auto* parameter = apvts.getParameter(id)) parameter->setValueNotifyingHost(value != 0 ? 1.f : 0.f);
        }
    }
    pullParams();
    auto tap = [&](int which) {
        core_.footswitch(which, true); coreRight_.footswitch(which, true);
        core_.footswitch(which, false); coreRight_.footswitch(which, false);
    };
    const int restore = restoreWet_.exchange(-1);
    if (restore >= 0 && core_.state().wetOn != (restore != 0)) tap(0);
    if (midiWet >= 0 && core_.state().wetOn != (midiWet != 0)) tap(0);
    if (midiLoop >= 0 && (core_.state().loop != mood::LoopLed::Off) != (midiLoop != 0)) tap(1);
    if (midiFreeze >= 0) { core_.setFreeze(midiFreeze != 0); coreRight_.setFreeze(midiFreeze != 0); }
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

    float* outL = buffer.getWritePointer(0);
    float* outR = nOut > 1 ? buffer.getWritePointer(1) : scratchRight_.data();
    core_.process(xL, outL, scratchLeft_.data(), n);
    if (core_.p.spread) coreRight_.process(xR, scratchLeft_.data(), outR, n);
    else coreRight_.process(xR, outR, scratchLeft_.data(), n);
    for (int c = nOut; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);

    const auto st = core_.state(), rightState = coreRight_.state();
    ui.wetOn = st.wetOn; ui.freeze = st.freeze; ui.loopLed = static_cast<int>(st.loop);
    ui.lsrc = st.lsrc; ui.winStart = st.winStart; ui.winLen = st.winLen; ui.pos = static_cast<float>(st.pos);
    ui.clockHz = static_cast<float>(st.clockHz); ui.histCycles = st.histCycles;
    ui.peakIn = std::max(st.peakIn, rightState.peakIn); ui.peakOut = std::max(st.peakOut, rightState.peakOut);

    const bool leftDirty = core_.takeLoopWaveDirty(), rightDirty = coreRight_.takeLoopWaveDirty();
    bool wantWave = leftDirty || rightDirty || pendingWave_;
    if (st.loop == mood::LoopLed::Dub && ++dubBlocks_ % 40 == 0) wantWave = true;
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

void MoodProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    state.setProperty("source", source.load(), nullptr);
    state.setProperty("inputGain", inputGain.load(), nullptr);
    // Hosts can re-save a restored state before processing the first block.
    const int pendingWet = restoreWet_.load();
    state.setProperty("wetEnabled", pendingWet >= 0 ? pendingWet == 1 : ui.wetOn.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void MoodProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) {
        if (!xml->hasTagName(apvts.state.getType())) return;
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.hasProperty("source")) { const int saved = static_cast<int>(state["source"]); source = saved == File ? Live : juce::jlimit(0, 1, saved); }
        if (state.hasProperty("inputGain")) inputGain = static_cast<float>(state["inputGain"]);
        if (state.hasProperty("wetEnabled")) restoreWet_ = static_cast<bool>(state["wetEnabled"]) ? 1 : 0;
        apvts.replaceState(state);
        // footswitches are momentary: never restore one as held down
        for (auto* id : {"wetSwitch", "loopSwitch"})
            if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(0.f);
    }
}

juce::AudioProcessorEditor* MoodProcessor::createEditor() { return new MoodEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MoodProcessor(); }
