#pragma once
#include "PluginProcessor.h"

// Pedal-style front panel. Repaints are limited to the small regions that change
// (LEDs, readout, meters, loop display): a full-window repaint at 30 Hz can starve
// the UI thread on high-DPI screens.
class MoodEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit MoodEditor(MoodProcessor&);
    ~MoodEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

    // ---- small components (public so the .cpp can define them) ----
    struct KnobLook : juce::LookAndFeel_V4 {
        KnobLook();
        void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
        // The rest of the widget set, drawn to the same sheet: a hard offset
        // instead of a bevel, square corners, one rule for a track and a scale
        // printed under it.
        void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float sliderPos,
                              float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
        void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                          int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        // The panel has exactly two faces — the pixel font and the terminal font —
        // and every piece of text on it comes from one of them.
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
    };
    // Segmented selector for 2..N choices; the wet channel now has four modes.
    struct ThreeWay : juce::Component {
        ThreeWay(juce::RangedAudioParameter& p, juce::StringArray labels, std::function<void()> onChange);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        juce::RangedAudioParameter& param;
        juce::StringArray labels;
        juce::ParameterAttachment attach;
        int index = 0;
        std::function<void()> changed;
    };
    // One ramp destination as a click-cycling cell: the arrows either side step
    // through the list, so it stays a big target instead of a tiny menu.
    struct Stepper : juce::Component {
        Stepper(juce::RangedAudioParameter& p, juce::String caption, juce::StringArray names, std::function<void()> onChange);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        // The parameter is a normalised 0..1 float with `names.size()` stops, so
        // index in and index out both have to go through the same conversion. Using
        // the index as the value was the bug that left only OFF and the first entry
        // reachable, and pointed the engine at the wrong knob.
        int count() const { return juce::jmax(2, names.size()); }
        int arrowWidth() const { return juce::jlimit(12, 20, juce::jmax(1, getWidth() - 6) / 6); }
        void step(int by);
        juce::RangedAudioParameter& param;
        juce::String cap;
        juce::StringArray names;
        juce::ParameterAttachment attach;
        int index = 0;
        std::function<void()> changed;
    };
    struct Footswitch : juce::Component {
        Footswitch(juce::RangedAudioParameter& p, juce::String key) : param(p), keyName(std::move(key)) {}
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { press(true); }
        void mouseUp(const juce::MouseEvent&) override { press(false); }
        void press(bool down);
        juce::RangedAudioParameter& param;
        juce::String keyName;
        bool isDown = false;
    };
    struct LoopView : juce::Component {
        explicit LoopView(MoodProcessor& p) : proc(p) {}
        void paint(juce::Graphics&) override;
        MoodProcessor& proc;
        std::array<float, MoodProcessor::kWaveBins> wave {};
        uint32_t version = 0;
    };

private:
    void timerCallback() override;
    void showReadout(const juce::String& label, const juce::String& value);
    void touch(const juce::String& id);

    MoodProcessor& proc;
    KnobLook look;

    struct Knob { juce::String id, label; std::unique_ptr<juce::Slider> slider; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
    std::vector<Knob> knobs;
    std::unique_ptr<ThreeWay> wetMode, routing, loopMode;
    std::unique_ptr<Footswitch> fsWet, fsLoop;
    LoopView loopView;

    juce::ComboBox sourceBox;
    juce::ComboBox presetBox;
    juce::TextButton loadButton { "Load file..." };
    juce::Slider inputGain;
    juce::Slider balance;
    // Loop edit: capture ceiling, and the trim markers that decide what plays.
    juce::Slider memory, loopEnd, loopStart;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> memoryAtt, endAtt, startAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> balanceAtt;
    struct Dip { std::unique_ptr<juce::ToggleButton> button; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att; };
    std::vector<Dip> dips;
    std::unique_ptr<juce::FileChooser> chooser;
    // The ramp engine: two destinations, a shape, a rate and a polarity, plus the
    // BOUNCE and SWEEP dips that bend the shape.
    std::unique_ptr<Stepper> rampWet, rampLoop, rampWave;
    juce::Slider rampRate;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rampRateAtt;
    struct RampSwitch { std::unique_ptr<juce::ToggleButton> button; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att; };
    std::array<RampSwitch, 4> rampSwitches;   // UNI, BOUNCE, SWEEP, SYNC
    // Tempo switches that belong to one knob each, and only appear in the modes that
    // can use the beat: TAPE and WEAVE for the wet side, ENV for the loop.
    juce::ToggleButton wetSync, envSync;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetSyncAtt, envSyncAtt;
    juce::Rectangle<int> wetSyncDesign, envSyncDesign;
    int lastModeWet = -1, lastModeLoop = -1;
    // Under the hood: the five options and the CLASSIC switch the pedal hides
    // behind both footswitches. They live along the bottom of the window, so
    // there is no second page to dive into.
    struct HoodControl { juce::Slider slider; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
    std::array<HoodControl, 5> hood;   // tone, fade, width, wet division, loop division
    juce::ToggleButton hoodClassic, hoodFreeze;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> hoodClassicAtt, hoodFreezeAtt;

    // All of these are in DESIGN coordinates (the 900x640 panel); view maps them
    // into the real window. Paint and layout both work in that space.
    juce::Rectangle<int> pedalArea, lcdArea, ledWet, ledLoop, meterArea, loopArea;
    juce::Rectangle<int> knobDesign[6], wetModeDesign, routingDesign, loopModeDesign;
    juce::Rectangle<int> inputGainDesign, balanceDesign;
    juce::Rectangle<int> fsWetDesign, fsLoopDesign, memoryDesign, loopEndDesign, loopStartDesign;
    juce::Rectangle<int> hoodArea, hoodCellDesign[7], dipCellDesign[6];

    // Hover help. The readout at the top of the side column used to hold whatever
    // you last touched; now anything without an explanation of its own explains
    // itself there for as long as the mouse is over it. Rectangles are in design
    // coordinates and the timer hit-tests the real mouse against them, which needs
    // no per-control event plumbing and cannot steal a click.
    struct HoverItem { juce::Rectangle<int> rect; juce::String label, text; };
    std::vector<HoverItem> hoverHelp;
    juce::String hoverLabel, hoverText;
    void rebuildHoverHelp();
    // The four stacked panels of the side column, in design coordinates.
    juce::Rectangle<int> srcArea, dipArea, rampArea, rampRateDesign, rampSwitchDesign[4], presetBoxDesign;
    juce::Rectangle<int> rampWetDesign, rampLoopDesign, rampWaveDesign, sourceBoxDesign, loadButtonDesign;
    // The panel is drawn at a fixed design size and scaled to the window, so the
    // pixel-art proportions survive any window the host gives us.
    float viewScale = 1.f;
    juce::AffineTransform view;
    void repaintDesign(const juce::Rectangle<int>& designArea) { repaint(designArea.transformedBy(view)); }
    juce::String lcdLabel { "READY" }, lcdValue { "Tap L to loop, A for wet" }, lastTouched;
    // The status segment at the right of the title bar: what the host is actually
    // running us at, repainted only when that changes.
    juce::String barMeta;
    juce::String lcdMeta;
    bool ledWetOn = false, ledLoopOn = false, blink = false;
    uint32_t lastCycles = 0; double flashUntil = 0; int lastLoopLed = 0;
    float meterIn = 0, meterOut = 0;
    bool keyA = false, keyL = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoodEditor)
};
