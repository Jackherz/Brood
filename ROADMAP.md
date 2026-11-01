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
- **SPREAD per channel** — the stereo width applies to each side separately.

## Done

- The ramp engine: two generators (wet and loop) writing to TIME, MIX, LENGTH,
  MODIFY, CLOCK or BALANCE, five waveforms, unipolar or bipolar, a rate from one
  cycle every twenty seconds to eight a second, plus the BOUNCE and SWEEP dips.
  Bit-exact between the browser and native cores (`tests/parity/scenarios/ramps.txt`).
- Four wet modes (reverb, delay, slip, weave) and three loop modes (env, tape,
  stretch), three-way routing, wet/loop balance.
- DIP switches: trails, dry kill, latch, spread, smooth clock, tempo sync.
- Loop memory up to 30 s, and END trims how much of the loop plays (the start point is LENGTH's, as on the pedal). LENGTH's travel is linear, so the window reaches the very start of a 30 s take.
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
