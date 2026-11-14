# Fast modulation: an audit of every target at the top rate

Requested on 14 November 2026, after a fast modulation on CLOCK popped badly: check every
parameter a modulation source can reach for pops, clicks and aliasing at the top rate.

## How it was measured

One source at the top rate (8 Hz) and full depth on one target, for every target, every
shape (TRI, SQR, SIN, RND, SMTH), in every wet mode (with the wet side on) and every loop
mode, with a loop playing: 420 runs, against the same setups unmodulated. The input and
the loop are a 220 Hz sine. Even pitch-shifted two octaves, that has nothing above 8 kHz,
so energy above 8 kHz in the output is artefact: clicks, zipper or aliasing. The
unmodulated worst case peaks at 0.0002 there.

## What it found, and what changed

| Target | Worst before | Cause | Now |
| --- | --- | --- | --- |
| CLOCK | 0.49, every mode, even with the wet side off | the clock's anti-aliasing and reconstruction filters were biquads, retuned as CLOCK moved; every retune of a running biquad is a step | 0.0015 to 0.03 |
| LENGTH in STRETCH | 0.25, every shape | a grain read at `ph * size`, so a moving LENGTH sped its read head up and down: a warble that aliased | 0.0002 |
| LENGTH in ENV | 0.0099, random shape | the slice length followed LENGTH tick by tick, so a fast move yanked a fading slice back up or cut it | 0.0003 |
| TIME in REVERB | 0.0097, square shape | TIME scales the room's eight lines; a fast swing ran their read heads at two to three times speed | 0.0002 |
| MIX, WET MOD, WET/LOOP | at the floor | | unchanged |

- **Clock filters:** now state-variable filters (the topology-preserving form), which have
  the same response as the biquads (they agree to 1e-15 with the tuning still) and are
  built to be retuned while running. This also removes the pops of an ordinary CLOCK move,
  which glides, and is why scenarios that set CLOCK at the start render a little
  differently.
- **STRETCH:** each grain reads on its own count at normal speed; LENGTH sets only how long
  its window takes. The two grains stay half a window apart, so they still add up to a
  steady level.
- **ENV:** a slice keeps the length it started with, and a new LENGTH takes over at the
  next slice, synced or not. SOFT off the grid still glides its length, as before.
- **REVERB:** the room size moves at a capped speed, so no line's read head runs outside
  0.75x to 1.25x. A full sweep of TIME arrives within about 0.16 s.

## Left as they are, and why

- **CLOCK in DELAY, 0.03 at the random shape:** echoes written at one clock rate and read
  at another change pitch by the ratio, up to 32x when CLOCK swings five octaves. That's
  real pitch, the tape-speed character, not a click.
- **LENGTH in TAPE, 0.0076:** the 4x cap on how fast the playhead may run while the window
  follows LENGTH (chosen in the five fixes). Reading at up to 4x folds a little top back.
- **LOOP MOD in STRETCH, 0.0032:** 38 dB under the signal; it's the speed changes STRETCH
  is for.
- HI-Q and WEAR's worn mode measure the same as the plain path under CLOCK modulation.

## Tests

- `native/tests/dsp_tests.cpp` checks the six worst cases against limits well under what
  they used to reach. All six fail on the code before this change.
- `tests/parity/scenarios/mod_fast.txt` runs every fixed case through both engines.
