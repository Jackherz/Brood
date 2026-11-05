#include "PluginEditor.h"

namespace {
// The stock: cream paper and one ink. The look itself lives in the shared kit.
const kit::Stock& stock() { static const auto s = kit::cream(); return s; }
const juce::Colour kInk = stock().ink, kPaper = stock().panel, kMuted = stock().soft, kLine = stock().rule, kPanel = stock().head;

juce::Font font(float size) { return kit::label(size); }
juce::Font capFont() { return font(14); }
juce::Font labelFont() { return font(16); }
juce::Font textFont() { return font(18); }
constexpr int kSp1 = 4, kSp2 = 8;

constexpr int W = 1200, H = 820;

// One whispered sentence per knob per mode: enough to know which way to turn,
// never quite enough to stop wondering. The exact value appears in the display
// when you touch the knob.
const char* kWhisper[6][4] = {
    {"how long the room keeps breathing", "how far back the echo remembers",
     "how wide a slice of time is borrowed", "how far back the threads reach"},
    {"how much of what you hear is not here", "how much of what you hear is not here",
     "how much of what you hear is not here", "how much of what you hear is not here"},
    {"how much of the moment gets caught", "the size of the window that repeats",
     "how wide each grain is stretched", "how much of the moment gets caught"},
    {"how much the surface smears", "how much the echo wants to stay",
     "which way, and how fast, time slides", "long cloth, or short clipped stitches"},
    {"the heartbeat everything obeys", "the heartbeat everything obeys",
     "the heartbeat everything obeys", "the heartbeat everything obeys"},
    {"how softly it must speak to be caught", "which way the tape runs, and how fast",
     "how far the grains drift from now", "how softly it must speak to be caught"},
};

// Which knob listens to which mode: 0 = wet modes, 1 = loop modes, 2 = shared.
constexpr int kKnobMode[6] = {0, 2, 1, 0, 2, 1};

}  // namespace

// ───────── modulation on a knob ─────────
void BroodEditor::Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& slider) {
    kit::Look::drawRotarySlider(g, x, y, w, h, pos, a0, a1, slider);
    auto* mod = dynamic_cast<ModSlider*>(&slider);
    if (mod == nullptr || mod->target < 0 || mod->editor == nullptr) return;
    const auto box = juce::Rectangle<int>(x, y, w, h).toFloat();
    const auto c = box.getCentre();
    const float r = kit::Look::knobOuterRadius(box);
    const float from = juce::degreesToRadians(-135.f), span = juce::degreesToRadians(270.f);
    bool any = false;
    for (int k = 0; k < 2; ++k) {
        const float d = mod->editor->depthParam(k, mod->target)->convertFrom0to1(mod->editor->depthParam(k, mod->target)->getValue());
        if (std::fabs(d) < 0.005f) continue;
        any = true;
        const bool uni = proc.apvts.getRawParameterValue("mod" + juce::String(k + 1) + "Uni")->load() > 0.5f;
        const float lo = juce::jlimit(0.f, 1.f, uni ? juce::jmin(pos, pos + d) : pos - std::fabs(d));
        const float hi = juce::jlimit(0.f, 1.f, uni ? juce::jmax(pos, pos + d) : pos + std::fabs(d));
        juce::Path arc;
        const float radius = r - 1.5f + 4.f * static_cast<float>(k);
        arc.addCentredArc(c.x, c.y, radius, radius, 0.f, from + span * lo, from + span * hi, true);
        g.setColour(BroodEditor::modColour(k));
        g.strokePath(arc, juce::PathStrokeType(2.5f));
    }
    // Hovering the handle zone: where the selected source's ring would go.
    if (mod->hoverSource >= 0 && std::fabs(mod->editor->depthParam(mod->hoverSource, mod->target)->convertFrom0to1(mod->editor->depthParam(mod->hoverSource, mod->target)->getValue())) < 0.005f) {
        g.setColour(BroodEditor::modColour(mod->hoverSource).withAlpha(0.45f));
        g.drawEllipse(juce::Rectangle<float>(r * 2.f - 3.f, r * 2.f - 3.f).withCentre(c).expanded(4.f * static_cast<float>(mod->hoverSource)), 1.f);
    }
    if (any) {
        const float now = juce::jlimit(0.f, 1.f, proc.ui.effective[static_cast<size_t>(mod->target)].load());
        const auto at = c.getPointOnCircumference(r - 7.f, from + span * now);
        g.setColour(stock().ink);
        g.fillEllipse(juce::Rectangle<float>(5.f, 5.f).withCentre(at));
    }
}

int BroodEditor::ModSlider::sourceAtRing(juce::Point<float> p) const {
    if (target < 0 || editor == nullptr || getSliderStyle() != juce::Slider::RotaryHorizontalVerticalDrag) return -1;
    const auto box = getLocalBounds().toFloat();
    const float r = kit::Look::knobOuterRadius(box);
    const float face = juce::jmin(box.getWidth(), box.getHeight()) * 0.5f - 15.f * juce::jmin(box.getWidth(), box.getHeight()) / 104.f;
    const float d = p.getDistanceFrom(box.getCentre());
    if (d < face + 1.f || d > r + 7.f) return -1;
    auto routed = [&](int k) { return std::fabs(editor->depthParam(k, target)->convertFrom0to1(editor->depthParam(k, target)->getValue())) >= 0.005f; };
    if (routed(0) && routed(1)) return d >= r + 0.5f ? 1 : 0;   // each to its own ring
    if (routed(0) != routed(1) && editor->selectedSource() != (routed(0) ? 0 : 1)) return editor->selectedSource();
    return routed(0) ? 0 : routed(1) ? 1 : editor->selectedSource();
}
void BroodEditor::ModSlider::mouseMove(const juce::MouseEvent& e) {
    const int at = sourceAtRing(e.position);
    if (at != hoverSource) {
        hoverSource = at;
        setMouseCursor(at >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
    juce::Slider::mouseMove(e);
}
void BroodEditor::ModSlider::mouseExit(const juce::MouseEvent& e) {
    if (hoverSource != -1) { hoverSource = -1; setMouseCursor(juce::MouseCursor::NormalCursor); repaint(); }
    juce::Slider::mouseExit(e);
}
void BroodEditor::ModSlider::mouseDown(const juce::MouseEvent& e) {
    menuPress = e.mods.isPopupMenu();
    if (menuPress && target >= 0 && editor != nullptr) { editor->showRouteMenu(target); return; }
    const int ring = sourceAtRing(e.position);
    settingDepth = target >= 0 && editor != nullptr && (ring >= 0 || e.mods.isAltDown());
    if (settingDepth) {
        depthSource = ring >= 0 ? ring : editor->selectedSource();
        auto* prm = editor->depthParam(depthSource, target);
        depthAtStart = prm->convertFrom0to1(prm->getValue());
        prm->beginChangeGesture();
        editor->selectSource(depthSource);
        editor->showDepth(depthSource, target);
        return;
    }
    juce::Slider::mouseDown(e);
}
void BroodEditor::ModSlider::mouseDrag(const juce::MouseEvent& e) {
    if (menuPress) return;
    if (settingDepth) {
        auto* prm = editor->depthParam(depthSource, target);
        prm->setValueNotifyingHost(prm->convertTo0to1(juce::jlimit(-1.f, 1.f, depthAtStart - static_cast<float>(e.getDistanceFromDragStartY()) / (e.mods.isShiftDown() ? 600.f : 150.f))));
        editor->showDepth(depthSource, target);
        repaint();
        return;
    }
    juce::Slider::mouseDrag(e);
}
void BroodEditor::ModSlider::mouseUp(const juce::MouseEvent& e) {
    if (menuPress) { menuPress = false; return; }
    if (settingDepth) { editor->depthParam(depthSource, target)->endChangeGesture(); settingDepth = false; return; }
    juce::Slider::mouseUp(e);
}
void BroodEditor::ModSlider::mouseDoubleClick(const juce::MouseEvent& e) {
    // On a ring: that source comes off this control, the way a double-click resets anything
    // else. Alt-double-click anywhere does the same for the selected source.
    const int ring = sourceAtRing(e.position);
    const int source = ring >= 0 ? ring : e.mods.isAltDown() ? (editor != nullptr ? editor->selectedSource() : -1) : -1;
    if (source >= 0 && target >= 0 && editor != nullptr) {
        auto* prm = editor->depthParam(source, target);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(0.f));
        prm->endChangeGesture();
        hoverSource = -1;
        editor->showReadout("MOD " + juce::String(source + 1), juce::String(brood::kModTargetNames[target]).toLowerCase() + " removed");
        repaint();
        return;
    }
    juce::Slider::mouseDoubleClick(e);
}
// Delete or Backspace on a clicked knob: all of its modulation goes.
void BroodEditor::ModSlider::removeRoutes() {
    for (int k = 0; k < 2; ++k) {
        auto* prm = editor->depthParam(k, target);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(0.f));
        prm->endChangeGesture();
    }
    repaint();
}
bool BroodEditor::ModSlider::keyPressed(const juce::KeyPress& key) {
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && target >= 0 && editor != nullptr) { removeRoutes(); return true; }
    return juce::Slider::keyPressed(key);
}

// ───────── modulation chip ─────────
void BroodEditor::ModChip::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().withTrimmedRight(2).withTrimmedBottom(2);
    const bool on = editor.proc.apvts.getRawParameterValue("mod" + juce::String(k + 1) + "On")->load() > 0.5f;
    const bool selected = editor.selectedMod == k;
    const auto colour = modColour(k);
    g.setColour(kInk); g.fillRect(r.translated(2, 2));
    g.setColour(kPaper); g.fillRect(r);
    g.setColour(kInk); g.drawRect(r, selected ? 2 : 1);
    const auto number = juce::Rectangle<int>(5, 4, 15, 15);
    g.setColour(on ? colour : kLine); g.fillRect(number);
    g.setColour(kPaper); g.setFont(font(15));
    g.drawText(juce::String(k + 1), number, juce::Justification::centred);
    int routes = 0;
    for (int t = 0; t < brood::kModTargets; ++t)
        if (std::fabs(editor.depthParam(k, t)->convertFrom0to1(editor.depthParam(k, t)->getValue())) >= 0.005f) ++routes;
    g.setColour(kMuted); g.setFont(font(14));
    g.drawText(juce::String(routes) + (routes == 1 ? " ROUTE" : " ROUTES"), 24, 3, getWidth() - 48, 16, juce::Justification::centredLeft);
    // ON: lit like every other switch.
    const auto box = onBox();
    g.setColour(on ? stock().lit : kPaper); g.fillRect(box);
    g.setColour(kInk); g.drawRect(box, 1);
    // The last two seconds of what it's doing.
    const auto plot = juce::Rectangle<float>(6.f, 22.f, static_cast<float>(r.getWidth() - 12), static_cast<float>(r.getHeight() - 26));
    juce::Path path;
    const size_t n = trace.size();
    for (size_t i = 0; i < n; ++i) {
        const float v = trace[(static_cast<size_t>(written) + i) % n];
        const float px = plot.getX() + plot.getWidth() * static_cast<float>(i) / static_cast<float>(n - 1), py = plot.getBottom() - plot.getHeight() * v;
        if (i == 0) path.startNewSubPath(px, py); else path.lineTo(px, py);
    }
    g.setColour(on ? colour : kLine);
    g.strokePath(path, juce::PathStrokeType(1.5f));
}
void BroodEditor::ModChip::mouseDown(const juce::MouseEvent& e) {
    dragging = false;
    const auto id = "mod" + juce::String(k + 1) + "On";
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(1, "Clear this source's routes");
        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int chosen) {
            if (chosen == 1) for (int t = 0; t < brood::kModTargets; ++t) { auto* prm = editor.depthParam(k, t); prm->setValueNotifyingHost(prm->convertTo0to1(0.f)); }
        });
        return;
    }
    if (onBox().expanded(3).contains(e.getPosition())) {
        auto* prm = editor.proc.apvts.getParameter(id);
        prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->getValue() > 0.5f ? 0.f : 1.f); prm->endChangeGesture();
        repaint();
        return;
    }
    editor.selectSource(k);
}
void BroodEditor::ModChip::mouseDrag(const juce::MouseEvent& e) {
    if (e.getDistanceFromDragStart() > 4) dragging = true;
    if (dragging) editor.chipDragged(k, e.getScreenPosition());
}
void BroodEditor::ModChip::mouseUp(const juce::MouseEvent& e) {
    if (dragging) editor.chipDropped(k, e.getScreenPosition());
    dragging = false;
}

// ───────── the loop's gain ─────────
BroodEditor::GainBox::GainBox(juce::RangedAudioParameter& p)
    : attach(p, [this](float v) { db = v; repaint(); }), param(p) {
    attach.sendInitialUpdate();
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setTitle("Loop gain");
}
void BroodEditor::GainBox::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    g.setColour(kPaper); g.fillRect(r);
    g.setColour(kInk); g.drawRect(r, 1);
    g.setFont(font(14));
    const int rounded = juce::roundToInt(db * 2.f);
    g.drawText("GAIN " + juce::String(rounded > 0 ? "+" : "") + juce::String(static_cast<float>(rounded) * 0.5f, 1) + " DB", r, juce::Justification::centred);
}
void BroodEditor::GainBox::mouseDown(const juce::MouseEvent&) { dbAtStart = db; attach.beginGesture(); }
void BroodEditor::GainBox::mouseDrag(const juce::MouseEvent& e) {
    attach.setValueAsPartOfGesture(juce::jlimit(-24.f, 12.f, dbAtStart - static_cast<float>(e.getDistanceFromDragStartY()) * (e.mods.isShiftDown() ? 0.02f : 0.12f)));
}
void BroodEditor::GainBox::mouseUp(const juce::MouseEvent&) { attach.endGesture(); }
void BroodEditor::GainBox::mouseDoubleClick(const juce::MouseEvent&) { attach.setValueAsCompleteGesture(0.f); }

// ───────── the route list ─────────
BroodEditor::RouteList::RouteList(BroodEditor& e) : editor(e) {
    close.onClick = [this] { setVisible(false); };
    clear.onClick = [this] {
        for (int k = 0; k < 2; ++k) for (int t = 0; t < brood::kModTargets; ++t) { auto* prm = editor.depthParam(k, t); prm->setValueNotifyingHost(prm->convertTo0to1(0.f)); }
        rebuild(true);
    };
    addAndMakeVisible(close); addAndMakeVisible(clear);
    setVisible(false);
}
void BroodEditor::RouteList::rebuild(bool dropZeroes) {
    std::vector<std::pair<int, int>> keep;
    if (!dropZeroes) for (const auto& row : rows) keep.emplace_back(row.k, row.t);
    rows.clear();
    for (int k = 0; k < 2; ++k)
        for (int t = 0; t < brood::kModTargets; ++t) {
            auto* prm = editor.depthParam(k, t);
            const bool kept = std::find(keep.begin(), keep.end(), std::make_pair(k, t)) != keep.end();
            if (!kept && std::fabs(prm->convertFrom0to1(prm->getValue())) < 0.005f) continue;
            Row row { k, t, std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox), nullptr, std::make_unique<juce::TextButton>("X") };
            row.att = std::make_unique<juce::SliderParameterAttachment>(*prm, *row.depth);
            row.depth->onValueChange = [this] { repaint(); };
            row.remove->onClick = [this, prm] {
                prm->setValueNotifyingHost(prm->convertTo0to1(0.f));
                juce::MessageManager::callAsync([safe = juce::Component::SafePointer<RouteList>(this)] { if (safe != nullptr) safe->rebuild(true); });
            };
            addAndMakeVisible(*row.depth); addAndMakeVisible(*row.remove);
            rows.push_back(std::move(row));
        }
    resized();
    repaint();
}
void BroodEditor::RouteList::resized() {
    auto r = getLocalBounds().withTrimmedRight(3).withTrimmedBottom(3).withTrimmedTop(kit::panelHeadHeight).reduced(14, 10);
    auto foot = r.removeFromBottom(32);
    close.setBounds(foot.removeFromRight(90));
    foot.removeFromRight(8);
    clear.setBounds(foot.removeFromRight(110));
    for (auto& row : rows) {
        auto line = r.removeFromTop(32);
        row.remove->setBounds(line.removeFromRight(34).reduced(0, 2));
        line.removeFromRight(70);
        row.depth->setBounds(line.withLeft(line.getX() + 230));
    }
}
void BroodEditor::RouteList::paint(juce::Graphics& g) {
    kit::drawPanel(g, getLocalBounds().withTrimmedRight(3).withTrimmedBottom(3), stock(), "M", "ROUTES", juce::String(static_cast<int>(rows.size())) + (rows.size() == 1 ? " ROUTE" : " ROUTES"));
    auto r = getLocalBounds().withTrimmedRight(3).withTrimmedBottom(3).withTrimmedTop(kit::panelHeadHeight).reduced(14, 10);
    if (rows.empty()) {
        g.setColour(kMuted); g.setFont(font(19));
        g.drawText("NO ROUTES YET. DRAG A SOURCE ONTO A KNOB.", r, juce::Justification::centred);
        return;
    }
    for (auto& row : rows) {
        auto line = r.removeFromTop(32);
        const auto num = line.removeFromLeft(22).withSizeKeepingCentre(18, 18);
        g.setColour(modColour(row.k)); g.fillRect(num);
        g.setColour(kPaper); g.setFont(font(16));
        g.drawText(juce::String(row.k + 1), num, juce::Justification::centred);
        g.setColour(kInk); g.setFont(font(19));
        g.drawText(brood::kModTargetNames[row.t], line.withWidth(200).withTrimmedLeft(8), juce::Justification::centredLeft);
        const int depth = juce::roundToInt(row.depth->getValue() * 100.0);
        g.setColour(kMuted);
        g.drawText((depth > 0 ? "+" : "") + juce::String(depth) + "%", line.withLeft(line.getRight() - 100).withWidth(60), juce::Justification::centredRight);
    }
}

// ───────── segmented selector ─────────
BroodEditor::ThreeWay::ThreeWay(juce::RangedAudioParameter& p, juce::StringArray l, std::function<void()> onChange)
    : param(p), labels(std::move(l)), attach(p, [this](float v) { index = juce::roundToInt(v); repaint(); if (changed) changed(); }), changed(std::move(onChange)) {
    attach.sendInitialUpdate();
}
void BroodEditor::ThreeWay::paint(juce::Graphics& g) {
    // One plate across the column, the chosen cell a solid block.
    const auto bounds = getLocalBounds().withTrimmedRight(2).withTrimmedBottom(2);
    const int count = juce::jmax(1, labels.size());
    g.setColour(kInk); g.fillRect(bounds.translated(2, 2));
    g.setColour(kPaper); g.fillRect(bounds);
    g.setColour(kInk); g.drawRect(bounds, 1);
    for (int i = 0; i < count; ++i) {
        const int x0 = bounds.getX() + bounds.getWidth() * i / count, x1 = bounds.getX() + bounds.getWidth() * (i + 1) / count;
        const juce::Rectangle<int> r(x0, bounds.getY(), x1 - x0, bounds.getHeight());
        if (i == index) { g.setColour(kInk); g.fillRect(r); }
        else if (i > 0) { g.setColour(kInk); g.drawVerticalLine(x0, static_cast<float>(r.getY()), static_cast<float>(r.getBottom())); }
        g.setColour(i == index ? kPaper : kInk); g.setFont(font(16));
        g.drawFittedText(labels[i].toUpperCase(), r.reduced(2, 0), juce::Justification::centred, 1, 0.8f);
    }
}
void BroodEditor::ThreeWay::mouseDown(const juce::MouseEvent& e) {
    const int count = juce::jmax(1, labels.size());
    const int next = juce::jlimit(0, count - 1, e.x * count / juce::jmax(1, getWidth()));
    attach.setValueAsCompleteGesture(static_cast<float>(next));
}



// ───────── ramp destination stepper ─────────
BroodEditor::Stepper::Stepper(juce::RangedAudioParameter& p, juce::String caption, juce::StringArray l, std::function<void()> onChange)
    : param(p), cap(std::move(caption)), names(std::move(l)),
      attach(p, [this](float v) { index = brood::rampIndexOfValue(v, count()); repaint(); if (changed) changed(); }),
      changed(std::move(onChange)) {
    attach.sendInitialUpdate();
    setTitle(cap);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}
// A ramp cell is a button, so it is drawn like one: an ink frame with a hard
// shadow, arrow blocks either side and the chosen value in an inverted display.
// The whole cell steps; the arrows are there to say that it can.
void BroodEditor::Stepper::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    const bool hot = isMouseOver(true);
    auto body = r.reduced(2).withTrimmedRight(2).withTrimmedBottom(2);
    g.setColour(kInk); g.fillRect(body.translated(3, 3));                 // the hard shadow
    g.setColour(hot ? juce::Colour(0xfff1efe6) : kPaper); g.fillRect(body);
    g.setColour(kInk); g.drawRect(body, 2);
    auto inner = body.reduced(2, 2);
    const int arrow = arrowWidth();
    auto tri = [&](juce::Rectangle<int> zone, bool right) {
        g.setColour(hot ? kInk : juce::Colour(0xff8b8b80)); g.fillRect(zone);
        juce::Path t;
        const float w = 4.f, h = 7.f, cx = zone.getCentreX(), cy = zone.getCentreY();
        t.addTriangle(cx + (right ? -w / 2 : w / 2), cy - h / 2, cx + (right ? -w / 2 : w / 2), cy + h / 2, cx + (right ? w / 2 : -w / 2), cy);
        g.setColour(kPaper); g.fillPath(t);
    };
    tri(inner.removeFromLeft(arrow), false);
    tri(inner.removeFromRight(arrow), true);
    inner.removeFromLeft(3);
    g.setFont(labelFont()); g.setColour(kMuted);
    const int capW = juce::jmin(inner.getWidth() / 2, 44);
    g.drawFittedText(cap, inner.removeFromLeft(capW), juce::Justification::centredLeft, 1);
    auto value = inner.withTrimmedLeft(2);
    g.setColour(kPaper); g.fillRect(value);
    g.setColour(kInk); g.drawRect(value, 1);
    g.setFont(textFont());
    g.drawFittedText(index >= 0 && index < names.size() ? names[index] : juce::String(), value.reduced(2, 0), juce::Justification::centred, 1);
}
void BroodEditor::Stepper::step(int by) {
    const int next = ((index + by) % count() + count()) % count();
    attach.setValueAsCompleteGesture(static_cast<float>(brood::rampValueOfIndex(next, count())));
}
void BroodEditor::Stepper::mouseDown(const juce::MouseEvent& e) {
    // Anywhere on the cell steps forward; only the painted back-arrow steps back.
    step(e.x < arrowWidth() + 2 ? -1 : 1);
}

// ───────── footswitch ─────────
void BroodEditor::Footswitch::press(bool down) {
    if (down == isDown) return;
    isDown = down;
    if (down) { param.beginChangeGesture(); param.setValueNotifyingHost(1.f); }
    else { param.setValueNotifyingHost(0.f); param.endChangeGesture(); }
    repaint();
}
void BroodEditor::Footswitch::paint(juce::Graphics& g) {
    auto r = getLocalBounds().withTrimmedRight(3).withTrimmedBottom(3);
    const int travel = isDown ? 2 : 0;
    g.setColour(kInk); g.fillRect(r.translated(3, 3));
    r.translate(travel, travel);
    g.setColour(isDown ? kPanel : kPaper); g.fillRect(r);
    g.setColour(kInk); g.drawRect(r, 1);
    auto row = r.reduced(12, 0);
    // The light: an outlined square, solid while this side is on.
    const auto led = row.removeFromLeft(16).withSizeKeepingCentre(16, 16);
    g.setColour(kInk); g.fillRect(led);
    g.setColour(lit ? stock().lit : kPaper); g.fillRect(led.reduced(lit ? 1 : 2));
    row.removeFromLeft(10);
    g.setColour(kInk); g.setFont(font(24));
    g.drawText(name, row.removeFromLeft(62), juce::Justification::centredLeft);
    g.setFont(font(16));
    g.drawText("[ " + keyName + " ]", row.removeFromRight(36), juce::Justification::centredRight);
    g.setColour(kMuted);
    g.drawText("TAP / HOLD", row, juce::Justification::centred);
}


// ───────── loop display ─────────
bool BroodEditor::LoopView::markers(int& startAt, int& endAt) const {
    const int lsrc = proc.ui.lsrc.load();
    if (lsrc <= 0) return false;
    startAt = juce::jlimit(0, lsrc, proc.ui.winStart.load());
    endAt = juce::jlimit(startAt, lsrc, startAt + proc.ui.winLen.load());
    return true;
}

int BroodEditor::LoopView::nearestMarker(float x) const {
    int s = 0, e = 0;
    if (!markers(s, e)) return -1;
    const float scale = getWidth() / static_cast<float>(proc.ui.lsrc.load());
    const float xs = s * scale, xe = e * scale;
    // Equal distance (a window squeezed to a sliver): to the right of the pair is
    // END's side, to the left START's, so both stay reachable.
    const float toStart = std::fabs(x - xs), toEnd = std::fabs(x - xe);
    if (std::fabs(toStart - toEnd) < 0.5f) return x > xe ? 1 : 0;
    return toStart < toEnd ? 0 : 1;
}

void BroodEditor::LoopView::mouseMove(const juce::MouseEvent& e) {
    const int h = nearestMarker(static_cast<float>(e.x));
    if (h != hot) { hot = h; repaint(); }
}
void BroodEditor::LoopView::mouseExit(const juce::MouseEvent&) { if (dragging < 0 && hot != -1) { hot = -1; repaint(); } }

void BroodEditor::LoopView::mouseDown(const juce::MouseEvent& e) {
    int s = 0, end = 0;
    if (!markers(s, end)) return;
    dragging = nearestMarker(static_cast<float>(e.x));
    hot = dragging;
    anchorEnd = end;   // START moves under a fixed end, so remember where it was
    for (auto* id : {"start", "loopEnd"}) if (auto* prm = proc.apvts.getParameter(id)) prm->beginChangeGesture();
    moveMarker(static_cast<float>(e.x));
}
void BroodEditor::LoopView::mouseDrag(const juce::MouseEvent& e) { if (dragging >= 0) moveMarker(static_cast<float>(e.x)); }
void BroodEditor::LoopView::mouseUp(const juce::MouseEvent&) {
    if (dragging < 0) return;
    dragging = -1;
    for (auto* id : {"start", "loopEnd"}) if (auto* prm = proc.apvts.getParameter(id)) prm->endChangeGesture();
}
void BroodEditor::LoopView::mouseDoubleClick(const juce::MouseEvent&) {
    for (auto* id : {"start", "loopEnd"})
        if (auto* prm = proc.apvts.getParameter(id)) {
            prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->getDefaultValue()); prm->endChangeGesture();
        }
    if (onTouch) onTouch("loopEnd");
}

// The inverse of Core::loopWindow(): a marker position in samples of the take
// becomes START and END. ENV and STRETCH measure both against the whole take;
// TAPE measures END against LENGTH's span and START backwards from the tail.
void BroodEditor::LoopView::moveMarker(float x) {
    const int len = juce::jmax(64, proc.ui.lsrc.load());
    const int span = juce::jmax(64, proc.ui.winSpan.load());
    const bool tape = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load()) == 1;
    const double at = juce::jlimit(0.0, static_cast<double>(len), static_cast<double>(x) / juce::jmax(1, getWidth()) * len);
    auto* startP = proc.apvts.getParameter("start");
    auto* endP = proc.apvts.getParameter("loopEnd");
    if (startP == nullptr || endP == nullptr) return;
    const double minW = juce::jmax(64.0, 0.02 * span);   // END never closes the window
    int s = 0, e = 0;
    markers(s, e);
    if (dragging == 1) {
        const double w = juce::jlimit(minW, static_cast<double>(span), at - s);
        endP->setValueNotifyingHost(static_cast<float>(w / span));
    } else {
        const int room = tape ? len - span : len - 64;
        // START may not pass END, may not open the window wider than the span,
        // and in TAPE cannot leave the room LENGTH allows.
        const double lo = juce::jmax(0.0, anchorEnd - static_cast<double>(span));
        double xs = juce::jlimit(lo, juce::jmax(lo, anchorEnd - minW), at);
        if (tape) xs = juce::jmin(xs, static_cast<double>(juce::jmax(0, room)));
        const double where = room <= 0 ? 0.0 : tape ? 1.0 - xs / room : xs / room;
        startP->setValueNotifyingHost(static_cast<float>(juce::jlimit(0.0, 1.0, where)));
        endP->setValueNotifyingHost(static_cast<float>(juce::jlimit(0.02, 1.0, (anchorEnd - xs) / span)));
    }
    if (onTouch) onTouch(dragging == 1 ? "loopEnd" : "start");
}

void BroodEditor::LoopView::paint(juce::Graphics& g) {
    // Pick up a new peak envelope if the audio thread has published one (a try-lock:
    // if it is busy, the next frame gets it).
    const uint32_t ver = proc.ui.waveVersion.load();
    if (ver != version && proc.copyWave(wave)) version = ver;
    const auto b = getLocalBounds().toFloat();
    g.setColour(kPaper); g.fillRect(b);
    g.setColour(kInk); g.drawRect(b, 1);
    const int lsrc = proc.ui.lsrc.load();
    if (lsrc <= 0) {
        g.setColour(kMuted); g.setFont(font(15));
        g.drawText("LISTENING FOR LITTLE MOMENTS_", b, juce::Justification::centred);
        return;
    }
    int s = 0, e = 0;
    markers(s, e);
    const float xs = b.getWidth() * s / lsrc, xe = b.getWidth() * e / lsrc;
    // Everything outside the window is dimmed, so what plays is the only ink.
    g.setColour(juce::Colour(0xffdcdcd2));
    g.fillRect(b.withWidth(xs));
    g.fillRect(b.withLeft(xe));
    const int n = BroodProcessor::kWaveBins;
    const float bw = b.getWidth() / n, mid = b.getCentreY();
    const float gain = juce::Decibels::decibelsToGain(proc.apvts.getRawParameterValue("loopDb")->load());   // what you see is the level you hear
    for (int i = 0; i < n; ++i) {
        const float a = std::min(1.f, wave[static_cast<size_t>(i)] * 1.6f * gain) * (b.getHeight() / 2 - 14);
        const float x = i * bw;
        g.setColour(x >= xs && x < xe ? kInk : kLine);
        g.fillRect(x, mid - a, std::max(1.f, bw - 0.4f), 2 * a + 1);
    }
    // The play head.
    const float px = b.getWidth() * (proc.ui.winStart.load() + proc.ui.pos.load()) / lsrc;
    g.setColour(kMuted); g.fillRect(px - 0.5f, b.getY() + 12, 1.f, b.getHeight() - 24);
    // The markers: a rule the full height, and a tab that names it — START's tab
    // hangs inside the window from the top, END's from the bottom, so the two never
    // cover each other even when the window is a sliver.
    auto marker = [&](float x, bool isEnd, bool lit) {
        const float w = lit ? 3.f : 2.f;
        g.setColour(kInk);
        g.fillRect(juce::jlimit(0.f, b.getRight() - w, x - w * 0.5f), b.getY(), w, b.getHeight());
        const juce::String name = isEnd ? "END" : "START";
        g.setFont(capFont());
        const float tw = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), name) + 8);
        auto tab = juce::Rectangle<float>(tw, 12.f).withY(isEnd ? b.getBottom() - 12.f : b.getY());
        tab.setX(isEnd ? juce::jmax(0.f, x - tw) : juce::jmin(b.getRight() - tw, x));
        g.setColour(lit ? kInk : juce::Colour(0xff3a3a35)); g.fillRect(tab);
        g.setColour(kPaper); g.drawText(name, tab, juce::Justification::centred);
    };
    marker(xs, false, hot == 0 || dragging == 0);
    marker(xe, true, hot == 1 || dragging == 1);
}

// ───────── editor ─────────
BroodEditor::BroodEditor(BroodProcessor& p)
    : AudioProcessorEditor(p), proc(p), look(stock(), p), presets(p), presetBar(presets), presetBrowser(presets, stock()),
      benchStrip(stock(), "E-01", "BROOD", "BROOD"), loopView(p) {
    setLookAndFeel(&look);
    const std::pair<const char*, const char*> defs[] = {
        {"time", "TIME"}, {"mix", "MIX"}, {"length", "LENGTH"}, {"modWet", "MODIFY"}, {"clock", "CLOCK"}, {"modLoop", "MODIFY"}};
    for (const auto& [id, label] : defs) {
        Knob k { id, label, std::make_unique<ModSlider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), nullptr };
        k.slider->editor = this;
        k.slider->setWantsKeyboardFocus(true);
        for (int t = 0; t < brood::kModTargets; ++t) if (juce::String(id) == brood::kModTargetIds[t]) k.slider->target = t;
        k.slider->setRotaryParameters(juce::degreesToRadians(-135.f), juce::degreesToRadians(135.f), true);
        k.slider->setMouseDragSensitivity(220);
        k.slider->setDoubleClickReturnValue(true, static_cast<double>(proc.apvts.getParameter(id)->getDefaultValue()));
        k.slider->setTitle(juce::String(label) + (juce::String(id) == "modWet" ? " (wet)" : juce::String(id) == "modLoop" ? " (loop)" : ""));
        // The wet channel's two knobs are drawn as the black discs.
        k.slider->getProperties().set("dark", juce::String(id) == "time" || juce::String(id) == "modWet");
        const juce::String sid = id;
        k.slider->onValueChange = [this, sid] { touch(sid); };
        k.slider->setRepaintsOnMouseActivity(true);   // the marks turn to ink on hover
        k.slider->onDragStart = [this, sid] { touch(sid); };
        addAndMakeVisible(*k.slider);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, *k.slider);
        knobs.push_back(std::move(k));
    }
    auto modeChanged = [this](const char* label, const char* id) {
        return [this, label, id] {
            auto* prm = proc.apvts.getParameter(id);
            if (lastTouched.isNotEmpty() || isVisible()) showReadout(label, prm->getCurrentValueAsText().toLowerCase());
            repaintDesign(pedalArea);
        };
    };
    wetMode = std::make_unique<ThreeWay>(*proc.apvts.getParameter("wetMode"), juce::StringArray{"Reverb", "Delay", "Slip", "Weave"}, modeChanged("WET MODE", "wetMode"));
    routing = std::make_unique<ThreeWay>(*proc.apvts.getParameter("routing"), juce::StringArray{"In", "In + Loop", "Loop"}, modeChanged("ROUTING", "routing"));
    loopMode = std::make_unique<ThreeWay>(*proc.apvts.getParameter("loopMode"), juce::StringArray{"Env", "Tape", "Stretch"}, modeChanged("LOOP MODE", "loopMode"));
    for (auto* t : {wetMode.get(), routing.get(), loopMode.get()}) addAndMakeVisible(*t);
    fsWet = std::make_unique<Footswitch>(*proc.apvts.getParameter("wetSwitch"), "WET", "A");
    fsLoop = std::make_unique<Footswitch>(*proc.apvts.getParameter("loopSwitch"), "LOOP", "L");
    addAndMakeVisible(*fsWet); addAndMakeVisible(*fsLoop);
    addAndMakeVisible(loopView);
    gainBox = std::make_unique<GainBox>(*proc.apvts.getParameter("loopDb"));
    addAndMakeVisible(*gainBox);

    sourceBox.addItem("Live input", 1);
    sourceBox.addItem("Demo guitar", 2);
    sourceBox.addItem("Audio file", 3);
    sourceBox.setSelectedId(proc.source.load() + 1, juce::dontSendNotification);
    sourceBox.onChange = [this] {
        const int s = sourceBox.getSelectedId() - 1;
        if (s == BroodProcessor::File && proc.fileName().isEmpty()) { loadButton.triggerClick(); return; }
        proc.source = s;
        showReadout("INPUT", sourceBox.getText().toLowerCase());
    };
    addAndMakeVisible(sourceBox);
    loadButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Choose a guitar recording", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) { sourceBox.setSelectedId(proc.source.load() + 1, juce::dontSendNotification); return; }
            if (proc.loadFile(f)) { proc.source = BroodProcessor::File; sourceBox.setSelectedId(3, juce::dontSendNotification); showReadout("INPUT", f.getFileName()); }
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
    balance.editor = this;
    balance.target = 6;
    balance.setSliderStyle(juce::Slider::LinearHorizontal);
    balance.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    balance.setTitle("Balance");
    balance.onValueChange = [this] { touch("balance"); };
    addAndMakeVisible(balance);
    balanceAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "balance", balance);
    auto loopSlider = [this](juce::Slider& sl, const char* id, const char* name) {
        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        sl.setTitle(name);
        sl.onValueChange = [this, id] { touch(id); };
        addAndMakeVisible(sl);
    };
    loopSlider(memory, "memory", "Loop memory");
    memoryAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "memory", memory);
    loopView.onTouch = [this](const juce::String& id) { touch(id); loopView.repaint(); };
    // One row of four switches. DRY KILL was MIX at the top of its travel, LATCH is
    // what FREEZE does, SPREAD is WIDTH above the middle, and SYNC sits beside the
    // knob it governs. FREEZE and CLASSIC came up from under the hood: they are
    // switches, and the hood is now sliders only.
    struct DipDef { const char* id; const char* label; const char* on; const char* off; };
    static constexpr DipDef dipDefs[] = {
        {"trails", "Trails", "tails keep falling", "tails cut"},
        {"smooth", "Smooth", "clock glides", "clock steps"},
        {"freeze", "Freeze", "held, ringing", "moving"},
        {"classic", "Classic", "mk I manners", "as built"},
    };
    for (size_t i = 0; i < dips.size(); ++i) {
        const auto& def = dipDefs[i];
        auto& d = dips[i];
        d.button = std::make_unique<juce::ToggleButton>(def.label);
        d.button->getProperties().set("compact", true);
        d.button->onClick = [this, def, b = d.button.get()] {
            showReadout(juce::String(def.label).toUpperCase(), b->getToggleState() ? def.on : def.off);
        };
        addAndMakeVisible(*d.button);
        d.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, def.id, *d.button);
    }
    auto makeSyncSwitch = [this](juce::ToggleButton& button, const char* id, const char* readout,
                                 std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att) {
        button.setButtonText("SYNC");
        button.getProperties().set("plate", true);
        button.onClick = [this, sw = &button, name = juce::String(readout)] {
            showReadout(name, sw->getToggleState() ? "on the beat" : "free");
        };
        addChildComponent(button);             // shown only in the modes that use it
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, button);
    };
    makeSyncSwitch(wetSync, "sync", "SYNC / WET", wetSyncAtt);
    makeSyncSwitch(envSync, "envSync", "SYNC / ENV", envSyncAtt);
    envSoft.setButtonText("SOFT");
    envSoft.getProperties().set("plate", true);
    envSoft.onClick = [this] { showReadout("SOFT / ENV", envSoft.getToggleState() ? "slices fade" : "slices cut"); };
    addChildComponent(envSoft);
    envSoftAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "envSoft", envSoft);
    // PRE lives with the wet channel, under what it changes.
    wetPre.setButtonText("BEFORE THE LOOP");
    wetPre.onClick = [this] { showReadout("WET CHANNEL", wetPre.getToggleState() ? "before the loop: it gets recorded" : "after the loop: the loop stays clean"); };
    addAndMakeVisible(wetPre);
    wetPreAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "wetPre", wetPre);
    // Modulation: two sources, the selected one's controls showing.
    for (int k = 0; k < 2; ++k) {
        const juce::String m = "mod" + juce::String(k + 1), who = "MOD " + juce::String(k + 1);
        chips[k] = std::make_unique<ModChip>(*this, k);
        addAndMakeVisible(*chips[k]);
        auto& c = sources[static_cast<size_t>(k)];
        c.wave = std::make_unique<Stepper>(*proc.apvts.getParameter(m + "Wave"), "SHAPE", juce::StringArray{"TRI", "SQR", "SIN", "RND", "SMTH"},
            [this, who, m] { if (isVisible()) showReadout(who + " SHAPE", proc.apvts.getParameter(m + "Wave")->getCurrentValueAsText().toLowerCase()); });
        addChildComponent(*c.wave);
        c.rate.setSliderStyle(juce::Slider::LinearHorizontal);
        c.rate.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        c.rate.setTitle(who + " rate");
        c.rate.setDoubleClickReturnValue(true, static_cast<double>(proc.apvts.getParameter(m + "Rate")->getDefaultValue()));
        c.rate.onValueChange = [this, m] { touch(m + "Rate"); };
        addChildComponent(c.rate);
        c.rateAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, m + "Rate", c.rate);
        const std::pair<const char*, const char*> defs[] = {{"Uni", "UNI"}, {"Bounce", "BOUNCE"}, {"Sweep", "SWEEP"}, {"Sync", "SYNC"}, {"Hold", "HOLD"}};
        for (size_t i = 0; i < c.switches.size(); ++i) {
            const juce::String lab = defs[i].second;
            auto button = std::make_unique<juce::ToggleButton>(lab);
            button->getProperties().set("compact", true);
            button->onClick = [this, lab, who, b = button.get()] {
                showReadout(who + " " + lab, b->getToggleState() ? "on" : "off");
                repaintDesign(rampArea);   // the rate cell's caption changes with SYNC
            };
            addChildComponent(*button);
            c.switchAtts[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, m + defs[i].first, *button);
            c.switches[i] = std::move(button);
        }
    }
    routesButton.onClick = [this] {
        if (routeList->isVisible()) { routeList->setVisible(false); return; }
        routeList->rebuild(true);
        routeList->setVisible(true);
        routeList->toFront(false);
    };
    addAndMakeVisible(routesButton);
    routeList = std::make_unique<RouteList>(*this);
    addChildComponent(*routeList);
    selectSource(0);
    addAndMakeVisible(presetBar);
    presetBar.onBrowse = [this] { presetBrowser.isVisible() ? presetBrowser.close(true) : presetBrowser.open(); };
    presetBar.onStepped = [this] { showReadout("PATCH", presets.currentName()); };
    addChildComponent(presetBrowser);
    presetBrowser.onClosed = [this] { presetBar.refresh(); showReadout("PATCH", presets.currentName()); grabKeyboardFocus(); };
    // The Bench: what the loop is playing goes to the shared folder, for an instrument
    // in the range to take.
    addAndMakeVisible(benchStrip);
    benchStrip.reservedRight = 132;
    benchStrip.onSend = [this] { benchSend(); };
    benchStrip.onTake = [this] { benchTake(); };
    benchSource.addItem("THE LOOP", 1);
    benchSource.addItem("4 BARS OUT", 4);
    benchSource.addItem("8 BARS OUT", 8);
    benchSource.addItem("16 BARS OUT", 16);
    benchSource.setSelectedId(1, juce::dontSendNotification);
    benchSource.setTitle("What SEND sends");
    addAndMakeVisible(benchSource);

    // Under the hood, on the front page: the five options and the CLASSIC switch.
    const std::pair<const char*, const char*> hoodDefs[] = {
        {"tone", "TONE"}, {"fade", "FADE"}, {"width", "WIDTH"}, {"wetDiv", "WET DIV"}, {"loopDiv", "LOOP DIV"}};
    for (size_t i = 0; i < hood.size(); ++i) {
        auto& h = hood[i];
        h.slider.setSliderStyle(juce::Slider::LinearHorizontal);
        h.slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        h.slider.setTitle(hoodDefs[i].second);
        h.slider.setDoubleClickReturnValue(true, static_cast<double>(proc.apvts.getParameter(hoodDefs[i].first)->getDefaultValue()));
        const juce::String id = hoodDefs[i].first;
        h.slider.onValueChange = [this, id] { touch(id); };
        addAndMakeVisible(h.slider);
        h.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, hoodDefs[i].first, h.slider);
    }
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(W * 0.7f), juce::roundToInt(H * 0.7f), 2000, 1367);   // 0.7x to about 1.67x of the design panel
    getConstrainer()->setFixedAspectRatio(static_cast<double>(W) / H);
    setSize(W, H);
    presetBar.refresh();
    updateModeControls();
    updateBench();
    startTimerHz(30);
}

BroodEditor::~BroodEditor() {
    stopTimer();
    fsWet->press(false); fsLoop->press(false);
    setLookAndFeel(nullptr);
}

void BroodEditor::touch(const juce::String& id) {
    lastTouched = id;
    // The readout's own names for the parameters, so it never shows a raw id.
    static const std::pair<const char*, const char*> names[] = {
        {"modWet", "MODIFY (WET)"}, {"modLoop", "MODIFY (LOOP)"}, {"loopEnd", "END"}, {"start", "START"},
        {"balance", "WET / LOOP"}, {"mod1Rate", "MOD 1 RATE"}, {"mod2Rate", "MOD 2 RATE"}, {"wetDiv", "WET DIV"}, {"loopDiv", "LOOP DIV"},
    };
    juce::String label = id.toUpperCase();
    for (const auto& [key, name] : names) if (id == key) label = name;
    showReadout(label, proc.describe(id), true);
    if (id == "clock") repaintDesign(pedalArea);
    // The small readouts beside the trim and hood sliders are drawn by this
    // editor rather than by the control, so they need a repaint to follow it.
    if (id == "memory" || id == "start" || id == "loopEnd" || id == "balance") repaintDesign(loopArea);
    if (id == "mod1Rate" || id == "mod2Rate") repaintDesign(rampArea);
    if (id == "tone" || id == "fade" || id == "width" || id == "wetDiv" || id == "loopDiv") repaintDesign(hoodArea);
}

void BroodEditor::showReadout(const juce::String& label, const juce::String& value, bool follows) {
    readoutFollows = follows;
    if (label == lcdLabel && value == lcdValue) return;
    lcdLabel = label; lcdValue = value;
    repaintDesign(lcdArea);
}

juce::Rectangle<int> BroodEditor::whisperRect(int knob) const {
    const auto kb = knobDesign[juce::jlimit(0, 5, knob)];
    // Row one leaves room for a SYNC switch between the knob and its line.
    return { kb.getCentreX() - 118, knob < 3 ? kb.getBottom() + 18 : kb.getBottom() + 2, 236, 34 };
}

juce::RangedAudioParameter* BroodEditor::depthParam(int k, int t) const {
    return proc.apvts.getParameter("mod" + juce::String(k + 1) + "_" + brood::kModTargetIds[t]);
}

void BroodEditor::selectSource(int k) {
    selectedMod = k;
    for (int i = 0; i < 2; ++i) {
        auto& c = sources[static_cast<size_t>(i)];
        c.wave->setVisible(i == k);
        c.rate.setVisible(i == k);
        for (auto& b : c.switches) b->setVisible(i == k);
        chips[i]->repaint();
    }
    repaintDesign(rampArea);
    if (isVisible()) rebuildHoverHelp();
}

juce::Rectangle<int> BroodEditor::targetDesign(int t) const {
    if (t == 6) return balanceDesign.expanded(4, 6);
    static constexpr int knobOf[] = {0, 1, 2, 3, 4, 5};
    return knobDesign[knobOf[t]];
}

int BroodEditor::targetAt(juce::Point<int> screen, juce::Point<float>& design) const {
    const auto local = getLocalPoint(nullptr, screen).toFloat();
    float x = local.x, y = local.y;
    view.inverted().transformPoint(x, y);
    design = {x, y};
    // Knobs in this order hold what kModTargetIds lists, so look each one up by id.
    for (int t = 0; t < brood::kModTargets; ++t) {
        juce::Rectangle<int> rect = t == 6 ? balanceDesign.expanded(4, 6) : juce::Rectangle<int>();
        if (t < 6) for (size_t i = 0; i < knobs.size(); ++i) if (knobs[i].id == brood::kModTargetIds[t]) rect = knobDesign[i];
        if (rect.toFloat().contains(design)) return t;
    }
    return -1;
}

void BroodEditor::chipDragged(int k, juce::Point<int> screen) {
    dragSource = k;
    dropTarget = targetAt(screen, dragAt);
    repaint();
}

void BroodEditor::chipDropped(int k, juce::Point<int> screen) {
    juce::Point<float> at;
    const int t = targetAt(screen, at);
    dragSource = dropTarget = -1;
    repaint();
    if (t < 0) return;
    auto* prm = depthParam(k, t);
    if (std::fabs(prm->convertFrom0to1(prm->getValue())) < 0.005f) {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(0.5f));
        prm->endChangeGesture();
    }
    selectSource(k);
    showDepth(k, t);
    if (routeList->isVisible()) routeList->rebuild(false);
}

void BroodEditor::showDepth(int k, int t) {
    auto* prm = depthParam(k, t);
    const int depth = juce::roundToInt(prm->convertFrom0to1(prm->getValue()) * 100.f);
    showReadout("MOD " + juce::String(k + 1) + " > " + brood::kModTargetNames[t], (depth > 0 ? "+" : "") + juce::String(depth) + "%");
}

void BroodEditor::showRouteMenu(int target) {
    juce::PopupMenu menu;
    menu.addSectionHeader(brood::kModTargetNames[target]);
    bool any = false;
    for (int k = 0; k < 2; ++k) {
        auto* prm = depthParam(k, target);
        const float d = prm->convertFrom0to1(prm->getValue());
        if (std::fabs(d) < 0.005f) continue;
        any = true;
        menu.addItem(1 + k, "Remove MOD " + juce::String(k + 1) + " (" + juce::String(juce::roundToInt(d * 100.f)) + "%)");
    }
    if (!any) menu.addItem(9, "No routes. Drag a source onto this control.", false);
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, target](int chosen) {
        if (chosen < 1 || chosen > 2) return;
        auto* prm = depthParam(chosen - 1, target);
        prm->setValueNotifyingHost(prm->convertTo0to1(0.f));
        repaint();
    });
}

int BroodEditor::routeMask() const {
    int mask = 0;
    for (int k = 0; k < 2; ++k)
        for (int t = 0; t < brood::kModTargets; ++t)
            if (std::fabs(depthParam(k, t)->convertFrom0to1(depthParam(k, t)->getValue())) >= 0.005f) mask |= 1 << (k * brood::kModTargets + t);
    return mask;
}

// While a chip is being dragged: the control it would land on, and a tag at the mouse.
void BroodEditor::paintOverChildren(juce::Graphics& g) {
    if (dragSource < 0) return;
    g.addTransform(view);
    const auto colour = modColour(dragSource);
    if (dropTarget >= 0) {
        g.setColour(colour);
        g.drawRect(dropTarget == 6 ? balanceDesign.expanded(4, 6) : [&] {
            juce::Rectangle<int> rect;
            for (size_t i = 0; i < knobs.size(); ++i) if (knobs[i].id == brood::kModTargetIds[dropTarget]) rect = knobDesign[i];
            return rect;
        }(), 2);
    }
    const auto tag = juce::Rectangle<float>(58.f, 20.f).withCentre(dragAt.translated(0.f, -16.f));
    g.setColour(colour); g.fillRect(tag);
    g.setColour(kPaper); g.setFont(font(16));
    g.drawText("MOD " + juce::String(dragSource + 1), tag, juce::Justification::centred);
}

void BroodEditor::benchSend() {
    using Capture = BroodProcessor::OutputCapture;
    if (proc.outputCaptureState() == Capture::waiting || proc.outputCaptureState() == Capture::recording) {
        proc.cancelOutputCapture();
        showReadout("THE BENCH", "stopped, nothing sent");
        return;
    }
    if (benchSource.getSelectedId() > 1) {
        proc.armOutputCapture(benchSource.getSelectedId());
        return;
    }
    juce::AudioBuffer<float> moment;
    double rate = 0;
    const bool sent = proc.copyMoment(moment, rate) && kit::bench::send(moment, rate, "E-01", "BROOD", "BROOD");
    showReadout("THE BENCH", sent ? "moment sent" : "could not send it");
    if (sent) momentSent = juce::Time::currentTimeMillis();
}

void BroodEditor::benchTake() {
    if (!waiting.has_value()) return;
    const bool taken = proc.takeMoment(waiting->audio);
    showReadout("THE BENCH", taken ? "it's the loop now" : "could not read it");
    if (taken) { momentTaken = waiting->stamp; benchCheckedAt = 0; }
}

void BroodEditor::updateBench() {
    using Capture = BroodProcessor::OutputCapture;
    // The bars are in: send them from here, off the audio thread.
    if (proc.outputCaptureState() == Capture::done) {
        juce::AudioBuffer<float> moment;
        double rate = 0;
        const bool sent = proc.takeOutputCapture(moment, rate) && kit::bench::send(moment, rate, "E-01", "BROOD", "BROOD");
        showReadout("THE BENCH", sent ? "bars sent" : "could not send them");
        if (sent) momentSent = juce::Time::currentTimeMillis();
    }
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - benchCheckedAt >= 1000.0) { benchCheckedAt = now; waiting = kit::bench::latest("E-01"); }
    const bool fresh = waiting.has_value() && waiting->stamp != momentTaken;
    const int caught = proc.ui.lsrc.load() >= 64 ? 1 : 0;
    const juce::String state = caught == 1 ? juce::String(proc.ui.winLen.load() / proc.ui.clockHz.load(), 2) + " S CAUGHT" : juce::String("LISTENING");
    const auto capture = proc.outputCaptureState();
    const bool bars = benchSource.getSelectedId() > 1;
    const juce::String fromThere = fresh ? "FROM " + waiting->fromChip + " " + waiting->fromName + " / " + juce::String(waiting->seconds, 2) + " S" : juce::String();
    if (capture == Capture::waiting) benchStrip.set(state, "WAITING FOR THE NEXT BAR", "STOP", true, fresh);
    else if (capture == Capture::recording) benchStrip.set(state, "RECORDING " + juce::String(juce::roundToInt(proc.outputCaptureProgress() * 100.f)) + "%", "STOP", true, fresh);
    else benchStrip.set(state, fresh ? fromThere : momentSent != 0 ? "SENT: AN INSTRUMENT CAN TAKE IT" : "NOTHING WAITING", "SEND >", bars || caught == 1, fresh);
}

void BroodEditor::updateModeControls() {
    const int wm = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load());
    const int lm = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load());
    const int pre = proc.apvts.getRawParameterValue("wetPre")->load() > 0.5f ? 1 : 0;
    if (wm == lastModeWet && lm == lastModeLoop && pre == lastPre) return;
    lastModeWet = wm; lastModeLoop = lm; lastPre = pre;
    // With the wet side before the loop it hears only the input, so ROUTING stands aside.
    routing->setEnabled(pre == 0);
    routing->setAlpha(pre == 0 ? 1.f : 0.35f);
    wetSync.setVisible(wm == 1 || wm == 3);
    envSync.setVisible(lm == 0);
    envSoft.setVisible(lm == 0);
    rebuildHoverHelp();
    repaintDesign(pedalArea);
}

void BroodEditor::resized() {
    // Everything is laid out in design coordinates (1200x820) and mapped into the
    // window by one transform, so the panel scales instead of reflowing and no
    // text can slide over its neighbour when the window is resized. Children get
    // the same transform rather than scaled bounds, so the text inside a field or
    // a cell grows with the window too instead of staying at its pixel size.
    viewScale = std::min(getWidth() / static_cast<float>(W), getHeight() / static_cast<float>(H));
    view = juce::AffineTransform::scale(viewScale)
               .translated((getWidth() - W * viewScale) * 0.5f, (getHeight() - H * viewScale) * 0.5f);
    auto place = [this](juce::Component& c, const juce::Rectangle<int>& design) {
        c.setTransform({});
        c.setBounds(design);
        c.setTransform(view);
    };

    // The sheet every plug-in in the range shares: masthead, main sheet with the
    // readout column to its right, and the Bench along the bottom.
    mastheadArea = {24, 24, 1152, 72};
    presetBarDesign = {776, 30, 400, 36};
    place(presetBar, presetBarDesign);
    pedalArea = {24, 108, 828, 548};
    place(presetBrowser, pedalArea.withTrimmedRight(-3).withTrimmedBottom(-3));
    place(benchStrip, {24, 748, 1155, 50});
    {
        const auto area = benchStrip.reservedArea();
        benchSourceDesign = area.translated(24, 748);
    }
    place(benchSource, benchSourceDesign);
    place(*routeList, pedalArea.withTrimmedRight(-3).withTrimmedBottom(-3));

    // ── the three columns ──
    const int cx[3] = {pedalArea.getX() + 138, pedalArea.getX() + 414, pedalArea.getX() + 690};
    for (int i = 0; i < 6; ++i) {
        const int row = i / 3;
        knobDesign[i] = juce::Rectangle<int>(120, 120).withCentre({cx[i % 3], row == 0 ? 218 : 450});
        place(*knobs[static_cast<size_t>(i)].slider, knobDesign[i]);
    }
    // Each tempo switch sits directly under the knob it governs, between the knob
    // and its whisper, so it cannot be read as belonging to a neighbour.
    wetSyncDesign = juce::Rectangle<int>(54, 19).withCentre({knobDesign[0].getCentreX(), knobDesign[0].getBottom() + 6});
    envSyncDesign = juce::Rectangle<int>(54, 19).withCentre({knobDesign[2].getCentreX() - 29, knobDesign[2].getBottom() + 6});
    envSoftDesign = juce::Rectangle<int>(54, 19).withCentre({knobDesign[2].getCentreX() + 29, knobDesign[2].getBottom() + 6});
    place(wetSync, wetSyncDesign);
    place(envSync, envSyncDesign);
    place(envSoft, envSoftDesign);
    wetModeDesign = {cx[0] - 124, 336, 248, 28};
    routingDesign = {cx[1] - 124, 336, 248, 28};
    loopModeDesign = {cx[2] - 124, 336, 248, 28};
    place(*wetMode, wetModeDesign);
    place(*routing, routingDesign);
    place(*loopMode, loopModeDesign);
    fsWetDesign = juce::Rectangle<int>(248, 62).withCentre({cx[0], 610});
    fsLoopDesign = juce::Rectangle<int>(248, 62).withCentre({cx[2], 610});
    place(*fsWet, fsWetDesign);
    wetPreDesign = juce::Rectangle<int>(170, 24).withCentre({cx[0], 562});
    place(wetPre, wetPreDesign);
    place(*fsLoop, fsLoopDesign);

    // ── the side column ──
    // The readout, then four numbered panels in the order you reach for them while
    // playing: the loop you are hearing, what moves it, the switches, and last the
    // input, which is set once per session.
    auto side = juce::Rectangle<int>(864, 108, 312, 627);
    lcdArea = side.removeFromTop(84);
    side.removeFromTop(10);
    loopArea = side.removeFromTop(196);
    side.removeFromTop(10);
    rampArea = side.removeFromTop(150);
    side.removeFromTop(10);
    dipArea = side.removeFromTop(62);
    side.removeFromTop(10);
    srcArea = side;

    // 01 LOOP: the waveform carries START and END as markers; under it, two rows of
    // label / slider / figure on the same three columns so the figures line up.
    constexpr int labelW = 74, figureW = 44;
    auto inner = loopArea.withTrimmedTop(kit::panelHeadHeight).reduced(12, 6);
    loopViewDesign = inner.removeFromTop(100);
    place(loopView, loopViewDesign);
    gainDesign = juce::Rectangle<int>(84, 17).withPosition(loopViewDesign.getRight() - 85, loopViewDesign.getY() + 1);
    place(*gainBox, gainDesign);
    inner.removeFromTop(kSp2);
    auto trimRow = [&](juce::Rectangle<int>& out, juce::Slider& sl) {
        out = inner.removeFromTop(21).withTrimmedLeft(labelW).withTrimmedRight(figureW);
        place(sl, out);
    };
    trimRow(memoryDesign, memory);
    trimRow(balanceDesign, balance);

    // 02 MODULATION: the two sources and the route list, then the selected source's
    // shape, rate and switches.
    auto ramp = rampArea.withTrimmedTop(kit::panelHeadHeight).reduced(10, 7);
    auto rampRow = ramp.removeFromTop(46);
    routesDesign = rampRow.removeFromRight(60);
    rampRow.removeFromRight(6);
    const int chipW = (rampRow.getWidth() - 6) / 2;
    chipDesign[0] = rampRow.removeFromLeft(chipW);
    rampRow.removeFromLeft(6);
    chipDesign[1] = rampRow;
    place(*chips[0], chipDesign[0]);
    place(*chips[1], chipDesign[1]);
    place(routesButton, routesDesign);
    ramp.removeFromTop(5);
    rampRow = ramp.removeFromTop(28);
    const int half = (rampRow.getWidth() - kSp2) / 2;
    rampWaveDesign = rampRow.removeFromLeft(half);
    rampRow.removeFromLeft(kSp2);
    rampRateDesign = rampRow;                    // "RATE" and its figure are painted at the ends
    ramp.removeFromTop(5);
    rampRow = ramp.removeFromTop(26);
    // Five cells, sized to their words: BOUNCE needs more room than UNI.
    const int swWeights[] = {50, 72, 66, 54, 55};
    const int swTotal = rampRow.getWidth();
    for (size_t i = 0; i < 5; ++i)
        rampSwitchDesign[i] = i + 1 == 5 ? rampRow : rampRow.removeFromLeft(swTotal * swWeights[i] / 297);
    for (auto& c : sources) {
        place(*c.wave, rampWaveDesign);
        place(c.rate, rampRateDesign.withTrimmedLeft(36).withTrimmedRight(40).reduced(0, 3));
        for (size_t i = 0; i < c.switches.size(); ++i) place(*c.switches[i], rampSwitchDesign[i]);
    }

    // 03 SWITCHES: four cells, sized to their words.
    auto dipRow = dipArea.withTrimmedTop(kit::panelHeadHeight).reduced(10, 0).withSizeKeepingCentre(dipArea.getWidth() - 20, 26);
    dipRow.setY(dipArea.getY() + kit::panelHeadHeight + 5);
    const int dipWeights[] = {72, 74, 72, 79};
    const int dipTotal = dipRow.getWidth();
    for (size_t i = 0; i < dips.size(); ++i) {
        dipCellDesign[i] = i + 1 == dips.size() ? dipRow : dipRow.removeFromLeft(dipTotal * dipWeights[i] / 297);
        place(*dips[i].button, dipCellDesign[i]);
    }

    // 04 INPUT: source and file on one row, level and meters on the next.
    auto src = srcArea.withTrimmedTop(kit::panelHeadHeight).reduced(10, 6);
    auto r1 = src.removeFromTop(28);
    sourceBoxDesign = r1.removeFromLeft(170);
    place(sourceBox, sourceBoxDesign);
    r1.removeFromLeft(kSp2);
    loadButtonDesign = r1;
    place(loadButton, loadButtonDesign);
    src.removeFromTop(6);
    auto r2 = src.removeFromTop(24);
    meterArea = r2.removeFromRight(110).withSizeKeepingCentre(110, 19);
    r2.removeFromRight(kSp2);
    inputGainDesign = r2.withTrimmedLeft(44);    // "LEVEL" is painted in the gap
    place(inputGain, inputGainDesign);

    // ── under the hood ──
    // A band under the main sheet: the five sliders the pedal keeps behind both
    // footswitches, in five even cells.
    hoodArea = {24, 668, 828, 67};
    const int hoodLeft = hoodArea.getX() + 96;
    const int cell = (hoodArea.getRight() - 12 - hoodLeft - 4 * 14) / 5;
    for (int i = 0; i < 5; ++i) hoodCellDesign[i] = {hoodLeft + i * (cell + 14), hoodArea.getY() + 8, cell, 52};
    for (size_t i = 0; i < hood.size(); ++i) place(hood[i].slider, hoodCellDesign[i].withY(hoodArea.getY() + 30).withHeight(26));
    rebuildHoverHelp();
}


// Everything that has no explanation of its own: the switches, the steppers, the
// sliders whose label is only a label, the source row, the footswitches. The six
// knobs are left out on purpose — they already whisper, and two explanations of
// the same control in two different voices is worse than one.
void BroodEditor::rebuildHoverHelp() {
    hoverHelp.clear();
    auto add = [this](juce::Rectangle<int> r, const char* label, const char* text) {
        hoverHelp.push_back({r, juce::String(label), juce::String(text)});
    };
    static const char* dipText[] = {
        "the tails keep falling after you switch the wet side off.",
        "the sample rate slides instead of stepping, so CLOCK bends pitch as it turns.",
        "the hold gesture without the holding: the wet side comes up frozen and rings on its own.",
        "the MK I in the room: a noisy clock, a loop that never sits still, a longer reverb.",
    };
    static const char* dipNames[] = {"TRAILS", "SMOOTH", "FREEZE", "CLASSIC"};
    for (size_t i = 0; i < dips.size(); ++i) add(dipCellDesign[i], dipNames[i], dipText[i]);
    // These two are only in the table while their mode is on, so help never explains
    // a control that is not on the panel.
    if (wetSync.isVisible())
        add(wetSyncDesign.expanded(4), "SYNC / WET", "TIME becomes a division of the beat: in DELAY the echo lands on the grid, in WEAVE so does how far back the threads reach.");
    if (envSync.isVisible())
        add(envSyncDesign.expanded(4), "SYNC / ENV", "the envelope slices by a division of the beat instead of a free length, so the loop stutters in time.");
    if (envSoft.isVisible())
        add(envSoftDesign.expanded(4), "SOFT / ENV", "the slices fade in and out instead of cutting, and a new one takes over from the last, so nothing clicks. a rounder, less percussive stutter.");

    add(chipDesign[0], "MOD 1", "click to show its controls. drag it onto any knob or WET/LOOP, or drag up or down around a knob, to route it. the square turns it on or off.");
    add(chipDesign[1], "MOD 2", "click to show its controls. drag it onto any knob or WET/LOOP, or drag up or down around a knob, to route it. the square turns it on or off.");
    add(routesDesign, "ROUTES", "every route in one list, with its depth.");
    add(rampWaveDesign, "MOD SHAPE", "click to step the shape: triangle, square, sine, random, smooth random.");
    add(rampRateDesign, "MOD RATE", "how slowly this source turns. with SYNC on it becomes a span of the beat.");
    static const char* rampSwitchText[] = {
        "the swing sits above the knob instead of either side of it.",
        "the swing reflects instead of snapping back, so nothing jumps.",
        "the shape is ignored and the knob just climbs, again and again.",
        "the rate becomes a division of the beat: eight bars down to a sixteenth.",
        "this source stops where it is and its knobs stay pushed there. CC 52 holds both.",
    };
    static const char* rampSwitchNames[] = {"UNIPOLAR", "BOUNCE", "SWEEP", "MOD SYNC", "MOD HOLD"};
    for (int i = 0; i < 5; ++i) add(rampSwitchDesign[i], rampSwitchNames[i], rampSwitchText[i]);
    add(wetPreDesign, "BEFORE THE LOOP", "on: the wet side runs into the looper, so catches and overdubs keep it and SEND carries it. off: the loop stays clean and the wet side plays over it.");

    add(memoryDesign.expanded(0, 2), "LOOP MEMORY", "how much of the recent past is kept. hard left is the pedal's own buffer, which shrinks as CLOCK drops.");
    add(gainDesign, "LOOP GAIN", "the loop's own level. drag up or down, shift for fine, double-click for 0 dB.");
    add(loopViewDesign, "START / END", "drag a marker to trim what plays; START keeps the end where it is. double-click opens the whole take. moves crossfade, so they do not click.");
    add(balanceDesign.expanded(0, 2), "WET / LOOP", "the loop keeps its level across the whole travel and the wet side is added on top; hard right is the loop alone.");

    add(sourceBoxDesign, "SOURCE", "what it listens to: the track, a phrase made here, or a file.");
    add(loadButtonDesign, "LOAD FILE", "drop a recording in. it starts playing from the moment it arrives.");
    add(inputGainDesign.expanded(0, 3), "LEVEL", "how hard the input hits the machine. easy to overdo.");

    add(wetModeDesign, "WET MODE", "four ways of treating time: reverb, delay, slip, weave.");
    add(routingDesign, "ROUTING", "what the wet side is allowed to hear: the input, the loop, or both.");
    add(loopModeDesign, "LOOP MODE", "three ways of holding a moment: envelope, tape, stretch.");

    add(fsWetDesign, "WET FOOTSWITCH", "tap for the wet side, hold to freeze it where it stands. key A.");
    add(fsLoopDesign, "LOOP FOOTSWITCH", "tap to catch the recent past, tap again to stop, hold to overdub. key L.");

    add(benchSourceDesign, "SEND", "what SEND sends: the loop, or the exact output for that many bars, from the next bar line.");
    add(presetBarDesign, "PATCH", "click the name to search, star and audition sounds. the arrows step through them. SAVE keeps what you hear.");

    static const char* hoodText[] = {
        "a hi-cut on the wet side. right is open, left is soft and far away.",
        "how much of the old take an overdub erases. left keeps the pedal's slow erosion, right replaces it.",
        "how far apart the two speakers sit: left is mono, the middle is the image as built, right opens the sides.",
        "the wet side runs on a divided clock, so the room and the echo stretch out.",
        "the loop runs on a divided clock: lower, longer, further from the other side.",
    };
    static const char* hoodNames[] = {"TONE", "FADE", "WIDTH", "WET DIV", "LOOP DIV"};
    for (int i = 0; i < 5; ++i) add(hoodCellDesign[i], hoodNames[i], hoodText[i]);
}

void BroodEditor::paint(juce::Graphics& g) {
    g.fillAll(stock().page);
    g.addTransform(view);   // draw in design coordinates; the window scales it
    kit::drawMasthead(g, mastheadArea, stock(), "E-01", kInk, kPaper, "BROOD.", "EFFECTS LINE", "MICRO-LOOPER + SPACE");
    g.setColour(kMuted); g.setFont(font(17));
    g.drawText("PATCH", presetBarDesign.getX() - 56, presetBarDesign.getY(), 48, presetBarDesign.getHeight() - 2, juce::Justification::centredRight);

    // ── the main sheet ──
    kit::drawSheet(g, pedalArea, stock());
    {
        static const char* columns[] = {"WET CHANNEL", "SHARED", "MICRO-LOOPER"};
        for (int c = 0; c < 3; ++c) {
            const auto col = juce::Rectangle<int>(pedalArea.getX() + c * pedalArea.getWidth() / 3, pedalArea.getY(), pedalArea.getWidth() / 3, pedalArea.getHeight());
            if (c > 0) { g.setColour(kLine); g.drawVerticalLine(col.getX(), static_cast<float>(col.getY() + 1), static_cast<float>(col.getBottom() - 1)); }
            g.setColour(kMuted); g.setFont(font(15));
            g.drawText(columns[c], col.getX(), col.getY() + 6, col.getWidth(), 16, juce::Justification::centred);
            g.setColour(kInk);
            g.drawHorizontalLine(col.getY() + 24, static_cast<float>(col.getX() + 14), static_cast<float>(col.getRight() - 14));
        }
    }
    const int wm = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load());
    const int lm = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load());
    for (size_t i = 0; i < knobs.size(); ++i) {
        const auto kb = knobDesign[i];
        const auto nameRow = kb.withY(kb.getY() - 20).withHeight(20);
        g.setColour(kInk); g.setFont(font(20));
        g.drawText(knobs[i].label, nameRow, juce::Justification::centred);
        // The cell carries its own number, tied to the name, so six knobs read as six
        // numbered lines of a manifest.
        g.setColour(kMuted); g.setFont(capFont());
        const int nameW = kit::textWidth(font(20), knobs[i].label);
        g.drawText(juce::String(i + 1).paddedLeft('0', 2),
                   juce::Rectangle<int>(kb.getCentreX() + nameW / 2 + 5, nameRow.getY() + 1, 18, 14),
                   juce::Justification::centredLeft);
        const int mode = kKnobMode[i] == 0 ? wm : kKnobMode[i] == 1 ? lm : 0;
        // While the knob is touched the whisper gives way to the value, in ink, right
        // where you are looking.
        const bool showValue = static_cast<int>(i) == activeKnob && activeKnobText.isNotEmpty();
        g.setColour(showValue ? kInk : kMuted);
        g.setFont(showValue ? font(20) : font(16));
        g.drawFittedText(showValue ? activeKnobText.toUpperCase() : juce::String(kWhisper[i][juce::jlimit(0, 3, mode)]),
                         whisperRect(static_cast<int>(i)), juce::Justification::centredTop, 2, 1.f);
    }
    g.setColour(kMuted); g.setFont(font(15));
    g.drawText("ONE CLOCK. TWO WORLDS.", pedalArea.getX() + pedalArea.getWidth() / 3, fsWetDesign.getY(), pedalArea.getWidth() / 3, fsWetDesign.getHeight(), juce::Justification::centred);

    // Two figures the heads can quote: what the host gave us, and how much past
    // the looper is set to keep.
    const auto* memoryParam = proc.apvts.getRawParameterValue("memory");
    const float memV = memoryParam != nullptr ? memoryParam->load() : 0.f;
    const juce::String srcTail = juce::String(proc.getTotalNumInputChannels()) + " IN / "
                               + juce::String(proc.getTotalNumOutputChannels()) + " OUT";
    const juce::String loopTail = memV <= 0.001f ? juce::String("PEDAL MEMORY")
                                                 : juce::String(brood::expMap(memV, 1.0, 30.0), 1) + " S MEMORY";
    kit::drawPanel(g, loopArea, stock(), "01", "LOOP", loopTail);
    kit::drawPanel(g, rampArea, stock(), "02", "MODULATION", "MOD " + juce::String(selectedMod + 1) + " SELECTED");
    kit::drawPanel(g, dipArea, stock(), "03", "SWITCHES", juce::String(static_cast<int>(dips.size())) + " FITTED");
    kit::drawPanel(g, srcArea, stock(), "04", "INPUT", srcTail);

    // Row labels: one column, one size, so the rows read as one table.
    auto rowLabel = [&](const char* text, const juce::Rectangle<int>& slider, int labelW) {
        g.setColour(kInk); g.setFont(labelFont());
        g.drawText(text, slider.getX() - labelW, slider.getY(), labelW - kSp1, slider.getHeight(), juce::Justification::centredLeft);
    };
    rowLabel("MEMORY", memoryDesign, 74);
    rowLabel("WET/LOOP", balanceDesign, 74);
    rowLabel("LEVEL", inputGainDesign, 44);
    // RATE sits inside the rate cell, so it costs no extra row. With SYNC on it is
    // no longer a rate at all, so the caption says what the readout now means.
    const juce::String modId = "mod" + juce::String(selectedMod + 1);
    const auto* rampSyncValue = proc.apvts.getRawParameterValue(modId + "Sync");
    const bool spanMode = rampSyncValue != nullptr && rampSyncValue->load() > 0.5f;
    g.setFont(labelFont()); g.setColour(kMuted);
    g.drawText(spanMode ? "SPAN" : "RATE", rampRateDesign.getX(), rampRateDesign.getY(), 34, rampRateDesign.getHeight(), juce::Justification::centredLeft);
    // Short figures for the narrow column; the full wording lives in the readout
    // and in the host's parameter display.
    g.setColour(kMuted); g.setFont(labelFont());
    auto figure = [&](const char* id, const juce::Rectangle<int>& r, bool percent) {
        const auto* v = proc.apvts.getRawParameterValue(id);
        if (v == nullptr) return;
        juce::String txt;
        if (percent) txt = juce::String(juce::roundToInt(v->load() * 100.f)) + "%";
        else if (v->load() <= 0.001f) txt = "PEDAL";
        else txt = juce::String(brood::expMap(v->load(), 1, 30), 1) + "s";
        g.drawText(txt, r.withX(r.getRight() + kSp1).withWidth(40), juce::Justification::centredRight);
    };
    figure("memory", memoryDesign, false);
    figure("balance", balanceDesign, true);   // loop side, %
    // The rate's figure: a period, or with SYNC the span of the beat it turns over.
    if (const auto* rv = proc.apvts.getRawParameterValue(modId + "Rate")) {
        const double secs = 1.0 / brood::rampHertz(rv->load());
        const juce::String rate = spanMode ? juce::String(brood::rampDivLabel(rv->load()))
                                : secs < 1 ? juce::String(juce::roundToInt(secs * 1000)) + "ms"
                                           : juce::String(secs, secs < 10 ? 1 : 0) + "s";
        g.drawText(rate, rampRateDesign.withLeft(rampRateDesign.getRight() - 40), juce::Justification::centredRight);
    }

    // ── under the hood ──
    kit::drawSheet(g, hoodArea, stock());
    g.setColour(kInk); g.setFont(labelFont());
    g.drawText("UNDER", hoodArea.getX() + 12, hoodArea.getY() + 16, 80, 16, juce::Justification::centredLeft);
    g.drawText("THE HOOD", hoodArea.getX() + 12, hoodArea.getY() + 33, 80, 16, juce::Justification::centredLeft);
    g.setColour(kLine);
    g.drawVerticalLine(hoodCellDesign[0].getX() - 8, static_cast<float>(hoodArea.getY() + 8), static_cast<float>(hoodArea.getBottom() - 8));
    static const char* hoodNames[] = {"TONE", "FADE", "WIDTH", "WET DIV", "LOOP DIV"};
    static const char* hoodIds[] = {"tone", "fade", "width", "wetDiv", "loopDiv"};
    static const float hoodDefaults[] = {1.f, 0.f, 0.5f, 0.f, 0.f};
    float hv[5];
    for (int i = 0; i < 5; ++i) {
        const auto* v = proc.apvts.getRawParameterValue(hoodIds[i]);
        hv[i] = v != nullptr ? v->load() : hoodDefaults[i];
    }
    auto percent = [](float v) { return juce::String(juce::roundToInt(v * 100.f)) + "%"; };
    juce::String hoodText[5];
    hoodText[0] = hv[0] > 0.995f ? "open" : "cut " + percent(1.f - hv[0]);
    hoodText[1] = hv[1] <= 0.001f ? "pedal" : "keeps " + percent(1.f - hv[1]);
    hoodText[2] = hv[2] <= 0.005f ? "mono" : hv[2] < 0.49f ? "narrow " + percent(hv[2] * 2) :
                  hv[2] > 0.51f ? "wide " + percent((hv[2] - 0.5f) * 2) : "as built";
    hoodText[3] = juce::String(brood::divMap(hv[3])) + "x";
    hoodText[4] = juce::String(brood::divMap(hv[4])) + "x";
    for (int i = 0; i < 5; ++i) {
        const auto cellRect = hoodCellDesign[i].withHeight(18);
        g.setColour(kInk); g.setFont(labelFont());
        const int nameW = kit::textWidth(g.getCurrentFont(), hoodNames[i]) + 2;
        g.drawText(hoodNames[i], cellRect.withWidth(nameW), juce::Justification::centredLeft);
        g.setColour(kMuted);
        g.drawFittedText(hoodText[i], cellRect.withLeft(cellRect.getX() + nameW + kSp2), juce::Justification::centredRight, 1, 0.8f);
    }

    // Ten cells rather than a continuous bar: a level you can count.
    auto meter = [&](int y, const char* name, float peak) {
        g.setColour(kMuted); g.setFont(capFont());
        g.drawText(name, meterArea.getX(), y - 3, 26, 13, juce::Justification::centredLeft);
        constexpr int cells = 10;
        const int left = meterArea.getX() + 28, span = meterArea.getWidth() - 28;
        const int cw = juce::jmax(2, span / cells);
        const float level = juce::jlimit(0.f, 1.f, (juce::Decibels::gainToDecibels(peak, -60.f) + 60.f) / 60.f);
        const int lit = juce::roundToInt(level * (float) cells);
        for (int i = 0; i < cells; ++i) {
            const auto c = juce::Rectangle<int>(left + i * cw + 1, y, cw - 2, 8);
            g.setColour(i < lit ? kInk : kPaper); g.fillRect(c);
            g.setColour(i < lit ? kInk : kLine); g.drawRect(c, 1);
        }
    };
    meter(meterArea.getY(), "IN", meterIn); meter(meterArea.getY() + 11, "OUT", meterOut);

    // The readout: while the mouse rests on something that has no explanation of its
    // own it explains that; otherwise it's the last thing you moved.
    const bool helping = hoverText.isNotEmpty();
    juce::String value = helping ? hoverText : lcdValue.toUpperCase();
    if (!helping && blink) value += "_";
    kit::drawReadout(g, lcdArea, stock(), "THE SIGNAL / " + (helping ? hoverLabel : lcdLabel), helping ? "HELP" : "LIVE", value, helping ? juce::String() : lcdMeta, helping);
}


void BroodEditor::timerCallback() {
    auto& ui = proc.ui;

    // Hover help. The mouse position is mapped back into design coordinates and
    // matched against the table, so no control needs its own enter/exit handler
    // and a click is never intercepted: this only ever changes what the readout
    // panel says.
    // While a slider is being dragged the readout is the more useful thing to
    // show, so help stands down for the duration of the gesture.
    const HoverItem* hovered = nullptr;
    if (!hoverHelp.empty() && !juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) {
        const auto at = getMouseXYRelative();
        float mx = static_cast<float>(at.x), my = static_cast<float>(at.y);
        view.inverted().transformPoint(mx, my);
        const juce::Point<float> mouse(mx, my);
        for (const auto& item : hoverHelp)
            if (item.rect.toFloat().contains(mouse)) { hovered = &item; break; }
    }
    const juce::String newLabel = hovered != nullptr ? hovered->label : juce::String();
    const juce::String newText = hovered != nullptr ? hovered->text : juce::String();
    if (newLabel != hoverLabel || newText != hoverText) {
        hoverLabel = newLabel; hoverText = newText;
        repaintDesign(lcdArea);
    }
    // The tempo switches follow the mode selectors: DELAY and WEAVE for the wet side,
    // ENV for the loop. Changing mode rebuilds the help table, because a switch that
    // has left the panel must not still have an explanation.
    updateModeControls();
    // The knob under the mouse (or under a drag) shows its value in place of its
    // whisper; repaint only the two lines that change.
    {
        int act = -1;
        for (size_t i = 0; i < knobs.size(); ++i) if (knobs[i].slider->isMouseOverOrDragging()) act = static_cast<int>(i);
        juce::String txt = act >= 0 ? proc.describe(knobs[static_cast<size_t>(act)].id) : juce::String();
        // ENV's two knobs act on the slices the input makes, so with the loop playing
        // and nothing coming in they have nothing to move yet. Say so where you're looking.
        if (act == 2 || act == 5)
            if (lastModeLoop == 0 && ui.loopLed.load() == 1 && !ui.envGate.load())
                txt = "PLAY SOMETHING: ENV SLICES WHEN IT HEARS YOU";
        if (act != activeKnob || txt != activeKnobText) {
            if (activeKnob >= 0) repaintDesign(whisperRect(activeKnob));
            activeKnob = act; activeKnobText = txt;
            if (act >= 0) repaintDesign(whisperRect(act));
        }
    }
    const double now = juce::Time::getMillisecondCounterHiRes();
    const bool wasBlink = blink;
    blink = static_cast<int>(now / 160.0) % 2 == 0;
    // The readout's cursor is drawn by paint(), so it needs its own repaint when
    // nothing else has changed — otherwise it sits still.
    if (blink != wasBlink && hoverText.isEmpty() && lcdValue.isNotEmpty()) repaintDesign(lcdArea);
    const uint32_t cycles = ui.histCycles.load();
    if (cycles != lastCycles) { lastCycles = cycles; flashUntil = now + 120; }   // off: blink each new loop recorded
    const int led = ui.loopLed.load();   // 0 off, 1 play, 2 rec, 3 dub
    const bool wetOn = ui.wetOn.load() && !(ui.freeze.load() && !blink);
    const bool loopOn = led == 1 || ((led == 2 || led == 3) && blink) || (led == 0 && now < flashUntil);
    // The chips' moving traces, the knobs' rings, and the route list keeping up.
    for (int k = 0; k < 2; ++k) {
        auto& chip = *chips[k];
        chip.trace[static_cast<size_t>(chip.written)] = ui.modValue[static_cast<size_t>(k)].load();
        chip.written = (chip.written + 1) % static_cast<int>(chip.trace.size());
        chip.repaint();
    }
    const int mask = routeMask();
    if (mask != routeMaskSeen) {
        routeMaskSeen = mask;
        if (routeList->isVisible()) routeList->rebuild(false);
    }
    if (mask != 0) for (auto& knob : knobs) if (knob.slider->isShowing()) knob.slider->repaint();
    fsWet->setLit(wetOn);
    fsLoop->setLit(loopOn);
    if (led != lastLoopLed) {
        const char* msg[] = {nullptr, "playing", "recording...", "overdubbing..."};
        if (msg[led] != nullptr) showReadout("LOOP", msg[led]);
        else if (lastLoopLed == 1) showReadout("LOOP", "stopped");
        lastLoopLed = led;
    }
    const float mi = ui.peakIn.load(), mo = ui.peakOut.load();
    if (std::fabs(mi - meterIn) > 1e-3f || std::fabs(mo - meterOut) > 1e-3f) { meterIn = mi; meterOut = mo; repaintDesign(meterArea.expanded(2)); }
    const int lsrc = ui.lsrc.load();
    const juce::String meta = juce::String(ui.clockHz.load() / 1000.f, 1) + " kHz    "
        + (lsrc > 0 ? "loop " + juce::String(ui.winLen.load() / ui.clockHz.load(), 2) + " s" : juce::String("no loop"))
        + "    cpu " + juce::String(ui.load.load(), 1) + "%";
    if (meta != lcdMeta) { lcdMeta = meta; repaintDesign(lcdArea); }
    if (readoutFollows && lastTouched.isNotEmpty()) {
        const juce::String v = proc.describe(lastTouched);   // follows automation and mode changes
        if (v != lcdValue) { lcdValue = v; repaintDesign(lcdArea); }
    }
    updateBench();
    loopView.repaint();   // the play head moves every frame; paint() fetches new peaks
}

bool BroodEditor::keyPressed(const juce::KeyPress& k) {
    const auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    return c == 'a' || c == 'l';   // handled in keyStateChanged so holds work
}

bool BroodEditor::keyStateChanged(bool) {
    if (presetBrowser.isVisible()) return false;   // typing in its search box is not playing
    const bool a = juce::KeyPress::isKeyCurrentlyDown('a') || juce::KeyPress::isKeyCurrentlyDown('A');
    const bool l = juce::KeyPress::isKeyCurrentlyDown('l') || juce::KeyPress::isKeyCurrentlyDown('L');
    bool used = false;
    if (a != keyA) { keyA = a; fsWet->press(a); used = true; }
    if (l != keyL) { keyL = l; fsLoop->press(l); used = true; }
    return used;
}
