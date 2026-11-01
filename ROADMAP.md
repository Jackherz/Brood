# Roadmap

What the emulation still owes the pedal, in the order it is meant to land.

## Next: the ramps that keep time

The ramp engine is in (see Done, below). What it still owes the pedal is the part
that makes it musical rather than merely moving:

- **SYNC assignment** — the generator's phase follows the host tempo instead of a
  free rate, so a slow ramp lands on the bar rather than wherever it happens to be.
  The hardware does this from the hidden menu; CC 52 stops a running ramp.
- **DIRECT MICRO-LOOP blend** — how much of the *unprocessed* loop reaches the
  output, mixed in beside the wet side.

## Done

- Round 12: one design sheet for both panels. Spacing, type and elevation are
  tokens rather than one-off numbers; panel heads are halftone bands with a
  number, a name and a dotted leader out to a figure; knobs are numbered; the
  readout is an alert box with a status chip and a blinking cursor; the meters are
  ten countable cells; the switches are boxed cells; sliders are a rule with a
  boxed tab; buttons and combo boxes are flat frames with a hard shadow. The
  palette did not change.

- Round 11: **LATCH** and **DRY KILL** are gone — MIX alone takes the dry away at
  the top of its travel, and the FREEZE switch latches what a held footswitch only
  does while held. **SPREAD** is gone too: **WIDTH** owns the stereo image, from
  mono at 0 % through the image as built at 50 % to open sides above it.
- **Bounces capture the take.** The loop used to vanish when a host prepared the
  plugin for an offline pass, and nothing carried it in the project state. Now
  `prepare()` keeps the take, and the audio itself is written into the saved state
  as gzipped 16-bit PCM per core, swapped in on the audio thread after a reload.
  A bounce in Ableton is what was playing.

- The ramp engine: two generators (wet and loop) writing to TIME, MIX, LENGTH,
  MODIFY, CLOCK or BALANCE, five waveforms, unipolar or bipolar, a rate from one
  cycle every twenty seconds to eight a second, plus the BOUNCE and SWEEP dips.
  Bit-exact between the browser and native cores (`tests/parity/scenarios/ramps.txt`).
- Four wet modes (reverb, delay, slip, weave) and three loop modes (env, tape,
  stretch), three-way routing, wet/loop balance.
- DIP switches: trails, smooth clock, tempo sync.
- Loop memory up to 30 s, and END trims how much of the loop plays (the start point is LENGTH's, as on the pedal). LENGTH's travel is linear, so the window reaches the very start of a 30 s take.
- New knob face: a flat disc, one hairline rim, a thin pointer and five marks, after the SR-01. Wet channel black, shared and loop pale. The panel now uses ink for structure and hairline grey for grouping only — no dashed rules left — and the ramp cells read as buttons.
- The WET / LOOP balance is a blend of two full-level worlds; the direct loop is no longer ducked against the wet level, so SLIP and WEAVE cannot swallow the loop in the middle of the travel.
- Ramp cells read as buttons (frame, shadow, arrow blocks, inverted value) and say what pressing them does in the hover readout.
- A **FREEZE** switch: the held-footswitch freeze without holding the foot. The wet side comes up held, the reverb rings on its own, and letting the switch go releases the tail. Also on CC 105.
- Click-free re-takes: the capture history advances while a loop is playing too, so catching a loop over and over never splices a step in, and the capture length is a whole number of samples (MEMORY could previously leave it fractional and poison the loop with NaN).
- Hidden options: tone, fade, stereo width, wet and loop clock division, classic.
- Host tempo sync for the delay (VST3/AU follows the DAW playhead).

## Deliberately not implemented

- **Buffer length** — superseded by LOOP MEMORY, which is the same idea with a
  wider range and a real-time unit.
- **True bypass** — a hardware relay; there is nothing to emulate in a plugin.
- **MIDI clock ignore / MIDI clock sync** — the host is the clock.
- **Expression and CV inputs** — a DAW has automation instead.
- **MISO** — routing already covers what it does.
- **Synth mode** (ADSR envelopes, pitch transposition per the effects-database
  entry) — mentioned in third-party documentation but not part of the MK II
  firmware feature set this build follows.
