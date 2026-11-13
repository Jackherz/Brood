#pragma once

#include <algorithm>
#include <cmath>

// The window the waveform's markers ask for, as the parameters that give it: the inverse of
// Core::loopWindow(). START counts from the oldest audio in every mode; a window against
// the end of the take is START 1, so it keeps following the newest audio. TAPE's window
// sits inside LENGTH's span, which can leave a marker nowhere to go, so there LENGTH gives
// way instead: only as far as the marker needs, and it shows on the knob.
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
    const auto startFor = [len, first] (double span)
    {
        if (first >= len - span - 0.5) return 1.0;   // against the end: follow the newest audio
        return len > 64.0 ? std::clamp (first / (len - 64.0), 0.0, 1.0) : 0.0;
    };
    if (! tape)
    {
        out.end = width / len;
        out.start = startFor (width);
        return out;
    }
    const double wanted = std::round (len * std::min (1.0, 0.02 + 0.98 * std::clamp (lengthKnob, 0.0, 1.0)));
    const double span = std::clamp (wanted, width, std::max (width, len - first));
    if (span != wanted)
        out.length = std::clamp ((span / len - 0.02) / 0.98, 0.0, 1.0);
    out.start = startFor (span);
    out.end = std::clamp (width / span, 0.02, 1.0);
    return out;
}
