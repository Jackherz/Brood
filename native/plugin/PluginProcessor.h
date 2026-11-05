#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <optional>
#include "dsp/brood.h"
#include "kit/Bench.h"
#include "kit/TapeInput.h"

// The pedal as an AudioProcessor. Every control (knobs, toggles, dips, and both
// footswitches as momentary switches) is a host parameter, so a DAW can automate
// it or map it to a MIDI controller.
class BroodProcessor final : public juce::AudioProcessor {
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

    juce::AudioProcessorValueTreeState apvts;

    // Readout text for a parameter in real-world units (depends on mode and clock).
    juce::String describe(const juce::String& id) const;
    juce::String describe(const juce::String& id, float value) const;

    // ---- input source ----
    enum Source { Live = 0, Demo = 1, File = 2 };
    std::atomic<int> source { Live };
    std::atomic<float> inputGain { 1.f };   // up to +24 dB; past 0 dB it drives the tape
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
        std::atomic<bool> wetOn { false }, freeze { false }, envGate { false };
        std::array<std::atomic<float>, brood::kModTargets> effective {};   // each control as the sound has it now
        std::array<std::atomic<float>, 2> modValue {};                     // each source now, 0..1
        std::atomic<int> loopLed { 0 }, lsrc { 0 }, winStart { 0 }, winLen { 0 }, winSpan { 0 };
        std::atomic<float> pos { 0 }, clockHz { 48000 }, peakIn { 0 }, peakOut { 0 }, load { 0 };
        std::atomic<float> bpm { 120.f };
        std::atomic<uint32_t> histCycles { 0 };
        std::atomic<uint32_t> waveVersion { 0 };
    } ui;
    static constexpr int kWaveBins = 400;
    // copy of the loop's peak envelope; returns false if the audio thread holds the lock
    bool copyWave(std::array<float, kWaveBins>& out) const;

private:
    void pullParams();
    void makeDemo(double sr);
    float param(const char* id) const { return apvts.getRawParameterValue(id)->load(); }

    brood::Core core_, coreRight_;
    bool lastWetSw_ = false, lastLoopSw_ = false;
    std::atomic<float> hostBpm_ { 120.f };   // last tempo the host reported
    std::atomic<float> hostBarQuarters_ { 4.f };   // a bar's length in quarter notes
    kit::bench::OutputRecorder outputRecorder_;
    std::atomic<int> restoreWet_ { -1 };
    // A take restored from the state, waiting to be swapped in by the audio thread.
    // (A host that renders offline may be a fresh instance with nothing captured.)
    struct PendingTake { std::vector<float> pcm; bool play = false; bool has = false; };
    juce::SpinLock takeLock_;
    PendingTake pendingTake_[2];
    void applyPendingTakes();
    std::vector<float> inputLeft_, inputRight_, scratchLeft_, scratchRight_;
    float inputGainNow_ = 1.f;   // audio thread: ramped across a block so moving it can't click
    kit::TapeInput tape_;
    int dubBlocks_ = 0;
    bool pendingWave_ = false;

    juce::AudioBuffer<float> demo_;
    double demoPos_ = 0;

    juce::AudioFormatManager formats_;
    juce::AudioBuffer<float> file_;
    double fileRate_ = 0, filePos_ = 0;
    juce::String fileName_;
    juce::SpinLock fileLock_;

    std::array<float, kWaveBins> wave_ {}, waveRight_ {};
    mutable juce::SpinLock waveLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BroodProcessor)
};
