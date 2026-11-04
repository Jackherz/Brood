#include "PluginEditor.h"

#if BROOD_HAVE_FONTS
#include "BinaryData.h"
#endif

namespace {
// Monochrome, windowed pixel-art palette shared with the browser front panel.
const juce::Colour kBoard { 0xffe9e9e2 }, kPaper { 0xfffaf9f3 }, kInk { 0xff181815 };
const juce::Colour kMuted { 0xff77776f }, kLine { 0xffadada3 }, kPanel { 0xfff0efe8 };

juce::Font font(float size) {
#if BROOD_HAVE_FONTS
    static auto face = juce::Typeface::createSystemTypefaceFor(BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize);
    return juce::Font(juce::FontOptions(face).withHeight(size));
#else
    return juce::Font(juce::FontOptions()
                          .withName(juce::Font::getDefaultMonospacedFontName())
                          .withHeight(size));
#endif
}
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

// ── the sheet ─────────────────────────────────────────────────────────────
// One small system instead of a drawer of one-off numbers, after the product
// styleguide: spacing snaps to 4 / 8 / 16 / 24 / 32, type to five sizes, and
// elevation is always a hard offset (1 px controls, 2 px panels, 3 px readouts),
// never a blur. Rules carry the structure — ink for frames, hairline grey for
// grouping — and the only textures are a 2 px halftone band and a 20 px dot
// grid, both built from the paper's own colour so the cream/ink palette holds.
// The steps of that scale this layout actually spends. They are declared one at a
// time so that a step the panel does not use is never a warning — 32 is part of
// the sheet, not of this editor, and arrives here the day a cell needs it.
constexpr int kSp1 = 4, kSp2 = 8, kSp3 = 16, kSp4 = 24;
const juce::Colour kGrid { 0xffe4e2d6 };      // the dot grid under the paper
const juce::Colour kHalftone { 0xffc2c1b6 };  // the hatched band on a panel head
juce::Font capFont() { return font(11); }
juce::Font labelFont() { return font(13); }
juce::Font textFont() { return font(15); }

// A halftone: paper with every fourth pixel inked, the way a printed sheet makes
// a grey it does not have a colour for. Small enough areas only — this is a loop
// of single pixels, not a gradient.
void halftone(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour bg, juce::Colour dot) {
    g.setColour(bg); g.fillRect(r);
    // Only the part being repainted: a knob or a meter repaints a corner of the
    // window thirty times a second, and the texture does not need to be walked
    // for pixels that are clipped anyway.
    const auto clip = r.getIntersection(g.getClipBounds().expanded(1));
    if (clip.isEmpty()) return;
    g.setColour(dot);
    for (int y = clip.getY(); y < clip.getBottom(); ++y)
        for (int x = clip.getX() + (y % 2) * 2; x < clip.getRight(); x += 4) g.fillRect(x, y, 1, 1);
}
void dotGrid(juce::Graphics& g, juce::Rectangle<int> r, int pitch, juce::Colour c) {
    const auto clip = r.getIntersection(g.getClipBounds().expanded(pitch));
    if (clip.isEmpty()) return;
    g.setColour(c);
    for (int y = r.getY() + pitch; y < r.getBottom(); y += pitch) {
        if (y < clip.getY() || y >= clip.getBottom()) continue;
        for (int x = r.getX() + pitch; x < r.getRight(); x += pitch)
            if (x >= clip.getX() && x < clip.getRight()) g.fillRect(x, y, 1, 1);
    }
}
// NAME ......... FIGURE: the manifest way of tying a label to its value when
// there is no room for a rule between them.
void leader(juce::Graphics& g, int x1, int x2, int y, juce::Colour c) {
    g.setColour(c);
    for (int x = x1; x < x2 - 2; x += kSp1) g.fillRect(x, y, 1, 1);
}
// Registration marks: four L-shaped ticks, as on a trim sheet. They read as
// structure without adding a line anybody has to look past.
void cornerTicks(juce::Graphics& g, juce::Rectangle<int> r, int len, juce::Colour c) {
    g.setColour(c);
    const float x = (float) r.getX(), y = (float) r.getY(), X = (float) r.getRight(), Y = (float) r.getBottom();
    g.drawLine(x, y, x + (float) len, y, 1.f); g.drawLine(x, y, x, y + (float) len, 1.f);
    g.drawLine(X, y, X - (float) len, y, 1.f); g.drawLine(X, y, X, y + (float) len, 1.f);
    g.drawLine(x, Y, x + (float) len, Y, 1.f); g.drawLine(x, Y, x, Y - (float) len, 1.f);
    g.drawLine(X, Y, X - (float) len, Y, 1.f); g.drawLine(X, Y, X, Y - (float) len, 1.f);
}
// A panel head cell: `01` in an inverted block, the name beside it, a dotted
// leader out to the right and a small figure at the end. Every panel opens the
// same way, so the column reads as one document.
void headCell(juce::Graphics& g, juce::Rectangle<int> head, const juce::String& index, const juce::String& name, const juce::String& tail) {
    auto r = head.reduced(kSp2, 0);
    g.setColour(kInk); g.setFont(capFont());
    const int iw = 20;
    g.fillRect(r.removeFromLeft(iw).withSizeKeepingCentre(iw, 15));
    g.setColour(kPaper);
    g.drawText(index, r.withX(head.getX() + 8).withWidth(iw), juce::Justification::centred);
    r.removeFromLeft(kSp1 + 1);
    g.setColour(kInk); g.setFont(labelFont());
    const int nameW = juce::jmin(r.getWidth(), juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), name) + 2);
    g.drawText(name, r.removeFromLeft(nameW), juce::Justification::centredLeft);
    g.setColour(kMuted); g.setFont(capFont());
    const int tailW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), tail) + 2;
    g.drawText(tail, r.removeFromRight(tailW), juce::Justification::centredRight);
    leader(g, r.getX() + 3, r.getRight() - 3, head.getCentreY(), kLine);
}

constexpr int W = 900, H = 712;
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

// ───────── look and feel ─────────
BroodEditor::KnobLook::KnobLook() {
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

void BroodEditor::KnobLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& slider) {
    // A flat disc with a thin pointer, after the SR-01: a line standing up from
    // the middle, no bevels, no ring. The hard offset shadow is the one
    // nod to the pixel panel it sits in. Everything is proportional so the knob
    // looks the same at every window size (fixed tick radii used to get clipped).
    const float u = std::min(static_cast<float>(w), static_cast<float>(h)) / 104.f;
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)).reduced(15.f * u);
    const float r = std::min(area.getWidth(), area.getHeight()) / 2;
    const auto c = area.getCentre();
    // Five marks, not twenty-one: the scale reads at a glance and the knob stays
    // the loudest thing in the cell. There is no ring around the face — the marks
    // are the scale, and on hover they turn to ink, which is the knob saying
    // "this one".
    const bool hot = slider.isMouseOverOrDragging();
    g.setColour(hot ? kInk : juce::Colour(0xffb3b2a7));
    for (int i = 0; i <= 4; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / 4.f;
        g.drawLine(c.x + (r + 6.f * u) * std::sin(a), c.y - (r + 6.f * u) * std::cos(a),
                   c.x + (r + 11.f * u) * std::sin(a), c.y - (r + 11.f * u) * std::cos(a), 1.2f * u);
    }
    // The wet channel's two knobs are the black discs, as on the pedal that lent
    // us the shape; the shared and loop knobs are the pale ones.
    const bool dark = static_cast<bool>(slider.getProperties()["wet"]);
    auto face = [&](juce::Point<float> centre, float rad) {
        juce::Path p;
        p.addEllipse(centre.x - rad, centre.y - rad, rad * 2.f, rad * 2.f);
        return p;
    };
    g.setColour(kInk); g.fillPath(face(c.translated(2.6f * u, 3.f * u), r));      // the hard shadow
    g.setColour(dark ? juce::Colour(0xff191916) : juce::Colour(0xffe8e5db));
    g.fillPath(face(c, r));
    g.setColour(kInk); g.strokePath(face(c, r), juce::PathStrokeType(1.2f * u));
    juce::Path pointer;
    pointer.addRectangle(-1.4f * u, -r * 0.83f, 2.8f * u, r * 0.83f);
    const float angle = a0 + pos * (a1 - a0);
    g.setColour(dark ? juce::Colour(0xfff7f5ec) : kInk);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(c));
}

void BroodEditor::KnobLook::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    // One switch for the whole panel: a sliding block on the left and its name
    // beside it, drawn in a 26 px design row and scaled to the cell's height. The
    // name is ink when the switch is on, so a row of them reads at a glance.
    const float u = b.getHeight() / 26.f;
    g.addTransform(juce::AffineTransform::scale(u));
    const auto bounds = juce::Rectangle<float>(0, 0, b.getWidth() / u, 26.f);
    const auto sw = juce::Rectangle<float>(24, 14).withCentre({13, bounds.getCentreY()});
    g.setColour(kInk); g.fillRect(sw);
    const auto handle = juce::Rectangle<float>(9, 10).withCentre({b.getToggleState() ? sw.getRight() - 6 : sw.getX() + 6, sw.getCentreY()});
    g.setColour(kPaper); g.fillRect(handle);
    if (b.hasKeyboardFocus(true) || over) { g.setColour(kMuted); g.drawRect(sw.expanded(2), 1.f); }
    g.setColour(b.getToggleState() ? kInk : kMuted); g.setFont(labelFont());
    g.drawFittedText(b.getButtonText().toUpperCase(), bounds.withTrimmedLeft(30).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

// A slider is a printed scale: one 2 px rule for the travel, five marks under it
// and a boxed thumb. No track highlight, no rounded cap, no glow.
void BroodEditor::KnobLook::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float, float, juce::Slider::SliderStyle style, juce::Slider& slider) {
    if (style != juce::Slider::LinearHorizontal) {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0.f, 0.f, style, slider);
        return;
    }
    const float cy = (float) y + (float) h * 0.5f;
    const float left = (float) x + 1.f, right = (float) x + (float) w - 1.f;
    g.setColour(kLine);
    for (int i = 0; i <= 4; ++i) {
        const float px = left + (right - left) * (float) i / 4.f;
        g.fillRect(px - 0.5f, cy + 6.f, 1.f, 4.f);
    }
    g.setColour(kInk);
    g.fillRect(left, cy - 1.f, right - left, 2.f);
    // The thumb sits on the rule and under a hard shadow, so it reads as a
    // physical tab rather than a dot.
    const auto tab = juce::Rectangle<float>(9.f, 15.f).withCentre({ juce::jlimit(left, right, pos), cy - 1.f });
    g.setColour(kInk); g.fillRect(tab.translated(2.f, 2.f));
    g.setColour(slider.isMouseOverOrDragging() ? juce::Colour(0xfff4f2e9) : kPaper); g.fillRect(tab);
    g.setColour(kInk); g.drawRect(tab, 1.f);
    g.fillRect(tab.withSizeKeepingCentre(3.f, 15.f).reduced(0.f, 4.f));
}

void BroodEditor::KnobLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    const auto r = b.getLocalBounds().toFloat().reduced(2.f);
    const bool off = !b.isEnabled();
    // Disabled is drawn, not faded: a grey frame and no shadow, so it still sits
    // on the same sheet instead of turning into a ghost.
    if (!off) { g.setColour(kInk); g.fillRect(r.translated(2.f, 2.f)); }
    g.setColour(down && !off ? kInk : over && !off ? juce::Colour(0xfff1efe6) : kPaper); g.fillRect(r);
    g.setColour(off ? kLine : down ? kPaper : kInk); g.drawRect(r, 1.5f);
}
void BroodEditor::KnobLook::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) {
    g.setColour(!b.isEnabled() ? kLine : down ? kPaper : kInk);
    g.setFont(textFont());
    g.drawFittedText("[ " + b.getButtonText().toUpperCase() + " ]", b.getLocalBounds().reduced(6, 0),
                     juce::Justification::centred, 1, 0.85f);
}

// A field, not a widget: hairline frame, hard shadow, and the chevron inside its
// own inverted cell so the right-hand end of every field looks the same.
void BroodEditor::KnobLook::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                        int, int, int, int, juce::ComboBox& box) {
    auto r = juce::Rectangle<float>(0.f, 0.f, (float) width, (float) height).reduced(2.f);
    g.setColour(kInk); g.fillRect(r.translated(2.f, 2.f));
    g.setColour(box.isMouseOver(true) ? juce::Colour(0xfff1efe6) : kPaper); g.fillRect(r);
    g.setColour(kInk); g.drawRect(r, 1.5f);
    auto cell = r.removeFromRight(juce::jmin(r.getWidth(), (float) height * 0.85f));
    g.fillRect(cell);
    g.setColour(kPaper);
    juce::Path chev;
    const float cx = cell.getCentreX(), cy = cell.getCentreY();
    chev.addTriangle(cx - 4.f, cy - 2.5f, cx + 4.f, cy - 2.5f, cx, cy + 3.5f);
    g.fillPath(chev);
}

juce::Font BroodEditor::KnobLook::getComboBoxFont(juce::ComboBox&) { return font(15); }
juce::Font BroodEditor::KnobLook::getPopupMenuFont() { return font(16); }

// ───────── 3-way toggle ─────────
BroodEditor::ThreeWay::ThreeWay(juce::RangedAudioParameter& p, juce::StringArray l, std::function<void()> onChange)
    : param(p), labels(std::move(l)), attach(p, [this](float v) { index = juce::roundToInt(v); repaint(); if (changed) changed(); }), changed(std::move(onChange)) {
    attach.sendInitialUpdate();
}
void BroodEditor::ThreeWay::paint(juce::Graphics& g) {
    // Drawn at the design width and scaled, so the labels grow with the window.
    const float s = getWidth() / 130.f;
    g.addTransform(juce::AffineTransform::scale(s));
    const auto bounds = juce::Rectangle<float>(0, 0, 130.f, getHeight() / s).toNearestInt();
    const int count = juce::jmax(1, labels.size());
    const int row = bounds.getHeight() / count;
    // Elevation 2, like a panel: the selector is a plate sitting on the paper, and
    // the chosen row is a solid block rather than a tint.
    g.setColour(kInk); g.fillRect(bounds.translated(2, 2));
    g.setColour(kPaper); g.fillRect(bounds);
    g.setColour(kInk); g.drawRect(bounds, 1);
    for (int i = 0; i < count; ++i) {
        const auto r = juce::Rectangle<int>(1, 1 + i * row, bounds.getWidth() - 2, row - 1);
        if (i == index) {
            g.setColour(kInk); g.fillRect(r);
            // A small block at the left edge, the mark that says "this line".
            g.setColour(kPaper); g.fillRect(r.withWidth(4).reduced(0, 3));
        } else if (i > 0) {
            g.setColour(kLine); g.drawHorizontalLine(i * row, 0.f, static_cast<float>(getWidth()));
        }
        g.setColour(i == index ? kPaper : kInk); g.setFont(font(15));
        g.drawText(labels[i].toUpperCase(), r.reduced(9, 0), juce::Justification::centredLeft);
        if (i != index) {
            g.setColour(kMuted); g.setFont(capFont());
            g.drawText(juce::String(i + 1), r.reduced(4, 0), juce::Justification::centredRight);
        }
    }
}
void BroodEditor::ThreeWay::mouseDown(const juce::MouseEvent& e) {
    const int count = juce::jmax(1, labels.size());
    const int next = juce::jlimit(0, count - 1, e.y * count / juce::jmax(1, getHeight()));
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
        t.addTriangle(cx + (right ? w / 2 : -w / 2), cy - h / 2, cx + (right ? w / 2 : -w / 2), cy + h / 2, cx + (right ? -w / 2 : w / 2), cy);
        g.setColour(kPaper); g.fillPath(t);
    };
    tri(inner.removeFromLeft(arrow), false);
    tri(inner.removeFromRight(arrow), true);
    inner.removeFromLeft(3);
    g.setFont(labelFont()); g.setColour(kMuted);
    const int capW = juce::jmin(inner.getWidth() / 2, 44);
    g.drawFittedText(cap, inner.removeFromLeft(capW), juce::Justification::centredLeft, 1);
    // OFF is drawn as an outline and ON as a solid block, so which one you are
    // looking at needs no legend: a filled display means the tide is running.
    auto value = inner.withTrimmedLeft(2);
    const bool on = index > 0;
    const juce::String shown = index >= 0 && index < names.size() ? names[index] : juce::String();
    if (on) {
        g.setColour(kInk); g.fillRect(value);
        g.setColour(kPaper);
        g.setFont(textFont());
        g.drawFittedText(shown, value.reduced(2, 0), juce::Justification::centred, 1);
    } else {
        g.setColour(kPaper); g.fillRect(value);
        g.setColour(kMuted); g.drawRect(value, 1);
        g.setFont(textFont());
        g.drawFittedText(shown, value.reduced(2, 0), juce::Justification::centred, 1);
    }
}
void BroodEditor::Stepper::step(int by) {
    const int next = ((index + by) % count() + count()) % count();
    attach.setValueAsCompleteGesture(static_cast<float>(brood::rampValueOfIndex(next, count())));
}
void BroodEditor::Stepper::mouseDown(const juce::MouseEvent& e) {
    // Anywhere on the cell steps forward; only the painted back-arrow steps back.
    // The hit zone is the arrow that is drawn, so the picture and the behaviour
    // are the same thing.
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
    const float s = getWidth() / 84.f;
    g.addTransform(juce::AffineTransform::scale(s));
    auto r = juce::Rectangle<int>(0, 0, 84, juce::roundToInt(getHeight() / s)).reduced(3);
    g.setColour(kInk); g.fillRect(r.translated(3, 3));
    g.setColour(isDown ? kPanel : kPaper); g.fillRect(r);
    g.setColour(kInk); g.drawRect(r, 1);
    g.setFont(font(20)); g.drawText(name, r.removeFromTop(40), juce::Justification::centredBottom);
    g.setColour(kMuted); g.setFont(font(13));
    g.drawText("TAP / HOLD", r.removeFromTop(19), juce::Justification::centred);
    g.setColour(kInk); g.setFont(font(12));
    g.drawText("[ " + keyName + " ]", r, juce::Justification::centredTop);
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
    for (int i = 0; i < n; ++i) {
        const float a = std::min(1.f, wave[static_cast<size_t>(i)] * 1.6f) * (b.getHeight() / 2 - 14);
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
BroodEditor::BroodEditor(BroodProcessor& p) : AudioProcessorEditor(p), proc(p), loopView(p) {
    setLookAndFeel(&look);
    const std::pair<const char*, const char*> defs[] = {
        {"time", "TIME"}, {"mix", "MIX"}, {"length", "LENGTH"}, {"modWet", "MODIFY"}, {"clock", "CLOCK"}, {"modLoop", "MODIFY"}};
    for (const auto& [id, label] : defs) {
        Knob k { id, label, std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), nullptr };
        k.slider->setRotaryParameters(juce::degreesToRadians(-135.f), juce::degreesToRadians(135.f), true);
        k.slider->setMouseDragSensitivity(220);
        k.slider->setDoubleClickReturnValue(true, static_cast<double>(proc.apvts.getParameter(id)->getDefaultValue()));
        k.slider->setTitle(juce::String(label) + (juce::String(id) == "modWet" ? " (wet)" : juce::String(id) == "modLoop" ? " (loop)" : ""));
        // The wet channel's two knobs are drawn as the black discs.
        k.slider->getProperties().set("wet", juce::String(id) == "time" || juce::String(id) == "modWet");
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
        d.button->onClick = [this, def, b = d.button.get()] {
            showReadout(juce::String(def.label).toUpperCase(), b->getToggleState() ? def.on : def.off);
        };
        addAndMakeVisible(*d.button);
        d.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, def.id, *d.button);
    }
    auto makeSyncSwitch = [this](juce::ToggleButton& button, const char* id, const char* readout,
                                 std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att) {
        button.setButtonText("SYNC");
        button.onClick = [this, sw = &button, name = juce::String(readout)] {
            showReadout(name, sw->getToggleState() ? "on the beat" : "free");
        };
        addChildComponent(button);             // shown only in the modes that use it
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, button);
    };
    makeSyncSwitch(wetSync, "sync", "SYNC / WET", wetSyncAtt);
    makeSyncSwitch(envSync, "envSync", "SYNC / ENV", envSyncAtt);
    auto rampChanged = [this](const char* label, const char* id) {
        return [this, label, id] {
            auto* prm = proc.apvts.getParameter(id);
            if (lastTouched.isNotEmpty() || isVisible()) showReadout(label, prm->getCurrentValueAsText().toLowerCase());
        };
    };
    rampWet = std::make_unique<Stepper>(*proc.apvts.getParameter("rampWet"), "WET", juce::StringArray{"OFF", "TIME", "MIX", "LENGTH", "MODIFY", "CLOCK", "BALANCE"}, rampChanged("MOD / WET", "rampWet"));
    rampLoop = std::make_unique<Stepper>(*proc.apvts.getParameter("rampLoop"), "LOOP", juce::StringArray{"OFF", "TIME", "MIX", "LENGTH", "MODIFY", "CLOCK", "BALANCE"}, rampChanged("MOD / LOOP", "rampLoop"));
    rampWave = std::make_unique<Stepper>(*proc.apvts.getParameter("rampWave"), "SHAPE", juce::StringArray{"TRI", "SQR", "SIN", "RND", "SMTH"}, rampChanged("MOD SHAPE", "rampWave"));
    for (auto* st : {rampWet.get(), rampLoop.get(), rampWave.get()}) addAndMakeVisible(*st);
    rampRate.setSliderStyle(juce::Slider::LinearHorizontal);
    rampRate.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rampRate.setTitle("Modulation rate");
    rampRate.setDoubleClickReturnValue(true, static_cast<double>(proc.apvts.getParameter("rampSpeed")->getDefaultValue()));
    rampRate.onValueChange = [this] { touch("rampSpeed"); };
    addAndMakeVisible(rampRate);
    rampRateAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "rampSpeed", rampRate);
    // The two dips that bend the shape, the polarity that keeps it above the knob,
    // and the switch that hands the rate to the tempo instead.
    const std::pair<const char*, const char*> rampSwitchDefs[] = {{"rampUni", "UNI"}, {"bounce", "BOUNCE"}, {"sweep", "SWEEP"}, {"rampSync", "SYNC"}, {"rampHold", "HOLD"}};
    for (size_t i = 0; i < rampSwitches.size(); ++i) {
        auto& rs = rampSwitches[i];
        const juce::String lab = rampSwitchDefs[i].second;
        rs.button = std::make_unique<juce::ToggleButton>(lab);
        rs.button->onClick = [this, lab, b = rs.button.get()] {
            showReadout("MOD " + lab, b->getToggleState() ? "on" : "off");
            repaintDesign(rampArea);   // the rate cell's caption changes with SYNC
        };
        addAndMakeVisible(*rs.button);
        rs.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, rampSwitchDefs[i].first, *rs.button);
    }
    refreshPatchMenu(1);
    presetBox.setTitle("Patch");
    // Ids: 1-5 factory, 11-14 the user slots, 21-24 "save to" the same slots.
    presetBox.onChange = [this] {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id <= 5) { applyPatch(id - 1); lastPatchId = id; }
        else if (id > 10 && id <= 10 + kUserSlots) {
            if (loadUserPatch(id - 10)) lastPatchId = id;
            else {
                showReadout("PATCH", "user " + juce::String(id - 10) + " is empty: save one first");
                presetBox.setSelectedId(lastPatchId, juce::dontSendNotification);
            }
        } else if (id > 20 && id <= 20 + kUserSlots) {
            saveUserPatch(id - 20);
            lastPatchId = id - 10;
            refreshPatchMenu(lastPatchId);
        }
    };
    addAndMakeVisible(presetBox);
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
    setResizeLimits(juce::roundToInt(W * 0.75f), juce::roundToInt(H * 0.75f), 1800, 1280);   // 0.75x to 2x of the design panel
    getConstrainer()->setFixedAspectRatio(static_cast<double>(W) / H);
    setSize(W, H);
    updateModeControls();
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
        {"balance", "WET / LOOP"}, {"rampSpeed", "MOD RATE"}, {"wetDiv", "WET DIV"}, {"loopDiv", "LOOP DIV"},
    };
    juce::String label = id.toUpperCase();
    for (const auto& [key, name] : names) if (id == key) label = name;
    showReadout(label, proc.describe(id), true);
    if (id == "clock") repaintDesign(pedalArea);
    // The small readouts beside the trim and hood sliders are drawn by this
    // editor rather than by the control, so they need a repaint to follow it.
    if (id == "memory" || id == "start" || id == "loopEnd" || id == "balance") repaintDesign(loopArea);
    if (id == "rampSpeed") repaintDesign(rampArea);
    if (id == "tone" || id == "fade" || id == "width" || id == "wetDiv" || id == "loopDiv") repaintDesign(hoodArea);
}

void BroodEditor::showReadout(const juce::String& label, const juce::String& value, bool follows) {
    readoutFollows = follows;
    if (label == lcdLabel && value == lcdValue) return;
    lcdLabel = label; lcdValue = value;
    repaintDesign(lcdArea);
}

// A factory patch is a whole state, the same as the browser's: every control goes
// back to its default first, then the patch sets what it cares about. Values are
// real units (mode indices, knob positions) and go through each parameter's own
// conversion — normalised numbers written by hand drifted when WEAVE made the wet
// selector four stops long, and two patches loaded the wrong mode.
void BroodEditor::applyPatch(int index) {
    struct Setting { const char* id; float value; };
    static const std::vector<Setting> patches[] = {
        {},
        {{"time", .73f}, {"mix", .56f}, {"modWet", .83f}, {"length", .65f}, {"clock", .80f}, {"loopMode", 2}, {"modLoop", .62f}},
        {{"time", .46f}, {"mix", .48f}, {"modWet", .70f}, {"wetMode", 1}, {"routing", 0}},
        {{"time", .45f}, {"mix", .54f}, {"modWet", .83f}, {"wetMode", 2}, {"clock", .80f}},
        {{"time", .67f}, {"mix", .58f}, {"length", .35f}, {"modWet", .75f}, {"clock", .70f}, {"modLoop", .57f},
         {"routing", 2}, {"loopMode", 2}},
    };
    index = juce::jlimit(0, static_cast<int>(std::size(patches)) - 1, index);
    auto set = [](juce::RangedAudioParameter& prm, float normalised) {
        prm.beginChangeGesture(); prm.setValueNotifyingHost(normalised); prm.endChangeGesture();
    };
    // The footswitches are momentary and a patch never engages a channel.
    for (auto* prm : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(prm))
            if (ranged->getParameterID() != "wetSwitch" && ranged->getParameterID() != "loopSwitch")
                set(*ranged, ranged->getDefaultValue());
    for (const auto& s : patches[index])
        if (auto* prm = proc.apvts.getParameter(s.id)) set(*prm, prm->convertTo0to1(s.value));
    showReadout("PATCH", presetBox.getItemText(index).toLowerCase());
}

juce::File BroodEditor::userPatchFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Brood").getChildFile("patches.xml");
}

void BroodEditor::refreshPatchMenu(int selectId) {
    presetBox.clear(juce::dontSendNotification);
    const char* factory[] = {"A LITTLE DAYDREAM", "SOFT FOCUS", "GLASS ECHO", "LITTLE GHOST", "MELT AWAY"};
    for (int i = 0; i < 5; ++i) presetBox.addItem(factory[i], i + 1);
    const auto xml = juce::XmlDocument::parse(userPatchFile());
    presetBox.addSectionHeading("YOUR PATCHES");
    for (int slot = 1; slot <= kUserSlots; ++slot) {
        const bool used = xml != nullptr && xml->getChildByAttribute("slot", juce::String(slot)) != nullptr;
        presetBox.addItem("USER " + juce::String(slot) + (used ? "" : "  -  EMPTY"), 10 + slot);
    }
    presetBox.addSectionHeading("SAVE WHAT YOU HEAR TO");
    for (int slot = 1; slot <= kUserSlots; ++slot) presetBox.addItem("SAVE TO USER " + juce::String(slot), 20 + slot);
    presetBox.setSelectedId(selectId, juce::dontSendNotification);
}

// A user patch is every control except the footswitches, as normalised values.
void BroodEditor::saveUserPatch(int slot) {
    const auto file = userPatchFile();
    auto root = juce::XmlDocument::parse(file);
    if (root == nullptr || !root->hasTagName("BROOD_PATCHES")) root = std::make_unique<juce::XmlElement>("BROOD_PATCHES");
    if (auto* old = root->getChildByAttribute("slot", juce::String(slot))) root->removeChildElement(old, true);
    auto* patch = root->createNewChildElement("PATCH");
    patch->setAttribute("slot", slot);
    for (auto* prm : proc.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(prm))
            if (ranged->getParameterID() != "wetSwitch" && ranged->getParameterID() != "loopSwitch") {
                auto* entry = patch->createNewChildElement("P");
                entry->setAttribute("id", ranged->getParameterID());
                entry->setAttribute("v", ranged->getValue());
            }
    file.getParentDirectory().createDirectory();
    const bool ok = root->writeTo(file);
    showReadout("PATCH", ok ? "saved to user " + juce::String(slot) : "could not write " + file.getFullPathName());
}

bool BroodEditor::loadUserPatch(int slot) {
    const auto root = juce::XmlDocument::parse(userPatchFile());
    const auto* patch = root != nullptr ? root->getChildByAttribute("slot", juce::String(slot)) : nullptr;
    if (patch == nullptr) return false;
    for (const auto* entry : patch->getChildIterator())
        if (auto* prm = proc.apvts.getParameter(entry->getStringAttribute("id"))) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(static_cast<float>(entry->getDoubleAttribute("v", prm->getDefaultValue())));
            prm->endChangeGesture();
        }
    showReadout("PATCH", "user " + juce::String(slot));
    return true;
}

juce::Rectangle<int> BroodEditor::whisperRect(int knob) const {
    const auto kb = knobDesign[juce::jlimit(0, 5, knob)];
    // Row one leaves room for a SYNC switch between the knob and its line.
    return { kb.getCentreX() - 79, knob < 3 ? kb.getBottom() + 11 : kb.getBottom() - 1, 158, 22 };
}

void BroodEditor::updateModeControls() {
    const int wm = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load());
    const int lm = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load());
    if (wm == lastModeWet && lm == lastModeLoop) return;
    lastModeWet = wm; lastModeLoop = lm;
    wetSync.setVisible(wm == 1 || wm == 3);
    envSync.setVisible(lm == 0);
    rebuildHoverHelp();
    repaintDesign(pedalArea);
}

void BroodEditor::resized() {
    // Everything is laid out in design coordinates (900x712) and mapped into the
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

    // ── the pedal ──
    pedalArea = {20, 20, 520, 600};
    // The patch is the one control that changes everything, so it sits in the
    // masthead, level with the wordmark, instead of under the side column.
    presetBoxDesign = {pedalArea.getRight() - 192, pedalArea.getY() + 33, 172, 26};
    place(presetBox, presetBoxDesign);
    const int cx[3] = {pedalArea.getX() + 95, pedalArea.getCentreX(), pedalArea.getRight() - 95};
    for (int i = 0; i < 6; ++i) {
        const int row = i / 3;
        knobDesign[i] = juce::Rectangle<int>(104, 104).withCentre({cx[i % 3], pedalArea.getY() + (row == 0 ? 162 : 406)});
        place(*knobs[static_cast<size_t>(i)].slider, knobDesign[i]);
    }
    // Each tempo switch sits directly under the knob it governs, between the knob
    // and its whisper, so it cannot be read as belonging to a neighbour.
    wetSyncDesign = juce::Rectangle<int>(54, 18).withCentre({knobDesign[0].getCentreX(), knobDesign[0].getBottom() + 1});
    envSyncDesign = juce::Rectangle<int>(54, 18).withCentre({knobDesign[2].getCentreX(), knobDesign[2].getBottom() + 1});
    place(wetSync, wetSyncDesign);
    place(envSync, envSyncDesign);
    const int ty = pedalArea.getY() + 294;
    wetModeDesign = {cx[0] - 65, ty - 44, 130, 88};   // four wet modes
    routingDesign = {cx[1] - 65, ty - 36, 130, 72};
    loopModeDesign = {cx[2] - 65, ty - 36, 130, 72};
    place(*wetMode, wetModeDesign);
    place(*routing, routingDesign);
    place(*loopMode, loopModeDesign);
    const int fy = pedalArea.getBottom() - 52;
    fsWetDesign = juce::Rectangle<int>(84, 84).withCentre({cx[0], fy});
    fsLoopDesign = juce::Rectangle<int>(84, 84).withCentre({cx[2], fy});
    place(*fsWet, fsWetDesign);
    place(*fsLoop, fsLoopDesign);
    ledWet = juce::Rectangle<int>(14, 14).withCentre({cx[0] + 16, fy - 56});
    ledLoop = juce::Rectangle<int>(14, 14).withCentre({cx[2] - 16, fy - 56});

    // ── the side column ──
    // The readout, then four numbered panels in the order you reach for them while
    // playing: the loop you are hearing, what moves it, the switches, and last the
    // input, which is set once per session. 10 px gutters do the separating.
    auto side = juce::Rectangle<int>(560, 20, 320, 600);
    lcdArea = side.removeFromTop(78);
    side.removeFromTop(10);
    loopArea = side.removeFromTop(186);
    side.removeFromTop(10);
    rampArea = side.removeFromTop(130);
    side.removeFromTop(10);
    dipArea = side.removeFromTop(60);
    side.removeFromTop(10);
    srcArea = side;

    // 01 LOOP: the waveform carries START and END as markers, so it gets the
    // height the two trim rows used to take; under it, two rows of label / slider
    // / figure on the same three columns so the figures line up.
    constexpr int labelW = 66, figureW = 44;
    auto inner = loopArea.withTrimmedTop(kSp4).reduced(12, 6);
    loopViewDesign = inner.removeFromTop(100);
    place(loopView, loopViewDesign);
    inner.removeFromTop(kSp2);
    auto trimRow = [&](juce::Rectangle<int>& out, juce::Slider& sl) {
        out = inner.removeFromTop(21).withTrimmedLeft(labelW).withTrimmedRight(figureW);
        place(sl, out);
    };
    trimRow(memoryDesign, memory);
    trimRow(balanceDesign, balance);

    // 02 MODULATION: destinations on top, then shape and rate, then the switches.
    auto ramp = rampArea.withTrimmedTop(kSp4).reduced(12, 7);
    auto rampRow = ramp.removeFromTop(28);
    const int half = (rampRow.getWidth() - kSp2) / 2;
    rampWetDesign = rampRow.removeFromLeft(half);
    rampRow.removeFromLeft(kSp2);
    rampLoopDesign = rampRow;
    place(*rampWet, rampWetDesign);
    place(*rampLoop, rampLoopDesign);
    ramp.removeFromTop(5);
    rampRow = ramp.removeFromTop(28);
    rampWaveDesign = rampRow.removeFromLeft(half);
    place(*rampWave, rampWaveDesign);
    rampRow.removeFromLeft(kSp2);
    rampRateDesign = rampRow;                    // "RATE" and its figure are painted at the ends
    place(rampRate, rampRateDesign.withTrimmedLeft(36).withTrimmedRight(40).reduced(0, 3));
    ramp.removeFromTop(5);
    rampRow = ramp.removeFromTop(26);
    // Five cells, sized to their words: BOUNCE needs more room than UNI.
    const int swWeights[] = {50, 70, 64, 56, 57};
    const int swTotal = rampRow.getWidth();
    for (size_t i = 0; i < rampSwitches.size(); ++i) {
        rampSwitchDesign[i] = i + 1 == rampSwitches.size() ? rampRow : rampRow.removeFromLeft(swTotal * swWeights[i] / 297);
        place(*rampSwitches[i].button, rampSwitchDesign[i]);
    }

    // 03 SWITCHES: four even cells.
    auto dipRow = dipArea.withTrimmedTop(kSp4).reduced(12, 0).withSizeKeepingCentre(dipArea.getWidth() - 24, 26)
                         .withY(dipArea.getY() + kSp4 + 5);
    const int dw = dipRow.getWidth() / static_cast<int>(dips.size());
    for (size_t i = 0; i < dips.size(); ++i) {
        dipCellDesign[i] = dipRow.removeFromLeft(dw);
        place(*dips[i].button, dipCellDesign[i]);
    }

    // 04 INPUT: source and file on one row, level and meters on the next.
    auto src = srcArea.withTrimmedTop(kSp4).reduced(12, 8);
    auto r1 = src.removeFromTop(28);
    sourceBoxDesign = r1.removeFromLeft(176);
    place(sourceBox, sourceBoxDesign);
    r1.removeFromLeft(kSp2);
    loadButtonDesign = r1;
    place(loadButton, loadButtonDesign);
    src.removeFromTop(10);
    auto r2 = src.removeFromTop(24);
    meterArea = r2.removeFromRight(96).withSizeKeepingCentre(96, 19);
    r2.removeFromRight(kSp3);
    inputGainDesign = r2.withTrimmedLeft(48);    // "LEVEL" is painted in the gap
    place(inputGain, inputGainDesign);

    // ── under the hood ──
    // One band across the bottom of the window: the five sliders the pedal keeps
    // behind both footswitches, in five even cells.
    hoodArea = {20, 636, 860, 56};
    const int hoodLeft = hoodArea.getX() + 118;
    const int cell = (hoodArea.getRight() - 14 - hoodLeft - 4 * kSp3) / 5;
    for (int i = 0; i < 5; ++i) hoodCellDesign[i] = {hoodLeft + i * (cell + kSp3), hoodArea.getY() + 6, cell, 44};
    for (size_t i = 0; i < hood.size(); ++i) place(hood[i].slider, hoodCellDesign[i].withY(hoodArea.getY() + 24).withHeight(22));
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

    add(rampWetDesign, "MOD / WET", "click to step what the first tide moves: off, time, mix, length, modify, clock, balance. the left arrow steps back.");
    add(rampLoopDesign, "MOD / LOOP", "click to step what the second tide moves; its modify is the loop's own. the left arrow steps back.");
    add(rampWaveDesign, "MOD SHAPE", "click to step the shape: triangle, square, sine, random, smooth random.");
    add(rampRateDesign, "MOD RATE", "how slowly the tide turns. both sides share it. with SYNC on it becomes a span of the beat.");
    static const char* rampSwitchText[] = {
        "the swing sits above the knob instead of either side of it.",
        "the swing reflects instead of snapping back, so nothing jumps.",
        "the shape is ignored and the knob just climbs, again and again.",
        "the rate becomes a division of the beat: eight bars down to a sixteenth.",
        "both tides stop where they are and the knobs stay pushed there. CC 52 does the same.",
    };
    static const char* rampSwitchNames[] = {"UNIPOLAR", "BOUNCE", "SWEEP", "MOD SYNC", "MOD HOLD"};
    for (int i = 0; i < 5; ++i) add(rampSwitchDesign[i], rampSwitchNames[i], rampSwitchText[i]);

    add(memoryDesign.expanded(0, 2), "LOOP MEMORY", "how much of the recent past is kept. hard left is the pedal's own buffer, which shrinks as CLOCK drops.");
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

    add(presetBoxDesign, "PATCH", "five places to start. a patch resets every control, then sets its own.");

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
    g.fillAll(kBoard);
    g.addTransform(view);   // draw in design coordinates; the window scales it
    auto pa = pedalArea;
    g.setColour(kInk); g.fillRect(pa.translated(4, 4));
    g.setColour(kPaper); g.fillRect(pa);
    g.setColour(kInk); g.drawRect(pa, 2);
    // The paper is graph paper: one dot every 20 px, in a shade just off it.
    dotGrid(g, pa.withTrimmedTop(25).reduced(1), kSp4 - kSp1, kGrid);
    cornerTicks(g, pa.withTrimmedTop(25).reduced(5), 9, kLine);   // trim marks, in the margins
    auto bar = pa.removeFromTop(25);
    g.setColour(kInk); g.fillRect(bar);
    g.setColour(kPaper); g.setFont(textFont());
    const juce::String title = "*  BROOD.EXE   /   A SOUND EXPERIMENT";
    g.drawText(title, bar.reduced(11, 0), juce::Justification::centredLeft);
    // The bar is also the machine's status line: dotted fill, then the rate it is
    // actually running at, boxed off at the right the way a manifest ends.
    g.setFont(capFont());
    const int metaW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), barMeta) + 8;
    const int dotsFrom = bar.getX() + 11 + juce::roundToInt(juce::GlyphArrangement::getStringWidth(textFont(), title)) + 12;
    g.setColour(kMuted);
    for (int x = dotsFrom; x < bar.getRight() - 12 - metaW - 8; x += 5)
        for (int y = bar.getY() + 7; y < bar.getBottom() - 5; y += 5) g.fillRect(x, y, 2, 2);
    if (barMeta.isNotEmpty()) {
        auto chip = juce::Rectangle<int>(bar.getRight() - 9 - metaW, bar.getY() + 4, metaW, 17);
        g.setColour(kPaper); g.fillRect(chip);
        g.setColour(kInk); g.setFont(capFont());
        g.drawText(barMeta, chip.reduced(4, 0), juce::Justification::centredRight);
    }
    // The masthead: wordmark and subtitle on the left, the patch on the right, then
    // a rule that names the three columns under it. The column names are what
    // used to be "/ wet" and "/ loop" tacked onto two of the whispers.
    g.setColour(kInk); g.setFont(pixel(30));
    g.drawText("BROOD.", pedalArea.getX() + 23, pedalArea.getY() + 30, 280, 32, juce::Justification::centredLeft);
    g.setColour(kMuted); g.setFont(textFont());
    g.drawText("A LITTLE WORLD OF SOUND & SPACE", pedalArea.getX() + 24, pedalArea.getY() + 63, 325, 14, juce::Justification::centredLeft);
    g.setColour(kInk); g.setFont(labelFont());
    g.drawText("PATCH", presetBoxDesign.getX() - 48, presetBoxDesign.getY(), 44, presetBoxDesign.getHeight(), juce::Justification::centredRight);
    {
        const int ruleY = pedalArea.getY() + 85;
        g.setColour(kLine);
        g.drawHorizontalLine(ruleY, static_cast<float>(pedalArea.getX() + 14), static_cast<float>(pedalArea.getRight() - 14));
        static const char* columns[] = {"WET CHANNEL", "SHARED", "MICRO-LOOPER"};
        const int colX[3] = {knobDesign[0].getCentreX(), knobDesign[1].getCentreX(), knobDesign[2].getCentreX()};
        g.setFont(capFont());
        for (int c = 0; c < 3; ++c) {
            const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), columns[c]) + kSp3;
            const auto box = juce::Rectangle<int>(w, 13).withCentre({colX[c], ruleY});
            g.setColour(kPaper); g.fillRect(box);
            g.setColour(c == 1 ? kMuted : kInk);
            g.drawText(columns[c], box, juce::Justification::centred);
        }
    }

    const int wm = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load());
    const int lm = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load());
    for (size_t i = 0; i < knobs.size(); ++i) {
        const auto kb = knobDesign[i];
        // Row two's names sit under the mode selectors, so they start lower.
        const auto nameRow = kb.withY(kb.getY() - (i < 3 ? 17 : 14)).withHeight(18);
        g.setColour(kInk); g.setFont(font(17));
        g.drawText(knobs[i].label, nameRow, juce::Justification::centred);
        // The cell carries its own number, tied to the name rather than floating
        // in the corner, so six knobs read as six numbered lines of a manifest.
        g.setColour(kMuted); g.setFont(capFont());
        const int nameW = juce::GlyphArrangement::getStringWidthInt(font(17), knobs[i].label);
        g.drawText(juce::String(i + 1).paddedLeft('0', 2),
                   juce::Rectangle<int>(kb.getCentreX() + nameW / 2 + 5, nameRow.getY() + 1, 18, 13),
                   juce::Justification::centredLeft);
        const int mode = kKnobMode[i] == 0 ? wm : kKnobMode[i] == 1 ? lm : 0;
        // At most two lines, never squeezed: a column is 165 px wide, and a pixel
        // face pressed narrower stops reading as one. While the knob is touched
        // the whisper gives way to the value, in ink, right where you are looking.
        const bool showValue = static_cast<int>(i) == activeKnob && activeKnobText.isNotEmpty();
        g.setColour(showValue ? kInk : kMuted);
        g.setFont(showValue ? labelFont() : font(12));
        g.drawFittedText(showValue ? activeKnobText.toUpperCase() : juce::String(kWhisper[i][juce::jlimit(0, 3, mode)]),
                         whisperRect(static_cast<int>(i)), juce::Justification::centredTop, 2, 1.f);
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
    // The one line of copy left on the panel: centred between the footswitches,
    // with a leader either side so it reads as a caption rather than a label.
    {
        const int gapL = fsWetDesign.getRight() + kSp3, gapR = fsLoopDesign.getX() - kSp3;
        auto line = juce::Rectangle<int>(gapL, fsWetDesign.getCentreY() - 9, gapR - gapL, 18);
        g.setColour(kMuted); g.setFont(capFont());
        const juce::String words = "ONE CLOCK. TWO WORLDS.";
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), words);
        g.drawText(words, line, juce::Justification::centred);
        const int half = (line.getWidth() - w) / 2;
        leader(g, line.getX(), line.getX() + half - 6, line.getCentreY(), kLine);
        leader(g, line.getRight() - half + 6, line.getRight(), line.getCentreY(), kLine);
    }

    // Two figures the heads can quote: what the host gave us, and how much past
    // the looper is set to keep.
    const auto* memoryParam = proc.apvts.getRawParameterValue("memory");
    const float memV = memoryParam != nullptr ? memoryParam->load() : 0.f;
    const juce::String srcTail = juce::String(proc.getTotalNumInputChannels()) + " IN / "
                               + juce::String(proc.getTotalNumOutputChannels()) + " OUT";
    const juce::String loopTail = memV <= 0.001f ? juce::String("PEDAL MEMORY")
                                                 : juce::String(brood::expMap(memV, 1.0, 30.0), 1) + " s MEMORY";

    // "heading", not "title": the window title above already owns that name.
    // Every panel opens the same way: a halftone band, a number in a solid block,
    // the name, a dotted leader out to a small figure. Elevation is a 2 px hard
    // offset, so the column reads as sheets pinned to a board.
    auto panel = [&](juce::Rectangle<int> r, const char* index, const char* heading, const juce::String& tail) {
        g.setColour(kInk); g.fillRect(r.translated(2, 2));
        g.setColour(kPaper); g.fillRect(r);
        g.setColour(kInk); g.drawRect(r, 1);
        auto head = r.removeFromTop(kSp4);
        halftone(g, head, kPanel, kHalftone);
        g.setColour(kInk); g.drawHorizontalLine(head.getBottom(), static_cast<float>(head.getX()), static_cast<float>(head.getRight()));
        headCell(g, head, index, heading, tail);
    };
    panel(loopArea, "01", "LOOP", loopTail);
    panel(rampArea, "02", "MODULATION", "2 GEN / 1 RATE");
    panel(dipArea, "03", "SWITCHES", juce::String(static_cast<int>(dips.size())) + " FITTED");
    panel(srcArea, "04", "INPUT", srcTail);

    // Row labels: one column, one size, so the four trim rows and the level row
    // read as one table.
    auto rowLabel = [&](const char* text, const juce::Rectangle<int>& slider, int labelW) {
        g.setColour(kInk); g.setFont(labelFont());
        g.drawText(text, slider.getX() - labelW, slider.getY(), labelW - kSp1, slider.getHeight(), juce::Justification::centredLeft);
    };
    rowLabel("MEMORY", memoryDesign, 66);
    rowLabel("WET/LOOP", balanceDesign, 66);
    rowLabel("LEVEL", inputGainDesign, 48);
    // RATE sits inside the rate cell, so it costs no extra row. With SYNC on it is
    // no longer a rate at all, so the caption says what the readout now means.
    const auto* rampSyncValue = proc.apvts.getRawParameterValue("rampSync");
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
    if (const auto* rv = proc.apvts.getRawParameterValue("rampSpeed")) {
        const double secs = 1.0 / brood::rampHertz(rv->load());
        const juce::String rate = spanMode ? juce::String(brood::rampDivLabel(rv->load()))
                                : secs < 1 ? juce::String(juce::roundToInt(secs * 1000)) + "ms"
                                           : juce::String(secs, secs < 10 ? 1 : 0) + "s";
        g.drawText(rate, rampRateDesign.withLeft(rampRateDesign.getRight() - 40), juce::Justification::centredRight);
    }

    // ── under the hood ──
    // The pedal keeps these behind both footswitches. Here they are simply the
    // bottom row: no second page, and each slider is wider than a trim row.
    g.setColour(kInk); g.fillRect(hoodArea.translated(2, 2));
    g.setColour(kPanel); g.fillRect(hoodArea);
    g.setColour(kInk); g.drawRect(hoodArea, 1);
    g.setColour(kInk); g.setFont(labelFont());
    g.drawText("*  UNDER", hoodArea.getX() + 14, hoodArea.getY() + 12, 96, 14, juce::Justification::centredLeft);
    g.drawText("THE HOOD", hoodArea.getX() + 14, hoodArea.getY() + 28, 96, 14, juce::Justification::centredLeft);
    g.setColour(kLine);
    g.drawVerticalLine(hoodCellDesign[0].getX() - 12, static_cast<float>(hoodArea.getY() + 8), static_cast<float>(hoodArea.getBottom() - 8));
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
        const auto cellRect = hoodCellDesign[i].withHeight(14);
        g.setColour(kInk); g.setFont(labelFont());
        const int nameW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), hoodNames[i]) + 2;
        g.drawText(hoodNames[i], cellRect.withWidth(nameW), juce::Justification::centredLeft);
        g.setColour(kMuted);
        const auto value = cellRect.withLeft(cellRect.getX() + nameW + kSp2);
        g.drawFittedText(hoodText[i], value, juce::Justification::centredRight, 1, 0.8f);
        // Name, dotted leader, figure — the whole band reads as one ledger.
        const int figW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), hoodText[i]);
        leader(g, cellRect.getX() + nameW + kSp1, cellRect.getRight() - figW - kSp1, cellRect.getCentreY(), kLine);
    }

    // Ten cells rather than a continuous bar: a level you can count. Reading a
    // figure off a scale is half of what this panel is for.
    auto meter = [&](int y, const char* name, float peak) {
        g.setColour(kMuted); g.setFont(capFont());
        g.drawText(name, meterArea.getX(), y - 2, 24, 11, juce::Justification::centredLeft);
        constexpr int cells = 10;
        const int left = meterArea.getX() + 26, span = meterArea.getWidth() - 26;
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

    // The readout is the one thing here that behaves like a machine's own display,
    // so it is drawn as the styleguide's alert box: 3 px offset, a 2 px frame, a
    // title band with a status chip, and a blinking block where a cursor would be.
    // While the mouse rests on something that has no explanation of its own this
    // explains it; otherwise it is the readout for the last thing you moved.
    const bool helping = hoverText.isNotEmpty();
    g.setColour(kInk); g.fillRect(lcdArea.translated(3, 3));
    g.setColour(kPaper); g.fillRect(lcdArea);
    g.setColour(kInk); g.drawRect(lcdArea, 2);
    auto lcdHead = lcdArea.reduced(2).withHeight(19);
    g.fillRect(lcdHead);
    g.setColour(kPaper); g.setFont(textFont());
    g.drawText("*  THE SIGNAL  /  " + (helping ? hoverLabel : lcdLabel), lcdHead.reduced(8, 0),
               juce::Justification::centredLeft);
    {
        const juce::String chip = helping ? "HELP" : "LIVE";
        const int cw = juce::GlyphArrangement::getStringWidthInt(capFont(), chip) + 8;
        auto box = juce::Rectangle<int>(lcdHead.getRight() - cw - 2, lcdHead.getY() + 3, cw, 13);
        g.setColour(kPaper); g.fillRect(box);
        g.setColour(kInk); g.setFont(capFont());
        g.drawText(chip, box, juce::Justification::centred);
    }
    auto lr = lcdArea.reduced(10, 6).withTrimmedTop(19);
    if (helping) {
        g.setColour(kInk); g.setFont(font(14));
        g.drawFittedText(hoverText, lr.removeFromTop(32), juce::Justification::topLeft, 2, 0.9f);
    } else {
        auto line = lr.removeFromTop(27);
        g.setColour(kInk); g.setFont(font(20));
        const juce::String shown = lcdValue.toUpperCase();
        g.drawFittedText(shown, line, juce::Justification::centredLeft, 1);
        if (blink) {
            const int w = juce::jmin(line.getWidth() - 4,
                                     juce::GlyphArrangement::getStringWidthInt(font(20), shown) + 4);
            g.fillRect(line.getX() + w, line.getY() + 4, 6, line.getHeight() - 8);
        }
    }
    // The figure row: what the machine is doing right now.
    g.setColour(kMuted); g.setFont(labelFont());
    g.drawText(lcdMeta, lr, juce::Justification::centredLeft);
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
        const juce::String txt = act >= 0 ? proc.describe(knobs[static_cast<size_t>(act)].id) : juce::String();
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
    if (wetOn != ledWetOn) { ledWetOn = wetOn; repaintDesign(ledWet.expanded(8)); }
    if (loopOn != ledLoopOn) { ledLoopOn = loopOn; repaintDesign(ledLoop.expanded(8)); }
    if (led != lastLoopLed) {
        const char* msg[] = {nullptr, "playing", "recording...", "overdubbing..."};
        if (msg[led] != nullptr) showReadout("LOOP", msg[led]);
        else if (lastLoopLed == 1) showReadout("LOOP", "stopped");
        lastLoopLed = led;
    }
    const float mi = ui.peakIn.load(), mo = ui.peakOut.load();
    if (std::fabs(mi - meterIn) > 1e-3f || std::fabs(mo - meterOut) > 1e-3f) { meterIn = mi; meterOut = mo; repaintDesign(meterArea.expanded(2)); }
    const juce::String bar = juce::String(proc.getSampleRate() / 1000.0, 1) + " kHz  /  "
                           + juce::String(proc.getTotalNumInputChannels()) + " IN";
    if (bar != barMeta) { barMeta = bar; repaintDesign(pedalArea.withHeight(25)); }
    const int lsrc = ui.lsrc.load();
    const juce::String meta = juce::String(ui.clockHz.load() / 1000.f, 1) + " kHz    "
        + (lsrc > 0 ? "loop " + juce::String(ui.winLen.load() / ui.clockHz.load(), 2) + " s" : juce::String("no loop"))
        + "    cpu " + juce::String(ui.load.load(), 1) + "%";
    if (meta != lcdMeta) { lcdMeta = meta; repaintDesign(lcdArea); }
    if (readoutFollows && lastTouched.isNotEmpty()) {
        const juce::String v = proc.describe(lastTouched);   // follows automation and mode changes
        if (v != lcdValue) { lcdValue = v; repaintDesign(lcdArea); }
    }
    loopView.repaint();   // the play head moves every frame; paint() fetches new peaks
}

bool BroodEditor::keyPressed(const juce::KeyPress& k) {
    const auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    return c == 'a' || c == 'l';   // handled in keyStateChanged so holds work
}

bool BroodEditor::keyStateChanged(bool) {
    const bool a = juce::KeyPress::isKeyCurrentlyDown('a') || juce::KeyPress::isKeyCurrentlyDown('A');
    const bool l = juce::KeyPress::isKeyCurrentlyDown('l') || juce::KeyPress::isKeyCurrentlyDown('L');
    bool used = false;
    if (a != keyA) { keyA = a; fsWet->press(a); used = true; }
    if (l != keyL) { keyL = l; fsLoop->press(l); used = true; }
    return used;
}
