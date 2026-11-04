#pragma once

#include "Look.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <optional>

// The Bench: what passes between plug-ins. An effect catches a moment and sends it; an
// instrument takes it. The moment is a WAV and a note in a shared folder, so it works
// whichever process or host each plug-in lives in, and neither needs the other open.
namespace kit::bench
{
struct Moment
{
    juce::File audio;
    juce::String fromChip, fromName;
    double seconds = 0.0;
    juce::int64 stamp = 0;   // when it was sent: a later one is a new moment
};

inline juce::File folderForTests;   // tests keep away from the real Bench

inline juce::File defaultFolder()
{
    if (folderForTests != juce::File())
        return folderForTests;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Bench");
}

// Writes the audio first and the note last, so a reader never finds half a moment.
inline bool send (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& chip, const juce::String& name,
                  const juce::File& folder = defaultFolder())
{
    if (audio.getNumSamples() == 0 || sampleRate <= 0.0 || ! folder.createDirectory())
        return false;
    const auto stamp = juce::Time::currentTimeMillis();
    const auto file = folder.getChildFile ("moment-" + juce::String (stamp) + ".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        if (stream == nullptr)
            return false;
        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")   // the replacement isn't in every JUCE this builds against
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, (unsigned int) audio.getNumChannels(), 24, {}, 0));
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE
        if (writer == nullptr)
            return false;
        stream.release();   // the writer owns it now
        if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
            return false;
    }
    juce::XmlElement note ("MOMENT");
    note.setAttribute ("file", file.getFileName());
    note.setAttribute ("chip", chip);
    note.setAttribute ("name", name);
    note.setAttribute ("seconds", (double) audio.getNumSamples() / sampleRate);
    note.setAttribute ("stamp", juce::String (stamp));
    if (! note.writeTo (folder.getChildFile ("moment.xml")))
        return false;
    // Only the latest is kept.
    for (const auto& old : folder.findChildFiles (juce::File::findFiles, false, "moment-*.wav"))
        if (old != file)
            old.deleteFile();
    return true;
}

inline std::optional<Moment> latest (const juce::File& folder = defaultFolder())
{
    const auto note = folder.getChildFile ("moment.xml");
    if (! note.existsAsFile())
        return std::nullopt;
    const auto xml = juce::XmlDocument::parse (note);
    if (xml == nullptr || ! xml->hasTagName ("MOMENT"))
        return std::nullopt;
    Moment m { folder.getChildFile (xml->getStringAttribute ("file")), xml->getStringAttribute ("chip"), xml->getStringAttribute ("name"),
               xml->getDoubleAttribute ("seconds"), xml->getStringAttribute ("stamp").getLargeIntValue() };
    if (! m.audio.existsAsFile())
        return std::nullopt;
    return m;
}

// The strip along the bottom of every plug-in: its own name and state, then the one
// thing it can do with a moment.
class Strip final : public juce::Component
{
public:
    Strip (const Stock& stockToUse, juce::String chipText, juce::String nameText) : s (stockToUse), chip (std::move (chipText)), name (std::move (nameText))
    {
        action.onClick = [this] { if (onAction) onAction(); };
        addAndMakeVisible (action);
    }

    std::function<void()> onAction;

    // What the left cell says about this plug-in, what the right cell says about the
    // moment, and the key that acts on it (none: no key).
    void set (const juce::String& state, const juce::String& momentText, const juce::String& key, bool enabled)
    {
        if (state == stateText && momentText == moment && key == action.getButtonText() && enabled == action.isEnabled())
            return;
        stateText = state;
        moment = momentText;
        action.setButtonText (key);
        action.setEnabled (enabled);
        action.setVisible (key.isNotEmpty());
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3);
        g.setColour (s.shadow);
        g.fillRect (r.translated (3, 3));
        g.setColour (s.head);
        g.fillRect (r);
        g.setColour (s.ink);
        g.drawRect (r, 1);

        auto row = r.reduced (1, 1);
        auto title = row.removeFromLeft (92);
        g.setFont (wordmark (9.0f));
        g.drawText ("THE", title.removeFromTop (title.getHeight() / 2).withTrimmedLeft (10), juce::Justification::centredBottom);
        g.drawText ("BENCH", title.withTrimmedLeft (10), juce::Justification::centredTop);

        g.drawVerticalLine (row.getX(), (float) row.getY(), (float) row.getBottom());
        auto mine = row.removeFromLeft (row.getWidth() * 4 / 10).withTrimmedLeft (1);
        g.setColour (s.panel);
        g.fillRect (mine);
        auto text = mine.reduced (12, 0);
        const auto font = label (18.0f);
        g.setFont (font);
        g.setColour (s.ink);
        const auto head = chip + "  " + name;
        g.drawText (head, text.removeFromLeft (textWidth (font, head) + 4), juce::Justification::centredLeft);
        text.removeFromLeft (10);
        g.setColour (s.soft);
        g.drawFittedText (stateText, text, juce::Justification::centredLeft, 1, 0.8f);

        g.setColour (s.ink);
        g.drawVerticalLine (row.getX(), (float) row.getY(), (float) row.getBottom());
        auto rest = row.reduced (12, 0);
        if (action.isVisible())
            rest.removeFromRight (action.getWidth() + 8);
        g.setColour (s.soft);
        g.drawFittedText (moment, rest, juce::Justification::centredLeft, 1, 0.8f);
    }

    void resized() override
    {
        const auto r = getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3).reduced (6, 7);
        action.setBounds (r.withLeft (r.getRight() - 96));
    }

private:
    const Stock& s;
    juce::String chip, name, stateText, moment;
    juce::TextButton action;
};
} // namespace kit::bench
