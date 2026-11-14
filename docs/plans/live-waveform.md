# Live waveform on the take strip

Requested on 14 November 2026. Today the strip shows text until a loop exists, then the
take, which doesn't move. It should show what is coming in as it is recorded, including
anything ahead of the looper (WET set to PRE, WORN before the loop) and nothing after it.
Modelled on Silt's live view: a peak per slice of the ring, kept as it's written, drawn
scrolling with the newest audio at the right edge.

## Decisions (asked 14 November)

- With nothing caught, the strip always scrolls the input, in CAPTURE and RECORD.
- While RECORD is recording, the input keeps scrolling. A START rule rides along with the
  take, what's being kept is drawn bright and the rest dimmed.
- Once a loop plays, the strip shows the take. While overdubbing it redraws at the write
  head every frame instead of about every tenth of a second.
- Plug-in only for now. The DSP side is mirrored in the JS so the two stay the same; the
  browser's drawing comes later.

## What it draws, and why it's honest

The looper's history buffer (`hist`) is what a tap or a recording keeps: the input after
the worn conversion when WORN is before the loop, plus the wet side when WET is PRE. Its
peaks are the picture. Nothing after the loop (POST wet, WORN after the loop, GAIN, the
output) is in it, so none of that is drawn.

The window is MEMORY long, in core ticks, so in CAPTURE the strip is exactly what a tap
would keep. CLOCK changes how many ticks a second there are, so a slow clock scrolls more
slowly and holds more seconds, which is what the looper does.

## Build

1. **DSP (`native/dsp/brood.h`, mirrored in `src/brood-worklet.js`).** Read-only additions,
   no change to the sound:
   - `kLiveColTicks = 128`: a peak for every 128 ticks of history, kept in `liveCols` as
     `hist` is written (max of the column's absolute values, reset when a column starts).
   - `histWritten()`: ticks written since the buffer was made (64-bit), so a reader knows
     which columns are new.
   - `takeSoFar()`: ticks kept so far by a RECORD take, or by a CAPTURE hold; -1 otherwise.
   - `memoryTicks()`: `maxCaptureSamples()`, the window a tap keeps.
   - Overdub: a dirty range of cells written since the last read, and `loopPeaksRange()`
     so only the bins it touched are worked out again. `loopPeaks()` becomes a call to it.
2. **Processor.** After each block, copy the new columns (max of the two cores) into an
   atomic array the editor reads. Publish `histWritten`, `takeSoFar` and `memoryTicks`.
   While overdubbing, refresh the touched bins every block, in place of the whole take every
   40 blocks.
3. **Editor (`LoopView::paint`).** With no take: the scrolling input over the MEMORY window,
   with the heading line carrying the words ("LISTENING", "PRESS LOOP TO RECORD",
   "WAITING FOR THE BAR LINE", "RECORDING 4 BARS  2.10 S"). In CAPTURE the whole window is
   bright (a tap keeps it); in RECORD only the part being kept is. A START rule marks the
   take's start while recording or holding. Silence keeps today's centred words over a
   flat line. The timer repaints every frame while there's no take.
4. **Tests.** DSP: the columns match the history's peaks; PRE wet shows in them and POST
   doesn't; `takeSoFar()` counts a RECORD take and a CAPTURE hold, and is -1 otherwise;
   the sound is unchanged (the parity check and `scripts/diff-baseline.sh`). JS mirror
   tests for the same. Editor snapshots of each state.

## Checks

```bash
scripts/diff-baseline.sh --record    # before any change
cmake --build native/build
ctest --test-dir native/build -C Release --output-on-failure
npm test
node tests/parity/compare.js native/build/brood_render
scripts/diff-baseline.sh
make check
make install
```

Then snapshots of the strip: idle in CAPTURE, idle in RECORD, recording, overdubbing.
