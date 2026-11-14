# OVERDUB, CLEAR and UNDO, and the live view while re-recording

Requested on 14 November 2026, after the Push 3 check: a Push knob can't hold a
momentary switch, so overdub needs its own latching control. Also: a way to clear the take,
undo for clears and takes, and the live input on the strip whenever a new take is
being recorded.

## Decisions (asked 14 November)

- **OVERDUB** is a latching on/off host parameter (`overdub`, "Overdub") and a plate beside
  the LOOP switch. No MIDI CC. On, it layers into the playing loop until switched off.
  Holding LOOP still overdubs while held, as now.
- **With no loop playing, OVERDUB is greyed out and can't be clicked.** A host or Push
  that switches the parameter on anyway is ignored, and the parameter is set back to off
  so the controller shows the truth. If the loop stops (a tap, CLEAR, UNDO back to
  empty) while OVERDUB is on, it switches off.
- **CLEAR** is a plate by the strip and a host parameter (`clear`, "Clear"). It empties the
  take, stops the loop, and the strip goes back to the live input. The set saves nothing
  for the take afterwards.
- **UNDO** is a plate by the strip and a host parameter (`undo`, "Undo"). It keeps one step,
  and pressing it again redoes. It steps back over:
  - a CLEAR: the take comes back, stopped.
  - a new take (CAPTURE or RECORD): back to the take before it (stopped), or empty if there
    wasn't one.
  - an overdub: the last layer comes off. A layer is everything from OVERDUB on (or a hold
    starting) to OVERDUB off (or the hold ending).
- **The live view over an existing take** shows while RECORD is waiting for the bar line or
  recording, while a CAPTURE hold runs, and after CLEAR. A stopped take otherwise stays on the
  strip, so its START and END can still be edited.

CLEAR and UNDO are triggers: each acts when its parameter goes on, and the processor sets
it back to off from the message thread, so a Push button lights for a moment and goes
out.

## How it's built

### DSP (`native/dsp/brood.h`, mirrored in `src/brood-worklet.js`)

- **The undo copy is a second buffer the size of the take buffer** (`undoBuf`, allocated
  with `loopBuf`), plus what describes it: its length, and which kind of step it undoes.
  About 8 MB per side, 16 MB in all.
- **New take:** `capture()` swaps `loopBuf` and `undoBuf` (a pointer swap, no copying) before
  writing, so the old take is the undo copy. The step is "take".
- **CLEAR:** swap, so the cleared take is the undo copy; `lsrc = 0`, the loop stops, the
  overdub stops. The step is "clear".
- **Overdub:** when a layer begins (the overdub gain leaves zero), copy the take into
  `undoBuf` first. This is a copy of up to `lsrc` samples on the audio thread, as big as the
  copy a capture already makes. The step is "dub".
- **UNDO:** swap `loopBuf` and `undoBuf` and their lengths, and keep the step, so a second
  UNDO is the redo. The loop stops after an undo (it plays again with a tap, as a stopped take
  does now), and the playhead goes back to the start. With nothing to undo it does nothing.
  - Open: should an undone overdub keep playing, since the take is the same length? I'll
    keep it playing if the loop was playing, which is gentler. Say if you'd rather it stopped.
- **Latching overdub:** `p.overdubLatch` sets `overdub` while a loop plays. The hold
  gesture still works. Releasing a hold doesn't end a latched overdub.
- **The state** reports `canOverdub` (a loop is playing), `canUndo`, and what UNDO would do
  next ("undo clear", "undo take", "undo layer", or the redo of one), for the plates and the
  readout.

Everything sounds the same with the new controls left alone, so the parity check and the
recorded baseline must stay identical. New parity scenarios cover OVERDUB latched, CLEAR,
UNDO after each step, and REDO.

### Processor

- The three parameters. CLEAR and UNDO act on their rising edge, then an async update sets
  them back to off. OVERDUB is set back to off when the core says it can't overdub.
- Saving: after a CLEAR the set saves no take. The undo copy isn't saved, so a reopened
  set can't undo.
- The strip's waveform is redrawn after CLEAR and UNDO, as after a capture.

### Editor

- An OVERDUB plate beside the LOOP switch: lit while layering, greyed out and ignoring
  clicks with no loop playing.
- CLEAR and UNDO plates at the right of the strip's heading line, left of GAIN. UNDO's tooltip
  and the readout say what it will do next. UNDO greys out with nothing to undo.
- `LoopView::paint` draws the live view when nothing is caught, or when the core reports
  recording, waiting for the bar line or a CAPTURE hold (`takeSoFar >= 0` or armed).
- The take strip's heading reads "OVERDUB" while latched, as it does for a hold now.

### Browser version

The DSP mirrors all of it. The browser panel gets OVERDUB, CLEAR and UNDO buttons too, since
that's only three buttons sending messages. Its waveform stays as it is (it has no live view
yet).

## Tests

- DSP, C++ and JS:
  - OVERDUB latched layers and stops when switched off.
  - It is ignored with no loop playing, and switches off when the loop stops.
  - A hold still overdubs, and releasing it doesn't end a latched overdub.
  - CLEAR empties the take and stops the loop.
  - UNDO after a CLEAR brings the take back exactly (sample for sample), and a second UNDO
    clears it again.
  - UNDO after a new take brings back the old one; UNDO after a first take empties.
  - UNDO after an overdub restores the take as it was before the layer; a second UNDO puts the
    layer back.
  - UNDO with nothing to undo does nothing.
- Plugin tests: the triggers reset to off; a cleared take isn't saved; OVERDUB's host
  parameter is set back to off with no loop.
- Snapshots: OVERDUB greyed out and lit, CLEAR and UNDO in the heading, the live view
  while re-recording over a stopped take.

## Checks

```bash
scripts/diff-baseline.sh --record    # before any change
cmake --build native/build
ctest --test-dir native/build -C Release --output-on-failure
npm test
node tests/parity/compare.js native/build/brood_render
scripts/diff-baseline.sh
make check
npx vite build
make install
```

Then in Live (after a full restart): OVERDUB and CLEAR and UNDO from the Push.

## As built (14 November 2026)

- Built as planned. Undoing a layer keeps a playing loop playing; undoing a clear or a take
  leaves it stopped.
- The swaps all happen on the audio thread: CLEAR and UNDO reach the core as parameters
  read in `processBlock`, like the footswitches. A playing loop fades out before a swap
  (the same fade a tap uses), so neither clicks; a test checks the largest step.
- The LOOP switch is narrower to fit OVERDUB beside it, so its "TAP / HOLD" hint is left
  out (WET keeps its hint).
- CLEAR and UNDO are drawn as keys in the strip's own pale ink, since the dark strip hid
  the usual plates. UNDO reads REDO when that's what it will do.
- OVERDUB, CLEAR and UNDO never travel in a preset, and a reopened set restores them off,
  so a set saved in the moment CLEAR was on can't clear its own take.
- The browser panel has the three as a row under the LOOP switch. The worklet sets
  OVERDUB back to off the way the plug-in does.
- New parity scenarios: `dub_latch`, `clear_undo`, `undo_take`. The 29 existing renders are
  identical to the baseline recorded before the change.
