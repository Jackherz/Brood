#include "PluginEditor.h"

#if BROOD_HAVE_FONTS
#include "BinaryData.h"
#endif

namespace {
// Monochrome, windowed pixel-art palette shared with the browser front panel.
const juce::Colour kBoard { 0xffe9e9e2 }, kPaper { 0xfffaf9f3 }, kInk { 0xff181815 };
const juce::Colour kMuted { 0xff77776f }, kLine { 0xffadada3 }, kPanel { 0xfff0efe8 };

juce::Font font(float size, bool = false) {
#if BROOD_HAVE_FONTS
    static auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize);
    return juce::Font(juce::FontOptions(face).withHeight(size));
#else
    return juce::Font(juce::FontOptions()
                          .withName(juce::Font::getDefaultMonospacedFontName())
                          .withHeight(size));
#endif
}
juce::Font mono(float size) { return font(size); }
juce::Font pixel(float size) {
#if BROOD_HAVE_FONTS
    static auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::PressStart2PRegular_ttf, BinaryData::PressStart2PRegular_ttfSize);
    return juce::Font(juce::FontOptions(face).withHeight(size));
#else
    return juce::Font(juce::FontOptions()
                          .withName(juce::Font::getDefaultMonospacedFontName())
                          .withHeight(size));
#endif
}

constexpr int W = 900, H = 640;
const char* kKnobSub[6][3] = {
    {"decay", "delay", "refresh"}, {"dry / fx", "dry / fx", "dry / fx"}, {"slice", "loop length", "slice"},
    {"smear", "feedback", "speed"}, {"sample rate", "sample rate", "sample rate"}, {"sensitivity", "speed", "stretch"},
};
}  // namespace

// ───────── look and feel ─────────
MoodEditor::KnobLook::KnobLook() {
    setColour(juce::ComboBox::backgroundColourId, kPaper);
    setColour(juce::ComboBox::outlineColourId, kInk);
    setColour(juce::ComboBox::textColourId, kInk);
    setColour(juce::ComboBox::arrowColourId, kInk);
    setColour(juce::PopupMenu::backgroundColourId, kPaper);
    setColour(juce::PopupMenu::textColourId, kInk);
    setColour(juce::TextButton::buttonColourId, kInk);
    setColour(juce::TextButton::textColourOffId, kPaper);
    setColour(juce::Slider::trackColourId, kInk);
    setColour(juce::Slider::backgroundColourId, kLine);
    setColour(juce::Slider::thumbColourId, kInk);
}

void MoodEditor::KnobLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& slider) {
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)).reduced(13);
    const float r = std::min(area.getWidth(), area.getHeight()) / 2;
    const auto c = area.getCentre();
    g.setColour(kInk);
    for (int i = 0; i <= 20; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / 20.f;
        const float r1 = r + (i % 5 == 0 ? 3.f : 5.f), r2 = r + 9.f;
        g.drawLine(c.x + r1 * std::sin(a), c.y - r1 * std::cos(a), c.x + r2 * std::sin(a), c.y - r2 * std::cos(a), i % 5 == 0 ? 1.7f : 1.f);
    }
    auto stepped = [](juce::Point<float> centre, float rad) {
        juce::Path p;
        const float a = rad * 0.54f, b = rad * 0.86f;
        p.startNewSubPath(centre.x-a, centre.y-rad);
        p.lineTo(centre.x+a, centre.y-rad); p.lineTo(centre.x+a, centre.y-b);
        p.lineTo(centre.x+b, centre.y-b); p.lineTo(centre.x+b, centre.y-a);
        p.lineTo(centre.x+rad, centre.y-a); p.lineTo(centre.x+rad, centre.y+a);
        p.lineTo(centre.x+b, centre.y+a); p.lineTo(centre.x+b, centre.y+b);
        p.lineTo(centre.x+a, centre.y+b); p.lineTo(centre.x+a, centre.y+rad);
        p.lineTo(centre.x-a, centre.y+rad); p.lineTo(centre.x-a, centre.y+b);
        p.lineTo(centre.x-b, centre.y+b); p.lineTo(centre.x-b, centre.y+a);
        p.lineTo(centre.x-rad, centre.y+a); p.lineTo(centre.x-rad, centre.y-a);
        p.lineTo(centre.x-b, centre.y-a); p.lineTo(centre.x-b, centre.y-b);
        p.lineTo(centre.x-a, centre.y-b); p.closeSubPath(); return p;
    };
    const bool dark = slider.getTitle().equalsIgnoreCase("CLOCK");
    auto shadow = stepped(c.translated(2, 3), r);
    g.setColour(kInk); g.fillPath(shadow);
    auto face = stepped(c, r);
    g.setColour(dark ? kInk : kPaper); g.fillPath(face);
    g.setColour(kInk); g.strokePath(face, juce::PathStrokeType(2));
    auto inner = stepped(c, r * 0.80f);
    g.setColour(dark ? juce::Colour(0xff292924) : juce::Colour(0xffecebe3)); g.fillPath(inner);
    g.setColour(dark ? kPaper : kInk); g.strokePath(inner, juce::PathStrokeType(1.2f));
    juce::Path pointer;
    pointer.addRectangle(-2.5f, -r * 0.72f, 5.f, r * 0.47f);
    pointer.addRectangle(-2.f, -2.f, 4.f, 4.f);
    const float angle = a0 + pos * (a1 - a0);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(c));
}

void MoodEditor::KnobLook::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool) {
    const auto bounds = b.getLocalBounds().toFloat();
    const auto sw = juce::Rectangle<float>(18, 32).withCentre({bounds.getCentreX(), 19});
    g.setColour(kInk); g.fillRect(sw);
    const auto handle = juce::Rectangle<float>(12, 11).withCentre({sw.getCentreX(), b.getToggleState() ? sw.getY()+8 : sw.getBottom()-8});
    g.setColour(kPaper); g.fillRect(handle);
    if (b.hasKeyboardFocus(true)) { g.setColour(kMuted); g.drawRect(sw.expanded(3), 1.5f); }
    g.setColour(kInk); g.setFont(font(14));
    g.drawText(b.getButtonText().toUpperCase(), bounds.withTop(40), juce::Justification::centredTop);
}

// ───────── 3-way toggle ─────────
MoodEditor::ThreeWay::ThreeWay(juce::RangedAudioParameter& p, juce::StringArray l, std::function<void()> onChange)
    : param(p), labels(std::move(l)), attach(p, [this](float v) { index = juce::roundToInt(v); repaint(); if (changed) changed(); }), changed(std::move(onChange)) {
    attach.sendInitialUpdate();
}
void MoodEditor::ThreeWay::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    const int row = bounds.getHeight() / 3;
    g.setColour(kPaper); g.fillRect(bounds);
    g.setColour(kInk); g.drawRect(bounds, 1);
    for (int i = 0; i < 3; ++i) {
        const auto r = juce::Rectangle<int>(1, 1 + i * row, bounds.getWidth() - 2, row - 1);
        if (i == index) { g.setColour(kInk); g.fillRect(r); }
        else { g.setColour(kLine); if (i > 0) g.drawHorizontalLine(i * row, 0.f, static_cast<float>(getWidth())); }
        g.setColour(i == index ? kPaper : kInk); g.setFont(font(15));
        g.drawText((i == index ? ">  " : "   ") + labels[i].toUpperCase(), r.reduced(5, 0), juce::Justification::centredLeft);
    }
}
void MoodEditor::ThreeWay::mouseDown(const juce::MouseEvent& e) {
    const int next = juce::jlimit(0, 2, e.y * 3 / juce::jmax(1, getHeight()));
    attach.setValueAsCompleteGesture(static_cast<float>(next));
}

// ───────── footswitch ─────────
void MoodEditor::Footswitch::press(bool down) {
    if (down == isDown) return;
    isDown = down;
    if (down) { param.beginChangeGesture(); param.setValueNotifyingHost(1.f); }
    else { param.setValueNotifyingHost(0.f); param.endChangeGesture(); }
    repaint();
}
void MoodEditor::Footswitch::paint(juce::Graphics& g) {
    auto r = getLocalBounds().reduced(3);
    g.setColour(kInk); g.fillRect(r.translated(3, 3));
    g.setColour(isDown ? kPanel : kPaper); g.fillRect(r);
    g.setColour(kInk); g.drawRect(r, 1);
    g.setFont(font(20)); g.drawText(keyName == "A" ? "WET" : "LOOP", r.removeFromTop(40), juce::Justification::centredBottom);
    g.setColour(kMuted); g.setFont(font(13));
    g.drawText("TAP / HOLD", r.removeFromTop(19), juce::Justification::centred);
    g.setColour(kInk); g.setFont(font(12));
    g.drawText("[ " + keyName + " ]", r, juce::Justification::centredTop);
}

// ───────── loop display ─────────
void MoodEditor::LoopView::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    g.setColour(kPaper); g.fillRect(b);
    g.setColour(kInk); g.drawRect(b, 1);
    const int lsrc = proc.ui.lsrc.load();
    if (lsrc <= 0) {
        g.setColour(kMuted); g.setFont(mono(15));
        g.drawText("LISTENING FOR LITTLE MOMENTS_", b, juce::Justification::centred);
        return;
    }
    const float ws = proc.ui.winStart.load() / static_cast<float>(lsrc);
    g.setColour(juce::Colour(0xffe4e3db)); g.fillRect(b.withWidth(ws * b.getWidth()));
    const int n = MoodProcessor::kWaveBins;
    const float bw = b.getWidth() / n, mid = b.getCentreY();
    for (int i = 0; i < n; ++i) {
        const float a = std::min(1.f, wave[static_cast<size_t>(i)] * 1.6f) * (b.getHeight() / 2 - 4);
        g.setColour(i / static_cast<float>(n) >= ws ? kInk : kLine);
        g.fillRect(b.getX() + i * bw, mid - a, std::max(1.f, bw - 0.4f), 2 * a + 1);
    }
    const float px = b.getX() + (proc.ui.winStart.load() + proc.ui.pos.load()) / lsrc * b.getWidth();
    g.setColour(kInk); g.fillRect(px - 1.f, b.getY(), 2.f, b.getHeight());
}

// ───────── editor ─────────
MoodEditor::MoodEditor(MoodProcessor& p) : AudioProcessorEditor(p), proc(p), loopView(p) {
    setLookAndFeel(&look);
    const std::pair<const char*, const char*> defs[] = {
        {"time", "TIME"}, {"mix", "MIX"}, {"length", "LENGTH"}, {"modWet", "MODIFY"}, {"clock", "CLOCK"}, {"modLoop", "MODIFY"}};
    for (const auto& [id, label] : defs) {
        Knob k { id, label, std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), nullptr };
        k.slider->setRotaryParameters(juce::degreesToRadians(-135.f), juce::degreesToRadians(135.f), true);
        k.slider->setMouseDragSensitivity(220);
        k.slider->setDoubleClickReturnValue(true, static_cast<double>(proc.apvts.getParameter(id)->getDefaultValue()));
        k.slider->setTitle(juce::String(label) + (juce::String(id) == "modWet" ? " (wet)" : juce::String(id) == "modLoop" ? " (loop)" : ""));
        const juce::String sid = id;
        k.slider->onValueChange = [this, sid] { touch(sid); };
        k.slider->onDragStart = [this, sid] { touch(sid); };
        addAndMakeVisible(*k.slider);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, *k.slider);
        knobs.push_back(std::move(k));
    }
    auto modeChanged = [this](const char* label, const char* id) {
        return [this, label, id] {
            auto* prm = proc.apvts.getParameter(id);
            if (lastTouched.isNotEmpty() || isVisible()) showReadout(label, prm->getCurrentValueAsText().toLowerCase());
            repaint(pedalArea);
        };
    };
    wetMode = std::make_unique<ThreeWay>(*proc.apvts.getParameter("wetMode"), juce::StringArray{"Reverb", "Delay", "Slip"}, modeChanged("WET MODE", "wetMode"));
    routing = std::make_unique<ThreeWay>(*proc.apvts.getParameter("routing"), juce::StringArray{"In", "In + Loop", "Loop"}, modeChanged("ROUTING", "routing"));
    loopMode = std::make_unique<ThreeWay>(*proc.apvts.getParameter("loopMode"), juce::StringArray{"Env", "Tape", "Stretch"}, modeChanged("LOOP MODE", "loopMode"));
    for (auto* t : {wetMode.get(), routing.get(), loopMode.get()}) addAndMakeVisible(*t);
    fsWet = std::make_unique<Footswitch>(*proc.apvts.getParameter("wetSwitch"), "A");
    fsLoop = std::make_unique<Footswitch>(*proc.apvts.getParameter("loopSwitch"), "L");
    addAndMakeVisible(*fsWet); addAndMakeVisible(*fsLoop);
    addAndMakeVisible(loopView);

    sourceBox.addItem("Live input", 1);
    sourceBox.addItem("Demo guitar", 2);
    sourceBox.addItem("Audio file", 3);
    sourceBox.setSelectedId(proc.source.load() + 1, juce::dontSendNotification);
    sourceBox.onChange = [this] {
        const int s = sourceBox.getSelectedId() - 1;
        if (s == MoodProcessor::File && proc.fileName().isEmpty()) { loadButton.triggerClick(); return; }
        proc.source = s;
        showReadout("INPUT", sourceBox.getText().toLowerCase());
    };
    addAndMakeVisible(sourceBox);
    loadButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Choose a guitar recording", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) { sourceBox.setSelectedId(proc.source.load() + 1, juce::dontSendNotification); return; }
            if (proc.loadFile(f)) { proc.source = MoodProcessor::File; sourceBox.setSelectedId(3, juce::dontSendNotification); showReadout("INPUT", f.getFileName()); }
            else showReadout("INPUT", "could not read that file");
        });
    };
    addAndMakeVisible(loadButton);
    inputGain.setSliderStyle(juce::Slider::LinearHorizontal);
    inputGain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    inputGain.setRange(0, 2); inputGain.setValue(proc.inputGain.load(), juce::dontSendNotification);
    inputGain.setTitle("Input level");
    inputGain.onValueChange = [this] { proc.inputGain = static_cast<float>(inputGain.getValue()); showReadout("INPUT LEVEL", juce::String(juce::Decibels::gainToDecibels(inputGain.getValue(), -60.0), 1) + " dB"); };
    addAndMakeVisible(inputGain);
    balance.setSliderStyle(juce::Slider::LinearHorizontal);
    balance.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    balance.setTitle("Balance");
    balance.onValueChange = [this] { touch("balance"); };
    addAndMakeVisible(balance);
    balanceAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "balance", balance);
    const std::pair<const char*, const char*> dipDefs[] = {{"trails", "Trails"}, {"dryKill", "Dry kill"}, {"latch", "Latch"}, {"spread", "Spread"}, {"smooth", "Smooth"}};
    for (const auto& [id, label] : dipDefs) {
        Dip d { std::make_unique<juce::ToggleButton>(label), nullptr };
        const juce::String lab = label;
        d.button->onClick = [this, lab, b = d.button.get()] { showReadout("DIP", lab.toLowerCase() + (b->getToggleState() ? " on" : " off")); };
        addAndMakeVisible(*d.button);
        d.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, *d.button);
        dips.push_back(std::move(d));
    }
    presetBox.addItem("A LITTLE DAYDREAM", 1);
    presetBox.addItem("SOFT FOCUS", 2);
    presetBox.addItem("GLASS ECHO", 3);
    presetBox.addItem("LITTLE GHOST", 4);
    presetBox.addItem("MELT AWAY", 5);
    presetBox.setSelectedId(1, juce::dontSendNotification);
    presetBox.setTitle("Factory patch");
    presetBox.onChange = [this] {
        // Normalized host parameter values, matching the browser factory patches.
        static constexpr float patches[5][9] = {
            { .55f, .50f, .72f, .58f, .90f, .66f, 0.f, .5f, .5f },
            { .73f, .56f, .65f, .83f, .80f, .62f, 0.f, .5f, 1.f },
            { .46f, .48f, .72f, .70f, .90f, .66f, .5f, 0.f, .5f },
            { .45f, .54f, .72f, .83f, .80f, .66f, 1.f, .5f, .5f },
            { .67f, .58f, .35f, .75f, .70f, .57f, 0.f, 1.f, 1.f },
        };
        static const char* ids[] = {"time", "mix", "length", "modWet", "clock", "modLoop", "wetMode", "routing", "loopMode"};
        const int which = juce::jlimit(0, 4, presetBox.getSelectedId() - 1);
        for (int i = 0; i < 9; ++i) if (auto* parameter = proc.apvts.getParameter(ids[i])) {
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost(patches[which][i]); parameter->endChangeGesture();
        }
        for (auto* id : {"trails", "dryKill", "latch", "spread", "smooth"}) if (auto* parameter = proc.apvts.getParameter(id)) parameter->setValueNotifyingHost(0.f);
        if (auto* parameter = proc.apvts.getParameter("balance")) parameter->setValueNotifyingHost(0.5f);
        showReadout("PATCH", presetBox.getText().toLowerCase());
    };
    addAndMakeVisible(presetBox);
    setWantsKeyboardFocus(true);
    setSize(W, H);
    startTimerHz(30);
}

MoodEditor::~MoodEditor() {
    stopTimer();
    fsWet->press(false); fsLoop->press(false);
    setLookAndFeel(nullptr);
}

void MoodEditor::touch(const juce::String& id) {
    lastTouched = id;
    const juce::String label = id == "modWet" ? "MODIFY (WET)" : id == "modLoop" ? "MODIFY (LOOP)" : id.toUpperCase();
    showReadout(label, proc.describe(id));
    if (id == "clock") repaint(pedalArea);
}

void MoodEditor::showReadout(const juce::String& label, const juce::String& value) {
    if (label == lcdLabel && value == lcdValue) return;
    lcdLabel = label; lcdValue = value;
    repaint(lcdArea);
}

void MoodEditor::resized() {
    pedalArea = {20, 20, 520, 600};
    const int cx[3] = {pedalArea.getX() + 95, pedalArea.getCentreX(), pedalArea.getRight() - 95};
    for (int i = 0; i < 6; ++i) {
        const int row = i / 3;
        knobs[static_cast<size_t>(i)].slider->setBounds(juce::Rectangle<int>(104, 104).withCentre({cx[i % 3], pedalArea.getY() + (row == 0 ? 172 : 394)}));
    }
    const int ty = pedalArea.getY() + 282;
    wetMode->setBounds(cx[0] - 48, ty - 36, 130, 72);
    routing->setBounds(cx[1] - 48, ty - 36, 130, 72);
    loopMode->setBounds(cx[2] - 48, ty - 36, 130, 72);
    const int fy = pedalArea.getBottom() - 52;
    fsWet->setBounds(juce::Rectangle<int>(84, 84).withCentre({cx[0] + 20, fy}));
    fsLoop->setBounds(juce::Rectangle<int>(84, 84).withCentre({cx[2] - 20, fy}));
    ledWet = juce::Rectangle<int>(14, 14).withCentre({cx[0] + 20, fy - 56});
    ledLoop = juce::Rectangle<int>(14, 14).withCentre({cx[2] - 20, fy - 56});

    auto side = juce::Rectangle<int>(560, 20, 320, 600);
    lcdArea = side.removeFromTop(92);
    side.removeFromTop(14);
    auto src = side.removeFromTop(118).reduced(14, 10);
    src.removeFromTop(18);
    auto r1 = src.removeFromTop(28);
    sourceBox.setBounds(r1.removeFromLeft(170));
    r1.removeFromLeft(8);
    loadButton.setBounds(r1);
    src.removeFromTop(8);
    inputGain.setBounds(src.removeFromTop(22).withTrimmedLeft(52));
    meterArea = src.removeFromTop(22).translated(0, 2);
    side.removeFromTop(14);
    auto lv = side.removeFromTop(118).reduced(14, 10);
    lv.removeFromTop(18);
    loopView.setBounds(lv);
    side.removeFromTop(14);
    auto dipArea = side.removeFromTop(132).reduced(14, 10);
    dipArea.removeFromTop(18);
    auto dipRow = dipArea.removeFromTop(64);
    const int dw = dipRow.getWidth() / 5;
    for (auto& d : dips) d.button->setBounds(dipRow.removeFromLeft(dw));
    dipArea.removeFromTop(4);
    balance.setBounds(dipArea.removeFromTop(24).withTrimmedLeft(66).withTrimmedRight(34));
    presetBox.setBounds(574, 556, 292, 26);
}

void MoodEditor::paint(juce::Graphics& g) {
    g.fillAll(kBoard);
    auto pa = pedalArea;
    g.setColour(kInk); g.fillRect(pa.translated(4, 4));
    g.setColour(kPaper); g.fillRect(pa);
    g.setColour(kInk); g.drawRect(pa, 2);
    auto bar = pa.removeFromTop(25);
    g.fillRect(bar);
    g.setColour(kPaper); g.setFont(font(15));
    g.drawText("*  MOOD.EXE   /   A SOUND EXPERIMENT", bar.reduced(11, 0), juce::Justification::centredLeft);
    for (int x = bar.getX() + 278; x < bar.getRight() - 10; x += 5)
        for (int y = bar.getY() + 7; y < bar.getBottom() - 5; y += 5) g.fillRect(x, y, 2, 2);
    g.setColour(kInk); g.setFont(pixel(30));
    g.drawText("MOOD.", pedalArea.getX() + 23, pedalArea.getY() + 42, 280, 42, juce::Justification::centredLeft);
    g.setFont(font(16));
    g.drawText("A LITTLE WORLD OF SOUND & SPACE", pedalArea.getX() + 24, pedalArea.getY() + 84, 325, 18, juce::Justification::centredLeft);
    g.setColour(kMuted); g.setFont(font(13));
    g.drawText("WET   +   MICRO-LOOPER", pedalArea.getRight() - 180, pedalArea.getY() + 49, 160, 36, juce::Justification::centredRight);
    g.setColour(kLine); g.drawHorizontalLine(pedalArea.getY() + 111, static_cast<float>(pedalArea.getX() + 14), static_cast<float>(pedalArea.getRight() - 14));

    const int wm = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load());
    const int lm = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load());
    for (size_t i = 0; i < knobs.size(); ++i) {
        const auto kb = knobs[i].slider->getBounds();
        g.setColour(kInk); g.setFont(font(17));
        g.drawText(knobs[i].label, kb.withY(kb.getY() - 21).withHeight(18), juce::Justification::centred);
        g.setColour(kMuted); g.setFont(font(13));
        const int mode = i == 5 || i == 2 ? lm : wm;
        juce::String sub = kKnobSub[i][mode];
        if (i == 3) sub += "  /  WET";
        if (i == 5) sub += "  /  LOOP";
        g.drawText(sub.toUpperCase(), kb.withY(kb.getBottom() - 4).withHeight(16).withSizeKeepingCentre(160, 16), juce::Justification::centred);
    }
    g.setColour(kInk); g.setFont(font(16));
    g.drawText("WET", ledWet.getX() - 63, ledWet.getY() - 1, 54, 18, juce::Justification::centredRight);
    g.drawText("LOOP", ledLoop.getRight() + 9, ledLoop.getY() - 1, 54, 18, juce::Justification::centredLeft);
    auto drawLed = [&](juce::Rectangle<int> r, bool on) {
        g.setColour(kInk); g.fillRect(r);
        g.setColour(kPaper); g.fillRect(r.reduced(on ? 4 : 2));
        if (on) { g.setColour(kInk); g.fillRect(r.reduced(5)); }
    };
    drawLed(ledWet, ledWetOn); drawLed(ledLoop, ledLoopOn);
    g.setColour(kMuted); g.setFont(font(13));
    g.drawText("ONE CLOCK. TWO WORLDS.", pedalArea.getCentreX() - 80, pedalArea.getBottom() - 50, 160, 20, juce::Justification::centred);

    auto panel = [&](juce::Rectangle<int> r, const char* title) {
        g.setColour(kPaper); g.fillRect(r);
        g.setColour(kInk); g.drawRect(r, 1);
        auto head = r.removeFromTop(24);
        g.setColour(kPanel); g.fillRect(head);
        g.setColour(kInk); g.drawHorizontalLine(head.getBottom(), static_cast<float>(head.getX()), static_cast<float>(head.getRight()));
        g.setFont(font(16)); g.drawText(title, head.reduced(10, 0), juce::Justification::centredLeft);
    };
    panel({560, 126, 320, 118}, "01  /  INPUT SOURCE");
    panel({560, 258, 320, 118}, "02  /  LOOP MEMORY");
    panel({560, 390, 320, 132}, "03  /  THE LITTLE SWITCHES");
    g.setColour(kInk); g.setFont(font(14));
    g.drawText("LEVEL", 574, inputGain.getY(), 50, inputGain.getHeight(), juce::Justification::centredLeft);
    g.drawText("BALANCE", 574, balance.getY(), 64, balance.getHeight(), juce::Justification::centredLeft);
    g.setColour(kMuted); g.setFont(font(13));
    g.drawText("WET", balance.getX() - 26, balance.getY(), 24, balance.getHeight(), juce::Justification::centredRight);
    g.drawText("LOOP", balance.getRight() + 4, balance.getY(), 34, balance.getHeight(), juce::Justification::centredLeft);
    g.setColour(kInk); g.setFont(font(15)); g.drawText("*  PATCH SELECTOR", 574, 532, 292, 18, juce::Justification::centredLeft);
    g.setColour(kMuted); g.setFont(font(13));
    g.drawFittedText("UNOFFICIAL MOOD-INSPIRED STUDY / NOT AFFILIATED WITH CHASE BLISS", 574, 590, 292, 30, juce::Justification::centredLeft, 2);

    auto meter = [&](int y, const char* name, float peak) {
        g.setColour(kMuted); g.setFont(mono(12)); g.drawText(name, meterArea.getX(), y, 32, 8, juce::Justification::centredLeft);
        const auto track = juce::Rectangle<float>(static_cast<float>(meterArea.getX() + 34), static_cast<float>(y + 1), static_cast<float>(meterArea.getWidth() - 34), 6);
        g.setColour(kPaper); g.fillRect(track); g.setColour(kInk); g.drawRect(track, 1);
        const float level = juce::jlimit(0.f, 1.f, (juce::Decibels::gainToDecibels(peak, -60.f) + 60.f) / 60.f);
        g.fillRect(track.withWidth(track.getWidth() * level));
    };
    meter(meterArea.getY(), "IN", meterIn); meter(meterArea.getY() + 11, "OUT", meterOut);

    g.setColour(kPaper); g.fillRect(lcdArea);
    g.setColour(kInk); g.drawRect(lcdArea, 2);
    auto lcdHead = lcdArea.withHeight(23); g.fillRect(lcdHead);
    g.setColour(kPaper); g.setFont(font(15)); g.drawText("*  THE SIGNAL  /  " + lcdLabel, lcdHead.reduced(10, 0), juce::Justification::centredLeft);
    auto lr = lcdArea.reduced(12, 8).withTrimmedTop(22);
    g.setColour(kInk); g.setFont(font(22)); g.drawFittedText(lcdValue.toUpperCase(), lr.removeFromTop(30), juce::Justification::centredLeft, 1);
    g.setColour(kMuted); g.setFont(font(14)); g.drawText(lcdMeta, lr, juce::Justification::centredLeft);
}

void MoodEditor::timerCallback() {
    auto& ui = proc.ui;
    const double now = juce::Time::getMillisecondCounterHiRes();
    blink = static_cast<int>(now / 160.0) % 2 == 0;
    const uint32_t cycles = ui.histCycles.load();
    if (cycles != lastCycles) { lastCycles = cycles; flashUntil = now + 120; }   // off: blink each new loop recorded
    const int led = ui.loopLed.load();   // 0 off, 1 play, 2 rec, 3 dub
    const bool wetOn = ui.wetOn.load() && !(ui.freeze.load() && !blink);
    const bool loopOn = led == 1 || ((led == 2 || led == 3) && blink) || (led == 0 && now < flashUntil);
    if (wetOn != ledWetOn) { ledWetOn = wetOn; repaint(ledWet.expanded(8)); }
    if (loopOn != ledLoopOn) { ledLoopOn = loopOn; repaint(ledLoop.expanded(8)); }
    if (led != lastLoopLed) {
        const char* msg[] = {nullptr, "playing", "recording...", "overdubbing..."};
        if (msg[led] != nullptr) showReadout("LOOP", msg[led]);
        else if (lastLoopLed == 1) showReadout("LOOP", "stopped");
        lastLoopLed = led;
    }
    const float mi = ui.peakIn.load(), mo = ui.peakOut.load();
    if (std::fabs(mi - meterIn) > 1e-3f || std::fabs(mo - meterOut) > 1e-3f) { meterIn = mi; meterOut = mo; repaint(meterArea.expanded(2)); }
    const int lsrc = ui.lsrc.load();
    const juce::String meta = juce::String(ui.clockHz.load() / 1000.f, 1) + " kHz    "
        + (lsrc > 0 ? "loop " + juce::String(ui.winLen.load() / ui.clockHz.load(), 2) + " s" : juce::String("no loop"))
        + "    cpu " + juce::String(ui.load.load(), 1) + "%";
    if (meta != lcdMeta) { lcdMeta = meta; repaint(lcdArea); }
    if (lastTouched.isNotEmpty() && lcdLabel != "LOOP" && lcdLabel != "DIP" && lcdLabel != "INPUT" && lcdLabel != "PATCH") {
        const juce::String v = proc.describe(lastTouched);   // follows automation and mode changes
        if (v != lcdValue && lcdLabel.isNotEmpty()) { lcdValue = v; repaint(lcdArea); }
    }
    const uint32_t ver = ui.waveVersion.load();
    if (ver != loopView.version && proc.copyWave(loopView.wave)) loopView.version = ver;
    loopView.repaint();
}

bool MoodEditor::keyPressed(const juce::KeyPress& k) {
    const auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    return c == 'a' || c == 'l';   // handled in keyStateChanged so holds work
}

bool MoodEditor::keyStateChanged(bool) {
    const bool a = juce::KeyPress::isKeyCurrentlyDown('a') || juce::KeyPress::isKeyCurrentlyDown('A');
    const bool l = juce::KeyPress::isKeyCurrentlyDown('l') || juce::KeyPress::isKeyCurrentlyDown('L');
    bool used = false;
    if (a != keyA) { keyA = a; fsWet->press(a); used = true; }
    if (l != keyL) { keyL = l; fsLoop->press(l); used = true; }
    return used;
}
