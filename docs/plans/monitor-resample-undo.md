# Monitoring, resampling and four-step undo: proposal for review

Status: built on 2026-11-17 in both cores, the plug-in and the browser, uncommitted. The owner
asked for the whole plan with my recommendation on each open decision (section 10); each can
still change after listening. Inspected at main 21ceaed (same as origin).

## 0. What the code does today (the facts this plan builds on)

Signal flow, per core (two cores, left and right, each mono in, stereo out; DSP in
`native/dsp/brood.h`, mirrored sample for sample in `src/brood-worklet.js`):

- Input gain sits outside the cores. Plug-in: `inputGain` (LEVEL in the bottom strip, up to
  +24 dB, drives `kit::TapeInput` past 0 dB) is saved in state but is **not a host
  parameter**. Browser: a GainNode in front of the worklet.
- At the core clock: `wetSrc` is the input (IN), input + loop (IN + LOOP) or the loop (LOOP),
  times `wetIn` (the WET switch, faded). `wetOut` keeps the wet audible when WET is off only
  if TRAILS is on.
- The history (what CAPTURE and RECORD keep) is `x + wet` for IN and IN + LOOP, `x` for LOOP.
  An overdub writes the same thing, plus the IN + LOOP runaway guards from 65fd79a
  (`dubReplace`, `dubCeil`).
- Core output = `wet * min(1, 2*BALANCE) + loop * GAIN * min(1, 2*(1-BALANCE))`.
- Host output = `dry * dg + core * min(1, 2*MIX)`, where `dg` only ducks while something is
  "engaged" (WET on, FREEZE, or a loop playing). The browser adds `soloLoop`, a hidden 30 ms
  dry mute while a loop plays: the dishonest MIX the brief describes.

The WET switch today carries: on/off (tap), momentary freeze (hold, which also turns WET on
for the hold if it was off), the TRAILS decision, the dry-duck "engaged" state, `wetSwitch`
host parameter, MIDI CC 103 (on/off) and CC 105 (FREEZE latch, a separate `freeze` param),
key [A], and `wetEnabled` in saved state. Presets never engage it.

Loop memory: `kMaxLoopSamples` = 2,048,000 core ticks (32 s at a 64 kHz CLOCK). Each core
holds `hist`, `loopBuf` and `undoBuf` at that size: 3 x 8.2 MB = 24.6 MB a core, about
49 MB for both (the header comment still says 33 MB). The browser allocates the same.
CAPTURE is capped by MEMORY (pedal default, or 1 to 30 s); RECORD by 32 s.

Undo: one buffer. UNDO swaps `loopBuf` and `undoBuf` (take, clear, or overdub layer);
pressing again redoes. `beginLayer()` copies the whole take into `undoBuf` **on the audio
thread** (up to 8 MB in one go): an existing cost this plan removes. Parameters have no undo
of Brood's own (the APVTS has no UndoManager). A restored set has no undo.

Modulation targets (7): TIME, MIX, LENGTH, MODIFY/WET, CLOCK, MODIFY/LOOP, BALANCE.
Factory presets: 32 in the plug-in (29 set MIX, 5 set BALANCE), 5 in the browser.
State version 4.

## 1. Controls and mix law

### Signal flow (proposed)

Per core, with `x` the input after INPUT LEVEL, `L` the loop after LOOP LEVEL, `m` the
monitor gate (section 2, smoothed 0..1), and WET MIX `w` turned into two gains by the law
Brood already uses for MIX: `d = min(1, 2(1-w))`, `g = min(1, 2w)` (both full at noon,
0 is the source alone, 1 is WET alone):

| ROUTING | What WET hears | What you hear |
|---|---|---|
| IN | `x*m` | `d*(x*m) + g*WET + L` |
| IN + LOOP | `x*m + L` | `d*(x*m + L) + g*WET` |
| LOOP | `L` | `x*m + d*L + g*WET` |
| OFF (new) | nothing; tails ring or cut per TRAILS | `x*m + L` + any tail |

- INPUT LEVEL stays first, before the tape drive and before everything else, so it scales
  monitoring, capture, overdub and WET together.
- LOOP LEVEL stays in front of WET (as GAIN is now, at the owner's request in 2f0daad).
- The unprocessed input term stays at host rate, untouched by CLOCK, as the dry path is
  today. The loop and WET terms come through the core as now.
- No hidden duck: the "engaged" logic and the browser's `soloLoop` go. Only INPUT MONITOR
  (with its visible state) decides whether the input is heard. Bypass transparency stays: with
  no loop, ROUTING OFF or WET MIX 0, monitor open and no tail, the output is the input
  exactly (no soft limit).

### Labels and ranges

- **INPUT LEVEL** (host param `inputLevel`): off, then -48 to +24 dB; past 0 dB it drives
  the tape (plug-in) as LEVEL does now. Replaces LEVEL and the browser's input gain.
- **LOOP LEVEL** (host param `loopLevel`, replaces `loopDb`): off, then -48 to +12 dB.
  Replaces GAIN in the screen header and the BALANCE slider.
- **WET MIX** (host param `wetMix`, replaces `mix`): readout `DRY 100 / WET 40`, where
  "DRY" means the routed source unprocessed. Default 0.
- **ROUTING** gains a fourth position, **OFF**.
- MIX and BALANCE go. There are three level controls, not eight: input, loop, and the blend.

### Removing the WET button: where each of its jobs goes

| Job today | Replacement |
|---|---|
| WET on/off (tap) | ROUTING OFF. WET's feed is what ROUTING says; OFF feeds nothing |
| Momentary freeze (hold) | The FREEZE key becomes tap = latch, hold = freeze while held. Takes the big key where WET was, key [A] |
| FREEZE switch, CC 105 | Same `freeze` param, unchanged |
| TRAILS | Unchanged meaning, now about the feed stopping: a user move to ROUTING OFF lets tails ring (TRAILS on) or fades them (off). Brood's own automatic routing changes always let tails ring |
| CC 103 | Toggles ROUTING between OFF and the last routing that wasn't OFF |
| `wetSwitch` host param | Removed |
| Dry duck ("engaged") | Removed; INPUT MONITOR replaces it |
| `wetEnabled` in state | Removed; ROUTING is a normal saved parameter |
| WET LED | A lamp beside ROUTING that is lit while WET is fed or a tail is audible |

Defaults: INPUT MONITOR AUTO, ROUTING IN + LOOP (as now), WET MIX 0. "WET enabled" is then
simply ROUTING not OFF.

WET MIX at 0 must not mislead: the WET keeps running underneath (so raising WET MIX reveals
a reverb already going, with no jump), the WET column's knobs and FREEZE stay live, and the
screen says `WET MIX IS 0, NOT HEARD` when one of them is touched. FREEZE at WET MIX 0
freezes but says so. ROUTING OFF greys WET MIX and the screen says `WET HEARS NOTHING`.
Saved state stores the real values, nothing derived.

## 2. INPUT MONITOR: ON / OFF / AUTO

Host param `monitor` (choice: Auto, On, Off), CC to be assigned.

- Gate `m` targets: ON 1; OFF 0; AUTO 0 while a loop plays and no overdub is running, 1
  otherwise (no loop, a parked transport loop, recording a new take, overdubbing). Faded
  over 10 ms, crossing as the loop fades in or out.
- The gate sits only on what is *heard*, through the dry term and WET's feed. The history and
  RECORD still take `x` whatever the gate says, so OFF never stops Brood listening.
- What a new take or overdub keeps (the record bus): `d*x + g*WET` while WET's feed is input
  only (IN, or IN + LOOP while isolating, or IN + LOOP with no loop playing), otherwise plain
  `x`. LOOP routing keeps `x`, as now.
- OFF: the input reaches neither the output nor WET. A take caught in OFF is the plain
  input (the WET never heard it). **Tradeoff for review:** the alternative is to keep feeding
  WET with the input and mute WET's output, which only works cleanly in ROUTING IN.
- OFF and OVERDUB: OVERDUB greys out on the panel and in the browser; the `overdub` param is
  set back to off (as `resetOverdub_` does now when it can't latch); MIDI and automation that
  ask for it are refused the same way; holding LOOP on a playing loop does nothing and the
  screen says `OVERDUB NEEDS MONITOR ON OR AUTO`. Switching to OFF mid-overdub ends the layer
  through the existing 8 ms write fade and keeps it, as one undoable layer.
- RESAMPLE stays available in OFF.
- AUTO's effective state is always shown: a lamp by INPUT that is lit only while the input is
  audible, and the selector reads `AUTO: INPUT HEARD` or `AUTO: LOOP REPLACES INPUT`.
- The browser's `soloLoop` and its browser-only MIX help sentence go. Plug-in and browser
  behave the same, so help text is identical again.

## 3. OVERDUB and RESAMPLE

### OVERDUB (adds live input to the loop)

- ROUTING IN: the layer is `d*x + g*WET`, so the input can be recorded with WET on it.
- ROUTING IN + LOOP: for the layer's length WET is fed the input only; the loop is faded
  out of WET's feed over about 5 ms and back in after. ROUTING shows `IN` lit with
  `IN + LOOP` outlined and the screen says `WET HEARS INPUT ONLY WHILE OVERDUBBING`.
- ROUTING LOOP: the layer is `x`, as now.
- Because WET no longer hears the loop while overdubbing, the IN + LOOP runaway machinery
  (`dubReplace`, `dubCeil`, 65fd79a) is no longer needed and is removed. The
  `dub_runaway` parity scenario becomes a check that it cannot run away.
- WET tails are not cleared. A tail of the loop already ringing in a reverb will be in the
  first moments of the layer. Accepted, and said in the help.

### RESAMPLE (a new performance of the loop replaces it)

A new key beside OVERDUB. While it runs, the loop plays and is recorded into a separate
buffer; at the end that becomes the take and the old take goes to UNDO.

What is recorded, `P`:

| ROUTING | WET's feed during RESAMPLE | `P` |
|---|---|---|
| LOOP | `L` | `d*L + g*WET` |
| IN + LOOP | `L` only (input faded out of the feed, about 5 ms) | `d*L + g*WET` |
| IN | the input, as before (heard, not recorded) | `L` |
| OFF | nothing | `L` |

- Live input is never added to `P`. In ON the input is still heard while resampling; in
  AUTO and OFF it isn't.
- The WET MIX blend is recorded as dialled (and as modulated), not forced to 100%.
- The non-WET loop performance (TAPE speed, STRETCH, ENV gating, START/END, LENGTH, LOOP DIV,
  CLOCK, modulation of any of them) is always in `P`, even with ROUTING IN or OFF.
- Tails: not cleared. In IN + LOOP, WET may hold a tail of earlier input when RESAMPLE begins,
  and it will be in the first moments of the take. New input stops entering WET within 5 ms
  of the press, so only what was already ringing can reach the take. The screen says
  `WET: LOOP ONLY (TAILS RING ON)` while it runs.
- LOOP LEVEL is in `P` (what you heard is what you get), and is reset to 0 dB as part of the
  action so the new take isn't turned down twice. **For review.**
- The seam: RESAMPLE records about 10 ms beyond the end and crossfades it into the head of
  the new take, so a pass that ends on a ringing tail doesn't click.
- On a transport-locked take, the new take follows the transport, anchored at the song
  position where RESAMPLE began. If the host stops part way, it stops like a manual stop
  (keeps what was recorded). **For review.**
- FREEZE latched during RESAMPLE: the frozen WET is recorded as heard.

### After RESAMPLE (all one undoable action)

If `P` included loop WET (ROUTING LOOP or IN + LOOP, and `g > 0` at any point):

- ROUTING LOOP becomes **OFF** (the truthful "loop WET off": WET MIX keeps its value, and the
  routing says WET hears nothing).
- ROUTING IN + LOOP becomes **IN**, visibly, so input WET carries on.
- A latched FREEZE is released, or the frozen pad would ring forever on top of a take that
  already holds it. **For review.**
- Existing tails ring out naturally, whatever TRAILS says.

With ROUTING IN or OFF, or with `g` 0 throughout, ROUTING and FREEZE are left alone.

Reset instinct, always:

- START and END to the whole take (as `fitWindow()` does now), LENGTH full.
- LOOP MODE to TAPE at 1.00x and LOOP DIV 1x: the neutral player. STRETCH at 1x still
  re-grains and ENV still gates, so leaving them would process the take twice.
  **For review**: the alternative keeps the mode and only neutralises its speed.
- CLOCK is left alone: the take is in ticks at that clock, so it already plays back as heard.
- Modulation: routes and depths are kept. A source routed only to loop playback targets
  (LENGTH, MODIFY/LOOP) is switched off with its ON switch, visibly, with the screen saying
  `MOD 1 PAUSED: ITS MOVES ARE IN THE TAKE`. A source that also moves other targets keeps
  running and the screen warns that its loop routes still apply. Nothing is deleted, and UNDO
  puts it all back. **For review.**

## 4. RESAMPLE length, tempo and the 30 s limit

**Changed after use (18 November 2026):** the length stepper and the take's tempo are gone.
RESAMPLE always records one pass of the window as it stands at the press (START, END and
LENGTH set it), and a second press still stops it early. The travel counting and the 30 s
limit below are unchanged; the stepper and tempo reference are kept here as history.

### Length setting

A stepper, `RESAMPLE LOOP / 1/4 / 1/2 / 1 / 2 / 3 / 4 / 6 / 8 / 12 / 16 BARS / FREE`, showing
only bar counts that fit within the playback window.

- **LOOP**: one pass of the playback window as it stands at the press.
- **Bars**: N bars of the take's own tempo reference (below).
- **FREE**: until RESAMPLE is pressed again, or the limit.

### Length follows playback speed

The length is counted in *source travel*: how far the read position moves through the take,
in ticks, so a source bar at 2x passes in half the time, and a speed change part way changes
the remaining time. The rules:

- TAPE: travel is `|change in playhead|`; reverse counts forwards (a bar backwards is a bar).
- STRETCH: travel is the scan position's movement, not the grains'.
- ENV: travel is the playhead (ENV reads at 1x, divided by LOOP DIV); slices jump but don't
  count.
- LOOP DIV and CLOCK are already inside this (CLOCK changes ticks a second, not ticks a bar).
- Stationary or near-stationary (STRETCH or TAPE near 0, HOLD on a speed modulation): travel
  barely moves, so the 30 s limit ends it. The panel shows the estimate first (below).

### Tempo reference

Each take carries `refBpm`, `barQuarters` and the CLOCK it was recorded at, giving ticks a
bar. Saved with the take.

- Recorded while the host plays (CAPTURE or RECORD): the host tempo at that moment.
- The browser's TEMPO field counts as a usable tempo for RECORD with bars.
- Otherwise a guess, which is editable and labelled as a guess (`8 BARS AT 128? ` with a
  stepper): choose bar counts from 1, 2, 3, 4, 6, 8, 12, 16 and pick the one whose tempo is
  nearest 120 BPM on a log scale. A 15 s 4/4 take gives 8 bars at 128, not 4 at 64. Editing
  the bar count recalculates the tempo.

### The 30 s limit

- Counted in host time, so it can't be stretched by CLOCK. At 64 kHz that is 1.92 M ticks,
  inside the buffer (2.048 M).
- Before and during the run the panel estimates `remaining travel / (|speed| x clock)`. When it
  won't fit, the RESAMPLE key shows `30 S MAX` and the screen says
  `8 BARS NEEDS ABOUT 41 S AT 0.5X. STOPS AT 30 S`.
- At the limit it stops (with the seam crossfade), keeps the take, and the screen says
  `RESAMPLE STOPPED AT THE 30 S LIMIT`. UNDO brings the old take back. Nothing is compressed.
- Existing limits, for comparison: CAPTURE 1 to 30 s (MEMORY), RECORD 32 s. **For review**:
  bring RECORD to 30 s so every limit says the same number.
- RESAMPLE is available in both CAPTURE and RECORD whenever a loop plays. **For review**: the
  brief says "available in RECORD mode"; if you meant only RECORD, it greys out in CAPTURE.

## 5. Four-step undo

### What one action is

| Action | One step is |
|---|---|
| Knob, slider, fader | One gesture, press to release (begin/end change gesture). Mouse wheel moves on one control merge until 400 ms idle |
| Stepper, switch, selector, text entry | One click or one entry |
| Modulation chip | One depth drag; one route added or removed; ROUTES CLEAR as one |
| Preset load | One step (all parameters; presets carry no audio) |
| New take (CAPTURE or RECORD) | One step, including the START, END and LENGTH that `fitWindow()` moves |
| Overdub layer | Press to release, or latch on to latch off |
| CLEAR | One step |
| RESAMPLE | One step: the take swap and every automatic change in section 3 |
| MIDI CC | A burst on one controller, merged until 500 ms idle. **For review** |

Not steps: loop play and stop taps, FREEZE taps and holds, host automation, and changes made in
the host's own UI or on Push (they arrive without Brood's gesture). The host already undoes
those itself, and recording automation playback would flush the four steps in seconds.
**Tradeoff for review:** a Push user gets no Brood undo of knob moves; Live's own Cmd-Z still
covers them.

### Rules

- Up to four steps, oldest dropped. UNDO walks back; **REDO** (a new key beside UNDO in the
  screen header, where GAIN was, and a `redo` host param) walks forwards. Any new action
  drops the redo steps. The current "press UNDO again to redo" goes, since a second press
  now means the next step back.
- Undo restores a parameter's own value (what is saved and automated), never a modulated
  effective value. Modulation keeps moving around the restored value.
- Undoing a parameter goes to the host with a gesture, so a host writing automation will
  record it. If host automation is playing that parameter it will win at its next point; the
  screen says the value was restored, nothing more can be known.
- During an operation: UNDO while overdubbing ends and discards that layer; while recording a
  new take or waiting for the bar line, it stands down (as now) and leaves history alone;
  while resampling, it cancels and keeps the original take.
- Saved state: history is not saved. Loading a set or restoring host state clears it; loading
  a preset is a step.
- The screen header shows the depth, `UNDO 3`, so it is never a surprise how far back it goes.

### Memory and threads

Loop audio moves to fixed-size pages (16,384 ticks, 64 KB each), allocated once at
`prepare()` as one arena per core, with a free list. A take is a short page table, so
capture, clear, undo, redo and the RESAMPLE swap change page tables, and nothing is
allocated on the audio thread.

- Overdub copies a page into its undo snapshot the first time the layer writes to it
  (copy on write, at most 64 KB at a time), which replaces today's whole-take copy in
  `beginLayer()`.
- Reads of the loop go through the page table; a page index and an offset per read, the same
  in both cores, so parity still holds.
- Budget: `hist` plus an arena of four full takes (32.8 MB) a core: about 41 MB a core and
  82 MB for both, 33 MB more than today. That holds the playing take, a RESAMPLE target and
  history. With every take at the 32 s / 64 kHz maximum, two audio steps fit; with takes
  around 16 s at 64 kHz (21 s at 48 kHz) or shorter, all four do. Parameter steps cost
  nothing. When the arena is full, the oldest audio-bearing step and everything before it
  go, and `UNDO n` shows the true depth. **For review**, and the browser on phones may want a
  smaller arena (same DSP output, fewer audio steps).
- Ownership: the history lives in the processor (and in the browser's engine) on the message
  thread, so it works with the editor closed. The cores report finished operations (take,
  layer, clear, resample) through a lock-free queue with a version id; UNDO and REDO send
  "play version n" back the same way, and it lands through the fade the loop uses now. Both
  cores receive the same commands, so their arenas stay identical.

## 6. Layout (plug-in 1200 x 820, browser desktop the same canvas)

```
 SCREEN                                          [CLEAR] [UNDO 3] [REDO]
 ---------------------------------------------------------------------
 [ FREEZE  TAP / HOLD [A] ]  [ WEAR ]  [ LOOP [L] ][OVERDUB][RESAMPLE]
  WET CHANNEL          SHARED                    MICRO-LOOPER
  REVERB DELAY..   IN | IN+LOOP | LOOP | OFF  o   ENV TAPE STRETCH
  TIME             WET MIX                        LENGTH
  SMEAR            CLOCK                          SPEED
  TRAILS  WET DIV  SMOOTH                         LOOP DIV

 SIDE PANEL
  INPUT   o heard      LEVEL  ----o----  0.0 dB
                       MONITOR  AUTO | ON | OFF       AUTO: LOOP REPLACES INPUT
  LOOP                 LEVEL  ----o----  0.0 dB   (drag up/down: modulation, as BALANCE now)
                       MEMORY ----o----
                       CAPTURE | RECORD   RESAMPLE [LOOP >]
                       8 BARS AT 128?  (tempo, editable)
  MODULATION           (shorter, as it has spare room now)

 BOTTOM STRIP: TONE  FADE  WIDTH  NOISE   (LEVEL moves to INPUT)
```

- The six knobs keep their places; MIX becomes WET MIX in the same spot.
- The WET footswitch's place becomes the FREEZE key; the small FREEZE cell goes.
- GAIN leaves the screen header for the LOOP LEVEL slider, making room for REDO and the depth.
- LOOP, OVERDUB and RESAMPLE share the right-hand key row (about 150 + 66 + 72 px). Every new
  word gets the screen-fit check in both versions, desktop and phone.
- Phone: INPUT goes at the top of the SHARED tab; OVERDUB and RESAMPLE stay with LOOP on the
  MICRO-LOOPER tab.
- I'd build a mock of this as `editor_snapshot` renders and a browser screenshot before any
  DSP work, for you to look at first.

## 7. Parameters, MIDI, modulation, presets, state

- Host params: add `inputLevel`, `loopLevel`, `wetMix`, `monitor`, `resample`, `resampleLen`,
  `redo`, take tempo (`refBpm`, `refBars`); routing gets `Off`; remove `mix`, `balance`,
  `loopDb`, `wetSwitch`. Names fit a Push display.
- MIDI: CC 15 becomes WET MIX; CC 28 (the pedal's LEVEL BALANCE) becomes LOOP LEVEL; CC 22
  routing gains OFF at its top value; CC 103 toggles ROUTING OFF; new CCs for INPUT LEVEL,
  MONITOR, OVERDUB, RESAMPLE, UNDO and REDO from unused numbers, listed in the manual.
- Modulation targets stay seven: TIME, WET MIX, LENGTH, MODIFY/WET, CLOCK, MODIFY/LOOP,
  LOOP LEVEL (on the fader's curve). The BALANCE drag-for-modulation moves to LOOP LEVEL.
  INPUT LEVEL is not a target (it would modulate what's recorded). **For review.**
- Presets: each factory preset sets WET MIX explicitly. Because there is no WET switch now,
  a preset is heard with its WET as soon as it loads, which is a change. I'd start from
  `wetMix = old MIX` and you audition them; BALANCE values go.
- State version 5. Old sets load with defaults plus a best-effort carry-over (MIX to WET MIX,
  GAIN to LOOP LEVEL, LEVEL to INPUT LEVEL, wet off to ROUTING OFF) and nothing more,
  since compatibility isn't a goal. The browser's stored settings get the same.
- Help and readouts: new text, in your wording, shown to you before and after.

## 8. Tests

- DSP (C++ `dsp_tests.cpp`, JS `test/dsp.test.js`) and new parity scenarios, rendered by both
  cores and compared:
  - `monitor_modes`: ON, OFF, AUTO across play, stop, record, overdub; OFF mid-overdub keeps
    the layer.
  - `dub_isolation`: IN + LOOP overdub feeds WET input only; the take holds no new loop WET.
  - `resample_loop`, `resample_inloop`, `resample_in`, `resample_off`: what `P` holds, and the
    automatic ROUTING and FREEZE changes.
  - `resample_tails`: a ringing tail at the press appears; input after the press doesn't.
  - `resample_speed`: 2x, 0.5x, a speed change part way, reverse, STRETCH, ENV, LOOP DIV,
    and near-stationary hitting the limit.
  - `resample_limit`: stops at 30 s host time, keeps the take, UNDO restores.
  - `undo_four`: take, three parameter steps, four undos, redo, a new action dropping redo,
    arena exhaustion shortening history.
  - Existing scenarios that set MIX, BALANCE or the WET switch are rewritten, not deleted.
- Plug-in (`plugin_tests.cpp`): host params and ranges, MIDI CCs, OVERDUB refused in OFF
  from UI, MIDI and automation, undo with and without the editor, state v5 round trip, no
  allocation on the audio thread during capture, overdub, resample, undo.
- Browser: unit tests for the history and tempo guess, smoke test, and the screen-fit sweep
  (every new label and readout, desktop and phone).
- Listening, by you, in Live:
  1. Sample on an audio track: capture, AUTO hands over to the loop with no doubling and no
     knob rush; raise WET MIX; RESAMPLE with ROUTING LOOP lands with ROUTING OFF and no
     second reverb.
  2. Live guitar: ON, play over the loop, OVERDUB in IN + LOOP with the reverb on the guitar
     only, UNDO the layer.
  3. Monitor OFF: RESAMPLE a STRETCH loop at 0.5x for 2 bars; check the estimate, the length,
     and UNDO.

## 9. Order of work

As built, two departures from the text above: the tempo guess also offers 1/4 and 1/2 bar
(a 1.3 s take reads 1/2 BAR 90 BPM, not 1 BAR 180), and loop audio regions are rounded up to
whole 4096-sample chunks so takes of nearly the same length share room in the arena.

1. Layout mock (plug-in snapshot and browser), labels and help draft for your look.
2. New parameters, state v5, MIDI map, presets carried over; WET button out, ROUTING OFF,
   FREEZE key. Both cores, parity.
3. Mix law and INPUT MONITOR, `soloLoop` out. Both cores, parity.
4. OVERDUB isolation; runaway guards removed. Parity.
5. Paged loop memory and the four-step history (audio and parameters). Parity, no-allocation
   checks.
6. RESAMPLE, length, tempo reference and guess, 30 s limit, automatic changes.
7. Help, readouts, screen fit, manual, tour steps that mention MIX, BALANCE or WET.
8. Full test pass, installer for your listening tests. Nothing committed until you've seen
   each stage.

## 10. Decisions (settled on my recommendations; open to change after listening)

1. In OFF, a new take keeps the plain input (WET never hears it). Or keep WET fed and mute it?
2. ROUTING OFF as the replacement for the WET switch, with FREEZE taking the WET key.
3. RESAMPLE bakes LOOP LEVEL in and resets it to 0 dB.
4. RESAMPLE lands in TAPE at 1x, or keeps the mode and only neutralises speed.
5. Speed-only modulation sources are paused (ON off) after RESAMPLE; mixed ones keep running.
6. A latched FREEZE is released after a RESAMPLE that recorded loop WET.
7. RESAMPLE in both CAPTURE and RECORD, or RECORD only.
8. RECORD's limit to 30 s, to match.
9. MIDI CC bursts count as undo steps; host and Push changes don't.
10. The memory budget (82 MB for both cores, two to four audio steps), and a smaller arena on
    phones.
11. WET MIX uses the existing both-full-at-noon law (rather than equal power).
12. LOOP LEVEL as a modulation target in place of BALANCE; INPUT LEVEL not a target.
