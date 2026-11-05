#pragma once

#include <algorithm>
#include <cmath>

// The window the waveform's markers ask for, as the parameters that give it: the inverse of
// Core::loopWindow(). ENV and STRETCH measure against the whole take. TAPE's window sits
// inside LENGTH's span, which can leave a marker nowhere to go, so here LENGTH gives way
// instead: only as far as the marker needs, and it shows on the knob.
struct LoopTrim
{
    double start = 0.0, end = 1.0, length = 0.0;
    int firstSample = 0, width = 64;   // where it came out, after the limits
};

inline LoopTrim loopTrim (bool tape, int takeLength, double lengthKnob, double wantedStart, double wantedWidth) noexcept
{
    const double len = std::max (64, takeLength);
    const double shortest = std::max (64.0, 0.02 * len);
    const double width = std::clamp (wantedWidth, shortest, len);
    const double first = std::clamp (wantedStart, 0.0, len - width);

    LoopTrim out;
    out.firstSample = (int) std::lround (first);
    out.width = (int) std::lround (width);
    out.length = lengthKnob;
    if (! tape)
    {
        out.end = width / len;
        out.start = len > 64.0 ? std::clamp (first / (len - 64.0), 0.0, 1.0) : 0.0;
        return out;
    }
    const double wanted = std::round (len * std::min (1.0, 0.02 + 0.98 * std::clamp (lengthKnob, 0.0, 1.0)));
    const double span = std::clamp (wanted, width, std::max (width, len - first));
    if (span != wanted)
        out.length = std::clamp ((span / len - 0.02) / 0.98, 0.0, 1.0);
    const double room = len - span;
    out.start = room > 0.0 ? std::clamp (1.0 - first / room, 0.0, 1.0) : 0.0;
    out.end = std::clamp (width / span, 0.02, 1.0);
    return out;
}

// TAPE: LENGTH turned by hand, with START already moved off its default. START stays where it
// is and the window grows or shrinks from its end, until it reaches the end of the take.
inline double loopStartHolding (int firstSample, int takeLength, double lengthKnob) noexcept
{
    const double len = std::max (64, takeLength);
    const double span = std::round (len * std::min (1.0, 0.02 + 0.98 * std::clamp (lengthKnob, 0.0, 1.0)));
    const double room = len - span;
    return room > 0.0 ? std::clamp (1.0 - (double) firstSample / room, 0.0, 1.0) : 0.0;
}
