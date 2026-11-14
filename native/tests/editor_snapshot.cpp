// Renders the editor to a PNG without a DAW, so a layout change can be looked at
// (and diffed) from the command line.
//   editor_snapshot <out.png> [scale] [loop|listen|record|hold|dub] [paramId=normalisedValue ...]
// "loop" plays two seconds of the demo phrase and catches it, so the waveform and
// its markers have something to show. The others show the strip mid-gesture:
// "listen" plays the demo with nothing caught, "record" taps the loop switch and keeps
// playing (pass recMode=1 for RECORD), "hold" holds it down in CAPTURE, and "dub" catches a
// loop then holds the switch to overdub. "rerecord" (with recMode=1) records a take, stops it,
// and starts recording a second over it.
#include "plugin/PluginProcessor.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: editor_snapshot <out.png> [scale] [id=value ...]\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI initialise;
    BroodProcessor proc;
    proc.setPlayConfigDetails(2, 2, 48000, 256);
    proc.prepareToPlay(48000, 256);
    float scale = 1.f;
    juce::String scene;
    for (int i = 2; i < argc; ++i) {
        const juce::String arg(argv[i]);
        if (juce::StringArray{"loop", "listen", "record", "hold", "dub", "rerecord"}.contains(arg)) { scene = arg; continue; }
        if (!arg.containsChar('=')) { scale = arg.getFloatValue(); continue; }
        if (auto* p = proc.apvts.getParameter(arg.upToFirstOccurrenceOf("=", false, false)))
            p->setValueNotifyingHost(arg.fromFirstOccurrenceOf("=", false, false).getFloatValue());
    }
    if (scene.isNotEmpty()) {
        proc.source = BroodProcessor::Demo;
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer m;
        auto run = [&](double secs) { for (int i = 0; i < static_cast<int>(secs * 48000 / 256); ++i) { b.clear(); proc.processBlock(b, m); } };
        auto* sw = proc.apvts.getParameter("loopSwitch");
        const auto tapLoop = [&] { sw->setValueNotifyingHost(1.f); run(0.05); sw->setValueNotifyingHost(0.f); };
        run(scene == "listen" ? 3.0 : 2.0);
        if (scene == "loop" || scene == "dub") { tapLoop(); run(0.5); }
        if (scene == "record") { tapLoop(); run(0.9); }
        if (scene == "hold" || scene == "dub") { sw->setValueNotifyingHost(1.f); run(1.2); }
        if (scene == "rerecord") { tapLoop(); run(0.6); tapLoop(); run(0.3); tapLoop(); run(0.3); tapLoop(); run(0.5); }
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    editor->setSize(juce::roundToInt(editor->getWidth() * scale), juce::roundToInt(editor->getHeight() * scale));
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    if (!stream.openedOk() || !png.writeImageToStream(image, stream)) { std::fprintf(stderr, "could not write %s\n", argv[1]); return 1; }
    std::printf("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    return 0;
}
