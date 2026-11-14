#pragma once
#include "PluginProcessor.h"
#include "BroodPresets.h"
#include "LoopTrim.h"
#include "kit/Bench.h"
#include "kit/Look.h"

// Pedal-style front panel. Repaints are limited to the small regions that change
// (LEDs, readout, meters, loop display): a full-window repaint at 30 Hz can starve
// the UI thread on high-DPI screens.
class BroodEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    // An audio file dropped anywhere on the window becomes the loop.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int, int) override;
    // The look, plus modulation: on a knob a ring in each source's colour showing the swing and
    // a dot where the sound has it now, on a slider a bar for the swing and a tab for now.
    struct Look final : kit::Look {
        Look(const kit::Stock& s, BroodProcessor& p) : kit::Look(s), proc(p) {}
        void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
        BroodProcessor& proc;
    };
    // A control modulation can reach. Alt-drag sets the selected source's depth on it,
    // right-click lists its routes.
    struct ModSlider final : juce::Slider {
        using juce::Slider::Slider;
        BroodEditor* editor = nullptr;
        int target = -1;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        bool keyPressed(const juce::KeyPress&) override;
        void removeRoutes();
        // The rings are handles, as in Silt: a press around the knob drags that source's
        // depth, and where there's no ring yet it makes one for the selected source.
        int sourceAtRing(juce::Point<float>) const;
        float depthAtStart = 0;
        int depthSource = -1, hoverSource = -1;
        bool settingDepth = false, menuPress = false;
    };
    // One source: its colour and number, its shape moving live, its route count and ON.
    // Click selects it, drag it onto a control to route it, right-click to clear it.
    struct ModChip final : juce::Component {
        ModChip(BroodEditor& e, int source) : editor(e), k(source) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        juce::Rectangle<int> onBox() const { return { getWidth() - 21, 4, 15, 15 }; }
        BroodEditor& editor;
        const int k;
        std::array<float, 64> trace {};
        int written = 0;
        bool dragging = false;
    };
    // The loop's GAIN, beside the waveform as Silt has it: drag up or down, Shift for fine,
    // double-click for 0 dB.
    struct GainBox final : juce::Component {
        GainBox(juce::RangedAudioParameter& p, std::function<float()> normalising);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        juce::ParameterAttachment attach;
        std::function<float()> normalise;
        float db = 0, dbRunning = 0, lastY = 0;
        bool dragging = false;
    };
    // Every route in one list, over the main sheet, each with its depth.
    struct RouteList final : juce::Component {
        explicit RouteList(BroodEditor&);
        void paint(juce::Graphics&) override;
        void resized() override;
        // A row stays while the list is open, even at 0%, so a depth can pass through
        // zero: only its X, CLEAR ALL or closing the list takes a row at 0 away.
        void rebuild(bool dropZeroes);
        struct Row { int k, t; std::unique_ptr<juce::Slider> depth; std::unique_ptr<juce::SliderParameterAttachment> att; std::unique_ptr<juce::TextButton> remove; };
        BroodEditor& editor;
        std::vector<Row> rows;
        juce::TextButton close { "CLOSE" }, clear { "CLEAR ALL" };
    };
    static juce::Colour modColour(int k) { return k == 0 ? juce::Colour(0xffb4452a) : juce::Colour(0xff2f6f8f); }
    juce::RangedAudioParameter* depthParam(int k, int t) const;
    void selectSource(int k);
    int selectedSource() const { return selectedMod; }
    void chipDragged(int k, juce::Point<int> screen);
    void chipDropped(int k, juce::Point<int> screen);
    void showRouteMenu(int target);
    void showDepth(int k, int t);
    void paintOverChildren(juce::Graphics&) override;

    explicit BroodEditor(BroodProcessor&);
    ~BroodEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;

    // ---- small components (public so the .cpp can define them) ----
    // Segmented selector for 2..N choices; the wet channel now has four modes.
    struct ThreeWay : juce::Component {
        // flipped: the labels read against the parameter's order, for a switch whose "on" belongs on the left.
        ThreeWay(juce::RangedAudioParameter& p, juce::StringArray labels, std::function<void()> onChange, bool flipped = false);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        juce::RangedAudioParameter& param;
        juce::StringArray labels;
        bool flip;
        juce::ParameterAttachment attach;
        int index = 0;
        std::function<void()> changed, clicked;
    };
    // A short list as a click-cycling cell: the arrows either side step through it, so
    // it stays a big target instead of a tiny menu.
    struct Stepper : juce::Component {
        Stepper(juce::RangedAudioParameter& p, juce::String caption, juce::StringArray names, std::function<void()> onChange);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        // The parameter is a normalised 0..1 float with `names.size()` stops, so
        // index in and index out both have to go through the same conversion.
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
        Footswitch(juce::RangedAudioParameter& p, juce::String n, juce::String key) : param(p), name(std::move(n)), keyName(std::move(key)) {}
        void setLit(bool on) { if (on != lit) { lit = on; repaint(); } }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override { press(true); }
        void mouseUp(const juce::MouseEvent&) override { press(false); }
        void press(bool down);
        juce::RangedAudioParameter& param;
        juce::String name, keyName;
        bool isDown = false, lit = false;
    };
    // The take, with START and END as markers you drag on it. The markers are the
    // only trim controls: a press grabs whichever marker is nearer, START keeps
    // the end where it is as it moves, and a double-click opens the window fully.
    struct LoopView : juce::Component {
        explicit LoopView(BroodProcessor& p) : proc(p) { setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); }
        void paint(juce::Graphics&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        // Marker positions in samples of the take; false while nothing is caught.
        bool markers(int& startAt, int& endAt) const;
        int nearestMarker(float x) const;   // 0 START, 1 END, -1 none (no take)
        int regionAt(float x) const;        // the same, or 2 inside the window: slide it
        void moveMarker(float x);
        BroodProcessor& proc;
        std::function<void(const juce::String&)> onTouch;
        juce::String windowText;
        std::array<float, BroodProcessor::kWaveBins> wave {};
        uint32_t version = 0;
        std::vector<std::pair<float, bool>> cols;   // the live view's pixels: peak, and whether it's kept
        int dragging = -1, hot = -1, anchorStart = 0, anchorEnd = 0;
        double grabOffset = 0;   // from where it was pressed to the thing it holds, in samples
    };

private:
    void timerCallback() override;
    int timerTick = 0;
    // `follows` marks a readout that belongs to the last touched parameter, so the
    // timer may keep its value current (automation, mode changes). Anything else —
    // a switch, the loop state, a patch name — is left alone until replaced.
    void showReadout(const juce::String& label, const juce::String& value, bool follows = false);
    friend struct ModSlider;
    void touch(const juce::String& id);
    // Shows only the tempo switches the current modes can use.
    void updateModeControls();

    BroodProcessor& proc;
    Look look;
    BroodPresets presets;
    kit::PresetBar presetBar;
    kit::PresetBrowser presetBrowser;
    kit::bench::Strip benchStrip;
    juce::ComboBox benchSource;   // what SEND sends: the loop, or bars of the output
    juce::int64 momentSent = 0, momentTaken = 0;
    double benchCheckedAt = 0;
    std::optional<kit::bench::Moment> waiting;
    void benchSend();
    void benchTake();
    bool fileHovering = false;
    void updateBench();

    struct Knob { juce::String id, label; std::unique_ptr<ModSlider> slider; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
    std::vector<Knob> knobs;
    std::unique_ptr<ThreeWay> wetMode, routing, loopMode;
    std::unique_ptr<Footswitch> fsWet, fsLoop;
    LoopView loopView;
    std::unique_ptr<GainBox> gainBox;
    juce::Rectangle<int> gainDesign;

    juce::ComboBox sourceBox;
    juce::TextButton loadButton { "LOAD FILE" };
    juce::Slider inputGain;
    ModSlider balance;
    // Loop edit: the capture ceiling. START and END are the markers on the waveform.
    juce::Slider memory;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> memoryAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> balanceAtt;
    // The switches panel: TRAILS, SMOOTH, FREEZE and CLASSIC in one row.
    struct Dip { std::unique_ptr<juce::ToggleButton> button; std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att; };
    std::array<Dip, 3> dips;
    std::unique_ptr<Stepper> character;
    // WORN after the loop: shown under WEAR, in the caption's place, only while WEAR is WORN.
    juce::ToggleButton wearPost { "AFTER LOOP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wearPostAtt;
    juce::Rectangle<int> wearPostDesign;
    int lastWorn = -1;
    juce::String knobName(size_t knob) const;
    std::unique_ptr<juce::FileChooser> chooser;
    // Modulation: a chip per source, and the selected source's controls under them.
    struct SourceControls {
        std::unique_ptr<Stepper> wave;
        juce::Slider rate;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAtt;
        std::array<std::unique_ptr<juce::ToggleButton>, 5> switches;   // UNI, BOUNCE, SWEEP, SYNC, HOLD
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 5> switchAtts;
    };
    std::array<SourceControls, 2> sources;
    std::unique_ptr<ModChip> chips[2];
    juce::TextButton routesButton { "ROUTES" };
    std::unique_ptr<RouteList> routeList;
    int selectedMod = 0, routeMaskSeen = -1;
    int dropTarget = -1;                    // a chip is over this control
    int dragSource = -1;
    juce::Point<float> dragAt;              // design coordinates
    int routeMask() const;
    int targetAt(juce::Point<int> screen, juce::Point<float>& design) const;
    juce::Rectangle<int> targetDesign(int t) const;
    // PRE: the wet side runs into the looper. It belongs to the wet column.
    std::unique_ptr<ThreeWay> wetPre;
    // RECORD, in 01 LOOP: CAPTURE or RECORD, and in RECORD the bars and whether they wait
    // for the bar line.
    std::unique_ptr<ThreeWay> recMode;
    std::unique_ptr<Stepper> recBars;
    juce::ToggleButton recAtTap { "AT TAP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> recAtTapAtt;
    juce::Rectangle<int> recModeDesign, recBarsDesign, recAtTapDesign;
    int lastRec = -1;
    juce::Rectangle<int> wetPreDesign;
    // Tempo switches that belong to one knob each, and only appear in the modes that
    // can use the beat: DELAY and WEAVE for the wet side, ENV for the loop.
    juce::ToggleButton wetSync, envSync, envSoft;
    juce::ToggleButton tapeDelay;   // TAPE ECHO, beside SYNC in DELAY
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tapeDelayAtt;
    juce::Rectangle<int> tapeDelayDesign;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetSyncAtt, envSyncAtt, envSoftAtt;
    juce::Rectangle<int> wetSyncDesign, envSyncDesign, envSoftDesign;
    int lastModeWet = -1, lastModeLoop = -1, lastPre = -1;
    // Under the hood: the five sliders the pedal hides behind both footswitches, and NOISE.
    // They live along the bottom of the window, so there is no second page.
    struct HoodControl { juce::Slider slider; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
    std::array<HoodControl, 6> hood;   // tone, fade, width, wet division, loop division, worn noise

    // All of these are in DESIGN coordinates (the 1200x820 panel); view maps them
    // into the real window. Paint and layout both work in that space.
    juce::Rectangle<int> mastheadArea, pedalArea, lcdArea, meterArea, loopArea;
    juce::Rectangle<int> knobDesign[6], wetModeDesign, routingDesign, loopModeDesign;
    juce::Rectangle<int> inputGainDesign, balanceDesign;
    juce::Rectangle<int> fsWetDesign, fsLoopDesign, memoryDesign, loopViewDesign;
    // Under each knob: its value, in ink while it's touched.
    juce::Rectangle<int> valueRect(int knob) const;
    int activeKnob = -1;
    juce::String activeKnobText;
    std::array<juce::String, 6> knobValues;
    void updateKnobValues();
    juce::Rectangle<int> hoodArea, hoodCellDesign[6], dipCellDesign[5];

    // Hover help. The readout at the top of the side column used to hold whatever
    // you last touched; now anything without an explanation of its own explains
    // itself there for as long as the mouse is over it. Rectangles are in design
    // coordinates and the timer hit-tests the real mouse against them, which needs
    // no per-control event plumbing and cannot steal a click.
    struct HoverItem { juce::Rectangle<int> rect; juce::String label, text; };
    std::vector<HoverItem> hoverHelp;
    juce::String hoverLabel, hoverText;
    void rebuildHoverHelp();
    // The four numbered panels of the side column, in design coordinates.
    juce::Rectangle<int> characterDesign, srcArea, rampArea, rampRateDesign, rampSwitchDesign[5], presetBarDesign, benchSourceDesign, routesDesign, chipDesign[2];
    juce::Rectangle<int> rampWaveDesign, sourceBoxDesign, loadButtonDesign;
    // The panel is drawn at a fixed design size and scaled to the window, so the
    // pixel-art proportions survive any window the host gives us.
    float viewScale = 1.f;
    juce::AffineTransform view;
    void repaintDesign(const juce::Rectangle<int>& designArea) { repaint(designArea.transformedBy(view)); }
    juce::String lcdLabel { "READY" }, lcdValue { "Tap L to loop, A for wet" }, lastTouched;
    bool readoutFollows = false;
    juce::String lcdMeta;
    bool blink = false;
    uint32_t lastCycles = 0; double flashUntil = 0; int lastLoopLed = 0;
    float meterIn = 0, meterOut = 0;
    bool keyA = false, keyL = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BroodEditor)
};
