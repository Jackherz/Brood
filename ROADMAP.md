# Roadmap

What the emulation still owes the pedal, in the order it is meant to land.

## Next: what the list still owes

Tempo sync is done (rounds 15 and 16): the wet side, ENV and the modulation rate
each follow the host clock, with a switch of their own in the place the block
lives. What is left of the pedal's own list:

- **DIRECT MICRO-LOOP blend** — how much of the *unprocessed* loop reaches the
  output, mixed in beside the wet side.
- **CC 52** stops a running ramp; the MIDI map currently reaches every other
  ramp control.

## Parked: the pedal series (packaging decided 2026-11-01)

Direction is settled, work is not: this becomes a **series of separate plug-ins in
one repository**, one `juce_add_plugin` per pedal, sharing the DSP core, the parity
harness and the design sheet — not one binary that wears several models. A pedal's
panel is its own shape (Habit's reel is not this pedal's clock), memory is spent only in
the instances that need it, and two CC maps never have to share a device. The
tempting alternative — one instrument mixing blocks from several pedals — is
recorded in [docs/FUTURE-PEDALS.md](docs/FUTURE-PEDALS.md) with why it is only
half-feasible (wet blocks and tails would swap fine; there is one looper and one
clock) and the cheap way to price it later: write pedal two as named blocks, and
extract the looper behind an interface the day it needs one. Nothing below is
scheduled.

The **candidate queue** for the series, with research on each, is in
[docs/PEDAL-SERIES.md](docs/PEDAL-SERIES.md): Reverse Mode C, Clean, Generation Loss
MKII, Lossy, Onward, Brothers AM, Big Time and Lost + Found by Chase Bliss, and
Microcosm by Hologram Electronics. Its verdict in one line: the cheapest three
(Reverse Mode C, Clean, Generation Loss MKII) are mostly blocks we already own, the
expensive two (Lost + Found, Microcosm) are expensive because of how many algorithms
they contain rather than how hard they are, and Brothers AM is the fork in the road —
it is analogue, so shipping it means writing "model" instead of "clone". Nothing is
scheduled.

The open-source code that would do part of this work already exists, and is
catalogued with its licences in [docs/OPEN-SOURCE.md](docs/OPEN-SOURCE.md) — most
usefully `pedalzombie` (MIT), which already describes most of these pedals as data,
and DaisySP / Airwindows / chowdsp_wdf / Beads as vocabulary for the blocks.

- **blooper** — parked for now, by decision. Reverse and smooth/stepped speed,
  stopper, loop filter and one-shot are cheap because the blocks already exist;
  eight layers of undo and additive mode are the two hard pieces, and layers lose
  to the save budget (a full take is ~3.7 MB base64 per core; eight layers would
  put tens of MB into every autosave). Skip the 16-loop library and blooper's own
  MIDI map.
- **Habit** — the most feasible of the three, two to three rounds. An
  always-recording reel feeding a 60 s delay with a second read head is mostly
  `hist`, `Delay` and the existing capture path; the costs are a three-minute
  buffer (46 MB per core at the 64 kHz clock, halved as int16, or shared because
  Habit is mono) and a reel that must stay out of the saved state.
- **Bad Mood** — four to six rounds, and the one that fights our own rules. Relay
  and Glue are nearly free, Flip and Burst are a week each, Soup wants an FFT that
  is bit-identical in both cores, Radio is five loopers in one knob. It also opens
  a second layer of meaning on every knob ("Hidden Options"), which is the menu
  diving we removed on purpose — so it needs a written rule for secondary controls
  before any DSP. It also reinstates Latch, Dry Kill and Spread at dip level, which
  round 11 removed.

If the playground is picked up, the groundwork that pays for itself first is: a
pedal contract, panels as data instead of two hand-coded layouts, a state budget
policy, and a parity harness per model.

## Done

- Round 17: **Brood, on one page that reads in tiers, and in stereo.** The product
  is called Brood everywhere (bundles, window, wordmark, code); the plug-in IDs and
  the old state tag are kept, so existing Live sets still open, and the installer
  removes bundles left under the old name. The editor's children are scaled by the
  same transform as the paint, so text inside fields and cells grows with the
  window; the patch moved into the masthead; the knob columns are named on the
  rule (WET CHANNEL / SHARED / MICRO-LOOPER); the side column runs LOOP,
  MODULATION, SWITCHES, INPUT; FREEZE and CLASSIC joined TRAILS and SMOOTH, leaving
  the hood five sliders. Fixes found on the way: the engines' right outputs were
  being discarded (mono sources came out dual mono — reverb, WEAVE and WIDTH lost
  their stereo); two native factory patches loaded the wrong wet mode (normalised
  values written for three modes, read against four) and patches now reset every
  control like the browser's; the LEVEL label and its hover help never appeared;
  the readout could show one control's name over another's value; the title bar
  was drawn grey; `build-plugin.sh` carried a second, stale copy of its own body;
  the processor test still expected a 900 × 640 editor.
- Round 16: **END is an end marker, and the tempo switches live with their knobs.**
  END used to set a window *length* and leave START to divide whatever room was
  left, so dragging END hauled the start marker along with it. START is now
  measured against the take and clamped by the end marker, so END moves the end
  marker and nothing else — and the window's length no longer changes under a
  moving START, which is what kept the move click-free. Sync split three ways:
  SYNC beside TIME (DELAY and WEAVE only), ENV SYNC beside LENGTH (ENV only), and
  the modulation panel's own SYNC. A switch that would do nothing in the current
  mode is not drawn at all — REVERB and SLIP have no beat to follow.
- Round 15: **the tempo reaches ENV, WEAVE and the modulation rate, and RAMPS
  becomes MODULATION.** A synced ENV slices by division of the beat, a synced
  WEAVE reaches back by one, and a synced modulation rate is a span from eight
  bars to a sixteenth rather than a free frequency — eight independent stops, both
  cores, two new parity scenarios. Two new switches: one for ENV/WEAVE (later
  split in round 16) and one in the modulation panel. The panel, its guide entry
  and its readouts stopped calling itself THE RAMPS, since a function generator is
  what it is. Also: the selling decision written down (sell it open source), and
  the build now prints the commit it came from.
- Round 14: **the ramp cells step properly, and STILL is OFF.** The destination
  cells handed their option index straight to a 0..1 parameter, which clamped
  every stop above the first onto 1 — so WET and LOOP only ever offered OFF and
  TIME, SHAPE only TRI and SQR, a click landed on the wrong knob, and a value the
  user had chosen could read back as something else on the next redraw. The
  conversion is now one function (`rampIndexOfValue` / `rampValueOfIndex`) shared
  by both cores and both editors, covered by a round-trip test over every stop.
  The cell also steps from anywhere inside it (only the painted back arrow steps
  back), and OFF is named OFF — the engine's word — instead of STILL, drawn as an
  outline against the solid block of a running ramp. The round also cleared the
  four warnings a clang build printed: the TONE coefficient is a constant worked
  out once instead of a lazily cached float with an `== 0` sentinel, START's two
  read heads are whole samples rather than doubles compared for equality, and the
  editor's spacing tokens now declare only the steps the layout spends.

- Round 13: **START is back.** It had been removed because END was dragging the
  start around with it; with END fixed to the end only, a start trim is the piece
  ENV and STRETCH were missing — until now those modes could only ever hear the
  front of a take. START slides the window through the room LENGTH and END leave
  (in TAPE it reaches back from the tail, since TAPE plays the newest audio), and
  in both cores a change of START is a 25 ms raised-cosine crossfade rather than a
  jump in the read head, so dragging it cannot click. New parity scenario
  `tests/parity/scenarios/start.txt` sweeps it in all three modes at 0.00e+0.

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
