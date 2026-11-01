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

// ── the sheet ─────────────────────────────────────────────────────────────
// One small system instead of a drawer of one-off numbers, after the product
// styleguide: spacing snaps to 4 / 8 / 16 / 24 / 32, type to five sizes, and
// elevation is always a hard offset (1 px controls, 2 px panels, 3 px readouts),
// never a blur. Rules carry the structure — ink for frames, hairline grey for
// grouping — and the only textures are a 2 px halftone band and a 20 px dot
// grid, both built from the paper's own colour so the cream/ink palette holds.
constexpr int kSp1 = 4, kSp2 = 8, kSp3 = 16, kSp4 = 24, kSp5 = 32;
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
    // A flat disc with a thin pointer, after the SR-01: one hairline rim, a line
    // standing up from the middle, no bevels. The hard offset shadow is the one
    // nod to the pixel panel it sits in. Everything is proportional so the knob
    // looks the same at every window size (fixed tick radii used to get clipped).
    const float u = std::min(static_cast<float>(w), static_cast<float>(h)) / 104.f;
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)).reduced(15.f * u);
    const float r = std::min(area.getWidth(), area.getHeight()) / 2;
    const auto c = area.getCentre();
    // Five marks, not twenty-one: the scale reads at a glance and the knob stays
    // the loudest thing in the cell.
    g.setColour(juce::Colour(0xffb3b2a7));
    for (int i = 0; i <= 4; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / 4.f;
        g.drawLine(c.x + (r + 4.f * u) * std::sin(a), c.y - (r + 4.f * u) * std::cos(a),
                   c.x + (r + 9.f * u) * std::sin(a), c.y - (r + 9.f * u) * std::cos(a), 1.f * u);
    }
    // The wet channel's two knobs are the black discs, as on the pedal that lent
    // us the shape; the shared and loop knobs are the pale ones.
    const bool dark = slider.getTitle().startsWith("TIME") || slider.getTitle().startsWith("MODIFY (wet)");
    auto face = [&](juce::Point<float> centre, float rad) {
        juce::Path p;
        p.addEllipse(centre.x - rad, centre.y - rad, rad * 2.f, rad * 2.f);
        return p;
    };
    g.setColour(kInk); g.fillPath(face(c.translated(2.6f * u, 3.f * u), r));      // the hard shadow
    g.setColour(dark ? juce::Colour(0xff191916) : juce::Colour(0xffe8e5db));
    g.fillPath(face(c, r));
    g.setColour(kInk); g.strokePath(face(c, r), juce::PathStrokeType(1.2f * u));
    // The rim sits just outside the face: hairline, muted, and it darkens on hover.
    const bool hot = slider.isMouseOverOrDragging();
    g.setColour(hot ? kInk : juce::Colour(0xffb3b2a7));
    g.strokePath(face(c, r + 5.5f * u), juce::PathStrokeType(1.f * u));
    juce::Path pointer;
    pointer.addRectangle(-1.4f * u, -r * 0.83f, 2.8f * u, r * 0.83f);
    const float angle = a0 + pos * (a1 - a0);
    g.setColour(dark ? juce::Colour(0xfff7f5ec) : kInk);
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(c));
}

void MoodEditor::KnobLook::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool) {
    if (b.getHeight() < 34) {
        // Wide, short cell (the ramp panel): switch on the left, name beside it,
        // so UNI / BOUNCE / SWEEP sit in one comfortable row.
        const float u = b.getHeight() / 26.f;
        g.addTransform(juce::AffineTransform::scale(u));
        const auto bounds = juce::Rectangle<float>(0, 0, b.getWidth() / u, 26.f);
        const auto sw = juce::Rectangle<float>(24, 14).withCentre({13, bounds.getCentreY()});
        g.setColour(kInk); g.fillRect(sw);
        const auto handle = juce::Rectangle<float>(9, 10).withCentre({b.getToggleState() ? sw.getRight() - 6 : sw.getX() + 6, sw.getCentreY()});
        g.setColour(kPaper); g.fillRect(handle);
        if (b.hasKeyboardFocus(true)) { g.setColour(kMuted); g.drawRect(sw.expanded(3), 1.5f); }
        g.setColour(b.getToggleState() ? kInk : kMuted); g.setFont(font(12));
        g.drawFittedText(b.getButtonText().toUpperCase(), bounds.withTrimmedLeft(30).toNearestInt(), juce::Justification::centredLeft, 1);
        return;
    }
    // Drawn in a 48.7 x 56 design slot and centred in whatever cell it is given.
    // Scaling by width alone broke when the row went from six switches to three:
    // each cell doubled in width, so the switch doubled too. The scale is now the
    // smaller of "fit the slot's width" and "fit its height", and what is left
    // over becomes margin on both sides.
    constexpr float kSlotW = 292.f / 6.f, kSlotH = 56.f;
    const float u = juce::jmin(b.getWidth() / kSlotW, b.getHeight() / kSlotH);
    g.addTransform(juce::AffineTransform::scale(u).translated((b.getWidth() - kSlotW * u) * 0.5f, 0.f));
    const auto bounds = juce::Rectangle<float>(0, 0, kSlotW, b.getHeight() / u);
    const auto sw = juce::Rectangle<float>(18, 32).withCentre({bounds.getCentreX(), 20});
    g.setColour(kInk); g.fillRect(sw);
    const auto handle = juce::Rectangle<float>(12, 11).withCentre({sw.getCentreX(), b.getToggleState() ? sw.getY()+8 : sw.getBottom()-8});
    g.setColour(kPaper); g.fillRect(handle);
    if (b.hasKeyboardFocus(true)) { g.setColour(kMuted); g.drawRect(sw.expanded(3), 1.5f); }
    g.setColour(kInk); g.setFont(font(12));
    g.drawFittedText(b.getButtonText().toUpperCase(), bounds.withTop(40).withHeight(15).toNearestInt(), juce::Justification::centredTop, 1);
}

// A slider is a printed scale: one 2 px rule for the travel, five marks under it
// and a boxed thumb. No track highlight, no rounded cap, no glow.
void MoodEditor::KnobLook::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
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

void MoodEditor::KnobLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    const auto r = b.getLocalBounds().toFloat().reduced(2.f);
    const bool off = !b.isEnabled();
    // Disabled is drawn, not faded: a grey frame and no shadow, so it still sits
    // on the same sheet instead of turning into a ghost.
    if (!off) { g.setColour(kInk); g.fillRect(r.translated(2.f, 2.f)); }
    g.setColour(down && !off ? kInk : over && !off ? juce::Colour(0xfff1efe6) : kPaper); g.fillRect(r);
    g.setColour(off ? kLine : down ? kPaper : kInk); g.drawRect(r, 1.5f);
}
void MoodEditor::KnobLook::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) {
    g.setColour(!b.isEnabled() ? kLine : down ? kPaper : kInk);
    g.setFont(textFont());
    g.drawFittedText("[ " + b.getButtonText().toUpperCase() + " ]", b.getLocalBounds().reduced(6, 0),
                     juce::Justification::centred, 1, 0.85f);
}

// A field, not a widget: hairline frame, hard shadow, and the chevron inside its
// own inverted cell so the right-hand end of every field looks the same.
void MoodEditor::KnobLook::drawComboBox(juce::Graphics& g, int width, int height, bool,
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

juce::Font MoodEditor::KnobLook::getComboBoxFont(juce::ComboBox&) { return font(15); }
juce::Font MoodEditor::KnobLook::getPopupMenuFont() { return font(16); }

// ───────── 3-way toggle ─────────
MoodEditor::ThreeWay::ThreeWay(juce::RangedAudioParameter& p, juce::StringArray l, std::function<void()> onChange)
    : param(p), labels(std::move(l)), attach(p, [this](float v) { index = juce::roundToInt(v); repaint(); if (changed) changed(); }), changed(std::move(onChange)) {
    attach.sendInitialUpdate();
}
void MoodEditor::ThreeWay::paint(juce::Graphics& g) {
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
void MoodEditor::ThreeWay::mouseDown(const juce::MouseEvent& e) {
    const int count = juce::jmax(1, labels.size());
    const int next = juce::jlimit(0, count - 1, e.y * count / juce::jmax(1, getHeight()));
    attach.setValueAsCompleteGesture(static_cast<float>(next));
}


// ───────── ramp destination stepper ─────────
MoodEditor::Stepper::Stepper(juce::RangedAudioParameter& p, juce::String caption, juce::StringArray l, std::function<void()> onChange)
    : param(p), cap(std::move(caption)), names(std::move(l)),
      attach(p, [this](float v) { index = mood::rampIndexOfValue(v, count()); repaint(); if (changed) changed(); }),
      changed(std::move(onChange)) {
    attach.sendInitialUpdate();
    setTitle(cap);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}
// A ramp cell is a button, so it is drawn like one: an ink frame with a hard
// shadow, arrow blocks either side and the chosen value in an inverted display.
// The whole cell steps; the arrows are there to say that it can.
void MoodEditor::Stepper::paint(juce::Graphics& g) {
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
    g.setFont(font(11)); g.setColour(kMuted);
    const int capW = juce::jmin(inner.getWidth() / 2, 42);
    g.drawFittedText(cap, inner.removeFromLeft(capW), juce::Justification::centredLeft, 1);
    // OFF is drawn as an outline and ON as a solid block, so which one you are
    // looking at needs no legend: a filled display means the tide is running.
    auto value = inner.withTrimmedLeft(2);
    const bool on = index > 0;
    const juce::String shown = index >= 0 && index < names.size() ? names[index] : juce::String();
    if (on) {
        g.setColour(kInk); g.fillRect(value);
        g.setColour(kPaper);
        g.setFont(font(13));
        g.drawFittedText(shown, value.reduced(2, 0), juce::Justification::centred, 1);
    } else {
        g.setColour(kPaper); g.fillRect(value);
        g.setColour(kMuted); g.drawRect(value, 1);
        g.setFont(font(13));
        g.drawFittedText(shown, value.reduced(2, 0), juce::Justification::centred, 1);
    }
}
void MoodEditor::Stepper::step(int by) {
    const int next = ((index + by) % count() + count()) % count();
    attach.setValueAsCompleteGesture(static_cast<float>(mood::rampValueOfIndex(next, count())));
}
void MoodEditor::Stepper::mouseDown(const juce::MouseEvent& e) {
    // Anywhere on the cell steps forward; only the painted back-arrow steps back.
    // The hit zone is the arrow that is drawn, so the picture and the behaviour
    // are the same thing.
    step(e.x < arrowWidth() + 2 ? -1 : 1);
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
    const float s = getWidth() / 84.f;
    g.addTransform(juce::AffineTransform::scale(s));
    auto r = juce::Rectangle<int>(0, 0, 84, juce::roundToInt(getHeight() / s)).reduced(3);
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
    const float sc = getWidth() / 292.f;
    g.addTransform(juce::AffineTransform::scale(sc));
    const auto b = juce::Rectangle<float>(0, 0, 292.f, getHeight() / sc);
    g.setColour(kPaper); g.fillRect(b);
    g.setColour(kInk); g.drawRect(b, 1);
    const int lsrc = proc.ui.lsrc.load();
    if (lsrc <= 0) {
        g.setColour(kMuted); g.setFont(mono(15));
        g.drawText("LISTENING FOR LITTLE MOMENTS_", b, juce::Justification::centred);
        return;
    }
    const int start = proc.ui.winStart.load(), len = proc.ui.winLen.load();
    const float xs = start / static_cast<float>(lsrc), xe = (start + len) / static_cast<float>(lsrc);
    // Everything outside the trim is dimmed, so START/END are visible at a glance.
    g.setColour(juce::Colour(0xffdcdcd2));
    g.fillRect(b.withWidth(xs * b.getWidth()));
    g.fillRect(b.withLeft(b.getX() + xe * b.getWidth()));
    g.setColour(kInk);
    g.fillRect(b.getX() + xs * b.getWidth(), b.getY(), 1.5f, b.getHeight());
    g.fillRect(b.getX() + xe * b.getWidth() - 1.5f, b.getY(), 1.5f, b.getHeight());
    const int n = MoodProcessor::kWaveBins;
    const float bw = b.getWidth() / n, mid = b.getCentreY();
    for (int i = 0; i < n; ++i) {
        const float a = std::min(1.f, wave[static_cast<size_t>(i)] * 1.6f) * (b.getHeight() / 2 - 4);
        g.setColour(i / static_cast<float>(n) >= xs && i / static_cast<float>(n) < xe ? kInk : kLine);
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
            repaintDesign(pedalArea);
        };
    };
    wetMode = std::make_unique<ThreeWay>(*proc.apvts.getParameter("wetMode"), juce::StringArray{"Reverb", "Delay", "Slip", "Weave"}, modeChanged("WET MODE", "wetMode"));
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
    auto loopSlider = [this](juce::Slider& sl, const char* id, const char* name) {
        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        sl.setTitle(name);
        sl.onValueChange = [this, id] { touch(id); };
        addAndMakeVisible(sl);
    };
    loopSlider(memory, "memory", "Loop memory");
    loopSlider(loopEnd, "loopEnd", "How much of the loop plays");
    loopSlider(loopStart, "start", "Where the loop begins in the take");
    memoryAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "memory", memory);
    endAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "loopEnd", loopEnd);
    startAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "start", loopStart);
    // Three switches, not six: DRY KILL was MIX at the top of its travel, LATCH
    // is what the FREEZE switch does, and SPREAD is now WIDTH above the middle.
    const std::pair<const char*, const char*> dipDefs[] = {{"trails", "Trails"}, {"smooth", "Smooth"}, {"sync", "Sync"}};
    for (const auto& [id, label] : dipDefs) {
        Dip d { std::make_unique<juce::ToggleButton>(label), nullptr };
        const juce::String lab = label;
        d.button->onClick = [this, lab, b = d.button.get()] { showReadout("DIP", lab.toLowerCase() + (b->getToggleState() ? " on" : " off")); };
        addAndMakeVisible(*d.button);
        d.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, *d.button);
        dips.push_back(std::move(d));
    }
    auto rampChanged = [this](const char* label, const char* id) {
        return [this, label, id] {
            auto* prm = proc.apvts.getParameter(id);
            if (lastTouched.isNotEmpty() || isVisible()) showReadout(label, prm->getCurrentValueAsText().toLowerCase());
        };
    };
    rampWet = std::make_unique<Stepper>(*proc.apvts.getParameter("rampWet"), "WET", juce::StringArray{"OFF", "TIME", "MIX", "LENGTH", "MODIFY", "CLOCK", "BALANCE"}, rampChanged("RAMP / WET", "rampWet"));
    rampLoop = std::make_unique<Stepper>(*proc.apvts.getParameter("rampLoop"), "LOOP", juce::StringArray{"OFF", "TIME", "MIX", "LENGTH", "MODIFY", "CLOCK", "BALANCE"}, rampChanged("RAMP / LOOP", "rampLoop"));
    rampWave = std::make_unique<Stepper>(*proc.apvts.getParameter("rampWave"), "SHAPE", juce::StringArray{"TRI", "SQR", "SIN", "RND", "SMTH"}, rampChanged("RAMP SHAPE", "rampWave"));
    for (auto* st : {rampWet.get(), rampLoop.get(), rampWave.get()}) addAndMakeVisible(*st);
    rampRate.setSliderStyle(juce::Slider::LinearHorizontal);
    rampRate.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rampRate.setTitle("Ramp speed");
    rampRate.setDoubleClickReturnValue(true, static_cast<double>(proc.apvts.getParameter("rampSpeed")->getDefaultValue()));
    rampRate.onValueChange = [this] { touch("rampSpeed"); };
    addAndMakeVisible(rampRate);
    rampRateAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "rampSpeed", rampRate);
    // The two dips that bend the shape, and the polarity that keeps it above the knob.
    const std::pair<const char*, const char*> rampSwitchDefs[] = {{"rampUni", "UNI"}, {"bounce", "BOUNCE"}, {"sweep", "SWEEP"}};
    for (size_t i = 0; i < rampSwitches.size(); ++i) {
        auto& rs = rampSwitches[i];
        const juce::String lab = rampSwitchDefs[i].second;
        rs.button = std::make_unique<juce::ToggleButton>(lab);
        rs.button->onClick = [this, lab, b = rs.button.get()] { showReadout("RAMP", lab.toLowerCase() + (b->getToggleState() ? " on" : " off")); };
        addAndMakeVisible(*rs.button);
        rs.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, rampSwitchDefs[i].first, *rs.button);
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
        for (auto* id : {"trails", "smooth", "rampUni", "bounce", "sweep"}) if (auto* parameter = proc.apvts.getParameter(id)) parameter->setValueNotifyingHost(0.f);
        for (auto* id : {"rampWet", "rampLoop"}) if (auto* parameter = proc.apvts.getParameter(id)) parameter->setValueNotifyingHost(0.f);
        if (auto* parameter = proc.apvts.getParameter("rampWave")) parameter->setValueNotifyingHost(0.f);
        if (auto* parameter = proc.apvts.getParameter("balance")) parameter->setValueNotifyingHost(0.5f);
        showReadout("PATCH", presetBox.getText().toLowerCase());
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
    hoodFreeze.setButtonText("FREEZE");
    hoodFreeze.setTitle("Freeze the wet side");
    hoodFreeze.onClick = [this] { showReadout("FREEZE", hoodFreeze.getToggleState() ? "held, ringing" : "moving"); };
    addAndMakeVisible(hoodFreeze);
    hoodFreezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "freeze", hoodFreeze);
    hoodClassic.setButtonText("CLASSIC");
    hoodClassic.setTitle("Classic (MK I quirks)");
    hoodClassic.onClick = [this] { showReadout("CLASSIC", hoodClassic.getToggleState() ? "mk I manners, on" : "as built"); };
    addAndMakeVisible(hoodClassic);
    hoodClassicAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "classic", hoodClassic);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(W * 0.75f), juce::roundToInt(H * 0.75f), 1800, 1280);   // 0.75x to 2x of the design panel
    getConstrainer()->setFixedAspectRatio(static_cast<double>(W) / H);
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
    if (id == "clock") repaintDesign(pedalArea);
    // The small readouts beside the trim and hood sliders are drawn by this
    // editor rather than by the control, so they need a repaint to follow it.
    if (id == "memory" || id == "start" || id == "loopEnd" || id == "balance") repaintDesign(loopArea);
    if (id == "tone" || id == "fade" || id == "width" || id == "wetDiv" || id == "loopDiv") repaintDesign(hoodArea);
}

void MoodEditor::showReadout(const juce::String& label, const juce::String& value) {
    if (label == lcdLabel && value == lcdValue) return;
    lcdLabel = label; lcdValue = value;
    repaintDesign(lcdArea);
}


void MoodEditor::resized() {
    // Everything is laid out in design coordinates (900x712) and mapped into the
    // window by one transform, so the panel scales instead of reflowing and no
    // text can slide over its neighbour when the window is resized.
    viewScale = std::min(getWidth() / static_cast<float>(W), getHeight() / static_cast<float>(H));
    view = juce::AffineTransform::scale(viewScale)
               .translated((getWidth() - W * viewScale) * 0.5f, (getHeight() - H * viewScale) * 0.5f);
    auto place = [this](juce::Component& c, const juce::Rectangle<int>& design) { c.setBounds(design.transformedBy(view)); };

    pedalArea = {20, 20, 520, 600};
    const int cx[3] = {pedalArea.getX() + 95, pedalArea.getCentreX(), pedalArea.getRight() - 95};
    for (int i = 0; i < 6; ++i) {
        const int row = i / 3;
        knobDesign[i] = juce::Rectangle<int>(104, 104).withCentre({cx[i % 3], pedalArea.getY() + (row == 0 ? 172 : 394)});
        place(*knobs[static_cast<size_t>(i)].slider, knobDesign[i]);
    }
    const int ty = pedalArea.getY() + 282;
    wetModeDesign = {cx[0] - 48, ty - 44, 130, 88};   // four wet modes
    routingDesign = {cx[1] - 48, ty - 36, 130, 72};
    loopModeDesign = {cx[2] - 48, ty - 36, 130, 72};
    place(*wetMode, wetModeDesign);
    place(*routing, routingDesign);
    place(*loopMode, loopModeDesign);
    const int fy = pedalArea.getBottom() - 52;
    fsWetDesign = juce::Rectangle<int>(84, 84).withCentre({cx[0] + 20, fy});
    fsLoopDesign = juce::Rectangle<int>(84, 84).withCentre({cx[2] - 20, fy});
    place(*fsWet, fsWetDesign);
    place(*fsLoop, fsLoopDesign);
    ledWet = juce::Rectangle<int>(14, 14).withCentre({cx[0] + 20, fy - 56});
    ledLoop = juce::Rectangle<int>(14, 14).withCentre({cx[2] - 20, fy - 56});

    // ── the side column ──
    // Five stacked blocks in a 600 px column. The pitch is set by the controls,
    // not by the panels: 1 px borders, 24 px title bands and 9 px gutters do the
    // separating, so nothing had to shrink to make room for the ramp section.
    auto side = juce::Rectangle<int>(560, 20, 320, 600);
    lcdArea = side.removeFromTop(78);
    side.removeFromTop(9);
    srcArea = side.removeFromTop(92);
    side.removeFromTop(9);
    loopArea = side.removeFromTop(150);
    side.removeFromTop(9);
    dipArea = side.removeFromTop(90);
    side.removeFromTop(9);
    rampArea = side.removeFromTop(118);
    side.removeFromTop(9);
    auto presetRow = side.removeFromTop(26);

    auto src = srcArea.withTrimmedTop(24).reduced(12, 6);
    auto r1 = src.removeFromTop(26);
    sourceBoxDesign = r1.removeFromLeft(170);
    place(sourceBox, sourceBoxDesign);
    r1.removeFromLeft(6);
    loadButtonDesign = r1;
    place(loadButton, loadButtonDesign);
    src.removeFromTop(6);
    auto r2 = src.removeFromTop(24);
    r2.removeFromLeft(44);                       // "LEVEL" is painted in the gap
    meterArea = r2.removeFromRight(76).withSizeKeepingCentre(76, 18);
    place(inputGain, r2.withTrimmedRight(6));

    // Four rows: MEMORY, START, END, WET/LOOP. START is back (it was useful on ENV
    // and STRETCH, where END on its own could only ever shorten the loop from the
    // top), so the waveform gives up 19 px rather than the rows getting thinner.
    auto inner = loopArea.withTrimmedTop(24).reduced(12, 6);
    place(loopView, inner.removeFromTop(36));
    inner.removeFromTop(2);
    memoryDesign = inner.removeFromTop(19).withTrimmedLeft(54).withTrimmedRight(40);
    loopStartDesign = inner.removeFromTop(19).withTrimmedLeft(40).withTrimmedRight(40);
    loopEndDesign = inner.removeFromTop(19).withTrimmedLeft(26).withTrimmedRight(40);
    balanceDesign = inner.removeFromTop(19).withTrimmedLeft(58).withTrimmedRight(40);
    place(memory, memoryDesign);
    place(loopStart, loopStartDesign);
    place(loopEnd, loopEndDesign);
    place(balance, balanceDesign);

    auto dipRow = dipArea.withTrimmedTop(24).reduced(12, 5);
    const int dw = dipRow.getWidth() / juce::jmax(1, static_cast<int>(dips.size()));
    for (size_t i = 0; i < dips.size(); ++i) {
        dipCellDesign[i] = dipRow.removeFromLeft(dw);
        place(*dips[i].button, dipCellDesign[i]);
    }

    auto ramp = rampArea.withTrimmedTop(24).reduced(12, 5);
    auto rampRow = ramp.removeFromTop(26);
    const int half = (rampRow.getWidth() - 6) / 2;
    rampWetDesign = rampRow.removeFromLeft(half);
    place(*rampWet, rampWetDesign);
    rampRow.removeFromLeft(6);
    rampLoopDesign = rampRow;
    place(*rampLoop, rampLoopDesign);
    ramp.removeFromTop(3);
    rampRow = ramp.removeFromTop(26);
    rampWaveDesign = rampRow.removeFromLeft(half);
    place(*rampWave, rampWaveDesign);
    rampRow.removeFromLeft(6);
    rampRateDesign = rampRow;                    // "RATE" is painted over its left end
    place(rampRate, rampRateDesign.withTrimmedLeft(38).reduced(0, 3));
    ramp.removeFromTop(3);
    rampRow = ramp.removeFromTop(26);
    const int swCell = (rampRow.getWidth() - 14) / 3;   // three even cells, 7 px apart
    for (size_t i = 0; i < rampSwitches.size(); ++i) {
        const bool last = i + 1 == rampSwitches.size();
        rampSwitchDesign[i] = last ? rampRow : rampRow.removeFromLeft(swCell);
        if (!last) rampRow.removeFromLeft(7);
        place(*rampSwitches[i].button, rampSwitchDesign[i]);
    }

    presetBoxDesign = presetRow.withTrimmedLeft(44);   // "PATCH" is painted to its left

    // ── under the hood ──
    // One band across the bottom of the window: the five sliders and the CLASSIC
    // switch the pedal keeps behind both footswitches. Six even cells, so each
    // slider is as wide as a trim row and stays easy to grab.
    hoodArea = {20, 636, 860, 56};
    const int hoodLeft = hoodArea.getX() + 118;
    const int cell = (hoodArea.getRight() - 14 - hoodLeft - 6 * 8) / 7;
    for (int i = 0; i < 7; ++i) hoodCellDesign[i] = {hoodLeft + i * (cell + 8), hoodArea.getY() + 4, cell, 48};
    for (size_t i = 0; i < hood.size(); ++i) place(hood[i].slider, hoodCellDesign[i].withY(hoodArea.getY() + 23).withHeight(22));
    place(hoodClassic, hoodCellDesign[5].withY(hoodArea.getY() + 23).withHeight(22));
    place(hoodFreeze, hoodCellDesign[6].withY(hoodArea.getY() + 23).withHeight(22));
    place(presetBox, presetBoxDesign);
    rebuildHoverHelp();
}

// Everything that has no explanation of its own: the switches, the steppers, the
// sliders whose label is only a label, the source row, the footswitches. The six
// knobs are left out on purpose — they already whisper, and two explanations of
// the same control in two different voices is worse than one.
void MoodEditor::rebuildHoverHelp() {
    hoverHelp.clear();
    auto add = [this](juce::Rectangle<int> r, const char* label, const char* text) {
        hoverHelp.push_back({r, juce::String(label), juce::String(text)});
    };
    static const char* dipText[] = {
        "the tails keep falling after you switch the wet side off.",
        "the sample rate slides instead of stepping, so CLOCK bends pitch as it turns.",
        "the delay stops being a length of time and becomes a division of the beat.",
    };
    static const char* dipNames[] = {"TRAILS", "SMOOTH", "SYNC"};
    for (size_t i = 0; i < dips.size() && i < 3; ++i) add(dipCellDesign[i], dipNames[i], dipText[i]);

    add(rampWetDesign, "RAMP / WET", "click anywhere to step the first tide through off, time, mix, length, modify, clock and balance; the left arrow steps back. off leaves the knob alone, and a filled display means the tide is running.");
    add(rampLoopDesign, "RAMP / LOOP", "click anywhere to step the second tide through the same list; its modify means this channel's own. off leaves the knob alone, and a filled display means the tide is running.");
    add(rampWaveDesign, "RAMP SHAPE", "click anywhere to step the shape: triangle, square, sine, random, smooth random. it only changes what the two tides do, never where they are pointed.");
    add(rampRateDesign, "RAMP RATE", "how slowly the tide turns. both sides share it, so they stay in step with each other.");
    static const char* rampSwitchText[] = {
        "the swing sits above the knob instead of either side of it.",
        "the swing reflects instead of snapping back, so nothing jumps.",
        "the shape is ignored and the knob just climbs, again and again.",
    };
    static const char* rampSwitchNames[] = {"UNIPOLAR", "BOUNCE", "SWEEP"};
    for (int i = 0; i < 3; ++i) add(rampSwitchDesign[i], rampSwitchNames[i], rampSwitchText[i]);

    add(memoryDesign.expanded(0, 2), "LOOP MEMORY", "how much of the recent past is kept. hard left waits about a second, like the pedal.");
    add(loopStartDesign.expanded(0, 2), "START", "where the loop begins inside what was recorded. in ENV and STRETCH it slides towards the present; in TAPE it reaches back. it crossfades as it moves, so it will not click.");
    add(loopEndDesign.expanded(0, 2), "END", "how much of the loop plays before it wraps.");
    add(balanceDesign.expanded(0, 2), "WET / LOOP", "a crossfade between the two worlds. hard right is the loop on its own.");

    add(sourceBoxDesign, "SOURCE", "what the pedal listens to: the track, a phrase made here, or a file.");
    add(loadButtonDesign, "LOAD FILE", "drop a recording in. it starts looping from the moment it arrives.");
    add(inputGainDesign.expanded(0, 3), "LEVEL", "how hard the input hits the machine. easy to overdo.");

    add(wetModeDesign, "WET MODE", "four ways of treating time: reverb, delay, slip, weave.");
    add(routingDesign, "ROUTING", "what the wet side is allowed to hear: the input, the loop, or both.");
    add(loopModeDesign, "LOOP MODE", "three ways of holding a moment: envelope, tape, stretch.");

    add(fsWetDesign, "WET FOOTSWITCH", "tap for the wet side, hold to freeze it where it stands.");
    add(fsLoopDesign, "LOOP FOOTSWITCH", "tap to catch the recent past, hold to overdub on top of it.");

    add(presetBoxDesign, "PATCH", "five places to start. what the knobs do afterwards is yours.");

    static const char* hoodText[] = {
        "a hi-cut on the wet side. right is open, left is soft and far away.",
        "how much of the old take an overdub erases. left keeps the pedal's slow erosion, right replaces it. hold the LOOP footswitch while it plays to overdub.",
        "how far apart the two speakers sit: left is mono, the middle is the image as built, right opens the sides.",
        "the wet side runs on a divided clock, so the room and the echo stretch out.",
        "the loop runs on a divided clock: lower, longer, further from the other side.",
        "the MK I in the room: a noisy clock, a loop that never sits still.",
        "the hold gesture without the holding: the wet side comes up frozen and rings on its own.",
    };
    static const char* hoodNames[] = {"TONE", "FADE", "WIDTH", "WET DIV", "LOOP DIV", "CLASSIC", "FREEZE"};
    for (int i = 0; i < 7; ++i) add(hoodCellDesign[i], hoodNames[i], hoodText[i]);
}

void MoodEditor::paint(juce::Graphics& g) {
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
    g.fillRect(bar);
    g.setColour(kPaper); g.setFont(font(15));
    const juce::String title = "*  MOOD.EXE   /   A SOUND EXPERIMENT";
    g.drawText(title, bar.reduced(11, 0), juce::Justification::centredLeft);
    // The bar is also the machine's status line: dotted fill, then the rate it is
    // actually running at, boxed off at the right the way a manifest ends.
    g.setFont(capFont());
    const int metaW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), barMeta) + 8;
    const int dotsFrom = bar.getX() + 11 + juce::roundToInt(juce::GlyphArrangement::getStringWidth(font(15), title)) + 12;
    for (int x = dotsFrom; x < bar.getRight() - 12 - metaW - 8; x += 5)
        for (int y = bar.getY() + 7; y < bar.getBottom() - 5; y += 5) g.fillRect(x, y, 2, 2);
    if (barMeta.isNotEmpty()) {
        auto chip = juce::Rectangle<int>(bar.getRight() - 9 - metaW, bar.getY() + 4, metaW, 17);
        g.setColour(kPaper); g.fillRect(chip);
        g.setColour(kInk); g.setFont(capFont());
        g.drawText(barMeta, chip.reduced(4, 0), juce::Justification::centredRight);
    }
    // The masthead is a stack with a known height: wordmark, subtitle, rule, and
    // only then the knob names. The names sit at cellTop - 21 (99 px into the
    // pedal, 119 in window coordinates), so everything above them is measured
    // from that line backwards instead of being placed by eye. That is what
    // stops TIME / MIX / LENGTH from striking through either the subtitle or the
    // rule between them.
    g.setColour(kInk); g.setFont(pixel(30));
    g.drawText("MOOD.", pedalArea.getX() + 23, pedalArea.getY() + 30, 280, 40, juce::Justification::centredLeft);
    g.setFont(font(16));
    g.drawText("A LITTLE WORLD OF SOUND & SPACE", pedalArea.getX() + 24, pedalArea.getY() + 70, 325, 18, juce::Justification::centredLeft);
    g.setColour(kMuted); g.setFont(font(13));
    g.drawText("WET   +   MICRO-LOOPER", pedalArea.getRight() - 180, pedalArea.getY() + 38, 160, 36, juce::Justification::centredRight);
    g.setColour(kLine); g.drawHorizontalLine(pedalArea.getY() + 94, static_cast<float>(pedalArea.getX() + 14), static_cast<float>(pedalArea.getRight() - 14));

    const int wm = juce::roundToInt(proc.apvts.getRawParameterValue("wetMode")->load());
    const int lm = juce::roundToInt(proc.apvts.getRawParameterValue("loopMode")->load());
    for (size_t i = 0; i < knobs.size(); ++i) {
        const auto kb = knobDesign[i];
        // Row two's names sit under the mode selectors: start them at 348, not 341.
        const auto nameRow = kb.withY(kb.getY() - (i < 3 ? 21 : 14)).withHeight(18);
        g.setColour(kInk); g.setFont(font(17));
        g.drawText(knobs[i].label, nameRow, juce::Justification::centred);
        // The cell carries its own number, tied to the name rather than floating
        // in the corner, so six knobs read as six numbered lines of a manifest.
        g.setColour(kMuted); g.setFont(capFont());
        const int nameW = juce::GlyphArrangement::getStringWidthInt(font(17), knobs[i].label);
        g.drawText(juce::String(i + 1).paddedLeft('0', 2),
                   juce::Rectangle<int>(kb.getCentreX() + nameW / 2 + 5, nameRow.getY() + 1, 18, 13),
                   juce::Justification::centredLeft);
        g.setColour(kMuted); g.setFont(font(13));
        const int mode = kKnobMode[i] == 0 ? wm : kKnobMode[i] == 1 ? lm : 0;
        juce::String whisper = kWhisper[i][juce::jlimit(0, 3, mode)];
        if (i == 3) whisper += "  /  wet";
        if (i == 5) whisper += "  /  loop";
        g.setFont(font(13));
        // One column wide, wrapped to two lines: a long whisper previously ran
        // into the next knob's caption.
        // ... and row one's whispers stop at 256, clear of the selectors at 258.
        g.drawFittedText(whisper, kb.withY(kb.getBottom() - (i < 3 ? 8 : 2)).withHeight(20).withSizeKeepingCentre(150, 20),
                         juce::Justification::centred, 2);
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
        auto line = juce::Rectangle<int>(pedalArea.getCentreX() - 96, pedalArea.getBottom() - 48, 192, 18);
        g.setColour(kMuted); g.setFont(font(12));
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
    const juce::String loopTail = memV <= 0.001f ? juce::String("PEDAL")
                                                 : juce::String(mood::expMap(memV, 1.0, 30.0), 1) + " s";

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
    panel(srcArea, "01", "INPUT SOURCE", srcTail);
    panel(loopArea, "02", "LOOP MEMORY", loopTail);   // same rects resized() fills
    panel(dipArea, "03", "THE LITTLE SWITCHES", juce::String(dips.size()) + " FITTED");
    // Three switches in a row that used to hold six leaves a lot of air, so each
    // one gets its own cell: a hairline box, the switch inside it, the name under.
    // A table of three, rather than three things floating.
    for (size_t i = 0; i < dips.size(); ++i) {
        const auto cell = dipCellDesign[i].reduced(kSp1, 2);
        g.setColour(kPaper); g.fillRect(cell);
        g.setColour(kLine); g.drawRect(cell, 1);
        cornerTicks(g, cell.reduced(1), 4, kLine);
    }
    panel(rampArea, "04", "THE RAMPS", "2 GEN / 1 CLOCK");
    g.setColour(kMuted); g.setFont(font(11));
    g.drawText("CLICK A CELL TO STEP", rampArea.withTrimmedTop(4).withTrimmedRight(10), juce::Justification::centredRight);
    const int sx = srcArea.getX() + 14, lx = loopArea.getX() + 14;
    g.setColour(kInk); g.setFont(font(14));
    g.drawText("LEVEL", sx, inputGainDesign.getY(), 42, inputGainDesign.getHeight(), juce::Justification::centredLeft);
    // One label for the row: "BALANCE" plus its two end names did not fit and
    // overlapped in the 320 px side column.
    g.setFont(font(12));
    g.drawText("WET/LOOP", lx, balanceDesign.getY(), 56, balanceDesign.getHeight(), juce::Justification::centredLeft);
    g.setColour(kInk); g.setFont(font(13));
    g.drawText("MEMORY", lx, memoryDesign.getY(), 52, memoryDesign.getHeight(), juce::Justification::centredLeft);
    g.drawText("START", lx, loopStartDesign.getY(), 38, loopStartDesign.getHeight(), juce::Justification::centredLeft);
    g.drawText("END", lx, loopEndDesign.getY(), 24, loopEndDesign.getHeight(), juce::Justification::centredLeft);
    // RATE sits inside the rate cell, so it costs no extra row.
    g.setFont(font(11)); g.setColour(kMuted);
    g.drawText("RATE", rampRateDesign.getX(), rampRateDesign.getY(), 36, rampRateDesign.getHeight(), juce::Justification::centredLeft);
    g.setColour(kMuted); g.setFont(font(12));
    // Short readouts for the narrow column; the full wording lives in the host's
    // parameter display (that is what Ableton shows in its own inspector).
    auto readout = [&](const char* id, juce::Rectangle<int> r, bool percent) {
        const auto* v = proc.apvts.getRawParameterValue(id);
        if (v == nullptr) return;
        juce::String txt;
        if (percent) txt = juce::String(juce::roundToInt(v->load() * 100.f)) + "%";
        else if (v->load() <= 0.001f) txt = "PEDAL";
        else txt = juce::String(mood::expMap(v->load(), 1, 30), 1) + "s";
        g.drawText(txt, r, juce::Justification::centredRight);
    };
    readout("memory", memoryDesign.withX(memoryDesign.getRight() + 3).withWidth(36), false);
    readout("loopEnd", loopEndDesign.withX(loopEndDesign.getRight() + 3).withWidth(36), true);
    readout("start", loopStartDesign.withX(loopStartDesign.getRight() + 3).withWidth(36), true);
    readout("balance", balanceDesign.withX(balanceDesign.getRight() + 3).withWidth(36), true);   // loop side, %
    g.setColour(kInk); g.setFont(font(14));
    g.drawText("* PATCH", presetBoxDesign.getX() - 44, presetBoxDesign.getY(), 42, presetBoxDesign.getHeight(), juce::Justification::centredLeft);

    // ── under the hood ──
    // The pedal keeps these behind both footswitches. Here they are simply the
    // bottom row: no second page, and each slider is wider than a trim row.
    g.setColour(kPanel); g.fillRect(hoodArea);
    g.setColour(kInk); g.drawRect(hoodArea, 1);
    g.setColour(kInk); g.setFont(font(12));
    g.drawText("*  UNDER", hoodArea.getX() + 14, hoodArea.getY() + 12, 96, 14, juce::Justification::centredLeft);
    g.drawText("THE HOOD", hoodArea.getX() + 14, hoodArea.getY() + 28, 96, 14, juce::Justification::centredLeft);
    g.setColour(kLine);
    g.drawVerticalLine(hoodCellDesign[0].getX() - 10, static_cast<float>(hoodArea.getY() + 8), static_cast<float>(hoodArea.getBottom() - 8));
    static const char* hoodNames[] = {"TONE", "FADE", "WIDTH", "WET DIV", "LOOP DIV"};
    auto* toneP = proc.apvts.getRawParameterValue("tone");
    auto* fadeP = proc.apvts.getRawParameterValue("fade");
    auto* widthP = proc.apvts.getRawParameterValue("width");
    auto* wetDivP = proc.apvts.getRawParameterValue("wetDiv");
    auto* loopDivP = proc.apvts.getRawParameterValue("loopDiv");
    const float hv[5] = {toneP ? toneP->load() : 1.f, fadeP ? fadeP->load() : 0.f, widthP ? widthP->load() : 0.5f,
                         wetDivP ? wetDivP->load() : 0.f, loopDivP ? loopDivP->load() : 0.f};
    auto percent = [](float v) { return juce::String(juce::roundToInt(v * 100.f)) + "%"; };
    juce::String hoodText[5];
    hoodText[0] = hv[0] > 0.995f ? "open" : "cut " + percent(1.f - hv[0]);
    hoodText[1] = hv[1] <= 0.001f ? "pedal" : "keeps " + percent(1.f - hv[1]);
    hoodText[2] = hv[2] <= 0.005f ? "mono" : hv[2] < 0.49f ? "nar " + percent(hv[2] * 2) :
                  hv[2] > 0.51f ? "wid " + percent((hv[2] - 0.5f) * 2) : "mid";
    hoodText[3] = juce::String(mood::divMap(hv[3])) + "x";
    hoodText[4] = juce::String(mood::divMap(hv[4])) + "x";
    for (int i = 0; i < 7; ++i) {
        const auto cellRect = hoodCellDesign[i];
        g.setColour(kInk); g.setFont(font(11));
        // Fitted, not clipped: "keeps 100%" is wider than half a cell at full size.
        g.drawFittedText(i < 5 ? juce::String(hoodNames[i]) : juce::String(),
                         cellRect.withHeight(14).withWidth(cellRect.getWidth() / 2),
                         juce::Justification::centredLeft, 1, 0.8f);
        if (i < 5) {
            g.setColour(kMuted);
            const auto value = cellRect.withHeight(14).withLeft(cellRect.getCentreX() + 2);
            g.drawFittedText(hoodText[i], value, juce::Justification::centredRight, 1, 0.8f);
            // Name, dotted leader, figure — the whole band reads as one ledger.
            const int nameEnd = cellRect.getX() + juce::jmin(cellRect.getWidth() / 2,
                                                             juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), hoodNames[i]) + 4);
            leader(g, nameEnd + 3, value.getX() - 3, cellRect.getY() + 7, kLine);
        }
    }

    // Ten cells rather than a continuous bar: a level you can count. Reading a
    // figure off a scale is half of what this panel is for.
    auto meter = [&](int y, const char* name, float peak) {
        g.setColour(kMuted); g.setFont(capFont());
        g.drawText(name, meterArea.getX(), y, 24, 9, juce::Justification::centredRight);
        constexpr int cells = 10;
        const int left = meterArea.getX() + 28, span = meterArea.getWidth() - 28;
        const int cw = juce::jmax(2, span / cells);
        const float level = juce::jlimit(0.f, 1.f, (juce::Decibels::gainToDecibels(peak, -60.f) + 60.f) / 60.f);
        const int lit = juce::roundToInt(level * (float) cells);
        for (int i = 0; i < cells; ++i) {
            const auto cell = juce::Rectangle<int>(left + i * cw + 1, y, cw - 2, 8);
            g.setColour(i < lit ? kInk : kPaper); g.fillRect(cell);
            g.setColour(i < lit ? kInk : kLine); g.drawRect(cell, 1);
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
        g.drawFittedText(hoverText, lr.removeFromTop(30), juce::Justification::topLeft, 2);
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
    // The figure row: dotted leaders out to whatever the machine is doing.
    g.setColour(kMuted); g.setFont(font(13));
    g.drawText(lcdMeta, lr, juce::Justification::centredLeft);
}

void MoodEditor::timerCallback() {
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
    if (lastTouched.isNotEmpty() && lcdLabel != "LOOP" && lcdLabel != "DIP" && lcdLabel != "INPUT" && lcdLabel != "PATCH") {
        const juce::String v = proc.describe(lastTouched);   // follows automation and mode changes
        if (v != lcdValue && lcdLabel.isNotEmpty()) { lcdValue = v; repaintDesign(lcdArea); }
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
