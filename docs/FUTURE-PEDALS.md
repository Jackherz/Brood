# The pedal playground: feasibility notes

> Status: **research only.** Nothing in this file is built, scheduled, or
> promised. It is the dossier a future round starts from so the research does not
> have to be done twice. Round 12 (2026-11-01) is the last shipped work.

The idea being considered: stop treating this as one pedal emulation and make it a
**Chase Bliss / modular effect-pedal playground** — one instrument that can wear
several pedal models, sharing the engine, the parity harness and the design sheet.

That changes the question from "can we clone pedal X" to "what is the cheapest
architecture in which pedal X is a hundred lines of parameters plus one or two new
DSP blocks". The per-pedal verdicts below are written with that in mind.

---

## Parked: blooper (decided 2026-11-01: not yet)

The bottomless looper. **Verdict: ~60 % reachable cheaply, two genuinely hard
pieces, one thing I would skip.** The full feature-by-feature table was worked out
in conversation; the summary that matters for planning:

**Cheap, because the machinery already exists in `mood.h`:**

| Feature | Already have |
| --- | --- |
| Smooth Speed (tape, reverse below noon) | TAPE mode's `tapeSpeed`, STRETCH's `dir = ±1` |
| Stepped Speed (octaves and fifths) | same, quantised to a table |
| Stopper (fade or tape stop) | `loopGain` smoothing plus a `tapeSpeed` ramp |
| Filter (LP/HP on the loop) | `LowPass4`; TONE covers the wet bus |
| Repeats (layers fade while overdubbing) | FADE — already built and tested |
| Stability (wow, flutter, noise, age) | CLASSIC mode, as a switch |
| Trimmer / Speed Trimmer | LENGTH and END already move the play window |
| Dropper / Scrambler / Swapper | index remap and gate on the existing buffer |
| Stutter / Blip | per-sample window with edge fades |

**Hard:**

1. **Layers (8 levels of undo/redo).** Each layer is a whole buffer copy: 32 s at
   the 64 kHz core clock is 8.2 MB per layer per core, so 8 layers stereo is
   ~131 MB of RAM. The real blocker is the project state: at round 11 we made a
   save carry the audio, and a full-length take measures **~2.9 MB int16 → ~3.7 MB
   base64 per core** (measured 2026-11-01; gzip saves under 5 % on audio-like
   material — an earlier "50 KB" figure came from a half-second test and was
   wrong). Eight layers would put 30–60 MB into every Live autosave. Options, in
   preference order: keep layers in RAM only and save the composite; cap at four;
   store deltas against the previous layer.
2. **Additive mode** (modifications recorded back into the loop). The record path
   currently writes the input, not the loop's own output, so this needs a
   read-then-write ordering with a deliberate one-block delay to stay stable. This
   is blooper's identity and its most delicate DSP.
3. **The three-times tax.** Every loop feature is written three times — worklet,
   `mood.h`, and UI in two toolkits — and has to stay bit-exact under the parity
   harness. That is why a "cheap" feature still costs about a day.

**Skip:** the 16-loop library (Live already saves and versions; we would build a
worse browser) and blooper's own MIDI map (ours follows the MOOD CC layout on
purpose; honouring two maps needs a per-device table for no musical gain).

**Sequencing if it is ever picked up:** (A) reverse + smooth/stepped speed,
stopper, loop filter, one-shot — most of the sound, one round; (B) the two-channel
modifier engine, which is the same shape as the ramp engine and which Habit also
needs; (C) four layers, RAM-only history; (D) additive mode, then stability made
continuous.

---

## Chase Bliss Habit (2022, discontinued)

**Verdict: the most feasible of the three. Two to three rounds.** It is mostly a
re-arrangement of blocks this codebase already contains.

What it is: an "echo collector" — a **digital tape reel that records everything
that enters the pedal** (three minutes of memory) feeding a delay of up to
**60 seconds** with **two read heads** and two banks of modifiers. Mono I/O, two
presets, ramping, tap tempo, MIDI.

### Controls (as on the pedal)

- Knobs: **Volume, Repeats, Size, Modify, Spread, Scan** (+ ramp).
- Toggles: modifier **1 / 2 / 3**, modifier bank **A / Off / B**, routing
  **Feed / Out / In**.
- Footswitches: left holds **Scan**, right holds **Loop** (repeat the last echo);
  the Latch dip makes either persist.
- Dips include **Manual** (Scan stops cruising and follows the knob), **Collect**
  (memory overdub), **Latch**, and the Memory Dry positions.
- MIDI: CC 14–20 knobs, 21–23 toggles, 24 Loop, 25 Scan, 26 Clear, 93 tap,
  102 bypass, 51 clock ignore, 100 expression.

### How it maps onto what we have

| Habit block | Where it lives now | Work |
| --- | --- | --- |
| Always-recording reel | `hist`, the same always-writing buffer behind the looper | **Resize + expose** |
| Scan (read head into the reel) | `capture()` already copies a window out of `hist` | **Low** — a moving read pointer |
| Spread (second head) | one more pointer, offset from the first | **Trivial** |
| Repeats / Size | `Delay`, with its length and feedback | **Low** |
| Two modifier banks | trim, filter, dropper, tape, speed, stretch all exist as code | **Mid** — re-target onto the tap |
| Collect (memory overdub) | `overdub` and the capture path | **Low–mid** |
| Feed (output back to input) | a routing change, needs feedback guarding | **Low, careful** |
| Auto Scan (lets it cruise) | the ramp generators, retargeted to Scan | **Low** |
| Tap tempo, Clear, Manual dip | tempo sync, one button, one switch | **Low** |

### The two real costs

1. **Memory.** Three minutes at 48 kHz is 8.64 M samples: 34.6 MB as floats,
   17.3 MB as int16, and **46 MB per core** if the reel runs at the 64 kHz core
   clock. Two cores would be ~92 MB. Two ways out: keep the reel as int16 and
   convert on read (halves it, and a tape reel can afford the quantisation — it
   stays bit-exact because both cores do the same thing), or run the reel at a
   lower rate. Habit is mono anyway, which invites the bigger decision: **share
   one reel between both channels** instead of one per core.
2. **State.** A three-minute reel must **not** go into the project state — that is
   ~17 MB → ~23 MB base64 per core. Save the collected loop only, and let the reel
   be scratch, exactly as the hardware's memory is.

### Why it is worth doing first if any of this happens

Habit forces the **two-channel modifier engine**, which is also blooper's Round B
and which Bad Mood's Mask and Radio would reuse. It is the single piece of
groundwork that pays for itself across all three pedals.

---

## Chase Bliss Bad Mood (July 2026, Small Batch Bliss #3, $399)

**Verdict: the most expensive of the three — four to six rounds — and the one that
fights our interface rules.**

What it is: a deliberately grumpy reworking of MOOD MKII. Same layout (Wet Channel
left, Micro-Looper right, shared controls down the middle), entirely new modes,
both channels feeding a global saturator called **Glue**.

### What is the same as our pedal

Clock 2k–64k in harmonised steps with a Smooth dip; three-way routing (input /
input + loop / loop only); Mix; ramps with bounce, sweep and polarity; freeze on
the wet footswitch; fade while overdubbing; analog dry path. All of that is built
and tested here already.

### What is new

| Bad Mood block | What it does | Effort, and what we can reuse |
| --- | --- | --- |
| **Relay** (wet) | delay that does not fade: Modify picks a *count* of repeats, all at full level | **Low** — sum N taps, no decay |
| **Soup** (wet) | spectral reverb that resynthesises from thousands of frequency bands | **Mid–high** — needs an FFT that is bit-identical in JS and C++; a 32–64 band approximation is possible but is not the same effect |
| **Flip** (wet) | pitch harmoniser, fourths/fifths/octaves, Time sets lag between notes, 48 chords over MIDI | **Mid** — grain pitch shifter exists; the chord table and lag do not |
| **Glue** (global) | end-of-chain saturator → torn speaker → bit crush; standalone when Mix is down | **Low** — waveshaper + crusher + tone |
| **Cross** (global) | dynamic pitch and loudness interruption, sourced from the input or the other channel | **Mid** — envelope followers exist; pitch modulation does not |
| **Burst** (looper) | up to 8 onset-detected steps played as a sequence; loud playing scrambles fills | **Mid** — needs onset detection and a step clock |
| **Mask** (looper) | threshold on loud parts, disguised six ways: smooth, distortion, timestretch, pitch, noise, gates | **Mid** — four of the six exist as blocks |
| **Radio** (looper) | five loopers as stations on Modify (Tape, Ambient, Orchestral, Shoegaze, Dance) with interference between them | **High** — five sub-modes, each with its own playback logic |
| **Hidden Options** | a second meaning for *every* front knob: Time→Cross, Mix→Balance, Length→Fade, wet Modify→EQ, looper Modify→Blend, Clock→Glue, toggles→Sync/Spread/Input Mod | **The interface problem, see below** |
| **Half dip, Synth Mode, 122 presets** | loop length halved to match MK I; MIDI notes transpose Clock with ADSR | **Low / Mid** — flagged already as deliberately out of scope |

### The interface problem (the part I would not hand-wave)

Bad Mood opens its second layer by holding both footswitches, and prints those
"Hidden Options" under the knobs. That is exactly the menu diving we removed on
purpose: the standing rule is **one page, no hidden page, nothing shrunk to fit**.

So a Bad Mood bank needs a decision, made before any DSP:

- nine more visible cells (Cross, Balance, Fade, EQ, Blend, Glue, Sync, Spread,
  Input Mod) — the panel is already full, so this probably means a wider window or
  a taller one, and the 900 × 712 sheet is what keeps every layout honest;
- a per-model **panel bank** (the model selector switches banks) — which is a page
  under another name, and should be called that rather than smuggled in;
- or folding them onto controls that already exist and accepting a mismatch with
  the hardware (Sync on the SYNC dip, Spread on WIDTH, Fade on the existing FADE
  slider, Glue on TONE — four of the nine are nearly free that way).

Also note: **Bad Mood has Latch, Dry Kill and Spread as dip switches** — the three
things round 11 removed from this pedal. A Bad Mood bank would bring them back at
dip level, which is fine (different pedal, different grammar) but should be a
conscious exception rather than a quiet reversal.

---

## Groundwork that pays for itself whatever happens next

If the playground is the direction, these come before any second pedal:

1. **A pedal contract.** One header per model (`mood.h`, `habit.h`, …) exposing the
   same small surface — `prepare`, `process`, `params`, `state`, `capturedLoop`,
   `restoreLoop` — plus a registry that lists the models. The worklet and the
   processor both instantiate the same set, so parity keeps meaning what it means.
2. **Panels as data, not as two hand-coded layouts.** The doubling of every UI
   change — `src/main.jsx` and `PluginEditor.cpp` — is the largest recurring cost in
   this project. A declarative panel description (cells, labels, ranges, whispers)
   read by both sides would turn a round's UI work into an afternoon.
3. **A state budget policy.** Round 11 made saves carry audio. Before a second
   pedal arrives, write down what is saved, what is deliberately scratch, and what
   the size cap is — otherwise a reel or a layer stack quietly makes set files
   unusable.
4. **A parity harness per model.** `native/tests/render.cpp` already takes arbitrary
   parameter names; it needs a model selector and one scenario folder per pedal.
5. **A rule for secondary controls**, so "no menu diving" survives three pedals
   with nine hidden options each. Whatever the rule is, write it down here first.

## Legal-ish framing

The project already calls itself an unofficial study of one pedal. A multi-pedal
playground should keep that framing: descriptive use of model names in the docs, no
logos, no trade dress, no claim of affiliation — and the user's own copies of the
hardware remain the reference, not shipped assets.

## Sources (fetched 2026-11-01)

- blooper: <https://www.chasebliss.com/blooper> (modifiers, layers, additive mode,
  8 layers, 16 saved loops) and the feature list from
  <https://www.gluedtomusic.com/products/2808/chase-bliss-audio-blooper-looper-pedal/>.
- Habit: <https://www.chasebliss.com/habit> (reel, 60 s delay, Spread, modifiers,
  Collect, Feed, dips) and <https://midi.guide/d/chase-bliss/habit/> (CC map).
- Bad Mood: <https://www.effectsdatabase.com/model/chasebliss/badmood> (the full
  control, dip and Hidden Options description used above);
  <https://synthanatomy.com/2026/07/chase-bliss-bad-mood-a-grumpy-sounding-experimental-ambient-fx-pedal.html>
  and <https://guitarbomb.com/blog/chase-bliss-unveils-bad-mood/> for the release
  (pre-orders July 2026, shipping from late October 2026, $399).
