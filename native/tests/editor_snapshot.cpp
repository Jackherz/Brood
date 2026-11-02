// Renders the editor to a PNG without a DAW, so a layout change can be looked at
// (and diffed) from the command line.
//   editor_snapshot <out.png> [scale] [loop] [paramId=normalisedValue ...]
// "loop" plays two seconds of the demo phrase and catches it, so the waveform and
// its markers have something to show.
#include "plugin/PluginProcessor.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: editor_snapshot <out.png> [scale] [id=value ...]\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI initialise;
    BroodProcessor proc;
    proc.setPlayConfigDetails(2, 2, 48000, 256);
    proc.prepareToPlay(48000, 256);
    float scale = 1.f;
    bool catchLoop = false;
    for (int i = 2; i < argc; ++i) {
        const juce::String arg(argv[i]);
        if (arg == "loop") { catchLoop = true; continue; }
        if (!arg.containsChar('=')) { scale = arg.getFloatValue(); continue; }
        if (auto* p = proc.apvts.getParameter(arg.upToFirstOccurrenceOf("=", false, false)))
            p->setValueNotifyingHost(arg.fromFirstOccurrenceOf("=", false, false).getFloatValue());
    }
    if (catchLoop) {
        proc.source = BroodProcessor::Demo;
        juce::AudioBuffer<float> b(2, 256);
        juce::MidiBuffer m;
        auto run = [&](double secs) { for (int i = 0; i < static_cast<int>(secs * 48000 / 256); ++i) { b.clear(); proc.processBlock(b, m); } };
        run(2.0);
        auto* sw = proc.apvts.getParameter("loopSwitch");
        sw->setValueNotifyingHost(1.f); run(0.05); sw->setValueNotifyingHost(0.f);
        run(0.5);
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
