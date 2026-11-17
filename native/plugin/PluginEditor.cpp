#include "PluginEditor.h"
#if BROOD_PANEL_FONTS
#include "BroodFonts.h"
#endif

namespace {
// Brood's stock: warm paper and a lot of ink, with one ochre that only ever means "this is on".
// The look itself lives in the shared kit.
const kit::Stock& stock() {
    static const auto s = [] {
        auto k = kit::cream();
        k.page = juce::Colour(0xffefebe0); k.panel = juce::Colour(0xfff6f3ea); k.head = juce::Colour(0xffefebe0);
        k.rule = juce::Colour(0xffbdb9ab); k.onSunk = k.panel; k.onAccent = k.panel;
        k.accent = juce::Colour(0xff8f5f12); k.lit = juce::Colour(0xffc98f2e);
        k.frameless = true;   // rounded surfaces and plates, no frames or hard shadows
        return k;
    }();
    return s;
}
const juce::Colour kInk = stock().ink, kPaper = stock().panel, kMuted = stock().soft, kLine = stock().rule, kPanel = stock().head;

// The wordmark: "BROOD" in Squarepusher (thin cut, freeware by Derik Revenge of Flak
// Networks, after fluid's Euphoric; see THIRD_PARTY_NOTICES.md), its letters' outlines in font
// units (y up, capitals 360 tall, 3816 wide) with the D redrawn: its right corners cut at 45
// degrees so it reads as a D. Only the outlines ship, never the font file.
namespace wordmarkOutline {
struct Pt { short x, y; };
constexpr Pt c0[] = {{0, 360}, {648, 360}, {648, 0}, {144, 0}, {144, 72}, {576, 72}, {576, 144}, {144, 144}, {144, 216}, {576, 216}, {576, 288}, {72, 288}, {72, 0}, {0, 0}};
constexpr Pt c1[] = {{792, 360}, {1440, 360}, {1440, 144}, {1008, 144}, {1008, 72}, {1440, 72}, {1440, 0}, {936, 0}, {936, 216}, {1368, 216}, {1368, 288}, {864, 288}, {864, 0}, {792, 0}};
constexpr Pt c2[] = {{1584, 360}, {2232, 360}, {2232, 0}, {1584, 0}};
constexpr Pt c3[] = {{2160, 72}, {2160, 288}, {1656, 288}, {1656, 72}};
constexpr Pt c4[] = {{2376, 360}, {3024, 360}, {3024, 0}, {2376, 0}};
constexpr Pt c5[] = {{2952, 72}, {2952, 288}, {2448, 288}, {2448, 72}};
constexpr Pt c6[] = {{3168, 360}, {3672, 360}, {3816, 216}, {3816, 144}, {3672, 0}, {3168, 0}, {3168, 216}, {3240, 216}, {3240, 72}, {3642, 72}, {3744, 174}, {3744, 186}, {3642, 288}, {3168, 288}};
constexpr std::pair<const Pt*, int> contours[] = {{c0, static_cast<int>(std::size(c0))}, {c1, static_cast<int>(std::size(c1))}, {c2, static_cast<int>(std::size(c2))}, {c3, static_cast<int>(std::size(c3))}, {c4, static_cast<int>(std::size(c4))}, {c5, static_cast<int>(std::size(c5))}, {c6, static_cast<int>(std::size(c6))}};
constexpr float height = 360.f;   // capitals; 3816 wide
}

// The wordmark's outline, scaled so its capitals stand `capHeight` tall, top left at 0, 0.
juce::Path wordmarkPath(float capHeight) {
    juce::Path p;
    p.setUsingNonZeroWinding(false);
    const float k = capHeight / wordmarkOutline::height;
    for (const auto& [pts, n] : wordmarkOutline::contours) {
        p.startNewSubPath(pts[0].x * k, (wordmarkOutline::height - pts[0].y) * k);
        for (int i = 1; i < n; ++i) p.lineTo(pts[i].x * k, (wordmarkOutline::height - pts[i].y) * k);
        p.closeSubPath();
    }
    return p;
}

// Brood's own faces for the kit, from the font trial (docs/plans/font-trial): B612 Mono on the
// screen and the take strip, Overpass Medium for names, Chivo Mono two sizes down for figures
// on the paper, and four lines of help, since B612 Mono is wider than VT323. Built without the
// fonts (scripts/fetch-fonts.sh), the kit's faces stay.
void installFaces() {
#if BROOD_PANEL_FONTS
    static const bool once = [] {
        auto& f = kit::faces();
        f.screen = juce::Typeface::createSystemTypefaceFor(BroodFonts::B612MonoRegular_ttf, static_cast<size_t>(BroodFonts::B612MonoRegular_ttfSize));
        f.name = juce::Typeface::createSystemTypefaceFor(BroodFonts::OverpassMedium_ttf, static_cast<size_t>(BroodFonts::OverpassMedium_ttfSize));
        f.value = juce::Typeface::createSystemTypefaceFor(BroodFonts::ChivoMonoRegular_ttf, static_cast<size_t>(BroodFonts::ChivoMonoRegular_ttfSize));
        f.valueShrink = 2.0f;
        f.readoutLines = 4;
        return true;
    }();
    juce::ignoreUnused(once);
#endif
}

juce::Font font(float size) { return kit::value(size); }                  // figures on the paper
juce::Font nameFont(float size) { return kit::nameFont(true, size); }        // names and labels: the sans face
void plate(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, float corner = 4.f) { g.setColour(colour); g.fillRoundedRectangle(r, corner); }
juce::Font capFont() { return kit::label(14); }   // the take strip: the screen face
juce::Font labelFont() { return nameFont(16); }
juce::Font textFont() { return font(18); }
constexpr int kSp1 = 4, kSp2 = 8;
constexpr int kBalanceEnd = 32;
const juce::Colour kMoss(0xff5d7046);   // LOOP's section mark; ochre stays for "on"   // room for LOOP at BALANCE's left end
constexpr int balanceRouteBits = (1 << 6) | (1 << (brood::kModTargets + 6));   // in routeMask(): either source to BALANCE

constexpr int W = 1200, H = 820;

// One whispered sentence per knob per mode: enough to know which way to turn,
// never quite enough to stop wondering. It lives in hover help; the exact value
// appears under the knob when you touch it.
const char* kWhisper[6][4] = {
    {"How long the room keeps breathing", "How far back the echo remembers",
     "How wide a slice of time is borrowed", "How far back the threads reach"},
    {"Your dry sound against Brood: the loop and the room. Left is dry alone, right is Brood alone, the middle both in full",
     "Your dry sound against Brood: the loop and the echoes. Left is dry alone, right is Brood alone, the middle both in full",
     "Your dry sound against Brood: the loop and the slipped slices. Left is dry alone, right is Brood alone, the middle both in full",
     "Your dry sound against Brood: the loop and the threads. Left is dry alone, right is Brood alone, the middle both in full"},
    {"How much of the moment gets caught", "The size of the window that repeats",
     "How wide each grain is stretched", "How much of the moment gets caught"},
    {"How much the surface smears", "How much the echo wants to stay",
     "Which way, and how fast, time slides", "Long cloth, or short clipped stitches"},
    {"The heartbeat everything obeys", "The heartbeat everything obeys",
     "The heartbeat everything obeys", "The heartbeat everything obeys"},
    {"How softly it must speak to be caught", "Which way the tape runs, and how fast",
     "How far the grains drift from now", "How softly it must speak to be caught"},
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

void BroodEditor::Look::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) {
    kit::Look::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, slider);
    auto* mod = dynamic_cast<ModSlider*>(&slider);
    if (mod == nullptr || mod->target < 0 || mod->editor == nullptr || style != juce::Slider::LinearHorizontal) return;
    const float cy = static_cast<float>(y) + static_cast<float>(h) * 0.5f, left = static_cast<float>(x) + 1.f, right = static_cast<float>(x + w) - 1.f;
    const float at = static_cast<float>(slider.valueToProportionOfLength(slider.getValue()));
    int moving = -1, routes = 0;
    for (int k = 0; k < 2; ++k) {
        const float d = mod->editor->depthParam(k, mod->target)->convertFrom0to1(mod->editor->depthParam(k, mod->target)->getValue());
        if (std::fabs(d) < 0.005f) continue;
        const juce::String id = "mod" + juce::String(k + 1);
        const bool on = proc.apvts.getRawParameterValue(id + "On")->load() > 0.5f;
        const bool uni = proc.apvts.getRawParameterValue(id + "Uni")->load() > 0.5f;
        const float lo = juce::jlimit(0.f, 1.f, uni ? juce::jmin(at, at + d) : at - std::fabs(d));
        const float hi = juce::jlimit(0.f, 1.f, uni ? juce::jmax(at, at + d) : at + std::fabs(d));
        // Clear of the tab: the first source's swing under the track, the second's over it.
        g.setColour(BroodEditor::modColour(k).withAlpha(on ? 1.f : 0.35f));
        g.fillRoundedRectangle(left + (right - left) * lo, k == 0 ? cy + 8.f : cy - 13.f, (right - left) * (hi - lo), 3.f, 1.5f);
        ++routes;
        if (on) moving = moving < 0 ? k : 2;
    }
    if (routes == 0) return;
    // Where the sound has it now.
    const float now = juce::jlimit(0.f, 1.f, proc.ui.effective[static_cast<size_t>(mod->target)].load());
    const auto tab = juce::Rectangle<float>(5.f, 13.f).withCentre({left + (right - left) * now, cy - 1.f});
    g.setColour(moving == 0 || moving == 1 ? BroodEditor::modColour(moving) : stock().ink);
    g.fillRoundedRectangle(tab, 2.f);
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
// The source an up-or-down drag on a linear control works on: the one already on it if only one
// is, otherwise the selected one (as a knob's ring picks).
int BroodEditor::ModSlider::sourceForDepth() const {
    auto routed = [&](int k) { return std::fabs(editor->depthParam(k, target)->convertFrom0to1(editor->depthParam(k, target)->getValue())) >= 0.005f; };
    if (routed(0) != routed(1)) return routed(0) ? 0 : 1;
    return editor->selectedSource();
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
    // BALANCE: wait for the drag to say which it is.
    deciding = target >= 0 && editor != nullptr && isHorizontal();
    if (deciding) return;
    juce::Slider::mouseDown(e);
}
void BroodEditor::ModSlider::mouseDrag(const juce::MouseEvent& e) {
    if (menuPress) return;
    if (deciding) {
        const auto moved = e.getOffsetFromDragStart();
        if (moved.getDistanceFromOrigin() < 6) return;
        deciding = false;
        if (std::abs(moved.y) > std::abs(moved.x)) {
            settingDepth = true;
            depthSource = sourceForDepth();
            auto* prm = editor->depthParam(depthSource, target);
            depthAtStart = prm->convertFrom0to1(prm->getValue());
            prm->beginChangeGesture();
            editor->selectSource(depthSource);
        } else {
            juce::Slider::mouseDown(e);   // the value goes to the mouse, then follows it
        }
    }
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
    if (deciding) {   // a click that never moved: the value goes there
        deciding = false;
        juce::Slider::mouseDown(e);
        juce::Slider::mouseUp(e);
        return;
    }
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
    const bool selected = editor.selectedMod == k && editor.modOpen;   // tinted while its controls show
    const auto colour = modColour(k);
    plate(g, r.toFloat(), selected ? colour.withAlpha(0.22f) : kInk.withAlpha(isMouseOver(true) ? 0.1f : 0.05f), 5.f);
    const auto number = juce::Rectangle<int>(5, 4, 15, 15);
    plate(g, number.toFloat(), on ? colour : kLine, 3.f);
    g.setColour(kPaper); g.setFont(font(15));
    g.drawText(juce::String(k + 1), number, juce::Justification::centred);
    int routes = 0;
    for (int t = 0; t < brood::kModTargets; ++t)
        if (std::fabs(editor.depthParam(k, t)->convertFrom0to1(editor.depthParam(k, t)->getValue())) >= 0.005f) ++routes;
    g.setColour(kMuted); g.setFont(font(14));
    g.drawText(juce::String(routes) + (routes == 1 ? " ROUTE" : " ROUTES"), 24, 3, getWidth() - 48, 16, juce::Justification::centredLeft);
    // ON: lit like every other switch.
    const auto box = onBox();
    plate(g, box.toFloat(), on ? stock().lit : kInk.withAlpha(0.18f), 3.f);
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
    clickable = false;
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
    clickable = true;   // decided on the way up: a drag routes, a click opens or closes
}
void BroodEditor::ModChip::mouseDrag(const juce::MouseEvent& e) {
    if (e.getDistanceFromDragStart() > 4) dragging = true;
    if (dragging) editor.chipDragged(k, e.getScreenPosition());
}
void BroodEditor::ModChip::mouseUp(const juce::MouseEvent& e) {
    if (dragging) editor.chipDropped(k, e.getScreenPosition());
    else if (clickable) editor.chipClicked(k);
    dragging = false;
    clickable = false;
}

// ───────── the loop's gain ─────────
BroodEditor::GainBox::GainBox(juce::RangedAudioParameter& p, std::function<float()> normalising)
    : attach(p, [this](float v) { db = v; repaint(); }), normalise(std::move(normalising)) {
    attach.sendInitialUpdate();
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity(true);
    setTitle("Loop gain");
}
// As Silt has it on its screen: only words, until the mouse is on them.
void BroodEditor::GainBox::paint(juce::Graphics& g) {
    const auto& k = stock();
    if (isMouseOver(true)) { g.setColour(k.onSunk.withAlpha(0.25f)); g.fillRect(getLocalBounds()); }
    const int rounded = juce::roundToInt(db * 2.f);
    g.setColour(rounded == 0 ? k.onSunk : k.lit);
    g.setFont(capFont());
    g.drawText("GAIN " + juce::String(rounded > 0 ? "+" : "") + juce::String(static_cast<float>(rounded) * 0.5f, 1) + " DB", getLocalBounds().withTrimmedRight(4), juce::Justification::centredRight);
}
void BroodEditor::GainBox::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(1, "Normalise (peak at -1 dB)");
        menu.addItem(2, "Reset to 0 dB");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int chosen) {
            if (chosen == 1) attach.setValueAsCompleteGesture(normalise());
            else if (chosen == 2) attach.setValueAsCompleteGesture(0.f);
        });
        return;
    }
    dbRunning = db;
    lastY = e.position.y;
    attach.beginGesture();
    dragging = true;
}
void BroodEditor::GainBox::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging) return;
    // Each move adds its own change at the rate Shift sets now (pressed or let go mid-drag).
    dbRunning = juce::jlimit(-24.f, 12.f, dbRunning + (lastY - e.position.y) * (e.mods.isShiftDown() ? 0.02f : 0.15f));
    lastY = e.position.y;
    attach.setValueAsPartOfGesture(dbRunning);
}
void BroodEditor::GainBox::mouseUp(const juce::MouseEvent&) { if (dragging) attach.endGesture(); dragging = false; }
void BroodEditor::GainBox::mouseDoubleClick(const juce::MouseEvent&) { attach.setValueAsCompleteGesture(0.f); }
void BroodEditor::GainBox::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    attach.setValueAsCompleteGesture(juce::jlimit(-24.f, 12.f, db + (wheel.deltaY > 0.f ? 0.5f : -0.5f)));
}

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
        plate(g, num.toFloat(), modColour(row.k), 3.f);
        g.setColour(kPaper); g.setFont(font(16));
        g.drawText(juce::String(row.k + 1), num, juce::Justification::centred);
        g.setColour(kInk); g.setFont(nameFont(19));
        g.drawText(brood::kModTargetNames[row.t], line.withWidth(200).withTrimmedLeft(8), juce::Justification::centredLeft);
        const int depth = juce::roundToInt(row.depth->getValue() * 100.0);
        g.setColour(kMuted);
        g.drawText((depth > 0 ? "+" : "") + juce::String(depth) + "%", line.withLeft(line.getRight() - 100).withWidth(60), juce::Justification::centredRight);
    }
}

// ───────── segmented selector ─────────
BroodEditor::ThreeWay::ThreeWay(juce::RangedAudioParameter& p, juce::StringArray l, std::function<void()> onChange, bool flipped)
    : param(p), labels(std::move(l)), flip(flipped),
      attach(p, [this](float v) { const int at = juce::roundToInt(v); index = flip ? labels.size() - 1 - at : at; repaint(); if (changed) changed(); }),
      changed(std::move(onChange)) {
    attach.sendInitialUpdate();
}
void BroodEditor::ThreeWay::paint(juce::Graphics& g) {
    // One plate across the column, the chosen cell a solid block.
    const auto bounds = getLocalBounds().withTrimmedRight(2).withTrimmedBottom(2);
    const int count = juce::jmax(1, labels.size());
    plate(g, bounds.toFloat(), kInk.withAlpha(0.08f), 5.f);
    for (int i = 0; i < count; ++i) {
        const int x0 = bounds.getX() + bounds.getWidth() * i / count, x1 = bounds.getX() + bounds.getWidth() * (i + 1) / count;
        const juce::Rectangle<int> r(x0, bounds.getY(), x1 - x0, bounds.getHeight());
        if (i == index) plate(g, r.toFloat().reduced(2.f), kInk, 4.f);
        g.setColour(i == index ? kPaper : kInk); g.setFont(nameFont(16));
        g.drawFittedText(labels[i].toUpperCase(), r.reduced(2, 0), juce::Justification::centred, 1, 0.8f);
    }
}
void BroodEditor::ThreeWay::mouseDown(const juce::MouseEvent& e) {
    const int count = juce::jmax(1, labels.size());
    const int next = juce::jlimit(0, count - 1, e.x * count / juce::jmax(1, getWidth()));
    attach.setValueAsCompleteGesture(static_cast<float>(flip ? count - 1 - next : next));
    if (clicked) clicked();
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
    plate(g, body.toFloat(), kInk.withAlpha(hot ? 0.14f : 0.08f));
    auto inner = body.reduced(2, 2);
    const int arrow = arrowWidth();
    auto tri = [&](juce::Rectangle<int> zone, bool right) {
        juce::Path t;
        const float w = 4.f, h = 7.f, cx = zone.getCentreX(), cy = zone.getCentreY();
        t.addTriangle(cx + (right ? -w / 2 : w / 2), cy - h / 2, cx + (right ? -w / 2 : w / 2), cy + h / 2, cx + (right ? w / 2 : -w / 2), cy);
        g.setColour(hot ? kInk : kMuted); g.fillPath(t);
    };
    tri(inner.removeFromLeft(arrow), false);
    tri(inner.removeFromRight(arrow), true);
    inner.removeFromLeft(3);
    g.setFont(labelFont()); g.setColour(kMuted);
    const int capW = juce::jmin(inner.getWidth() / 2, capWidth);
    g.drawFittedText(cap, inner.removeFromLeft(capW), juce::Justification::centredLeft, 1);
    auto value = inner.withTrimmedLeft(2);
    g.setColour(kInk); g.setFont(textFont());
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
    // A big plate that darkens while it's held, with a light that's solid while this side is on.
    plate(g, r.toFloat(), kInk.withAlpha(isDown ? 0.22f : isMouseOver(true) ? 0.13f : 0.08f), 6.f);
    auto row = r.reduced(12, 0);
    const auto led = row.removeFromLeft(16).withSizeKeepingCentre(14, 14);
    g.setColour(lit ? stock().lit : kInk.withAlpha(0.25f)); g.fillEllipse(led.toFloat());
    row.removeFromLeft(10);
    g.setColour(kInk); g.setFont(nameFont(24));
    g.drawText(name, row.removeFromLeft(62), juce::Justification::centredLeft);
    g.setFont(font(16));
    g.drawText("[ " + keyName + " ]", row.removeFromRight(36), juce::Justification::centredRight);
    g.setColour(kMuted);
    if (row.getWidth() >= kit::textWidth(g.getCurrentFont(), "TAP / HOLD") + 8)   // narrowed beside OVERDUB, it goes
        g.drawText("TAP / HOLD", row, juce::Justification::centred);
}


void BroodEditor::StripKey::paintButton(juce::Graphics& g, bool hover, bool down) {
    const auto& k = stock();
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool lit = isEnabled() && (hover || down);
    if (down) { g.setColour(k.lit); g.fillRoundedRectangle(r, 2.f); }
    g.setColour(lit ? k.lit : k.onSunk.withAlpha(0.5f));
    g.drawRoundedRectangle(r, 2.f, 1.f);
    g.setColour(down ? k.sunk : lit ? k.lit : k.onSunk);
    g.setFont(capFont());
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
}

// CLEAR and UNDO are pulses: on, and the processor sets them back to off once it has acted.
void BroodEditor::pulse(const char* id) {
    if (auto* prm = proc.apvts.getParameter(id)) {
        prm->beginChangeGesture(); prm->setValueNotifyingHost(1.f); prm->endChangeGesture();
    }
}

// OVERDUB can only switch on over a playing loop; UNDO only with something to undo, and it
// says REDO when pressing it would put the step back. Greyed out, they ignore clicks.
void BroodEditor::updateTakeControls() {
    const auto grey = [](juce::Component& c, bool on) {
        if (c.isEnabled() != on) { c.setEnabled(on); c.setAlpha(on ? 1.f : 0.35f); }
    };
    grey(overdub, proc.ui.canOverdub.load() || proc.ui.dubLatched.load());
    grey(clearTake, proc.ui.lsrc.load() > 0 || proc.ui.loopLed.load() == 2 || proc.ui.recArmed.load());
    grey(undoTake, proc.ui.undoKind.load() != 0 && proc.ui.loopLed.load() != 2 && !proc.ui.recArmed.load());
    const juce::String undoText = proc.ui.undoRedo.load() ? "REDO" : "UNDO";
    if (undoTake.getButtonText() != undoText) undoTake.setButtonText(undoText);
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

// On a marker: that marker. Inside the window: the window. Outside it: the nearer marker,
// which comes to the mouse.
int BroodEditor::LoopView::regionAt(float x) const {
    int s = 0, e = 0;
    if (!markers(s, e)) return -1;
    const float scale = getWidth() / static_cast<float>(proc.ui.lsrc.load());
    const float xs = s * scale, xe = e * scale;
    constexpr float reach = 8.f;
    if (std::fabs(x - xs) <= reach || std::fabs(x - xe) <= reach) return nearestMarker(x);
    return x > xs && x < xe ? 2 : nearestMarker(x);
}

void BroodEditor::LoopView::mouseMove(const juce::MouseEvent& e) {
    const int region = regionAt(static_cast<float>(e.x));
    setMouseCursor(region == 2 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
    const int h = region == 2 ? -1 : region;
    if (h != hot) { hot = h; repaint(); }
}
void BroodEditor::LoopView::mouseExit(const juce::MouseEvent&) { if (dragging < 0 && hot != -1) { hot = -1; repaint(); } }

void BroodEditor::LoopView::mouseDown(const juce::MouseEvent& e) {
    int s = 0, end = 0;
    if (!markers(s, end)) return;
    dragging = regionAt(static_cast<float>(e.x));
    hot = dragging == 2 ? -1 : dragging;
    anchorStart = s;
    anchorEnd = end;   // the window as it was, so each drag measures from something still
    const double scale = proc.ui.lsrc.load() / static_cast<double>(juce::jmax(1, getWidth()));
    const double pressed = e.x * scale;
    // Held where it was pressed, so a grab moves nothing. Outside the window a marker comes to the mouse.
    const bool inside = pressed >= s - 8.0 * scale && pressed <= end + 8.0 * scale;
    grabOffset = !inside ? 0.0 : dragging == 1 ? end - pressed : s - pressed;
    for (auto* id : {"start", "loopEnd", "length"}) if (auto* prm = proc.apvts.getParameter(id)) prm->beginChangeGesture();
    moveMarker(static_cast<float>(e.x));
}
void BroodEditor::LoopView::mouseDrag(const juce::MouseEvent& e) { if (dragging >= 0) moveMarker(static_cast<float>(e.x)); }
void BroodEditor::LoopView::mouseUp(const juce::MouseEvent&) {
    if (dragging < 0) return;
    dragging = -1;
    for (auto* id : {"start", "loopEnd", "length"}) if (auto* prm = proc.apvts.getParameter(id)) prm->endChangeGesture();
}
void BroodEditor::LoopView::mouseDoubleClick(const juce::MouseEvent&) {
    for (auto* id : {"start", "loopEnd"})
        if (auto* prm = proc.apvts.getParameter(id)) {
            prm->beginChangeGesture(); prm->setValueNotifyingHost(prm->getDefaultValue()); prm->endChangeGesture();
        }
    if (onTouch) onTouch("loopEnd");
}

// A marker or the whole window, moved to where the mouse is. loopTrim() turns it into the parameters.
void BroodEditor::LoopView::moveMarker(float x) {
    const int len = juce::jmax(64, proc.ui.lsrc.load());
    const bool tape = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load()) == 1;
    auto* startP = proc.apvts.getParameter("start");
    auto* endP = proc.apvts.getParameter("loopEnd");
    auto* lengthP = proc.apvts.getParameter("length");
    if (startP == nullptr || endP == nullptr || lengthP == nullptr) return;
    const double at = static_cast<double>(x) / juce::jmax(1, getWidth()) * len + grabOffset;
    const double shortest = juce::jmax(64.0, 0.02 * len);
    double first = anchorStart, width = anchorEnd - anchorStart;
    if (dragging == 0) { first = juce::jlimit(0.0, anchorEnd - shortest, at); width = anchorEnd - first; }
    else if (dragging == 1) width = juce::jlimit(shortest, static_cast<double>(len) - anchorStart, at - anchorStart);
    else first = juce::jlimit(0.0, juce::jmax(0.0, len - width), at);
    const auto trim = loopTrim(tape, len, proc.apvts.getRawParameterValue("length")->load(), first, width);
    startP->setValueNotifyingHost(static_cast<float>(trim.start));
    endP->setValueNotifyingHost(static_cast<float>(trim.end));
    if (tape) lengthP->setValueNotifyingHost(lengthP->convertTo0to1(static_cast<float>(trim.length)));
    const double clock = juce::jmax(1.f, proc.ui.clockHz.load());
    windowText = juce::String(trim.firstSample / clock, 2) + " S IN, PLAYS " + juce::String(trim.width / clock, 2) + " S";
    if (onTouch) onTouch(dragging == 1 ? "loopEnd" : "start");
}

void BroodEditor::LoopView::paint(juce::Graphics& g) {
    // Pick up a new peak envelope if the audio thread has published one (a try-lock:
    // if it is busy, the next frame gets it).
    const uint32_t ver = proc.ui.waveVersion.load();
    if (ver != version && proc.copyWave(wave)) version = ver;
    // The take: an ink window, with what plays in pale and the rest of the take dimmed.
    const auto all = getLocalBounds().toFloat();
    const auto& k = stock();
    const juce::Colour dimmed = k.sunk.interpolatedWith(k.onSunk, 0.34f);
    g.setColour(k.sunk); g.fillRect(all);
    // A heading for what the looper is doing, in words as well as the switch's light (GAIN is
    // beside it, as on Silt's screen), so nothing in it ever covers the take or its markers.
    const auto b = all.withTrimmedTop(16.f);
    const float mid = b.getCentreY();
    const int lsrc = proc.ui.lsrc.load();
    const int led = proc.ui.loopLed.load();   // 0 off, 1 play, 2 rec, 3 dub
    // The live input while there's no take, and over one while a new take is being recorded
    // (RECORD recording or waiting for the bar line, or a CAPTURE hold).
    if (lsrc <= 0 || proc.ui.takeSoFar.load() >= 0 || proc.ui.recArmed.load()) {
        const bool record = proc.apvts.getRawParameterValue("recMode")->load() > 0.5f;
        const bool armed = proc.ui.recArmed.load();
        const juce::String bars = proc.describe("recBars").toUpperCase();
        // Nothing caught: the history scrolls past, newest at the right, over the window a tap
        // keeps. It's what the looper hears (the wet side too, when it's PRE), so it's what a
        // take would hold. In CAPTURE all of it is bright, since a tap keeps it; in RECORD only
        // what is being kept.
        constexpr int N = brood::kMaxLoopSamples, T = brood::kLiveColTicks;
        const int pos = proc.ui.histPos.load(), win = juce::jmax(T, proc.ui.memTicks.load()), kept = proc.ui.takeSoFar.load();
        const int px = juce::jmax(1, juce::roundToInt(b.getWidth()));
        const float rise = b.getHeight() / 2 - 10.f;
        const juce::Colour bright = k.onSunk;
        float loudest = 0;
        cols.resize(static_cast<size_t>(px));
        for (int x = 0; x < px; ++x) {
            const int64_t from = pos - static_cast<int64_t>(px - x) * win / px, to = pos - static_cast<int64_t>(px - x - 1) * win / px;
            float pk = 0;
            for (int64_t t = from; t < juce::jmax(to, from + 1); t += T) {
                const int at = static_cast<int>(((t % N) + N) % N);
                pk = std::max(pk, proc.livePeak(at / T));
            }
            loudest = std::max(loudest, pk);
            const int64_t age = pos - to;   // ticks back to this pixel's newest edge
            cols[static_cast<size_t>(x)] = {pk, kept >= 0 ? age < kept : !record};
        }
        const double clock = juce::jmax(1.f, proc.ui.clockHz.load());
        const juce::String keptText = juce::String(juce::jmax(0, kept) / clock, 2) + " S";
        if (loudest < 0.001f) {
            // Silence: the words where the waveform would be, as before.
            g.setColour(dimmed); g.fillRect(b.getX() + 12.f, mid + 14.f, b.getWidth() - 24.f, 1.f);
            g.setColour(k.onSunk); g.setFont(kit::label(18));
            const juce::String idle = record ? "PRESS LOOP TO RECORD_" : "LISTENING. PRESS LOOP TO KEEP THE LAST MOMENT_";
            const juce::String text = armed ? "WAITING FOR THE BAR LINE_"
                                    : led == 2 ? (record && bars != "FREE" ? "RECORDING " + bars + "_" : juce::String("RECORDING_")) : idle;
            g.drawText(text, b.withTrimmedBottom(16.f), juce::Justification::centred);
            return;
        }
        // A tick a second along the foot, as on a take.
        const double seconds = win / clock, every = seconds > 16.0 ? 5.0 : 1.0;
        g.setColour(k.sunk.interpolatedWith(k.onSunk, 0.6f));
        for (double t = every; t < seconds; t += every)
            g.fillRect(b.getRight() - b.getWidth() * static_cast<float>(t / seconds), b.getBottom() - 6.f, 1.f, 6.f);
        for (int x = 0; x < px; ++x) {
            const auto [pk, lit] = cols[static_cast<size_t>(x)];
            const float a = std::min(1.f, pk * 1.6f) * rise;
            g.setColour(lit ? bright : dimmed);
            g.fillRect(b.getX() + static_cast<float>(x), mid - a, 1.f, 2 * a + 1);
        }
        // Where the take being kept begins: it rides left as the take grows.
        if (kept >= 0) {
            const float sx = b.getRight() - b.getWidth() * static_cast<float>(kept) / static_cast<float>(win);
            g.setColour(k.lit); g.fillRect(juce::jlimit(2.f, b.getRight() - 4.f, sx - 1.f), b.getY(), 2.f, b.getHeight());
        }
        // The now line, at the right edge.
        g.setColour(k.lit); g.fillRect(b.getRight() - 1.f, b.getY(), 1.f, b.getHeight());
        const juce::String heading = armed ? juce::String("WAITING FOR THE BAR LINE")
                                   : led == 2 ? "RECORDING" + (record && bars != "FREE" ? " " + bars : juce::String()) + "  " + keptText
                                   : record ? juce::String("PRESS LOOP TO RECORD")
                                   : "LISTENING  A TAP KEEPS " + juce::String(seconds, 2) + " S";
        g.setFont(capFont());
        g.setColour(led == 2 || armed ? k.lit : k.onSunk);
        g.drawText(heading, all.withHeight(16.f).withTrimmedLeft(6.f), juce::Justification::centredLeft);
        return;
    }
    int s = 0, e = 0;
    markers(s, e);
    const float xs = b.getWidth() * s / lsrc, xe = b.getWidth() * e / lsrc;
    const double clock = juce::jmax(1.f, proc.ui.clockHz.load());
    // A tick a second along the foot (every five, on a long take), so length can be read.
    const double seconds = lsrc / clock, every = seconds > 16.0 ? 5.0 : 1.0;
    g.setColour(k.sunk.interpolatedWith(k.onSunk, 0.6f));
    for (double t = every; t < seconds; t += every)
        g.fillRect(b.getWidth() * static_cast<float>(t / seconds), b.getBottom() - 6.f, 1.f, 6.f);
    const int n = BroodProcessor::kWaveBins;
    const float bw = b.getWidth() / n;
    const float gain = juce::Decibels::decibelsToGain(proc.apvts.getRawParameterValue("loopDb")->load());   // what you see is the level you hear
    const float rise = b.getHeight() / 2 - 10.f;
    for (int i = 0; i < n; ++i) {
        const float a = std::min(1.f, wave[static_cast<size_t>(i)] * 1.6f * gain) * rise;
        const float x = i * bw;
        g.setColour(x >= xs && x < xe ? k.onSunk : dimmed);
        g.fillRect(x, mid - a, std::max(1.f, bw - 0.4f), 2 * a + 1);
    }
    // The play head: where the loop is heard from. TAPE has one, the playhead; STRETCH shows
    // both grains and ENV the slice (and the last one fading), each as bright as it's loud.
    const int winStart = proc.ui.winStart.load();
    for (size_t h = 0; h < 2; ++h) {
        const float level = juce::jlimit(0.f, 1.f, proc.ui.headLevel[h].load());
        if (level < 0.02f) continue;
        const float px = b.getWidth() * (winStart + proc.ui.head[h].load()) / lsrc;
        g.setColour(k.lit.withMultipliedAlpha(level)); g.fillRect(px - 0.5f, b.getY(), 1.f, b.getHeight());
    }
    static const char* const states[] = {"STOPPED", "PLAYING", "RECORDING", "OVERDUB"};
    g.setFont(capFont());
    g.setColour(led >= 2 ? k.lit : k.onSunk);
    // A transport take says so, and says it's waiting while the host is stopped.
    const bool parked = proc.ui.parked.load();
    const juce::String status = parked ? juce::String("WAITING FOR PLAY") : juce::String(states[juce::jlimit(0, 3, led)]);
    const juce::String follows = proc.ui.follows.load() ? juce::String("  FOLLOWS THE HOST") : juce::String();
    g.drawText(status + "  " + juce::String(seconds, 2) + " S" + follows, all.withHeight(16.f).withTrimmedLeft(6.f), juce::Justification::centredLeft);
    // The markers: a rule the full height, and a tab that names it — START's tab
    // hangs inside the window from the top, END's from the bottom, so the two never
    // cover each other even when the window is a sliver.
    auto marker = [&](float x, bool isEnd, bool lit) {
        const float w = lit ? 3.f : 2.f;
        g.setColour(lit ? k.lit : k.onSunk);
        // Never on the very edge, where a pale rule would eat into the window's outline.
        g.fillRect(juce::jlimit(2.f, b.getRight() - w - 2.f, x - w * 0.5f), b.getY(), w, b.getHeight());
        const juce::String name = isEnd ? "END" : "START";
        g.setFont(capFont());
        const float tw = static_cast<float>(kit::textWidth(g.getCurrentFont(), name) + 8);
        auto tab = juce::Rectangle<float>(tw, 13.f).withY(isEnd ? b.getBottom() - 13.f : b.getY());
        tab.setX(juce::jlimit(2.f, b.getRight() - tw - 2.f, isEnd ? x - tw : x));
        g.setColour(lit ? k.lit : k.onSunk); g.fillRect(tab);
        g.setColour(k.sunk); g.drawText(name, tab, juce::Justification::centred);
    };
    marker(xs, false, hot == 0 || dragging == 0);
    marker(xe, true, hot == 1 || dragging == 1);
}

// ───────── editor ─────────
BroodEditor::BroodEditor(BroodProcessor& p)
    : AudioProcessorEditor((installFaces(), p)), proc(p),   // the faces first: members set fonts as they're built
      look(stock(), p), presets(p), presetBar(presets), presetBrowser(presets, stock()),
      benchStrip(stock(), "BROOD", "", "BROOD"), loopView(p) {
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
        k.slider->getProperties().set("dark", juce::String(id) == "clock");   // one clock, between the two sides
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
    recMode = std::make_unique<ThreeWay>(*proc.apvts.getParameter("recMode"), juce::StringArray{"Capture", "Record"}, [this] {
        const bool record = proc.apvts.getRawParameterValue("recMode")->load() > 0.5f;
        showReadout("CAPTURE / RECORD", record ? "record: tap, play, tap. or hold while you play" : "capture: a tap keeps the recent past");
    });
    addAndMakeVisible(*recMode);
    recBars = std::make_unique<Stepper>(*proc.apvts.getParameter("recBars"), "BARS",
                                        juce::StringArray{"FREE", "1", "2", "3", "4", "6", "8", "12", "16"}, [this] {
        showReadout("RECORD", proc.describe("recBars") + (proc.describe("recBars") == "free" ? ": until the next tap" : ", then it plays"));
    });
    addChildComponent(*recBars);
    recAtTap.getProperties().set("plate", true);
    recAtTap.onClick = [this] { showReadout("RECORD", recAtTap.getToggleState() ? "bars start at the tap" : "bars start on the next bar line"); };
    addChildComponent(recAtTap);
    recAtTapAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "recAtTap", recAtTap);
    fsWet = std::make_unique<Footswitch>(*proc.apvts.getParameter("wetSwitch"), "WET", "A");
    fsLoop = std::make_unique<Footswitch>(*proc.apvts.getParameter("loopSwitch"), "LOOP", "L");
    addAndMakeVisible(*fsWet); addAndMakeVisible(*fsLoop);
    overdub.getProperties().set("plate", true);
    overdub.onClick = [this] { showReadout("OVERDUB", overdub.getToggleState() ? "layering until you switch it off" : "off"); };
    addAndMakeVisible(overdub);
    overdubAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "overdub", overdub);
    for (auto* b : {&clearTake, &undoTake}) loopView.addAndMakeVisible(*b);
    clearTake.onClick = [this] { pulse("clear"); showReadout("LOOP", "cleared. undo brings it back"); };
    undoTake.onClick = [this] {
        static const char* const what[] = {"", "the take before", "the cleared take", "the layer"};
        const int kind = juce::jlimit(0, 3, proc.ui.undoKind.load());
        const bool redo = proc.ui.undoRedo.load();
        pulse("undo");
        showReadout(redo ? "REDO" : "UNDO", juce::String(redo ? "again: " : "back to ") + (kind == 3 ? (redo ? "the layer is back" : "before the layer") : what[kind]));
    };
    addAndMakeVisible(loopView);
    // Normalise, as on Silt: the take's loudest moment to -1 dB.
    gainBox = std::make_unique<GainBox>(*proc.apvts.getParameter("loopDb"), [this] {
        std::array<float, BroodProcessor::kWaveBins> peaks {};
        const float peak = proc.copyWave(peaks) ? *std::max_element(peaks.begin(), peaks.end()) : 0.f;
        return peak > 1e-5f ? juce::jlimit(-24.f, 12.f, -1.f - juce::Decibels::gainToDecibels(peak)) : 0.f;
    });
    loopView.addAndMakeVisible(*gainBox);

    inputGain.setSliderStyle(juce::Slider::LinearHorizontal);
    inputGain.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    inputGain.setRange(0, 15.85);   // up to +24 dB, with unity in the middle of the travel
    inputGain.setSkewFactorFromMidPoint(1.0);
    inputGain.setDoubleClickReturnValue(true, 1.0);
    inputGain.setValue(proc.inputGain.load(), juce::dontSendNotification);
    inputGain.setTitle("Input level");
    inputGain.onValueChange = [this] {
        proc.inputGain = static_cast<float>(inputGain.getValue());
        const auto db = juce::Decibels::gainToDecibels(proc.inputGain.load(), -100.f);
        showReadout("LEVEL", juce::String(db, 1) + " dB" + (db > 0.05f ? ", into old tape" : ""));
        repaintDesign(hoodArea);
    };
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
    loopView.onTouch = [this](const juce::String& id) {
        touch(id);
        if (loopView.windowText.isNotEmpty()) showReadout(loopView.dragging == 0 ? "START" : loopView.dragging == 1 ? "END" : "THE WINDOW", loopView.windowText);
        loopView.repaint();
    };
    // One row of four switches. DRY KILL was MIX at the top of its travel, LATCH is
    // what FREEZE does, SPREAD is WIDTH above the middle, and SYNC sits beside the
    // knob it governs. FREEZE and CLASSIC came up from under the hood: they are
    // switches, and the hood is now sliders only.
    struct DipDef { const char* id; const char* label; const char* on; const char* off; };
    static constexpr DipDef dipDefs[] = {
        {"trails", "Trails", "tails keep falling", "tails cut"},
        {"smooth", "Smooth", "clock glides", "clock steps"},
        {"freeze", "Freeze", "held, ringing", "moving"},
    };
    for (size_t i = 0; i < dips.size(); ++i) {
        const auto& def = dipDefs[i];
        auto& d = dips[i];
        d.button = std::make_unique<juce::ToggleButton>(juce::String(def.label).toUpperCase());
        d.button->getProperties().set("plate", true);
        d.button->onClick = [this, def, b = d.button.get()] {
            showReadout(juce::String(def.label).toUpperCase(), b->getToggleState() ? def.on : def.off);
        };
        addAndMakeVisible(*d.button);
        d.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, def.id, *d.button);
    }
    // Turning SMOOTH on starts from the step CLOCK is on, so the click doesn't move the sound.
    dips[1].button->onClick = [this, shown = dips[1].button->onClick] {
        if (shown) shown();
        if (!dips[1].button->getToggleState()) return;
        if (auto* clock = proc.apvts.getParameter("clock")) {
            clock->beginChangeGesture();
            clock->setValueNotifyingHost(static_cast<float>(brood::clockTarget(clock->getValue(), false)));
            clock->endChangeGesture();
        }
    };
    // WEAR: one line from clean to worn, not two opposing checkboxes.
    character = std::make_unique<Stepper>(*proc.apvts.getParameter("character"), "WEAR", juce::StringArray { "CLEAN", "PEDAL", "WORN" }, [this] {
        const float v = proc.apvts.getRawParameterValue("character")->load();
        showReadout("WEAR", v < 0.25f ? "clean: fewer artefacts, wet a little later" : v > 0.75f ? "worn: a noisy clock, a restless loop, a longer reverb" : "the pedal as built");
        repaintDesign(hoodArea);   // NOISE says a figure only while it's worn
    });
    addAndMakeVisible(*character);
    // On WORN: where the wear sits, before the loop (recorded) or after it (the loop stays clean).
    wearPost = std::make_unique<ThreeWay>(*proc.apvts.getParameter("wearPost"), juce::StringArray{"Pre loop", "Post loop"}, [this] {
        if (isVisible()) showReadout("PRE / POST LOOP", proc.describe("wearPost"));
    });
    addChildComponent(*wearPost);
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
    makeSyncSwitch(wetSync, "sync", "SYNC", wetSyncAtt);
    makeSyncSwitch(envSync, "envSync", "SYNC", envSyncAtt);
    // TAPE ECHO, beside SYNC in DELAY: TIME glides and bends the echo's pitch.
    tapeDelay.setButtonText("TAPE");
    tapeDelay.getProperties().set("plate", true);
    tapeDelay.onClick = [this] { showReadout("TAPE", tapeDelay.getToggleState() ? "tape echo: TIME bends the pitch" : "digital: TIME crossfades"); };
    addChildComponent(tapeDelay);
    tapeDelayAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "tapeDelay", tapeDelay);
    envSoft.setButtonText("SOFT");
    envSoft.getProperties().set("plate", true);
    envSoft.onClick = [this] { showReadout("SOFT", envSoft.getToggleState() ? "slices fade" : "slices cut"); };
    addChildComponent(envSoft);
    envSoftAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "envSoft", envSoft);
    // Modulation: two sources, the selected one's controls showing.
    for (int k = 0; k < 2; ++k) {
        const juce::String m = "mod" + juce::String(k + 1), who = "MOD " + juce::String(k + 1);
        chips[k] = std::make_unique<ModChip>(*this, k);
        addAndMakeVisible(*chips[k]);
        auto& c = sources[static_cast<size_t>(k)];
        c.wave = std::make_unique<Stepper>(*proc.apvts.getParameter(m + "Wave"), "SHAPE", juce::StringArray{"TRI", "SQR", "SIN", "SAW", "RND", "SMTH"},
            [this, who, m] { if (isVisible()) showReadout(who + " SHAPE", proc.apvts.getParameter(m + "Wave")->getCurrentValueAsText().toLowerCase()); });
        addChildComponent(*c.wave);
        c.rate.setSliderStyle(juce::Slider::LinearHorizontal);
        c.rate.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        c.rate.setTitle(who + " rate");
        c.rate.setDoubleClickReturnValue(true, static_cast<double>(proc.apvts.getParameter(m + "Rate")->getDefaultValue()));
        c.rate.onValueChange = [this, m] { touch(m + "Rate"); };
        addChildComponent(c.rate);
        c.rateAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, m + "Rate", c.rate);
        const std::pair<const char*, const char*> defs[] = {{"Uni", "UNI"}, {"Sync", "SYNC"}, {"Hold", "HOLD"}};
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

    // The bottom strip: LEVEL (above), then the four sliders the pedal keeps behind both footswitches.
    const std::pair<const char*, const char*> hoodDefs[] = {
        {"tone", "TONE"}, {"fade", "FADE"}, {"width", "WIDTH"}, {"wearNoise", "NOISE"}};
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
    // WET DIV and LOOP DIV: a divided clock for each side, at the foot of its column.
    auto divStep = [this](const char* id, const char* caption) {
        auto st = std::make_unique<Stepper>(*proc.apvts.getParameter(id), caption, juce::StringArray{"1x", "2x", "4x"},
                                            [this, id, caption] { if (isVisible()) showReadout(caption, proc.describe(id)); });
        st->capWidth = 60;
        addAndMakeVisible(*st);
        return st;
    };
    wetDivStep = divStep("wetDiv", "WET DIV");
    loopDivStep = divStep("loopDiv", "LOOP DIV");
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(W * 0.75f), juce::roundToInt(H * 0.75f), 2000, 1367);   // 0.7x to about 1.67x of the design panel
    getConstrainer()->setFixedAspectRatio(static_cast<double>(W) / H);
    setSize(W, H);
    presetBar.refresh();
    updateModeControls();
    updateBench();
    updateTakeControls();   // greyed as they should be from the first frame
    startTimerHz(60);
}

BroodEditor::~BroodEditor() {
    stopTimer();
    fsWet->press(false); fsLoop->press(false);
    setLookAndFeel(nullptr);
}

// A knob says what it does in this mode: MODIFY alone tells you nothing.
juce::String BroodEditor::knobName(size_t i) const {
    const juce::String id = knobs[i].id;
    if (id == "modWet") {
        static const char* names[] = {"SMEAR", "FEEDBACK", "RATE", "STITCH"};
        return names[juce::jlimit(0, 3, static_cast<int>(proc.apvts.getRawParameterValue("wetMode")->load()))];
    }
    if (id == "modLoop") {
        static const char* names[] = {"SENSITIVITY", "SPEED", "STRETCH"};
        return names[juce::jlimit(0, 2, static_cast<int>(proc.apvts.getRawParameterValue("loopMode")->load()))];
    }
    return knobs[i].label;
}

void BroodEditor::touch(const juce::String& id) {
    lastTouched = id;
    // The readout's own names for the parameters, so it never shows a raw id.
    static const std::pair<const char*, const char*> names[] = {
        {"loopEnd", "END"}, {"start", "START"},
        {"balance", "BALANCE"}, {"mod1Rate", "MOD 1 RATE"}, {"mod2Rate", "MOD 2 RATE"}, {"wetDiv", "WET DIV"}, {"loopDiv", "LOOP DIV"}, {"wearNoise", "NOISE"},
    };
    juce::String label = id.toUpperCase();
    for (const auto& [key, name] : names) if (id == key) label = name;
    for (size_t i = 0; i < knobs.size(); ++i) if (knobs[i].id == id) label = knobName(i);
    showReadout(label, proc.describe(id), true);
    if (id == "clock") repaintDesign(pedalArea);
    // The small readouts beside the trim and hood sliders are drawn by this
    // editor rather than by the control, so they need a repaint to follow it.
    if (id == "memory" || id == "start" || id == "loopEnd" || id == "balance") repaintDesign(loopArea);
    if (id == "mod1Rate" || id == "mod2Rate") repaintDesign(rampArea);
    if (id == "tone" || id == "fade" || id == "width" || id == "wetDiv" || id == "loopDiv" || id == "wearNoise") repaintDesign(hoodArea);
    if (id == "wetDiv") repaintDesign(pedalArea);   // TIME's readout counts WET DIV in
}

void BroodEditor::showReadout(const juce::String& label, const juce::String& value, bool follows) {
    readoutFollows = follows;
    if (label == lcdLabel && value == lcdValue) return;
    lcdLabel = label; lcdValue = value;
    repaintDesign(lcdArea);
}

juce::Rectangle<int> BroodEditor::valueRect(int knob) const {
    const auto kb = knobDesign[juce::jlimit(0, 5, knob)];
    return { kb.getCentreX() - 118, kb.getBottom() - 2, 236, 18 };
}

// What each knob reads at rest, followed through automation, presets and mode changes.
void BroodEditor::updateKnobValues() {
    for (size_t i = 0; i < knobs.size(); ++i)
        if (auto text = proc.describe(knobs[i].id).toUpperCase(); text != knobValues[i]) {
            knobValues[i] = std::move(text);
            repaintDesign(valueRect(static_cast<int>(i)));
        }
}

juce::RangedAudioParameter* BroodEditor::depthParam(int k, int t) const {
    return proc.apvts.getParameter("mod" + juce::String(k + 1) + "_" + brood::kModTargetIds[t]);
}

void BroodEditor::selectSource(int k) {
    selectedMod = k;
    for (int i = 0; i < 2; ++i) {
        auto& c = sources[static_cast<size_t>(i)];
        const bool shown = modOpen && i == k;
        c.wave->setVisible(shown);
        c.rate.setVisible(shown);
        for (auto& b : c.switches) b->setVisible(shown);
        chips[i]->repaint();
    }
    repaintDesign(rampArea);
    if (isVisible()) rebuildHoverHelp();
}

void BroodEditor::chipClicked(int k) {
    modOpen = !(modOpen && selectedMod == k);
    selectSource(k);
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
bool BroodEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& path : files) if (proc.canReadAudio(juce::File(path))) return true;
    return false;
}

void BroodEditor::fileDragEnter(const juce::StringArray&, int, int) { fileHovering = true; repaint(); }
void BroodEditor::fileDragExit(const juce::StringArray&) { fileHovering = false; repaint(); }

void BroodEditor::filesDropped(const juce::StringArray& files, int, int) {
    fileHovering = false;
    repaint();
    for (const auto& path : files) {
        const juce::File file(path);
        if (!proc.canReadAudio(file)) continue;
        showReadout(file.getFileName(), proc.takeMoment(file) ? "it's the loop now" : "could not read it");
        return;
    }
}

void BroodEditor::paintOverChildren(juce::Graphics& g) {
    if (fileHovering) {
        const auto all = getLocalBounds();
        g.setColour(kPaper.withAlpha(0.8f));
        g.fillRect(all);
        g.setColour(kInk);
        g.drawRect(all.reduced(10), 3);
        g.setFont(font(34));
        g.drawText("DROP TO MAKE IT THE LOOP", all, juce::Justification::centred);
    }
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
    if (now - benchCheckedAt >= 1000.0) { benchCheckedAt = now; waiting = kit::bench::latest(); }
    const bool fresh = waiting.has_value() && waiting->stamp != momentTaken;
    const int caught = proc.ui.lsrc.load() >= 64 ? 1 : 0;
    const juce::String state = caught == 1 ? juce::String(proc.ui.winLen.load() / proc.ui.clockHz.load(), 2) + " S CAUGHT" : juce::String("LISTENING");
    const auto capture = proc.outputCaptureState();
    const bool bars = benchSource.getSelectedId() > 1;
    // The newest moment may be what this Brood just sent: TAKE brings it back into the loop.
    const bool ours = fresh && waiting->fromChip == "E-01" && momentSent != 0 && waiting->stamp >= momentSent - 5000;
    const juce::String fromThere = !fresh ? juce::String()
                                 : ours ? "SENT " + juce::String(waiting->seconds, 2) + " S. TAKE TO RESAMPLE"
                                 : "FROM " + waiting->fromChip + " " + waiting->fromName + " / " + juce::String(waiting->seconds, 2) + " S";
    if (capture == Capture::waiting) benchStrip.set(state, "WAITING FOR THE NEXT BAR", "STOP", true, fresh);
    else if (capture == Capture::recording) benchStrip.set(state, "RECORDING " + juce::String(juce::roundToInt(proc.outputCaptureProgress() * 100.f)) + "%", "STOP", true, fresh);
    else benchStrip.set(state, fresh ? fromThere : momentSent != 0 ? "SENT: AN INSTRUMENT CAN TAKE IT" : "NOTHING WAITING", "SEND >", bars || caught == 1, fresh);
}

void BroodEditor::updateModeControls() {
    const int wm = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load());
    const int lm = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load());
    const int worn = proc.apvts.getRawParameterValue("character")->load() > 0.75f ? 1 : 0;
    const int record = proc.apvts.getRawParameterValue("recMode")->load() > 0.5f ? 1 : 0;
    if (wm == lastModeWet && lm == lastModeLoop && worn == lastWorn && record == lastRec) return;
    lastModeWet = wm; lastModeLoop = lm; lastWorn = worn; lastRec = record;
    recBars->setVisible(record == 1);   // bars and the bar line are RECORD's alone
    // MEMORY is CAPTURE's: RECORD's length is the bars, or the second tap, up to 32 s.
    memory.setEnabled(record == 0);
    memory.setAlpha(record == 0 ? 1.f : 0.35f);
    repaintDesign(loopArea);
    recAtTap.setVisible(record == 1);
    wearPost->setVisible(worn == 1);   // only WORN has anything to keep out of the loop
    wetSync.setVisible(wm == 1 || wm == 3);
    tapeDelay.setVisible(wm == 1);
    resized();   // SYNC moves over to share its row with TAPE in DELAY
    dips[0].button->setVisible(wm != 2);   // TRAILS: SLIP has no tail to keep
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
    // In eighths, so the pixel type stays on a grid rather than going soft between sizes.
    viewScale = juce::jmax(0.75f, std::floor(std::min(getWidth() / static_cast<float>(W), getHeight() / static_cast<float>(H)) * 8.f) / 8.f);
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

    // ── the main sheet ──
    // The take across the top with the two footswitches under it, so what you press sits
    // against what it does. Then the three columns: modes, and the knobs in two rows, each
    // value under its knob and each switch under the value it belongs with.
    loopViewDesign = {pedalArea.getX() + 14, pedalArea.getY() + 8, pedalArea.getWidth() - 28, 100};
    place(loopView, loopViewDesign);
    const auto gainLocal = juce::Rectangle<int>(130, 16).withPosition(loopViewDesign.getWidth() - 130, 0);
    gainBox->setBounds(gainLocal);
    // CLEAR and UNDO on the heading, left of GAIN.
    const auto undoLocal = juce::Rectangle<int>(52, 15).withPosition(gainLocal.getX() - 6 - 52, 0);
    const auto clearLocal = undoLocal.withX(undoLocal.getX() - 4 - 56).withWidth(56);
    undoTake.setBounds(undoLocal); clearTake.setBounds(clearLocal);
    undoDesign = undoLocal.translated(loopViewDesign.getX(), loopViewDesign.getY());
    clearDesign = clearLocal.translated(loopViewDesign.getX(), loopViewDesign.getY());
    gainDesign = gainLocal.translated(loopViewDesign.getX(), loopViewDesign.getY());
    const int cx[3] = {pedalArea.getX() + 138, pedalArea.getX() + 414, pedalArea.getX() + 690};
    fsWetDesign = juce::Rectangle<int>(248, 46).withCentre({cx[0], 245});
    // LOOP gives up some width for OVERDUB beside it, the plate the same height as the switch.
    fsLoopDesign = juce::Rectangle<int>(176, 46).withPosition(cx[2] - 124, 222);
    overdubDesign = juce::Rectangle<int>(70, 43).withPosition(fsLoopDesign.getRight() + 2, 222);
    place(*fsWet, fsWetDesign);
    place(*fsLoop, fsLoopDesign);
    place(overdub, overdubDesign);
    // Between them, what's true of both sides: how worn the one clock is.
    characterDesign = juce::Rectangle<int>(138, 24).withCentre({cx[1], 234});   // 8 wider for PEDAL in Chivo Mono
    place(*character, characterDesign);
    wearPostDesign = juce::Rectangle<int>(160, 20).withCentre({cx[1], characterDesign.getCentreY() + 24});
    place(*wearPost, wearPostDesign);
    constexpr int knobSize = 104, firstKnobTop = 342, secondKnobTop = 510;
    for (int i = 0; i < 6; ++i) {
        knobDesign[i] = juce::Rectangle<int>(knobSize, knobSize).withCentre({cx[i % 3], (i < 3 ? firstKnobTop : secondKnobTop) + knobSize / 2});
        place(*knobs[static_cast<size_t>(i)].slider, knobDesign[i]);
    }
    // In DELAY, SYNC shares the row with TAPE; in WEAVE it sits alone in the middle.
    const bool delayRow = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load()) == 1;
    wetSyncDesign = juce::Rectangle<int>(54, 19).withCentre({knobDesign[0].getCentreX() - (delayRow ? 29 : 0), knobDesign[0].getBottom() + 26});
    tapeDelayDesign = juce::Rectangle<int>(54, 19).withCentre({knobDesign[0].getCentreX() + 29, knobDesign[0].getBottom() + 26});
    place(tapeDelay, tapeDelayDesign);
    envSyncDesign = juce::Rectangle<int>(54, 19).withCentre({knobDesign[2].getCentreX() - 29, knobDesign[2].getBottom() + 26});
    envSoftDesign = juce::Rectangle<int>(54, 19).withCentre({knobDesign[2].getCentreX() + 29, knobDesign[2].getBottom() + 26});
    place(wetSync, wetSyncDesign);
    place(envSync, envSyncDesign);
    place(envSoft, envSoftDesign);
    wetModeDesign = {cx[0] - 124, 292, 248, 28};
    routingDesign = {cx[1] - 124, 292, 248, 28};
    loopModeDesign = {cx[2] - 124, 292, 248, 28};
    place(*wetMode, wetModeDesign);
    place(*routing, routingDesign);
    place(*loopMode, loopModeDesign);
    // The switches, each with what it acts on: FREEZE and TRAILS under the wet column, SMOOTH
    // under CLOCK.
    const int switchY = knobDesign[3].getBottom() + 18;
    dipCellDesign[2] = {cx[0] - 124, switchY, 54, 19};   // FREEZE
    dipCellDesign[0] = {cx[0] - 66, switchY, 54, 19};    // TRAILS
    dipCellDesign[1] = juce::Rectangle<int>(66, 19).withCentre({cx[1], switchY + 10});   // SMOOTH
    for (size_t i = 0; i < dips.size(); ++i) place(*dips[i].button, dipCellDesign[i]);
    // WET DIV beside TRAILS; LOOP DIV mirrors it in the loop column.
    wetDivDesign = {dipCellDesign[0].getRight() + 6, switchY - 3, 130, 25};
    loopDivDesign = wetDivDesign.withX(2 * cx[1] - wetDivDesign.getRight());
    place(*wetDivStep, wetDivDesign);
    place(*loopDivStep, loopDivDesign);

    // ── the side column ──
    // The readout, then LOOP and MODULATION, in the order you reach for them while playing.
    // The readout lines up with the take and MODULATION with the foot of the main sheet, so
    // the two columns share their edges.
    auto side = juce::Rectangle<int>(864, 108, 312, 548);
    lcdArea = side.removeFromTop(112);
    side.removeFromTop(10);
    loopArea = side.removeFromTop(113);
    side.removeFromTop(10);
    rampArea = side;

    // 01 LOOP: two rows of label / slider / figure on the same three columns so the figures line up.
    constexpr int labelW = 74, figureW = 44;
    auto inner = loopArea.withTrimmedTop(kit::panelHeadHeight).reduced(12, 6);
    inner.removeFromTop(4);
    auto trimRow = [&](juce::Rectangle<int>& out, juce::Slider& sl) {
        out = inner.removeFromTop(21).withTrimmedLeft(labelW).withTrimmedRight(figureW);
        place(sl, out);
        inner.removeFromTop(4);
    };
    trimRow(memoryDesign, memory);
    trimRow(balanceDesign, balance);
    balanceDesign = balanceDesign.withTrimmedLeft(kBalanceEnd);   // LOOP is printed at the left end
    place(balance, balanceDesign);
    auto recRow = inner.removeFromTop(21);
    recModeDesign = recRow.removeFromLeft(120);
    recRow.removeFromLeft(6);
    recBarsDesign = recRow.removeFromLeft(104);
    recRow.removeFromLeft(6);
    recAtTapDesign = recRow;
    place(*recMode, recModeDesign);
    place(*recBars, recBarsDesign);
    place(recAtTap, recAtTapDesign.withSizeKeepingCentre(recAtTapDesign.getWidth(), 18));

    // MODULATION: the two sources and the route list; under them, once a chip is clicked,
    // that source's shape, rate and switches.
    auto ramp = rampArea.withTrimmedTop(kit::panelHeadHeight).reduced(10, 7);
    auto rampRow = ramp.removeFromTop(120);
    routesDesign = rampRow.removeFromRight(60);
    rampRow.removeFromRight(6);
    const int chipW = (rampRow.getWidth() - 6) / 2;
    chipDesign[0] = rampRow.removeFromLeft(chipW);
    rampRow.removeFromLeft(6);
    chipDesign[1] = rampRow;
    place(*chips[0], chipDesign[0]);
    place(*chips[1], chipDesign[1]);
    place(routesButton, routesDesign);
    ramp.removeFromTop(18);
    rampRow = ramp.removeFromTop(28);
    const int half = (rampRow.getWidth() - kSp2) / 2;
    rampWaveDesign = rampRow.removeFromLeft(half);
    rampRow.removeFromLeft(kSp2);
    rampRateDesign = rampRow;                    // "RATE" and its figure are painted at the ends
    ramp.removeFromTop(18);
    rampRow = ramp.removeFromTop(26);
    // Three even cells: UNI, SYNC, HOLD.
    const int swTotal = rampRow.getWidth();
    for (size_t i = 0; i < 3; ++i)
        rampSwitchDesign[i] = i + 1 == 3 ? rampRow : rampRow.removeFromLeft((swTotal - 2 * kSp2) / 3 + kSp2).withTrimmedRight(kSp2);
    for (auto& c : sources) {
        place(*c.wave, rampWaveDesign);
        place(c.rate, rampRateDesign.withTrimmedLeft(36).withTrimmedRight(40).reduced(0, 3));
        for (size_t i = 0; i < c.switches.size(); ++i) place(*c.switches[i], rampSwitchDesign[i]);
    }

    // ── the bottom strip ──
    // A band across the whole panel, always open: LEVEL, then TONE, FADE, WIDTH and NOISE,
    // in five even cells.
    hoodArea = {24, 668, 1152, 67};
    const int hoodLeft = hoodArea.getX() + 14;
    const int cell = (hoodArea.getRight() - 14 - hoodLeft - 4 * 24) / 5;
    for (int i = 0; i < 5; ++i) hoodCellDesign[i] = {hoodLeft + i * (cell + 24), hoodArea.getY() + 8, cell, 52};
    inputGainDesign = hoodCellDesign[0].withY(hoodArea.getY() + 30).withHeight(26);
    place(inputGain, inputGainDesign);
    for (size_t i = 0; i < hood.size(); ++i) place(hood[i].slider, hoodCellDesign[i + 1].withY(hoodArea.getY() + 30).withHeight(26));
    updateKnobValues();
    rebuildHoverHelp();
}


// Everything worth explaining: the switches, the steppers, the sliders whose label
// is only a label, the source row, the footswitches, and the six knobs in their whispers.
void BroodEditor::rebuildHoverHelp() {
    hoverHelp.clear();
    auto add = [this](juce::Rectangle<int> r, const char* label, const char* text) {
        hoverHelp.push_back({r, juce::String(label), juce::String(text)});
    };
    static const char* dipText[] = {
        "The tails keep falling after you switch the wet side off.",
        "The sample rate slides instead of stepping, so CLOCK bends pitch as it turns.",
        "The hold gesture without the holding: the wet side comes up frozen and rings on its own.",
    };
    static const char* dipNames[] = {"TRAILS", "SMOOTH", "FREEZE"};
    add(characterDesign, "WEAR", "Clean to worn. Clean= fewer artefacts. Worn= noise, a loop that warbles and a longer reverb.");
    for (size_t i = 0; i < dips.size(); ++i) if (dips[i].button->isVisible()) add(dipCellDesign[i], dipNames[i], dipText[i]);
    if (wearPost->isVisible())
        add(wearPostDesign, "PRE / POST LOOP", "Where the wear sits. Pre: the loop records it worn. Post: you hear it worn, but the loop keeps a clean take.");
    // These two are only in the table while their mode is on, so help never explains
    // a control that is not on the panel.
    if (tapeDelay.isVisible())
        add(tapeDelayDesign.expanded(4), "TAPE", "A tape echo: turning TIME bends the pitch, with wow, flutter and saturation. Off: TIME changes the delay without changing the pitch.");
    if (wetSync.isVisible())
        add(wetSyncDesign.expanded(4), "SYNC", "TIME becomes a division of the beat, so the echoes or threads land on the grid.");
    if (envSync.isVisible())
        add(envSyncDesign.expanded(4), "SYNC", "The envelope slices by a division of the beat instead of a free length, so the loop stutters in time.");
    if (envSoft.isVisible())
        add(envSoftDesign.expanded(4), "SOFT", "The slices fade in and out instead of cutting: a rounder, softer stutter with no clicks.");

    add(chipDesign[0], "MOD 1", "Tap for its shape and rate. Drag it onto a control, or drag around a knob's edge, to route it. The square is on or off.");
    add(chipDesign[1], "MOD 2", "Tap for its shape and rate. Drag it onto a control, or drag around a knob's edge, to route it. The square is on or off.");
    add(routesDesign, "ROUTES", "Every route in one list, with its depth.");
    if (modOpen) add(rampWaveDesign, "SHAPE", "Click to step the shape: triangle, square, sine, sawtooth, random, smooth random.");
    if (modOpen) add(rampRateDesign, "RATE", "How slowly this source turns. With SYNC on it becomes a span of the beat.");
    static const char* rampSwitchText[] = {
        "The swing sits above the knob instead of either side of it.",
        "The rate becomes a division of the beat: eight bars down to a sixteenth.",
        "This source stops where it is and its knobs stay pushed there. CC 52 holds both.",
    };
    static const char* rampSwitchNames[] = {"UNI", "SYNC", "HOLD"};
    if (modOpen) for (int i = 0; i < 3; ++i) add(rampSwitchDesign[i], rampSwitchNames[i], rampSwitchText[i]);

    add(memoryDesign.expanded(0, 2), "MEMORY", "In CAPTURE mode, MEMORY relates to how much of the input audio is captured. All the way left for short loops, and all the way right for loops of up to 30 seconds.");
    add(gainDesign, "GAIN", "The loop's own level. Drag or scroll; hold Shift for fine. Double-click for 0 dB, right-click to normalise.");
    // CLEAR and UNDO sit inside the window, and the first match wins, so they go first.
    add(clearDesign, "CLEAR", "Empties the take. UNDO brings it back.");
    add(undoDesign, "UNDO", "One step back: a clear, a new take, or the last overdub layer. Press again to redo.");
    add(loopViewDesign, "THE WINDOW", "Drag START or END to trim the loop. Double-click for the whole take.");
    add(recModeDesign, "CAPTURE / RECORD", "CAPTURE= a tap keeps what you just played. RECORD= tap to start, tap to play, or hold to overdub.");
    if (recBars->isVisible()) {
        add(recBarsDesign, "BARS", "Free records until the next tap. A number records that many bars, then plays.");
        add(recAtTapDesign, "AT TAP", "Off= a bar count waits for the next bar line, so loops land on the grid. On= it starts the moment you tap.");
    }
    add(balanceDesign.withTrimmedLeft(-kBalanceEnd).expanded(0, 2), "BALANCE", "Left= just the loop. Right= just the wet side. Middle= both in full. Drag up or down on it to add modulation.");

    add(hoodCellDesign[0], "LEVEL", "How hard the input hits the machine. Past the middle it's driven into old tape: rounder, duller, a little hiss.");

    add(wetModeDesign, "WET MODE", "Four ways of treating time: reverb, delay, slip, weave.");
    add(routingDesign, "ROUTING", "What the wet side hears. IN= the input. IN + LOOP= input and loop. LOOP= just the loop.");
    add(loopModeDesign, "LOOP MODE", "Three ways of holding a moment: envelope, tape, stretch.");

    add(fsWetDesign, "WET", "Tap for the wet side, hold to freeze it where it stands. Key A.");
    add(fsLoopDesign, "LOOP", "Tap to catch the recent past, tap again to stop, hold to overdub. Key L.");
    add(overdubDesign, "OVERDUB", "Latches: layers into the playing loop until you switch it off. Needs a loop playing.");

    add(benchSourceDesign, "SEND", "What SEND sends: the loop, or the exact output for that many bars, from the next bar line.");
    add(presetBarDesign, "PATCH", "Click the name to search, star and audition sounds. The arrows step through them. SAVE keeps what you hear.");

    static const char* hoodText[] = {
        "A high-cut on the wet side. Right is open, left is soft and far away.",
        "How much of the old take an overdub erases. Left keeps the pedal's slow erosion, right replaces it.",
        "How far apart the two speakers sit: left is mono, the middle is the image as built, right opens the sides.",
        "How much of the worn clock's hiss is let through. Only heard with WEAR on WORN; left is silent.",
    };
    static const char* hoodNames[] = {"TONE", "FADE", "WIDTH", "NOISE"};
    for (int i = 0; i < 4; ++i) add(hoodCellDesign[i + 1], hoodNames[i], hoodText[i]);
    add(wetDivDesign, "WET DIV", "The wet side runs on a divided clock, so the room and the echo stretch out.");
    add(loopDivDesign, "LOOP DIV", "The loop runs on a divided clock: lower, longer, further from the other side.");

    // The knobs say what they do in the current mode, on hover and to a screen reader.
    // Last in the table, so a switch beside a knob explains itself first.
    const int wm = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load());
    const int lm = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load());
    for (size_t i = 0; i < knobs.size(); ++i) {
        const int mode = kKnobMode[i] == 0 ? wm : kKnobMode[i] == 1 ? lm : 0;
        const juce::String text = juce::String(kWhisper[i][juce::jlimit(0, 3, mode)]) + ".";
        hoverHelp.push_back({knobDesign[i], knobName(i), text});
        knobs[i].slider->setDescription(text);
    }
}

// A side section: the sheet, a small square of its colour, its name, a dotted leader and a
// figure at the end. The colour marks the section; numbers are gone.
void BroodEditor::drawSection(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour mark, const juce::String& name, const juce::String& tail) const {
    kit::drawSheet(g, r, stock());
    auto row = r.removeFromTop(kit::panelHeadHeight).reduced(1, 1).reduced(8, 0);
    plate(g, row.removeFromLeft(10).withSizeKeepingCentre(10, 10).toFloat(), mark, 2.f);
    row.removeFromLeft(7);
    g.setColour(kInk);
    const auto named = nameFont(16);
    g.setFont(named);
    g.drawText(name, row.removeFromLeft(kit::textWidth(named, name) + 2), juce::Justification::centredLeft);
    const auto figure = font(16);
    g.setFont(figure);
    g.setColour(kMuted);
    const int tailWidth = tail.isEmpty() ? 0 : kit::textWidth(figure, tail) + 2;
    g.drawText(tail, row.removeFromRight(tailWidth), juce::Justification::centredRight);
    // The dotted leader between the name and the figure.
    g.setColour(kLine);
    for (int x = row.getX() + 4; x < row.getRight() - 4; x += 4) g.fillRect(x, row.getCentreY(), 1, 1);
}

// The masthead: wordmark, the two lines saying what it is, and the rule under it all.
// The kit's own masthead has a catalogue chip; Brood shows none.
void BroodEditor::drawMasthead(juce::Graphics& g) const {
    const auto area = mastheadArea;
    g.setColour(kInk);
    g.fillRect(area.getX(), area.getBottom() - 2, area.getWidth(), 2);
    auto row = area.withTrimmedBottom(10);
    constexpr float cap = 28.f;
    auto word = wordmarkPath(cap);
    const auto wordBox = row.removeFromLeft(static_cast<int>(std::ceil(word.getBounds().getWidth())) + 4);
    word.applyTransform(juce::AffineTransform::translation(static_cast<float>(wordBox.getX()), wordBox.toFloat().getCentreY() - cap / 2));
    g.fillPath(word);
    row.removeFromLeft(18);
    g.setColour(kMuted);
    g.setFont(nameFont(17.0f));
    auto lines = row.withSizeKeepingCentre(row.getWidth(), 34);
    g.drawText("GLYPH AUDIO", lines.removeFromTop(17), juce::Justification::centredLeft);
    g.drawText("MICRO-LOOPER + SPACE", lines, juce::Justification::centredLeft);
}

void BroodEditor::paint(juce::Graphics& g) {
    g.fillAll(stock().page);
    g.addTransform(view);   // draw in design coordinates; the window scales it
    drawMasthead(g);
    g.setColour(kMuted); g.setFont(nameFont(17));
    g.drawText("PATCH", presetBarDesign.getX() - 56, presetBarDesign.getY(), 48, presetBarDesign.getHeight() - 2, juce::Justification::centredRight);

    // ── the main sheet ──
    kit::drawSheet(g, pedalArea, stock());
    {
        static const char* columns[] = {"WET CHANNEL", "SHARED", "MICRO-LOOPER"};
        for (int c = 0; c < 3; ++c) {
            const auto col = juce::Rectangle<int>(pedalArea.getX() + c * pedalArea.getWidth() / 3, wetModeDesign.getY() - 20, pedalArea.getWidth() / 3, pedalArea.getBottom() - wetModeDesign.getY() + 20);
            if (c > 0) { g.setColour(kLine); g.drawVerticalLine(col.getX(), static_cast<float>(col.getY() + 2), static_cast<float>(col.getBottom() - 1)); }
            g.setColour(kMuted); g.setFont(nameFont(15));
            g.drawText(columns[c], col.getX(), col.getY(), col.getWidth(), 16, juce::Justification::centred);
        }
    }
    for (size_t i = 0; i < knobs.size(); ++i) {
        const auto kb = knobDesign[i];
        const auto nameRow = kb.withY(kb.getY() - 20).withHeight(20);
        g.setColour(kInk); g.setFont(nameFont(20));
        g.drawText(knobName(i), nameRow, juce::Justification::centred);
        // Its value, always; in ink while the knob is touched, right where you are looking.
        const bool touched = static_cast<int>(i) == activeKnob && activeKnobText.isNotEmpty();
        g.setColour(touched ? kInk : kMuted);
        g.setFont(font(touched ? 18 : 16));
        g.drawFittedText(touched ? activeKnobText.toUpperCase() : knobValues[i], valueRect(static_cast<int>(i)), juce::Justification::centred, 1, 0.7f);
    }
    g.setColour(kMuted); g.setFont(nameFont(15));

    // BALANCE's readout on the LOOP heading: "LOOP 100 / WET 60", where the sound has it now
    // while a source is moving it.
    {
        const bool moving = (routeMaskSeen & balanceRouteBits) != 0 && routeMaskSeen != -1;
        const float bv = moving ? juce::jlimit(0.f, 1.f, proc.ui.effective[6].load()) : proc.apvts.getRawParameterValue("balance")->load();
        drawSection(g, loopArea, kMoss, "LOOP", proc.describe("balance", bv).toUpperCase());
    }
    drawSection(g, rampArea, kInk, "MODULATION", {});

    // Row labels: one column, one size, so the rows read as one table.
    auto rowLabel = [&](const char* text, const juce::Rectangle<int>& slider, int labelW) {
        g.setColour(kInk); g.setFont(labelFont());
        g.drawText(text, slider.getX() - labelW, slider.getY(), labelW - kSp1, slider.getHeight(), juce::Justification::centredLeft);
    };
    const bool recordMode = proc.apvts.getRawParameterValue("recMode")->load() > 0.5f;
    {   // greyed in RECORD, with its slider: MEMORY is CAPTURE's
        juce::Graphics::ScopedSaveState faded(g);
        if (recordMode) g.beginTransparencyLayer(0.35f);
        rowLabel("MEMORY", memoryDesign, 74);
        if (recordMode) g.endTransparencyLayer();
    }
    rowLabel("BALANCE", balanceDesign.withTrimmedLeft(-kBalanceEnd), 74);
    // LOOP at the left end, WET at the right: the order every mention of BALANCE uses.
    g.setFont(nameFont(15)); g.setColour(kMuted);
    g.drawText("LOOP", balanceDesign.getX() - kBalanceEnd, balanceDesign.getY(), kBalanceEnd, balanceDesign.getHeight(), juce::Justification::centredLeft);
    g.drawText("WET", balanceDesign.getRight() + kSp1, balanceDesign.getY(), 40, balanceDesign.getHeight(), juce::Justification::centredLeft);
    // RATE sits inside the rate cell, so it costs no extra row. With SYNC on it is
    // no longer a rate at all, so the caption says what the readout now means.
    const juce::String modId = "mod" + juce::String(selectedMod + 1);
    const auto* rampSyncValue = proc.apvts.getRawParameterValue(modId + "Sync");
    const bool spanMode = rampSyncValue != nullptr && rampSyncValue->load() > 0.5f;
    g.setFont(labelFont()); g.setColour(kMuted);
    if (modOpen) g.drawText(spanMode ? "SPAN" : "RATE", rampRateDesign.getX(), rampRateDesign.getY(), 34, rampRateDesign.getHeight(), juce::Justification::centredLeft);
    // Short figures for the narrow column; the full wording lives in the readout
    // and in the host's parameter display.
    g.setColour(kMuted); g.setFont(font(14));   // MEMORY's and the RATE's figures are values
    auto figure = [&](const char* id, const juce::Rectangle<int>& r, bool percent) {
        const auto* v = proc.apvts.getRawParameterValue(id);
        if (v == nullptr) return;
        juce::String txt;
        if (percent) txt = juce::String(juce::roundToInt(v->load() * 100.f)) + "%";
        else if (v->load() <= 0.001f) txt = "PEDAL";
        else txt = juce::String(brood::expMap(v->load(), 1, 30), 1) + "s";
        g.drawText(txt, r.withX(r.getRight() + kSp1).withWidth(40), juce::Justification::centredRight);
    };
    if (recordMode) g.setColour(kMuted.withMultipliedAlpha(0.35f));
    figure("memory", memoryDesign, false);
    g.setColour(kMuted);
    // The rate's figure: a period, or with SYNC the span of the beat it turns over.
    if (const auto* rv = proc.apvts.getRawParameterValue(modId + "Rate"); rv != nullptr && modOpen) {
        const double secs = 1.0 / brood::rampHertz(rv->load());
        const juce::String rate = spanMode ? juce::String(brood::rampDivLabel(rv->load()))
                                : secs < 1 ? juce::String(juce::roundToInt(secs * 1000)) + "ms"
                                           : juce::String(secs, secs < 10 ? 1 : 0) + "s";
        g.drawText(rate, rampRateDesign.withLeft(rampRateDesign.getRight() - 40), juce::Justification::centredRight);
    }

    // ── the bottom strip ──
    kit::drawSheet(g, hoodArea, stock());
    static const char* hoodNames[] = {"LEVEL", "TONE", "FADE", "WIDTH", "NOISE"};
    static const char* hoodIds[] = {"tone", "fade", "width", "wearNoise"};
    static const float hoodDefaults[] = {1.f, 0.f, 0.5f, 1.f};
    float hv[4];
    for (int i = 0; i < 4; ++i) {
        const auto* v = proc.apvts.getRawParameterValue(hoodIds[i]);
        hv[i] = v != nullptr ? v->load() : hoodDefaults[i];
    }
    auto percent = [](float v) { return juce::String(juce::roundToInt(v * 100.f)) + "%"; };
    const float levelDb = juce::Decibels::gainToDecibels(proc.inputGain.load(), -100.f);
    juce::String hoodText[5];
    hoodText[0] = (levelDb > 0.05f ? "+" : "") + juce::String(levelDb, 1) + " dB";
    hoodText[1] = hv[0] > 0.995f ? "open" : "cut " + percent(1.f - hv[0]);
    hoodText[2] = hv[1] <= 0.001f ? "pedal" : "keeps " + percent(1.f - hv[1]);
    hoodText[3] = hv[2] <= 0.005f ? "mono" : hv[2] < 0.49f ? "narrow " + percent(hv[2] * 2) :
                  hv[2] > 0.51f ? "wide " + percent((hv[2] - 0.5f) * 2) : "as built";
    // Only the worn clock hisses, so that's the only time this says a figure.
    hoodText[4] = proc.apvts.getRawParameterValue("character")->load() > 0.75f ? (hv[3] <= 0.005f ? juce::String("off") : percent(hv[3])) : juce::String("when worn");
    for (int i = 0; i < 5; ++i) {
        const auto cellRect = hoodCellDesign[i].withHeight(18);
        g.setColour(kInk); g.setFont(labelFont());
        const int nameW = kit::textWidth(g.getCurrentFont(), hoodNames[i]) + 2;
        g.drawText(hoodNames[i], cellRect.withWidth(nameW), juce::Justification::centredLeft);
        g.setColour(kMuted); g.setFont(font(14));   // the figure: a value, as everywhere on the paper
        g.drawFittedText(hoodText[i], cellRect.withLeft(cellRect.getX() + nameW + kSp2), juce::Justification::centredRight, 1, 0.8f);
    }

    // The readout: while the mouse rests on something it explains that; otherwise it's
    // the last thing you moved.
    const bool helping = hoverText.isNotEmpty();
    juce::String value = helping ? hoverText : lcdValue.toUpperCase();
    // The cursor is always measured and only its ink blinks, so a long value squeezed to fit
    // doesn't change width, and shake, as it blinks.
    if (!helping) value += "_";
    kit::drawReadout(g, lcdArea, stock(), helping ? hoverLabel : lcdLabel, helping ? "HELP" : "LIVE", value, helping ? juce::String() : lcdMeta, helping, !helping && !blink);
}


bool BroodEditor::showHelpFor(const juce::String& label) {
    for (const auto& item : hoverHelp)
        if (item.label == label) { hoverLabel = item.label; hoverText = item.text; repaintDesign(lcdArea); return true; }
    return false;
}

void BroodEditor::timerCallback() {
    auto& ui = proc.ui;

    // Turn about at 60 Hz, so the things that move never redraw in the same frame: macOS joins
    // what's dirty in one frame into a single rectangle, and the loop view at the top with the
    // knobs below would repaint everything between them. The loop and the modulation each get
    // 30 Hz; meters and everything built from text run at 10 Hz, on a modulation tick.
    timerTick = (timerTick + 1) % 6;
    const bool modulationTick = timerTick % 2 == 1, slowTick = timerTick == 1;

    // Hover help. The mouse position is mapped back into design coordinates and
    // matched against the table, so no control needs its own enter/exit handler
    // and a click is never intercepted: this only ever changes what the readout
    // panel says.
    // While a slider is being dragged the readout is the more useful thing to
    // show, so help stands down for the duration of the gesture.
    const HoverItem* hovered = nullptr;
    if (slowTick) {
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
    }
    // The tempo switches follow the mode selectors: DELAY and WEAVE for the wet side,
    // ENV for the loop. Changing mode rebuilds the help table, because a switch that
    // has left the panel must not still have an explanation.
    if (slowTick) updateModeControls();
    // The knob under the mouse (or under a drag) shows its value beneath it; repaint
    // only the two lines that change.
    if (slowTick) {
        int act = -1;
        for (size_t i = 0; i < knobs.size(); ++i) if (knobs[i].slider->isMouseOverOrDragging()) act = static_cast<int>(i);
        juce::String txt = act >= 0 ? proc.describe(knobs[static_cast<size_t>(act)].id) : juce::String();
        // ENV's two knobs act on the slices the input makes, so with the loop playing
        // and nothing coming in they have nothing to move yet. Say so where you're looking.
        if (act == 2 || act == 5)
            if (lastModeLoop == 0 && ui.loopLed.load() == 1 && !ui.envGate.load())
                txt = "PLAY SOMETHING: ENV SLICES WHEN IT HEARS YOU";
        if (act != activeKnob || txt != activeKnobText) {
            if (activeKnob >= 0) repaintDesign(valueRect(activeKnob));
            activeKnob = act; activeKnobText = txt;
            if (act >= 0) repaintDesign(valueRect(act));
        }
        updateKnobValues();
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
    if (modulationTick) {
        for (int k = 0; k < 2; ++k) {
            auto& chip = *chips[k];
            chip.trace[static_cast<size_t>(chip.written)] = ui.modValue[static_cast<size_t>(k)].load();
            chip.written = (chip.written + 1) % static_cast<int>(chip.trace.size());
        }
        for (auto& chip : chips) chip->repaint();
        if (slowTick) {
            const int mask = routeMask();
            if (mask != routeMaskSeen) {
                routeMaskSeen = mask;
                if (routeList->isVisible()) routeList->rebuild(false);
            }
        }
        if (routeMaskSeen != 0) for (auto& knob : knobs) if (knob.slider->isShowing()) knob.slider->repaint();
        if ((routeMaskSeen & balanceRouteBits) != 0) {
            balance.repaint();
            repaintDesign(loopArea);
        }
    }
    fsWet->setLit(wetOn);
    fsLoop->setLit(loopOn);
    updateTakeControls();
    if (led != lastLoopLed) {
        const char* msg[] = {nullptr, "playing", "recording", "overdubbing"};
        if (msg[led] != nullptr) showReadout("LOOP", msg[led]);
        else if (lastLoopLed == 1) showReadout("LOOP", "stopped");
        lastLoopLed = led;
    }
    if (modulationTick) updateBench();
    else {
        // Every frame: the play head moves over a take, and with nothing caught the input
        // scrolls past. paint() fetches new peaks.
        loopView.repaint();
    }
    if (!slowTick) return;
    const int lsrc = ui.lsrc.load();
    const juce::String meta = juce::String(ui.clockHz.load() / 1000.f, 1) + " kHz    "
        + (lsrc > 0 ? "loop " + juce::String(ui.winLen.load() / ui.clockHz.load(), 2) + " s" : juce::String("no loop"))
        + "    cpu " + juce::String(ui.load.load(), 1) + "%";
    if (meta != lcdMeta) { lcdMeta = meta; repaintDesign(lcdArea); }
    if (readoutFollows && lastTouched.isNotEmpty()) {
        const juce::String v = proc.describe(lastTouched);   // follows automation and mode changes
        if (v != lcdValue) { lcdValue = v; repaintDesign(lcdArea); }
    }
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
