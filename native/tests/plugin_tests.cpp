// Headless integration tests for the JUCE processor (no DAW or GUI required).
#include "plugin/PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { std::printf("FAIL: %s\n", message); ++failures; } } while (0)

static void process(BroodProcessor& p, juce::AudioBuffer<float>& b, juce::MidiBuffer& m) {
    p.processBlock(b, m);
    m.clear();
}

int main() {
    juce::ScopedJuceInitialiser_GUI initialise;
    constexpr int size = 128;
    constexpr double rate = 48000;
    BroodProcessor p;
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
    BroodProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    juce::MemoryBlock resaved; restored.getStateInformation(resaved);
    BroodProcessor immediateReload;
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
    CHECK(restored.ui.loopLed.load() == static_cast<int>(brood::LoopLed::Play), "MIDI captures the listening looper");
    CHECK(restored.ui.lsrc.load() > 64, "loop contains captured samples");

    // A mono host layout must not be silently cleared.
    BroodProcessor mono;
    mono.setPlayConfigDetails(1, 1, rate, size);
    mono.prepareToPlay(rate, size);
    juce::AudioBuffer<float> mb(1, size); mb.clear(); mb.setSample(0, 0, 0.25f);
    process(mono, mb, m);
    CHECK(std::fabs(mb.getSample(0, 0) - 0.25f) < 1e-6f, "mono bypass");

    // A mono source on a stereo track must come out in stereo once the wet side is
    // on: each engine's right output used to be thrown away, which left reverb and
    // WIDTH as dual mono on every mono track.
    {
        BroodProcessor wide;
        wide.setPlayConfigDetails(2, 2, rate, size);
        wide.prepareToPlay(rate, size);
        if (auto* w = wide.apvts.getParameter("width")) w->setValueNotifyingHost(1.f);
        if (auto* mx = wide.apvts.getParameter("mix")) mx->setValueNotifyingHost(1.f);
        juce::MidiBuffer on; on.addEvent(juce::MidiMessage::controllerEvent(1, 103, 1), 0);
        double diff = 0, energy = 0;
        for (int block = 0; block < 400; ++block) {
            for (int i = 0; i < size; ++i) {
                const auto x = static_cast<float>(0.3 * std::sin(2 * juce::MathConstants<double>::pi * 330 * (block * size + i) / rate));
                b.setSample(0, i, x); b.setSample(1, i, x);
            }
            process(wide, b, block == 0 ? on : m);
            if (block > 100)
                for (int i = 0; i < size; ++i) {
                    diff += std::fabs(b.getSample(0, i) - b.getSample(1, i));
                    energy += std::fabs(b.getSample(0, i)) + std::fabs(b.getSample(1, i));
                }
        }
        CHECK(energy > 1.0, "wet output is audible");
        CHECK(diff > 0.05 * energy, "a mono source comes out in stereo");
    }

    // Sets saved before the rename carry the old state tag and must still load.
    {
        juce::MemoryBlock saved;
        restored.getStateInformation(saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
        CHECK(xml != nullptr, "state is XML");
        if (xml) {
            const char legacy[] = { 0x4d, 0x4f, 0x4f, 0x44, 0 };
            xml->setTagName(legacy);
            if (auto* mx = xml->getChildByAttribute("id", "mix")) mx->setAttribute("value", 0.25);
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary(*xml, old);
            BroodProcessor reopened;
            reopened.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
            CHECK(std::fabs(reopened.apvts.getRawParameterValue("mix")->load() - 0.25f) < 1e-4f, "legacy state loads");
        }
    }
    // Construct and paint the editor offscreen to catch UI/resource regressions.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
        CHECK(editor && editor->getWidth() == 900 && editor->getHeight() == 712, "editor size");
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
