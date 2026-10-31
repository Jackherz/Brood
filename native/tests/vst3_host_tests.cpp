// Load the actual VST3 bundle in a minimal JUCE host, without a DAW.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdio>
#include <memory>

static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { std::printf("FAIL: %s\n", message); ++failures; } } while (0)

static std::unique_ptr<juce::AudioPluginInstance> load(juce::AudioPluginFormatManager& manager, const juce::PluginDescription& description) {
    juce::String error;
    auto instance = manager.createPluginInstance(description, 48000, 128, error);
    if (!instance) std::printf("Could not instantiate: %s\n", error.toRawUTF8());
    if (instance) { instance->setPlayConfigDetails(2, 2, 48000, 128); instance->prepareToPlay(48000, 128); }
    return instance;
}
static float reverbEnergy(juce::AudioProcessor& p) {
    juce::AudioBuffer<float> audio(2, 128); juce::MidiBuffer midi;
    float energy = 0;
    for (int block = 0; block < 300; ++block) {
        audio.clear();
        if (block < 38) for (int i = 0; i < 128; ++i) {
            const auto x = static_cast<float>(0.3 * std::sin(2 * juce::MathConstants<double>::pi * 330 * (block * 128 + i) / 48000));
            audio.setSample(0, i, x); audio.setSample(1, i, x);
        }
        p.processBlock(audio, midi);
        if (block > 100) for (int i = 0; i < 128; ++i) energy += std::abs(audio.getSample(0, i));
    }
    return energy;
}
int main(int argc, char** argv) {
    if (argc != 2) { std::printf("Pass a .vst3 bundle path\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI initialise;
    juce::AudioPluginFormatManager manager; manager.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> found;
    const auto bundlePath = juce::File(argv[1]).getFullPathName();
    for (auto* format : manager.getFormats()) format->findAllTypesForFile(found, bundlePath);
    CHECK(found.size() > 0, "VST3 bundle scans");
    if (found.isEmpty()) return 1;
    auto plugin = load(manager, *found[0]);
    CHECK(plugin != nullptr, "VST3 instantiates");
    if (!plugin) return 1;
    CHECK(plugin->hasEditor(), "VST3 declares an editor");
    juce::AudioBuffer<float> audio(2, 128); juce::MidiBuffer midi;
    for (int i = 0; i < 128; ++i) { audio.setSample(0, i, 1.2f); audio.setSample(1, i, -0.25f); }
    plugin->processBlock(audio, midi);
    CHECK(std::fabs(audio.getSample(0, 64) - 1.2f) < 1e-4f, "VST3 bypass left");
    CHECK(std::fabs(audio.getSample(1, 64) + 0.25f) < 1e-4f, "VST3 bypass right");

    juce::AudioProcessorParameter* mix = nullptr;
    for (auto* parameter : plugin->getParameters()) if (parameter->getName(32).equalsIgnoreCase("mix")) mix = parameter;
    CHECK(mix != nullptr, "MIX is host-automatable");
    if (mix) { mix->setValueNotifyingHost(0.7f); CHECK(std::fabs(mix->getValue() - 0.7f) < 0.01f, "host changes MIX"); }
    audio.clear();
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 103, 1), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 15, 100), 0);
    plugin->processBlock(audio, midi);
    if (mix) CHECK(std::fabs(mix->getValue() - 100.f / 127.f) < 0.01f, "MIDI CC 15 controls MIX in VST3");
    CHECK(reverbEnergy(*plugin) > 1.f, "MIDI CC 103 enables reverb in VST3");
    juce::MemoryBlock state; plugin->getStateInformation(state);
    CHECK(state.getSize() > 0, "VST3 exports project state");
    auto reloaded = load(manager, *found[0]);
    if (reloaded) {
        reloaded->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        juce::MemoryBlock immediate; reloaded->getStateInformation(immediate);
        auto roundTrip = load(manager, *found[0]);
        if (roundTrip) {
            roundTrip->setStateInformation(immediate.getData(), static_cast<int>(immediate.getSize()));
            CHECK(reverbEnergy(*roundTrip) > 1.f, "VST3 wet state survives immediate re-save");
        } else CHECK(false, "VST3 creates round-trip instance");
        CHECK(reverbEnergy(*reloaded) > 1.f, "VST3 restores the wet channel");
    } else CHECK(false, "VST3 creates second instance");

    auto stereo = load(manager, *found[0]);
    if (stereo) {
        juce::AudioBuffer<float> block(2, 128); juce::MidiBuffer events;
        events.addEvent(juce::MidiMessage::controllerEvent(1, 103, 1), 0);
        float leftTail = 0, rightTail = 0;
        for (int n = 0; n < 240; ++n) {
            block.clear();
            if (n < 38) for (int i = 0; i < 128; ++i) block.setSample(0, i, static_cast<float>(0.3 * std::sin(2 * juce::MathConstants<double>::pi * 330 * (n * 128 + i) / 48000)));
            stereo->processBlock(block, events); events.clear();
            if (n > 100) for (int i = 0; i < 128; ++i) { leftTail += std::abs(block.getSample(0, i)); rightTail += std::abs(block.getSample(1, i)); }
        }
        CHECK(leftTail > 1.f && rightTail < 1e-5f, "VST3 preserves stereo separation with wet enabled");
    } else CHECK(false, "VST3 creates stereo test instance");
    std::printf(failures ? "%d VST3 hosting failure(s)\n" : "all VST3 hosting tests passed\n", failures);
    return failures ? 1 : 0;
}
