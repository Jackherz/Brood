# Roadmap

What the emulation still owes the pedal, in the order it is meant to land.

## Next: the ramp engine

**This is the next build.** MOOD MKII's defining behaviour is that its knobs can be
driven by a function generator instead of by hand, per channel. Once this exists the
pedal stops being four effects with two loopers and becomes the thing people
actually use it for: parameters that move on their own, in time, against each
other.

What it has to do:

- **RAMPS** — an eight-position switch choosing which destination the generator
  drives on that channel (Time, Mix, Length, Modify, Clock, and the ramping
  options). Two independent generators, one per channel, so the wet side can ramp
  while the loop stays still.
- **A waveform** — triangle, square, sine, random, smooth random, chosen in the
  hidden menu.
- **A rate** — RAMP SPEED, also in the hidden menu, from a slow breathing drift to
  audio-rate flutter.
- **BOUNCE** (dip) — the ramp ping-pongs instead of resetting, so it never snaps.
- **SWEEP** (dip) — a one-way drift: the ramp climbs and starts again from the
  bottom, which is how the pedal makes things rise for a minute.
- **POLARITY** — hidden menu: unipolar (one direction) or bipolar (both).
- **Synchronisation** — the generator's phase can follow the host tempo (or the
  internal clock) so a ramp lands on the bar.

Implementation notes for whoever picks this up (probably me, next round):

- The generator belongs in `MoodCore`/`mood::Core` as a per-core phase accumulator
  advanced once per **core tick**, not per host sample: everything else in this
  emulation is defined at the core rate, and parity depends on it.
- Keep it bit-exact between `src/mood-worklet.js` and `native/dsp/mood.h`; add a
  `tests/parity/scenarios/ramps.txt` that moves RAMPS, SPEED and the waveform
  while audio runs, the way `hidden.txt` does for the hidden options.
- Random waveforms need their own seeded generator. Do **not** reuse `this.rng` —
  it is shared with STRETCH grains and CLASSIC hiss, and drawing from it would
  change those sounds.
- The ramp should write to the *smoothed* value (`this.s.*`) the same way a knob
  does, so the existing 30 ms smoothing still prevents zipper noise.
- UI: RAMPS is a five-way selector like WET MODE, so it needs a slot on the front
  panel; BOUNCE and SWEEP are two more DIP switches, which means the switch row
  grows from six to eight and the plugin's side column needs a re-think.

## Done

- Four wet modes (reverb, delay, slip, weave) and three loop modes (env, tape,
  stretch), three-way routing, wet/loop balance.
- DIP switches: trails, dry kill, latch, spread, smooth clock, tempo sync.
- Loop memory up to 30 s, start/end trim of the captured region.
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
