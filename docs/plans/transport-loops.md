# Transport loops: a loop made with Live playing follows Live's transport

Requested on 14 November 2026. A loop should stop when Live stops, without stopping
anyone looping freely with Live stopped. Chosen: option 1, loops remember where they came
from.

## Decisions (asked 14 November)

- **A transport loop is a take caught while Live is playing.** That's decided when the take
  closes: the CAPTURE tap, or the moment RECORD ends (second tap, bar count, or the 32 s
  ceiling). A take made with Live stopped is free, and the transport never touches it.
  So are takes in the standalone app and the browser version, which have no transport.
- **Live stops: a transport loop fades out and parks.** It's the same fade as a tap, and a
  latched OVERDUB switches off, as it does when a tap stops the loop.
- **Live plays: it comes back locked to the song, like a clip.** Play from bar 1 and it
  starts from the top; start mid-song and it comes in where it would be by then. A jump
  in song position while playing (Live's loop brace, clicking ahead) puts it back in time.
- **A saved set remembers.** It saves which kind the take is. A reopened transport loop
  waits silently for play; a free loop plays at once if it was playing, as now.
- **A tap with a transport loop parked works as now.** With the loop stopped, a tap
  catches a new take in CAPTURE or starts a new recording in RECORD. That take is free,
  since Live is stopped.

## How the song lock works

When a transport take closes, the core notes the song position its first sample belongs
to, the **anchor**: the song position now, minus the take's length in beats at the host's
tempo. From then on, where the loop should be at any song position is how far the song is
past the anchor, in seconds at the host tempo, times the current clock rate, wrapped to
the playing window:

    ticks into the window = ((ppq - anchor) * 60 / bpm * clockHz) mod W

The playhead is set from this when Live starts playing and after a jump in song position,
and runs freely in between, as it does now. So CLOCK, TAPE's speed and modulation keep
working, and a loop at normal speed and an unmoved CLOCK stays in time with the song. A
jump is any song position more than a block away from where the last block predicted. It
moves the playhead under a short dip (about 10 ms out and back in), so it doesn't click.

ENV plays slices when the input opens its envelope, so it has no running playhead to
lock. There, following the transport only means stopping and starting.

## Build

1. **DSP (`native/dsp/brood.h`, mirrored in `src/brood-worklet.js`).**
   - `takeFollows` (bool) and `anchorPpq` (double), set in `capture()` from `p.hostPlaying`,
     `p.ppq`, `p.bpm` and the take's length; carried by `restoreLoop()`; swapped with the
     undo copy like `lsrc`, so UNDO/REDO bring each take back with its kind.
   - `parked` (bool): a transport loop that Live stopped. Process watches `p.hostPlaying` from
     block to block:
     - stop, with a transport loop playing: `loopState = Off`, OVERDUB off, `parked = true`;
     - play, with a loop parked: set the playhead from the song position and play;
     - a jump while playing a transport loop: the short dip and a reposition.
   - A tap, CLEAR, UNDO or a new take clears `parked`.
   - `state()` reports `follows` and `parked` for the strip.
2. **Processor.** It saves `follows` and `anchorPpq` with the take in the state (the TAKES
   tree), and passes them to `restoreLoop()`. With `follows`, the restored take starts
   parked and waits for play. The state version is unchanged, because old sets simply have
   no `follows` and load as free.
3. **Editor.** The strip's heading says which kind a playing take is ("PLAYING 4.00 S,
   FOLLOWS LIVE") and shows a parked one as "WAITING FOR PLAY". The loop switch's light
   goes out while parked.
4. **Renderers.** The scenario format gets `<block> param songStart <block>`: the song
   position restarts from bar 1 at that block. Pairing it with `hostPlaying` 0 and 1 lets
   scenarios stop, start and jump the song. Both renderers compute `ppq` the same way.

## Tests

- DSP, C++ and JS:
  - a take caught with the host stopped is free, and stopping or starting the host never
    touches it;
  - a take caught with the host playing follows: a stop fades it out (no click) and parks
    it, and a latched OVERDUB switches off;
  - play from bar 1 starts it from the top;
  - play from bar 3 of a 2-bar loop comes in at its top; from bar 2.5, halfway;
  - a jump in song position while playing puts it back in time, without a click;
  - a tap while parked catches a new, free take;
  - UNDO back to a transport take brings back its kind.
- Plugin: a saved transport take reopens parked and plays on play; a saved free take plays
  at once, as now; an old set loads as free.
- Parity scenarios: `transport_follow` (catch while playing, stop, start from the top, start
  mid-song, jump) and `transport_free` (caught while stopped, transport moves around it).
  Every existing render must stay identical: none of them stop the transport under a
  take.

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

Then in Live (after a full restart):
- loop with Live playing, then stop and start (from the top, mid-song, with the loop
  brace on);
- loop with Live stopped, then press play and stop: it shouldn't care;
- save, reopen, press play.

## As built (14 November 2026)

- Built as planned. The lock dip is 5 ms out and 5 ms back, separate from the loop's own
  fade, and exactly 1 the rest of the time, so every existing render is unchanged.
- The heading says FOLLOWS THE HOST rather than LIVE, since other hosts work the same way.
- Found and fixed on the way: UNDO back to a take (after a CLEAR or a new take) used to
  leave the loop stopped "ready for a tap", but a tap with the loop stopped catches a
  new take, so the undone take could never be heard. An undone take now plays, or
  waits for play if it's a transport take and the host is stopped. That changed the
  `clear_undo` and `undo_take` renders, and four tests that expected "stopped".
- Tests that check the lock remove it for a moment to prove they can fail (three fail
  without it). The plugin test runs a fake host transport; the snapshot tool has two
  scenes, `follow` and `parked`, and now runs pending message-thread work after each block.
