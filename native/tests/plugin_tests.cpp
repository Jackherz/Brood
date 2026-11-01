// Headless integration tests for the JUCE processor (no DAW or GUI required).
#include "plugin/PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { std::printf("FAIL: %s\n", message); ++failures; } } while (0)

static void process(MoodProcessor& p, juce::AudioBuffer<float>& b, juce::MidiBuffer& m) {
    p.processBlock(b, m);
    m.clear();
}

int main() {
    juce::ScopedJuceInitialiser_GUI initialise;
    constexpr int size = 128;
    constexpr double rate = 48000;
    MoodProcessor p;
    p.setPlayConfigDetails(2, 2, rate, size);
    p.prepareToPlay(rate, size);
    juce::AudioBuffer<float> b(2, size);
    juce::MidiBuffer m;
    // Fully bypassed stereo should not be summed to mono or limited.
    for (int i = 0; i < size; ++i) { b.setSample(0, i, 1.2f); b.setSample(1, i, -0.25f); }
    process(p, b, m);
    CHECK(std::fabs(b.getSample(0, 64) - 1.2f) < 1e-5f, "bypass left channel");
    CHECK(std::fabs(b.getSample(1, 64) + 0.25f) < 1e-5f, "bypass right channel");

    // Control a knob and enable wet with the pedal's CC assignments.
    m.addEvent(juce::MidiMessage::controllerEvent(1, 15, 100), 0);
    m.addEvent(juce::MidiMessage::controllerEvent(1, 103, 1), 0);
    m.addEvent(juce::MidiMessage::controllerEvent(1, 75, 1), 0);
    b.clear(); process(p, b, m);
    CHECK(std::fabs(p.apvts.getRawParameterValue("mix")->load() - 100.f / 127.f) < 0.001f, "MIDI CC 15 controls MIX");
    CHECK(p.ui.wetOn.load(), "MIDI CC 103 engages wet at value 1");
    CHECK(p.apvts.getRawParameterValue("trails")->load() == 1.f, "MIDI switches accept value 1");
    p.prepareToPlay(rate, size);
    b.clear(); process(p, b, m);
    CHECK(p.ui.wetOn.load(), "wet state survives DAW re-prepare");

    // The wet-engaged state and parameter settings survive a DAW project reload.
    juce::MemoryBlock saved; p.getStateInformation(saved);
    MoodProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    juce::MemoryBlock resaved; restored.getStateInformation(resaved);
    MoodProcessor immediateReload;
    immediateReload.setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
    immediateReload.setPlayConfigDetails(2, 2, rate, size);
    immediateReload.prepareToPlay(rate, size);
    b.clear(); process(immediateReload, b, m);
    CHECK(immediateReload.ui.wetOn.load(), "wet state survives immediate re-save before audio");
    restored.setPlayConfigDetails(2, 2, rate, size);
    restored.prepareToPlay(rate, size);
    b.clear(); process(restored, b, m);
    CHECK(restored.ui.wetOn.load(), "wet state restored");
    CHECK(std::fabs(restored.apvts.getRawParameterValue("mix")->load() - 100.f / 127.f) < 0.001f, "parameter restored");

    // Looper is always listening and can be captured via MIDI CC 102.
    for (int block = 0; block < 200; ++block) {
        for (int i = 0; i < size; ++i) {
            const auto x = static_cast<float>(0.2 * std::sin(2 * juce::MathConstants<double>::pi * 220 * (block * size + i) / rate));
            b.setSample(0, i, x); b.setSample(1, i, -x);
        }
        process(restored, b, m);
    }
    m.addEvent(juce::MidiMessage::controllerEvent(1, 102, 1), 0);
    b.clear(); process(restored, b, m);
    CHECK(restored.ui.loopLed.load() == static_cast<int>(mood::LoopLed::Play), "MIDI captures the listening looper");
    CHECK(restored.ui.lsrc.load() > 64, "loop contains captured samples");

    // A mono host layout must not be silently cleared.
    MoodProcessor mono;
    mono.setPlayConfigDetails(1, 1, rate, size);
    mono.prepareToPlay(rate, size);
    juce::AudioBuffer<float> mb(1, size); mb.clear(); mb.setSample(0, 0, 0.25f);
    process(mono, mb, m);
    CHECK(std::fabs(mb.getSample(0, 0) - 0.25f) < 1e-6f, "mono bypass");
    // Construct and paint the editor offscreen to catch UI/resource regressions.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
        CHECK(editor && editor->getWidth() == 900 && editor->getHeight() == 640, "editor size");
        if (editor) {
            juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            juce::Graphics graphics(image);
            editor->paintEntireComponent(graphics, true);
            if (const char* path = std::getenv("BROOD_TEST_SCREENSHOT")) {
                juce::FileOutputStream output { juce::File(juce::String::fromUTF8(path)) };
                juce::PNGImageFormat png;
                CHECK(output.openedOk() && png.writeImageToStream(image, output), "write editor screenshot");
            }
        }
    }
    std::printf(failures ? "%d processor failure(s)\n" : "all processor integration tests passed\n", failures);
    return failures ? 1 : 0;
}
