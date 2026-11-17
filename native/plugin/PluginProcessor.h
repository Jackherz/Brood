#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <optional>
#include "dsp/brood.h"
#include "kit/Bench.h"
#include "kit/TapeInput.h"
#include "BroodHistory.h"

// The pedal as an AudioProcessor. Every control (knobs, toggles, dips, and both
// footswitches as momentary switches) is a host parameter, so a DAW can automate
// it or map it to a MIDI controller.
class BroodProcessor final : public juce::AudioProcessor, private juce::AsyncUpdater {
public:
    BroodProcessor();
    ~BroodProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        const auto in = l.getMainInputChannelSet();
        const auto out = l.getMainOutputChannelSet();
        return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
            && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Brood"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;
    // 2: START counts from the oldest audio in every loop mode. Older sets and presets
    // stored TAPE's START from the newest, and are rewritten when they load.
    // 3: ROUTING alone decides what the wet side hears and what the loop keeps (PRE-LOOP
    // gone), and WET/LOOP counts the wet side (0 the loop alone, 1 the wet side alone).
    // 4: six shapes (SAW added after SIN) and no SWEEP or BOUNCE switches.
    // 5: WET MIX, LOOP LEVEL, INPUT LEVEL and INPUT MONITOR; ROUTING OFF in place of the WET
    // switch; MIX, BALANCE and GAIN gone.
    static constexpr int kStateVersion = 5;
    // A set or preset from before version 3, in place: PRE-LOOP on becomes ROUTING IN, off
    // becomes ROUTING LOOP; WET/LOOP and any modulation of it turn round. `raw` says whether
    // the values are the parameters' own (a set) or 0..1 (a preset).
    static void migrateRouting(const std::function<juce::var(const char*)>& get,
                               const std::function<void(const char*, double)>& set,
                               const std::function<void(const char*)>& drop, bool raw);
    // A set or preset from before version 4, in place: SWEEP becomes SAW, SWEEP with BOUNCE
    // becomes TRI at half the rate, BOUNCE goes, and the shape moves to its place in the row
    // of six. Raw and 0..1 values are the same for these parameters.
    // A set or preset from before version 5, in place (see the .cpp). `wetWasOn` and
    // `inputGainWas` are the old state's own properties (sets only).
    static void migrateLevels(const std::function<juce::var(const char*)>& get,
                              const std::function<void(const char*, double)>& set,
                              const std::function<void(const char*)>& drop, bool raw, bool wetWasOn, double inputGainWas);
    static void migrateShapes(const std::function<juce::var(const char*)>& get,
                              const std::function<void(const char*, double)>& set,
                              const std::function<void(const char*)>& drop);
    // RECORD's bar counts, in the order the stepper steps through them. 0 is FREE.
    static constexpr int kRecBarCounts[9] = {0, 1, 2, 3, 4, 6, 8, 12, 16};
    static int recBarsFor(float v) { return kRecBarCounts[brood::rampIndexOfValue(v, 9)]; }
    static void migrateStart(juce::ValueTree& state, int takeLength);

    juce::AudioProcessorValueTreeState apvts;

    // Readout text for a parameter in real-world units (depends on mode and clock).
    juce::String describe(const juce::String& id) const;
    juce::String describe(const juce::String& id, float value) const;
    // What a host (and a controller such as Push) shows for a value: at most 8 characters.
    juce::String hostValue(const juce::String& id, float value) const;
    float hostValueFromText(const juce::String& id, const juce::String& text) const;

    // ---- input source ----
    enum Source { Live = 0, Demo = 1, File = 2 };
    std::atomic<int> source { Live };
    // The demo loops (assets/demos, embedded when the build found them), in this order, with the
    // tempo each was played at. loadDemo decodes one on the calling thread (never the audio
    // thread) and, with no host to say otherwise, sets the tempo to it. False without the loops.
    struct DemoLoop { const char* file; const char* name; float bpm; };
    static constexpr DemoLoop kDemos[] = {
        {"ambiotica.flac", "Ambiotica", 128.f}, {"feedback-ambience.flac", "Feedback ambience", 128.f},
        {"tela-tape-loop.flac", "Tela tape loop", 97.f}, {"habit-and-keys-1.flac", "Habit and keys 1", 64.f},
        {"habit-and-keys-2.flac", "Habit and keys 2", 64.f}, {"chroma-rhodes.flac", "Chroma Rhodes", 60.f},
        {"nylon-tines.flac", "Nylon tines", 70.f},
    };
    bool loadDemo(int index);
    bool loadFile(const juce::File& f);           // message thread
    bool canReadAudio(const juce::File& f) const { return formats_.findFormatForFileExtension(f.getFileExtension()) != nullptr; }
    // The sound the patch box shows; kept with the project.
    juce::String presetName { "A Little Daydream" };
    // What the loop is playing, for the Bench. Message thread; false with nothing caught.
    bool copyMoment(juce::AudioBuffer<float>& out, double& sampleRate) const;
    // A moment from the Bench becomes the loop, at the clock it's running at. Message thread.
    bool takeMoment(const juce::File& file);
    // Or the exact output, for a number of bars from the next bar line (at once if the
    // host is stopped). The message thread arms it and collects it; the audio thread
    // only writes into what was allocated first.
    using OutputCapture = kit::bench::OutputRecorder::State;
    void armOutputCapture(int bars) { outputRecorder_.arm(bars, hostBarQuarters_.load(), hostBpm_.load(), getSampleRate()); }
    void cancelOutputCapture() { outputRecorder_.cancel(); }
    OutputCapture outputCaptureState() const { return outputRecorder_.state(); }
    float outputCaptureProgress() const { return outputRecorder_.progress(); }
    bool takeOutputCapture(juce::AudioBuffer<float>& out, double& sampleRate) { return outputRecorder_.take(out, sampleRate); }
    juce::String fileName() const { return fileName_; }

    // ---- UI snapshot (written by the audio thread, read by the editor) ----
    struct Ui {
        std::atomic<bool> wetFed { false }, freeze { false }, envGate { false };
        // INPUT MONITOR as it stands: the input heard now, and the routing WET has now (IN + LOOP
        // hears only the input while overdubbing).
        std::atomic<bool> inputHeard { true };
        // RESAMPLE: under way, possible now, stopped at its limit last time; how long it has run
        // and how long it needs at the speed now (seconds; 1e9 not moving).
        std::atomic<bool> resampling { false }, canResample { false }, resampleLimitHit { false };
        std::atomic<float> resampleSecs { 0 }, resampleNeed { 0 };
        // How far UNDO and REDO go.
        std::atomic<int> undoDepth { 0 }, redoDepth { 0 };
        std::atomic<int> routingNow { 1 };
        std::array<std::atomic<float>, brood::kModTargets> effective {};   // each control as the sound has it now
        std::array<std::atomic<float>, 2> modValue {};                     // each source now, 0..1
        std::atomic<int> loopLed { 0 }, lsrc { 0 }, winStart { 0 }, winLen { 0 }, winSpan { 0 };
        std::atomic<bool> recArmed { false };   // RECORD: waiting for the bar line
        std::atomic<float> pos { 0 }, clockHz { 48000 }, peakIn { 0 }, peakOut { 0 }, load { 0 };
        // Where the loop is heard from (in the window) and how loud each read is: the playhead in
        // TAPE, the two grains in STRETCH, the slice in ENV while it's the louder sound.
        std::array<std::atomic<float>, 2> head {}, headLevel {};
        std::atomic<float> bpm { 120.f };
        std::atomic<uint32_t> histCycles { 0 };
        std::atomic<uint32_t> waveVersion { 0 };
        // The take strip's live view: where the history is being written (in ticks of the ring),
        // how much a tap keeps, and how much a recording or a hold has kept so far (-1: none).
        std::atomic<int> histPos { 0 }, memTicks { 64000 }, takeSoFar { -1 };
        // OVERDUB, CLEAR and UNDO: whether OVERDUB can be switched on (a loop is playing), whether
        // it's latched, and what UNDO would step back over (0 nothing, 1 a take, 2 a clear,
        // 3 a layer), and whether that step is already undone (so UNDO redoes).
        std::atomic<bool> canOverdub { false }, dubLatched { false };
        // The take follows the host's transport, and is parked (the host stopped it).
        std::atomic<bool> follows { false }, parked { false };
    } ui;
    // Peak of each brood::kLiveColTicks of the history ring, both sides; any thread may read.
    float livePeak(int column) const {
        return live_[static_cast<size_t>(juce::jlimit(0, brood::kLiveCols - 1, column))].load(std::memory_order_relaxed);
    }
    static constexpr int kWaveBins = 400;
    // copy of the loop's peak envelope; returns false if the audio thread holds the lock
    bool copyWave(std::array<float, kWaveBins>& out) const;
    // UNDO and REDO over the last four actions (see BroodHistory). Message thread.
    std::unique_ptr<BroodHistory> history;
    // Runs any pending reset of CLEAR, UNDO or OVERDUB now, on the calling (message) thread.
    void settleControls() { handleUpdateNowIfNeeded(); }

private:
    void pullParams();
    float param(const char* id) const { return apvts.getRawParameterValue(id)->load(); }

    brood::Core core_, coreRight_;
    bool lastWetSw_ = false, lastLoopSw_ = false;
    std::atomic<float> hostBpm_ { 120.f };   // last tempo the host reported
    std::atomic<float> hostBarQuarters_ { 4.f };   // a bar's length in quarter notes
    // The host's transport at the start of this block (audio thread only), for RECORD's bar line.
    bool hostPlaying_ = false; double hostPpq_ = 0, hostBarStart_ = 0;
    kit::bench::OutputRecorder outputRecorder_;
    int lastRouting_ = 1;   // CC 103: the routing to go back to after OFF
    std::atomic<bool> flipFreeze_ { false };
    // CLEAR and UNDO act when their parameter goes on; the message thread then sets it back to
    // off, so a controller's button lights for a moment. OVERDUB is set back to off when it
    // couldn't latch (no loop playing) or the loop stopped under it.
    bool lastClear_ = false, lastUndo_ = false;
    std::atomic<bool> resetClear_ { false }, resetUndo_ { false }, resetOverdub_ { false };
    // A new take fits the window; the message thread moves START, END (and LENGTH in TAPE)
    // to match, and until then pullParams keeps the cores on the fitted values.
    // Controls Brood moved by itself (a new take fits the window; RESAMPLE resets the player):
    // until the host's parameter shows the value (or half a second), the cores keep it.
    struct Override { bool active = false; brood::AutoParam which = brood::AutoParam::Start; double value = 0; int samplesLeft = 0; };
    std::array<Override, 12> overrides_ {};
    void holdAuto(const brood::AutoChange& change);
    // The UNDO history's traffic between the threads, each one way: the cores' versions to the
    // message thread, its swaps and releases back, and MIDI moves to be recorded.
    juce::AbstractFifo versionFifo_ { 64 };
    std::array<brood::VersionEvent, 64> versionQueue_ {};
    struct Command { int type = 0, id = 0; };   // 0 swap, 1 release
    juce::AbstractFifo commandFifo_ { 64 };
    std::array<Command, 64> commandQueue_ {};
    void sendCommand(int type, int id);
    struct MidiMove { int cc = 0, index = 0; float before = 0, after = 0; };
    juce::AbstractFifo midiFifo_ { 128 };
    std::array<MidiMove, 128> midiQueue_ {};
    std::atomic<bool> undoRequest_ { false }, redoRequest_ { false }, resetRedo_ { false }, resetResample_ { false };
    bool lastRedo_ = false, lastResample_ = false;
    void handleAsyncUpdate() override;
    // A take restored from the state, waiting to be swapped in by the audio thread.
    // (A host that renders offline may be a fresh instance with nothing captured.)
    struct PendingTake { std::vector<float> pcm; bool play = false; bool has = false; bool follows = false; double anchor = 0; };
    juce::SpinLock takeLock_;
    PendingTake pendingTake_[2];
    void applyPendingTakes();
    std::vector<float> inputLeft_, inputRight_, scratchLeft_, scratchRight_;
    float inputGainNow_ = 1.f;   // audio thread: ramped across a block so moving it can't click
    kit::TapeInput tape_;
    bool pendingWave_ = false;
    // The live view's columns, copied from the cores as they're written (audio thread writes).
    std::unique_ptr<std::atomic<float>[]> live_ { new std::atomic<float>[brood::kLiveCols]() };
    int64_t liveSeen_ = -1;   // core_.histWritten() when last copied; -1 copies the lot
    void publishLive();

    juce::AudioBuffer<float> demo_;
    double demoRate_ = 0, demoPos_ = 0;
    juce::SpinLock demoLock_;

    juce::AudioFormatManager formats_;
    juce::AudioBuffer<float> file_;
    double fileRate_ = 0, filePos_ = 0;
    juce::String fileName_;
    juce::SpinLock fileLock_;

    std::array<float, kWaveBins> wave_ {}, waveRight_ {};
    mutable juce::SpinLock waveLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BroodProcessor)
};
