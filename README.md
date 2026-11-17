# Brood

A stereo micro-looper and spatial effect from Glyph Audio, as a plug-in for macOS (VST3,
AU and standalone) and as a browser version that runs the same DSP.

![Brood's panel](docs/images/brood.png)

Brood is inspired by a pedal. It is an independent approximation of that pedal's
published behaviour, not its firmware and not a bit-exact emulation, and it hasn't been
calibrated against the hardware. It is not affiliated with or endorsed by Chase Bliss.

## Features

- **Two channels on one clock.** A wet channel (REVERB, DELAY, SLIP or WEAVE) and a
  micro-looper (ENV, TAPE or STRETCH) share a CLOCK that sets the engine's own sample
  rate, so slowing it darkens and lengthens everything together. DELAY can be a tape
  echo, where turning TIME bends the echoes' pitch.
- **Always listening, or recording.** In CAPTURE, tap the looper to catch up to 30 s of
  the recent past. In RECORD, tap to record what you play and tap again to loop it, or
  record a set number of bars (up to 32 s) that lands on the host's bar lines. A new take
  plays all of itself; trim it with START and END markers on the waveform.
- **Overdub, resample, clear and undo.** Hold to overdub, or latch OVERDUB on (a controller
  that can't hold a button can still overdub). RESAMPLE plays the loop, as you hear it, into
  a new take, for one pass, a number of bars or as long as you like (up to 30 s). UNDO and
  REDO step through the last four things you did, knobs and takes alike.
- **See what's coming in.** Before there's a take, and while a new one records, the
  waveform scrolls the input live, including anything ahead of the looper.
- **Loops that follow the host.** A loop made while the host (Live, say) plays stops when
  playback stops and comes back locked to the song, like a clip. One made with the host
  stopped loops freely.
- **Input, loop and WET:** INPUT LEVEL, LOOP LEVEL and WET MIX, and INPUT MONITOR (AUTO lets
  the loop replace the input while it plays, and opens again to overdub). ROUTING says what
  WET hears: the input, the loop, both, or nothing. FREEZE holds WET ringing on its own.
- **Two modulation sources** with their own rate and shape, routed by dragging onto any
  knob.
- **Tempo sync** for the wet side, ENV and the modulation rate.
- **MIDI CC control** laid out like the pedal's, so a controller set up for it works, and
  host parameters with short names and values that fit a controller's display, such as
  Push's.
- **Presets and the Bench:** factory presets and a preset browser, and the Bench, which
  passes loops to and from other Glyph Audio plug-ins, or back into Brood itself: SEND,
  then TAKE, resamples the output into the loop.
- **Loops are saved with your project**, so a reopened session plays what you heard. A
  loop that follows the host waits for play.

Every control is described in the [manual](docs/MANUAL.md).

## Status

Brood is in active development. The plug-in is developed and tested in Ableton Live 12
on macOS; other hosts and platforms are untested.

## Install

Download `Brood-<version>-macOS.pkg` from [jackherz.com](https://jackherz.com). It works on
Apple Silicon and Intel Macs, and installs the VST3, the Audio Unit and the standalone
app. It's free, and not notarised by Apple, so macOS asks you to allow it once:

1. Open the package. macOS says it can't verify it: click **Done**.
2. Open **System Settings > Privacy & Security**, scroll to *Security*, and click
   **Open Anyway** beside the message about Brood. Confirm with your password.
3. The installer opens. Install, then restart your DAW and rescan its plug-ins.

To build it yourself instead, see [INSTALL.md](INSTALL.md).

## Browser version

Needs Node.js 20 or newer.

```sh
npm ci
npm run dev
```

Open the URL Vite prints, choose DEMO, a FILE or LIVE IN, and press START AUDIO. Audio
stays in your browser. Use headphones with live input.

## Documentation

- [INSTALL.md](INSTALL.md): installing and building on macOS, and what to do if your DAW can't see it
- [docs/MANUAL.md](docs/MANUAL.md): every control, the MIDI map, and how the sound is made
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): building by hand, tests, repository layout
- [ROADMAP.md](ROADMAP.md): what's next, and what is deliberately left out

## Part of Glyph Audio

Brood shares its panel kit, [glyph-audio-kit](https://github.com/Jackherz/glyph-audio-kit),
with [Silt](https://github.com/Jackherz/silt), a granular sampler. The Bench passes audio
between them.

## Licence and credits

Brood's code is licensed under the GNU GPL v3 ([LICENSE](LICENSE)). It began from
[MoodEmu](https://github.com/MaxTeicheira/Cba-guitar-pedal-), also GPLv3; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The native build uses
[JUCE](https://juce.com) 8 under its AGPLv3 licence, and the fonts are under the SIL Open
Font License. Brood does not include or use any Chase Bliss firmware.

If Brood is ever sold, it stays open source under GPLv3; the reasoning is in
[docs/OPEN-SOURCE.md](docs/OPEN-SOURCE.md).
