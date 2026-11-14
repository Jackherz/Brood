// Headless integration tests for the JUCE processor (no DAW or GUI required).
#include "plugin/PluginProcessor.h"
#include "plugin/BroodPresets.h"
#include "plugin/LoopTrim.h"
#include "kit/Bench.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(condition, message) do { if (!(condition)) { std::printf("FAIL: %s\n", message); ++failures; } } while (0)

// A host transport for the tests: playing or not, at a song position and tempo.
struct TestHead final : juce::AudioPlayHead {
    bool playing = false; double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setIsPlaying(playing);
        info.setPpqPosition(ppq);
        info.setPpqPositionOfLastBarStart(std::floor(ppq / 4) * 4);
        info.setBpm(bpm);
        info.setTimeSignature(TimeSignature{4, 4});
        return info;
    }
};

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
    m.addEvent(juce::MidiMessage::controllerEvent(1, 52, 127), 0);
    b.clear(); process(p, b, m);
    CHECK(p.apvts.getRawParameterValue("mod1Hold")->load() == 1.f && p.apvts.getRawParameterValue("mod2Hold")->load() == 1.f, "MIDI CC 52 holds both sources");
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
    // Presets: a factory sound, a saved one, a category, a favourite, and the old
    // user slots becoming ordinary presets exactly once.
    {
        juce::TemporaryFile folderFile;
        const auto folder = folderFile.getFile();
        const auto slots = folder.getSiblingFile(folder.getFileName() + "-slots.xml");
        slots.replaceWithText("<BROOD_PATCHES><PATCH slot=\"2\"><P id=\"mix\" v=\"0.125\"/></PATCH></BROOD_PATCHES>");
        BroodProcessor sound;
        BroodPresets presets(sound, folder, slots);
        CHECK(!slots.existsAsFile() && slots.withFileExtension("old").existsAsFile(), "the old slots are set aside");
        slots.withFileExtension("old").deleteFile();
        auto find = [&](const juce::String& name) {
            for (const auto& e : presets.list()) if (e.name == name) return std::optional<kit::PresetLibrary::Entry>(e);
            return std::optional<kit::PresetLibrary::Entry>();
        };
        const auto migrated = find("User 2");
        CHECK(migrated.has_value() && migrated->category == "USER", "a slot becomes a USER preset");
        if (migrated) {
            presets.load(*migrated);
            CHECK(std::fabs(sound.apvts.getParameter("mix")->getValue() - 0.125f) < 1e-4f, "with its values");
        }
        const auto echo = find("Glass Echo");
        CHECK(echo.has_value() && echo->factory && echo->category == "ECHOES", "the factory set is listed");
        if (echo) {
            presets.load(*echo);
            CHECK(juce::roundToInt(sound.apvts.getRawParameterValue("wetMode")->load()) == 1, "a factory preset sets its mode");
            CHECK(sound.presetName == "Glass Echo", "the name follows");
        }
        CHECK(presets.save("Mine", "LOOPS") && sound.presetName == "Mine", "save");
        const auto mine = find("Mine");
        CHECK(mine.has_value() && mine->category == "LOOPS", "saved with its category");
        CHECK(presets.shows(*mine, "LOOPS", "min") && !presets.shows(*mine, "ECHOES", ""), "views and search");
        presets.setFavourite("Mine", true);
        CHECK(presets.shows(*mine, "FAVOURITES", ""), "favourites");
        juce::MemoryBlock withName;
        sound.getStateInformation(withName);
        BroodProcessor back;
        back.setStateInformation(withName.getData(), static_cast<int>(withName.getSize()));
        CHECK(back.presetName == "Mine", "the name is kept with the project");
        folder.deleteRecursively();
    }
    // The Bench: a caught window goes out as a WAV and a note, and comes back the same.
    {
        BroodProcessor catcher;
        catcher.setPlayConfigDetails(2, 2, rate, size);
        catcher.prepareToPlay(rate, size);
        juce::AudioBuffer<float> empty;
        double momentRate = 0;
        CHECK(!catcher.copyMoment(empty, momentRate), "nothing caught, nothing to send");
        juce::AudioBuffer<float> io(2, size);
        juce::MidiBuffer none;
        auto run = [&](int blocks) {
            for (int i = 0; i < blocks; ++i) {
                for (int c = 0; c < 2; ++c) for (int n = 0; n < size; ++n) io.setSample(c, n, 0.3f);
                catcher.processBlock(io, none);
            }
        };
        run(static_cast<int>(rate / size));
        auto* sw = catcher.apvts.getParameter("loopSwitch");
        sw->setValueNotifyingHost(1.f); run(2); sw->setValueNotifyingHost(0.f); run(4);
        juce::AudioBuffer<float> moment;
        CHECK(catcher.copyMoment(moment, momentRate) && moment.getNumChannels() == 2 && moment.getNumSamples() >= 1, "a caught loop can be sent");
        juce::TemporaryFile folderFile;
        const auto folder = folderFile.getFile();
        kit::bench::folderForTests = folder;
        CHECK(!kit::bench::latest().has_value(), "an empty bench");
        CHECK(kit::bench::send(moment, momentRate, "E-01", "BROOD", "BROOD"), "send");
        // Offered back to who sent it too: TAKE after SEND resamples, and another Brood can take it.
        const auto got = kit::bench::latest();
        CHECK(got.has_value() && got->fromChip == "E-01" && got->audio.existsAsFile()
              && std::fabs(got->seconds - moment.getNumSamples() / momentRate) < 1e-3, "the note matches the audio");
        CHECK(got.has_value() && catcher.takeMoment(got->audio), "and the Brood that sent it can take it back");
        juce::Thread::sleep(1100);   // files are named to the second
        CHECK(kit::bench::send(moment, momentRate, "E-01", "BROOD", "BROOD"), "send again");
        CHECK(folder.findChildFiles(juce::File::findFiles, false, "*.wav").size() == 2, "everything sent stays, for the Finder");
        // A folder of your own, and a category inside it.
        const auto mine = folder.getChildFile("my samples");
        mine.createDirectory();
        kit::bench::setSetting("folder_BROOD", mine.getFullPathName());
        mine.getChildFile("Guitar").createDirectory();
        kit::bench::setSetting("category_BROOD", "Guitar");
        CHECK(kit::bench::categories("BROOD").contains("Guitar"), "a category is a folder");
        CHECK(kit::bench::send(moment, momentRate, "E-01", "BROOD", "BROOD"), "send into it");
        CHECK(mine.getChildFile("Guitar").findChildFiles(juce::File::findFiles, false, "*.wav").size() == 1, "it lands in the category");
        // Each plug-in has its own: Brood's choice doesn't move Silt's.
        const auto siltFolder = folder.getChildFile("silt samples");
        CHECK(kit::bench::audioRoot("SILT") == folder, "Silt hasn't moved");
        CHECK(kit::bench::audioFolder("SILT") == folder, "nor taken Brood's category");
        kit::bench::setSetting("folder_SILT", siltFolder.getFullPathName());
        CHECK(kit::bench::audioRoot("SILT") == siltFolder && kit::bench::audioRoot("BROOD") == mine, "two folders");
        // And the other way: a moment from an instrument becomes the loop.
        juce::AudioBuffer<float> fromSilt(2, 44100);
        for (int n = 0; n < fromSilt.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) fromSilt.setSample(c, n, 0.25f * std::sin(static_cast<float>(n) * 0.03f));
        CHECK(kit::bench::send(fromSilt, 44100.0, "I-01", "SILT", "SILT"), "an instrument sends");
        CHECK(siltFolder.findChildFiles(juce::File::findFiles, false, "*.wav").size() == 1, "into its own folder");
        const auto incoming = kit::bench::latest();   // the newest: Silt's
        CHECK(incoming.has_value() && incoming->fromName == "SILT", "and the effect sees it");
        if (incoming.has_value()) {
            const double clock = catcher.ui.clockHz.load();
            CHECK(catcher.takeMoment(incoming->audio), "take it");
            run(4);
            CHECK(std::fabs(catcher.ui.lsrc.load() / clock - 1.0) < 0.01, "one second of it, at the looper's clock");
            CHECK(catcher.ui.loopLed.load() == static_cast<int>(brood::LoopLed::Play), "and it plays");
        }
        // The markers reach all of the take in every mode, and come out where they were asked to.
        {
            auto* mode = catcher.apvts.getParameter("loopMode");
            auto* start = catcher.apvts.getParameter("start");
            auto* end = catcher.apvts.getParameter("loopEnd");
            auto* length = catcher.apvts.getParameter("length");
            const auto apply = [&](const LoopTrim& t) {
                start->setValueNotifyingHost(start->convertTo0to1(static_cast<float>(t.start)));
                end->setValueNotifyingHost(end->convertTo0to1(static_cast<float>(t.end)));
                length->setValueNotifyingHost(length->convertTo0to1(static_cast<float>(t.length)));
                run(static_cast<int>(rate / size));   // LENGTH glides
            };
            for (const int m : {0, 1, 2}) {
                mode->setValueNotifyingHost(mode->convertTo0to1(static_cast<float>(m)));
                run(4);
                const int len = catcher.ui.lsrc.load();
                const double slack = 2.0 + len / 400.0;
                for (const double lengthKnob : {0.1, 0.5, 1.0})
                    for (const auto& [first, width] : std::initializer_list<std::pair<double, double>>{{0.0, 1.0}, {0.0, 0.25}, {0.75, 0.25}, {0.5, 0.1}, {0.25, 0.5}}) {
                        const auto trim = loopTrim(m == 1, len, lengthKnob, first * len, width * len);
                        apply(trim);
                        CHECK(std::abs(catcher.ui.winStart.load() - trim.firstSample) <= slack, "START lands where the marker asked, in every mode");
                        CHECK(std::abs(catcher.ui.winLen.load() - trim.width) <= slack, "END lands where the marker asked, in every mode");
                        CHECK(trim.firstSample + trim.width <= len + 1, "and the window stays in the take");
                    }
            }
            // TAPE: turning LENGTH leaves a moved START where it is. START is a point in the
            // take now, so nothing has to hold it.
            mode->setValueNotifyingHost(mode->convertTo0to1(1.f));
            run(4);
            const int len = catcher.ui.lsrc.load();
            auto hold = loopTrim(true, len, 0.3, 0.2 * len, 0.1 * len);
            apply(hold);
            for (const double knob : {0.1, 0.4, 0.7}) {
                hold.length = knob;
                apply(hold);
                CHECK(std::abs(catcher.ui.winStart.load() - hold.firstSample) <= 2.0 + len / 400.0, "LENGTH leaves a moved START alone");
            }
            start->setValueNotifyingHost(start->getDefaultValue());
            end->setValueNotifyingHost(end->getDefaultValue());
            length->setValueNotifyingHost(length->getDefaultValue());
            mode->setValueNotifyingHost(mode->getDefaultValue());

            // A source dropped on WET/LOOP moves what the panel reads, around the slider's own value.
            auto* route = catcher.apvts.getParameter("mod1_balance");
            auto* on = catcher.apvts.getParameter("mod1On");
            auto* rateP = catcher.apvts.getParameter("mod1Rate");
            on->setValueNotifyingHost(1.f);
            route->setValueNotifyingHost(route->convertTo0to1(0.5f));
            rateP->setValueNotifyingHost(1.f);
            float low = 1.f, high = 0.f;
            for (int i = 0; i < 400; ++i) {
                run(1);
                low = std::min(low, catcher.ui.effective[6].load());
                high = std::max(high, catcher.ui.effective[6].load());
            }
            CHECK(high - low > 0.3f, "WET/LOOP's live value moves with its source");
            route->setValueNotifyingHost(route->convertTo0to1(0.f));
            on->setValueNotifyingHost(on->getDefaultValue());
            rateP->setValueNotifyingHost(rateP->getDefaultValue());
        }
        // An audio file dropped on the window, from anywhere: the loop, at the looper's clock.
        {
            const auto dropped = folder.getChildFile("dropped.wav"), notAudio = folder.getChildFile("notes.txt");
            notAudio.replaceWithText("not a sample");
            juce::AudioBuffer<float> half(2, 22050);
            for (int n = 0; n < half.getNumSamples(); ++n) for (int c = 0; c < 2; ++c) half.setSample(c, n, 0.25f * std::sin(static_cast<float>(n) * 0.05f));
            {
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(dropped);
                auto writer = juce::WavAudioFormat().createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(44100.0).withNumChannels(2).withBitsPerSample(24));
                CHECK(writer != nullptr && writer->writeFromAudioSampleBuffer(half, 0, half.getNumSamples()), "write a sample to drop");
            }
            std::unique_ptr<juce::AudioProcessorEditor> window(catcher.createEditor());
            auto* target = dynamic_cast<juce::FileDragAndDropTarget*>(window.get());
            CHECK(target != nullptr, "the window takes file drops");
            if (target != nullptr) {
                CHECK(!target->isInterestedInFileDrag({notAudio.getFullPathName()}), "not a text file");
                CHECK(target->isInterestedInFileDrag({notAudio.getFullPathName(), dropped.getFullPathName()}), "but a sample among other files");
                target->fileDragEnter({dropped.getFullPathName()}, 10, 10);
                juce::Image frame(juce::Image::ARGB, window->getWidth(), window->getHeight(), true);
                juce::Graphics graphics(frame);
                window->paintEntireComponent(graphics, true);
                target->filesDropped({notAudio.getFullPathName(), dropped.getFullPathName()}, 10, 10);
                run(4);
                const double clock = catcher.ui.clockHz.load();
                CHECK(std::fabs(catcher.ui.lsrc.load() / clock - 0.5) < 0.01, "half a second of it");
                CHECK(catcher.ui.loopLed.load() == static_cast<int>(brood::LoopLed::Play), "and it plays");
            }
        }
        kit::bench::folderForTests = juce::File();
        folder.deleteRecursively();
    }
    // The input level past 0 dB drives the tape, for everything after it: no peak, even dry.
    {
        BroodProcessor driven;
        driven.setPlayConfigDetails(2, 2, rate, size);
        driven.prepareToPlay(rate, size);
        driven.inputGain = 15.85f;
        juce::AudioBuffer<float> io(2, size);
        juce::MidiBuffer none;
        float most = 0.f;
        for (int b = 0; b < 200; ++b) {
            for (int n = 0; n < size; ++n) for (int c = 0; c < 2; ++c) io.setSample(c, n, 0.9f * std::sin(static_cast<float>(b * size + n) * 0.03f));
            driven.processBlock(io, none);
            if (b > 20) most = juce::jmax(most, io.getMagnitude(0, size));
        }
        CHECK(most > 0.5f && most < 1.0f, "cranked input rounds over");
    }
    // SEND's bars: the exact output, and nothing goes out early.
    {
        BroodProcessor catcher;
        catcher.setPlayConfigDetails(2, 2, rate, size);
        catcher.prepareToPlay(rate, size);
        using Capture = BroodProcessor::OutputCapture;
        catcher.armOutputCapture(4);   // no host: 120 BPM, 4/4, from now
        CHECK(catcher.outputCaptureState() == Capture::waiting, "armed");
        juce::AudioBuffer<float> io(2, size), heard(2, juce::roundToInt(8.0 * rate) + size);
        juce::MidiBuffer none;
        int at = 0;
        juce::Random noise(5);
        juce::AudioBuffer<float> taken;
        double takenRate = 0;
        CHECK(!catcher.takeOutputCapture(taken, takenRate), "nothing to take yet");
        while (catcher.outputCaptureState() != Capture::done && at + size <= heard.getNumSamples()) {
            for (int c = 0; c < 2; ++c) for (int n = 0; n < size; ++n) io.setSample(c, n, 0.2f * noise.nextFloat());
            catcher.processBlock(io, none);
            for (int c = 0; c < 2; ++c) heard.copyFrom(c, at, io, c, 0, size);
            at += size;
        }
        CHECK(catcher.outputCaptureState() == Capture::done, "it finishes");
        CHECK(catcher.takeOutputCapture(taken, takenRate) && std::fabs(taken.getNumSamples() / takenRate - 8.0) < 0.01, "4 bars at 120 is 8 s");
        bool same = true;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < taken.getNumSamples(); ++n) same = same && taken.getSample(c, n) == heard.getSample(c, n);
        CHECK(same, "exactly what came out");
        CHECK(catcher.outputCaptureState() == Capture::idle, "and it's ready again");
        catcher.armOutputCapture(4);
        catcher.cancelOutputCapture();
        catcher.processBlock(io, none);
        CHECK(catcher.outputCaptureState() == Capture::idle, "STOP stops it");
    }
    // Construct and paint the editor offscreen to catch UI/resource regressions.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
        CHECK(editor && editor->getWidth() == 1200 && editor->getHeight() == 820, "editor size");
        if (editor) {
            juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            juce::Graphics graphics(image);
            editor->paintEntireComponent(graphics, true);
            // TRAILS is shown wherever it does something, which is everywhere but SLIP.
            const auto trailsShown = [&](float mode) {
                auto* wetMode = restored.apvts.getParameter("wetMode");
                wetMode->setValueNotifyingHost(wetMode->convertTo0to1(mode));
                std::unique_ptr<juce::AudioProcessorEditor> window(restored.createEditor());
                for (auto* child : window->getChildren())
                    if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->getButtonText() == "TRAILS") return button->isVisible();
                return false;
            };
            CHECK(trailsShown(0.f) && trailsShown(1.f) && trailsShown(3.f), "TRAILS in REVERB, DELAY and WEAVE");
            CHECK(!trailsShown(2.f), "and not in SLIP");
            restored.apvts.getParameter("wetMode")->setValueNotifyingHost(0.f);
            bool wetPosition = false;
            for (auto* child : editor->getChildren()) wetPosition = wetPosition || child->getTitle() == "Wet position";
            CHECK(wetPosition, "the wet side's position is on the panel, by name");
            if (const char* path = std::getenv("BROOD_TEST_SCREENSHOT")) {
                juce::FileOutputStream output { juce::File(juce::String::fromUTF8(path)) };
                juce::PNGImageFormat png;
                CHECK(output.openedOk() && png.writeImageToStream(image, output), "write editor screenshot");
            }
        }
    }
    // A knob's explanation left the panel, so it has to follow the mode by itself.
    {
        auto* wetMode = restored.apvts.getParameter("wetMode");
        auto timeHelp = [&](float mode) {
            wetMode->setValueNotifyingHost(wetMode->convertTo0to1(mode));
            std::unique_ptr<juce::AudioProcessorEditor> window(restored.createEditor());
            for (auto* child : window->getChildren())
                if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->getTitle() == "TIME") return slider->getDescription();
            return juce::String();
        };
        CHECK(timeHelp(0.f) == "how long the room keeps breathing.", "TIME explains itself in REVERB");
        CHECK(timeHelp(1.f) == "how far back the echo remembers.", "and again in DELAY");
    }
    {   // A set saved before START meant the same in every mode: TAPE's START is rewritten so the
        // window comes back where it was; ENV's is left alone.
        auto saved = [](float mode, float start) {
            juce::ValueTree state("BROOD");
            for (const auto& [id, value] : std::initializer_list<std::pair<const char*, float>>{{"loopMode", mode}, {"start", start}, {"length", 0.6f}}) {
                juce::ValueTree param("PARAM");
                param.setProperty("id", id, nullptr);
                param.setProperty("value", value, nullptr);
                state.appendChild(param, nullptr);
            }
            return state;
        };
        const int take = 4 * 64000;
        auto tape = saved(1.f, 0.3f);
        BroodProcessor::migrateStart(tape, take);
        const double now = static_cast<double>(tape.getChildWithProperty("id", "start")["value"]);
        CHECK(std::fabs(now - brood::migrateTapeStart(0.3, 0.6, take)) < 1e-6, "an old TAPE START is rewritten");
        auto newest = saved(1.f, 0.f);
        BroodProcessor::migrateStart(newest, take);
        CHECK(static_cast<double>(newest.getChildWithProperty("id", "start")["value"]) == 1.0, "the old newest is the new 1");
        auto env = saved(0.f, 0.3f);
        BroodProcessor::migrateStart(env, take);
        CHECK(std::fabs(static_cast<double>(env.getChildWithProperty("id", "start")["value"]) - 0.3) < 1e-6, "ENV keeps its START");
    }
    {   // Every value a host shows fits a controller's display: 8 characters at most, in every mode
        BroodProcessor h;
        auto set = [&h](const char* id, float plain) { auto* q = h.apvts.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(plain)); };
        int longest = 0; juce::String worst;
        for (float wet : {0.f, 1.f, 2.f, 3.f})
            for (float loop : {0.f, 1.f, 2.f})
                for (float sync : {0.f, 1.f}) {
                    set("wetMode", wet); set("loopMode", loop); set("sync", sync); set("envSync", sync); set("mod1Sync", sync);
                    for (auto* prm : h.getParameters())
                        for (float v : {0.f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.f}) {
                            const auto text = prm->getText(v, 64);
                            if (text.length() > longest) { longest = text.length(); worst = prm->getName(32) + ": " + text; }
                        }
                }
        if (longest > 8) std::printf("  longest host value: %s\n", worst.toRawUTF8());
        CHECK(longest <= 8, "every host value is 8 characters or fewer");
        // Typed back in, the plain ones land where they were.
        set("wetMode", 1.f); set("sync", 0.f);
        for (const char* id : {"time", "mix", "loopEnd", "balance", "memory", "mod1_time"}) {
            auto* q = dynamic_cast<juce::RangedAudioParameter*>(h.apvts.getParameter(id));
            for (float v : {0.2f, 0.6f}) {
                const float back = q->getValueForText(q->getText(v, 64));
                if (std::fabs(back - v) > 0.02f) std::printf("  %s: %.3f -> \"%s\" -> %.3f\n", id, v, q->getText(v, 64).toRawUTF8(), back);
                CHECK(std::fabs(back - v) <= 0.02f, "a host value typed back in lands where it was");
            }
        }
    }
    std::printf(failures ? "%d processor failure(s)\n" : "all processor integration tests passed\n", failures);
    {   // OVERDUB, CLEAR and UNDO as host parameters
        BroodProcessor q;
        q.setPlayConfigDetails(2, 2, rate, size);
        q.prepareToPlay(rate, size);
        auto run = [&](int blocks) {
            for (int block = 0; block < blocks; ++block) {
                for (int i = 0; i < size; ++i) { const auto x = static_cast<float>(0.2 * std::sin(0.03 * (block * size + i))); b.setSample(0, i, x); b.setSample(1, i, x); }
                process(q, b, m);
            }
        };
        auto set = [&](const char* id, float v) { q.apvts.getParameter(id)->setValueNotifyingHost(v); };
        auto value = [&](const char* id) { return q.apvts.getRawParameterValue(id)->load(); };
        run(200);
        set("overdub", 1.f); run(2); q.settleControls();
        CHECK(value("overdub") < 0.5f && !q.ui.canOverdub.load(), "OVERDUB with no loop is set back to off");
        set("loopSwitch", 1.f); run(2); set("loopSwitch", 0.f); run(50);
        CHECK(q.ui.canOverdub.load(), "OVERDUB can switch on once a loop plays");
        set("overdub", 1.f); run(50); q.settleControls();
        CHECK(value("overdub") > 0.5f && q.ui.dubLatched.load(), "OVERDUB stays on while it layers");
        set("loopSwitch", 1.f); run(2); set("loopSwitch", 0.f); run(2); q.settleControls();
        CHECK(value("overdub") < 0.5f, "OVERDUB is set back to off when the loop stops");
        set("clear", 1.f); run(100); q.settleControls();
        CHECK(value("clear") < 0.5f, "CLEAR sets itself back to off");
        CHECK(q.ui.lsrc.load() == 0 && q.ui.undoKind.load() == 2, "CLEAR empties the take");
        {
            juce::MemoryBlock saved; q.getStateInformation(saved);
            BroodProcessor r; r.setPlayConfigDetails(2, 2, rate, size); r.prepareToPlay(rate, size);
            r.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            juce::AudioBuffer<float> rb(2, size); rb.clear(); r.processBlock(rb, m);
            CHECK(r.ui.lsrc.load() == 0, "a cleared take isn't saved");
        }
        set("undo", 1.f); run(5); q.settleControls();
        CHECK(value("undo") < 0.5f, "UNDO sets itself back to off");
        CHECK(q.ui.lsrc.load() > 64 && q.ui.undoRedo.load(), "UNDO brings the cleared take back");
        {   // a set saved in the moment CLEAR was on mustn't clear its own take when it opens
            set("clear", 1.f);
            juce::MemoryBlock saved; q.getStateInformation(saved);
            set("clear", 0.f);
            BroodProcessor r; r.setPlayConfigDetails(2, 2, rate, size); r.prepareToPlay(rate, size);
            r.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            juce::AudioBuffer<float> rb(2, size); rb.clear();
            for (int k = 0; k < 4; ++k) r.processBlock(rb, m);
            CHECK(r.apvts.getRawParameterValue("clear")->load() < 0.5f && r.ui.lsrc.load() > 64, "a saved CLEAR doesn't clear the reopened take");
        }
    }
    {   // A new take fits the window and moves the knobs to match
        BroodProcessor q;
        q.setPlayConfigDetails(2, 2, rate, size);
        q.prepareToPlay(rate, size);
        auto set = [&](const char* id, float v) { q.apvts.getParameter(id)->setValueNotifyingHost(v); };
        auto value = [&](const char* id) { return q.apvts.getParameter(id)->getValue(); };
        auto run = [&](int blocks) {
            for (int block = 0; block < blocks; ++block) {
                for (int i = 0; i < size; ++i) { const auto x = static_cast<float>(0.2 * std::sin(0.03 * (block * size + i))); b.setSample(0, i, x); b.setSample(1, i, x); }
                process(q, b, m);
            }
        };
        set("loopMode", 0.5f);   // TAPE
        set("length", 0.3f); set("loopEnd", 0.4f); set("start", 0.2f);
        run(200);
        set("loopSwitch", 1.f); run(1); set("loopSwitch", 0.f); run(1);
        CHECK(q.ui.winLen.load() == q.ui.lsrc.load(), "the new take plays all of itself straight away");
        run(3);
        CHECK(q.ui.winLen.load() == q.ui.lsrc.load(), "and still does before the knobs have moved");
        q.settleControls(); run(2);
        CHECK(value("start") > 0.999f && value("loopEnd") > 0.999f && value("length") > 0.999f, "START, END and LENGTH moved to the whole take");
        set("loopEnd", 0.5f); run(20);
        CHECK(q.ui.winLen.load() < q.ui.lsrc.load(), "and END trims it again afterwards");
    }
    {   // A transport take: saved with its kind, it reopens waiting for play and plays when the host does
        TestHead head;
        BroodProcessor q;
        q.setPlayConfigDetails(2, 2, rate, size);
        q.prepareToPlay(rate, size);
        q.setPlayHead(&head);
        auto run = [&](BroodProcessor& proc, int blocks) {
            for (int block = 0; block < blocks; ++block) {
                for (int i = 0; i < size; ++i) { const auto x = static_cast<float>(0.2 * std::sin(0.03 * (block * size + i))); b.setSample(0, i, x); b.setSample(1, i, x); }
                process(proc, b, m);
                if (head.playing) head.ppq += size / rate * head.bpm / 60.0;
            }
        };
        head.playing = true;
        run(q, 300);
        q.apvts.getParameter("loopSwitch")->setValueNotifyingHost(1.f); run(q, 1); q.apvts.getParameter("loopSwitch")->setValueNotifyingHost(0.f); run(q, 2);
        CHECK(q.ui.follows.load() && q.ui.loopLed.load() == static_cast<int>(brood::LoopLed::Play), "a take caught with the host playing follows it");
        head.playing = false; run(q, 100);
        CHECK(q.ui.parked.load() && q.ui.loopLed.load() == 0, "the host stops: it waits");
        juce::MemoryBlock saved; q.getStateInformation(saved);
        TestHead head2;
        BroodProcessor r; r.setPlayConfigDetails(2, 2, rate, size); r.prepareToPlay(rate, size); r.setPlayHead(&head2);
        r.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        juce::AudioBuffer<float> rb(2, size); rb.clear();
        for (int k = 0; k < 4; ++k) r.processBlock(rb, m);
        CHECK(r.ui.lsrc.load() > 64 && r.ui.follows.load() && r.ui.parked.load(), "reopened, it waits for play");
        head2.playing = true;
        for (int k = 0; k < 4; ++k) { r.processBlock(rb, m); head2.ppq += size / rate * 2.0; }
        CHECK(r.ui.loopLed.load() == static_cast<int>(brood::LoopLed::Play) && !r.ui.parked.load(), "and plays when the host does");
    }
    return failures ? 1 : 0;
}
