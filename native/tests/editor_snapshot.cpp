// Renders the editor to a PNG without a DAW, so a layout change can be looked at
// (and diffed) from the command line.
//   editor_snapshot <out.png> [scale] [paramId=normalisedValue ...]
#include "plugin/PluginProcessor.h"
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: editor_snapshot <out.png> [scale] [id=value ...]\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI initialise;
    BroodProcessor proc;
    proc.setPlayConfigDetails(2, 2, 48000, 256);
    proc.prepareToPlay(48000, 256);
    float scale = 1.f;
    for (int i = 2; i < argc; ++i) {
        const juce::String arg(argv[i]);
        if (!arg.containsChar('=')) { scale = arg.getFloatValue(); continue; }
        if (auto* p = proc.apvts.getParameter(arg.upToFirstOccurrenceOf("=", false, false)))
            p->setValueNotifyingHost(arg.fromFirstOccurrenceOf("=", false, false).getFloatValue());
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
