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
    // The pedal's hidden menu: on the hardware you hold both footswitches. Here it
    // is a panel over the front panel, so the normal layout stays uncluttered.
    struct HiddenPanel : juce::Component {
        explicit HiddenPanel(MoodProcessor& p);
        void paint(juce::Graphics&) override;
        void resized() override;
        MoodProcessor& proc;
        juce::Slider tone, fade, width, wetDiv, loopDiv;
        juce::ToggleButton classic;
        std::array<juce::Slider*, 5> sliders { &tone, &fade, &width, &wetDiv, &loopDiv };
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> sliderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> classicAtt;
        juce::Rectangle<int> rowDesign[5];
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
    juce::Slider memory, loopStart, loopEnd;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> memoryAtt, startAtt, endAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> balanceAtt;
    struct Dip { std::unique_ptr<juce::ToggleButton> button; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att; };
    std::vector<Dip> dips;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<HiddenPanel> hiddenPanel;
    juce::TextButton hiddenButton { "HIDDEN" };

    // All of these are in DESIGN coordinates (the 900x640 panel); view maps them
    // into the real window. Paint and layout both work in that space.
    juce::Rectangle<int> pedalArea, lcdArea, ledWet, ledLoop, meterArea, loopArea;
    juce::Rectangle<int> knobDesign[6], wetModeDesign, routingDesign, loopModeDesign;
    juce::Rectangle<int> inputGainDesign, balanceDesign;
    juce::Rectangle<int> fsWetDesign, fsLoopDesign, memoryDesign, loopStartDesign, loopEndDesign;
    juce::Rectangle<int> hiddenButtonDesign;
    bool hiddenOpen = false;
    // The panel is drawn at a fixed design size and scaled to the window, so the
    // pixel-art proportions survive any window the host gives us.
    float viewScale = 1.f;
    juce::AffineTransform view;
    void repaintDesign(const juce::Rectangle<int>& designArea) { repaint(designArea.transformedBy(view)); }
    juce::String lcdLabel { "READY" }, lcdValue { "Tap L to loop, A for wet" }, lastTouched;
    juce::String lcdMeta;
    bool ledWetOn = false, ledLoopOn = false, blink = false;
    uint32_t lastCycles = 0; double flashUntil = 0; int lastLoopLed = 0;
    float meterIn = 0, meterOut = 0;
    bool keyA = false, keyL = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoodEditor)
};
