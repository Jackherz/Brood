#pragma once

#include "Look.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <optional>

// The Bench: what passes between plug-ins, either way. A plug-in sends a moment and
// another takes it. A moment is a WAV and a note in a folder in Music, so it works
// whichever process or host each plug-in lives in, neither needs the other open, and
// everything sent stays there to use in the Finder.
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
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Bench");
}

// Where a new moment's audio goes: named for who sent it and when, so it reads in the Finder.
inline juce::File newAudioFile (const juce::String& name, const juce::File& folder = defaultFolder())
{
    folder.createDirectory();
    const auto when = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S");
    return folder.getNonexistentChildFile (juce::File::createLegalFileName (name + " " + when), ".wav", false);
}

// A finished WAV goes on the Bench. The note is written last, so a reader never finds
// half a moment. Each sender keeps one note: its latest.
inline bool publish (const juce::File& audio, double seconds, const juce::String& chip, const juce::String& name,
                     const juce::File& folder = defaultFolder())
{
    juce::XmlElement note ("MOMENT");
    note.setAttribute ("file", audio.getFileName());
    note.setAttribute ("chip", chip);
    note.setAttribute ("name", name);
    note.setAttribute ("seconds", seconds);
    note.setAttribute ("stamp", juce::String (juce::Time::currentTimeMillis()));
    return note.writeTo (folder.getChildFile ("." + juce::File::createLegalFileName (chip) + ".moment"));
}

inline bool send (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& chip, const juce::String& name,
                  const juce::File& folder = defaultFolder())
{
    if (audio.getNumSamples() == 0 || sampleRate <= 0.0 || ! folder.createDirectory())
        return false;
    const auto file = newAudioFile (name, folder);
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
    return publish (file, (double) audio.getNumSamples() / sampleRate, chip, name, folder);
}

// The newest moment from anyone but `myChip`.
inline std::optional<Moment> latest (const juce::String& myChip, const juce::File& folder = defaultFolder())
{
    std::optional<Moment> best;
    for (const auto& noteFile : folder.findChildFiles (juce::File::findFiles, false, ".*.moment"))
    {
        const auto xml = juce::XmlDocument::parse (noteFile);
        if (xml == nullptr || ! xml->hasTagName ("MOMENT") || xml->getStringAttribute ("chip") == myChip)
            continue;
        Moment m { folder.getChildFile (xml->getStringAttribute ("file")), xml->getStringAttribute ("chip"), xml->getStringAttribute ("name"),
                   xml->getDoubleAttribute ("seconds"), xml->getStringAttribute ("stamp").getLargeIntValue() };
        if (m.audio.existsAsFile() && (! best.has_value() || m.stamp > best->stamp))
            best = m;
    }
    return best;
}

// The strip along the bottom of every plug-in: its own name and state, what's waiting
// from elsewhere, and SEND, TAKE and the folder it all lives in.
class Strip final : public juce::Component
{
public:
    Strip (const Stock& stockToUse, juce::String chipText, juce::String nameText) : s (stockToUse), chip (std::move (chipText)), name (std::move (nameText))
    {
        sendKey.onClick = [this] { if (onSend) onSend(); };
        takeKey.onClick = [this] { if (onTake) onTake(); };
        folderKey.onClick = []
        {
            defaultFolder().createDirectory();
            defaultFolder().revealToUser();
        };
        folderKey.setTooltip ("Everything sent is kept here, to use in the Finder");
        addAndMakeVisible (sendKey);
        addAndMakeVisible (takeKey);
        addAndMakeVisible (folderKey);
    }

    std::function<void()> onSend, onTake;
    int reservedRight = 0;   // room left beside the keys for a control the plug-in adds

    void set (const juce::String& state, const juce::String& momentText, const juce::String& sendText, bool sendOn, bool takeOn)
    {
        if (state == stateText && momentText == moment && sendText == sendKey.getButtonText() && sendOn == sendKey.isEnabled() && takeOn == takeKey.isEnabled())
            return;
        stateText = state;
        moment = momentText;
        sendKey.setButtonText (sendText);
        sendKey.setEnabled (sendOn);
        takeKey.setEnabled (takeOn);
        repaint();
    }

    // Where the plug-in's own control goes, in this strip's coordinates.
    juce::Rectangle<int> reservedArea() const { return keysArea().withWidth (reservedRight - 8).withX (sendKey.getX() - reservedRight); }

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
        auto mine = row.removeFromLeft (row.getWidth() * 3 / 10).withTrimmedLeft (1);
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
        g.drawFittedText (stateText, text, juce::Justification::centredLeft, 1, 0.7f);

        g.setColour (s.ink);
        g.drawVerticalLine (row.getX(), (float) row.getY(), (float) row.getBottom());
        auto rest = row.reduced (12, 0).withRight (sendKey.getX() - reservedRight - 6);
        g.setColour (s.soft);
        g.drawFittedText (moment, rest, juce::Justification::centredLeft, 1, 0.7f);
    }

    void resized() override
    {
        auto r = keysArea();
        folderKey.setBounds (r.removeFromRight (keyWidth));
        r.removeFromRight (keyGap);
        takeKey.setBounds (r.removeFromRight (keyWidth));
        r.removeFromRight (keyGap);
        sendKey.setBounds (r.removeFromRight (keyWidth));
    }

private:
    static constexpr int keyWidth = 78, keyGap = 6;
    juce::Rectangle<int> keysArea() const { return getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3).reduced (6, 7); }

    const Stock& s;
    juce::String chip, name, stateText, moment;
    juce::TextButton sendKey { "SEND >" }, takeKey { "< TAKE" }, folderKey { "FOLDER" };
};
} // namespace kit::bench
