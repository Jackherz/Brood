# Five fixes: execution plan

Status: Phase A (thinking) done. Waiting for approval before Phase B (execution).
Repo: `~/Documents/AI/Brood-Source`, `main` only. Do not commit or push without asking.

## Answer these before execution

1. **Commit the WORN work first?** `git status` shows uncommitted work across 11 files: NOISE (`wearNoise`), WEAR after the loop (`wearPost`, engine only, no switch yet), CC 28, the short host names, and the new parity scenario `wear_post.txt`. Recommendation: finish the `wearPost` switch (step 0b) and commit it, so the five fixes start from a clean tree.
2. **START default.** The plan makes START mean "from the oldest audio" in every mode and moves the default from 0 to 1 (the newest). Factory presets, a fresh instance and every old set sound the same. The one audible change: a **fresh** instance switched to ENV or STRETCH, with END pulled in below 100%, puts its window at the newest audio instead of the oldest. OK? (Alternative: default 0, which changes a fresh TAPE instance to play the oldest audio. Not recommended.)
3. **Bar-locked recording starts at the tap**, not at the next bar line, and ends N bars of host time later. Confirm.
4. **No host tempo** (web version, standalone, or a host that sends none): bars use the tempo Brood already falls back to (`hostBpm_`, last seen, else 120; the web version's TEMPO field) and 4/4. Confirm.
5. **Push 3 value length.** Nothing published says where Push 3 cuts a plug-in value string. Before step 5 starts, load Brood on the Push, look at TIME in REVERB (today "decay 2.31 s", 12 characters) and MEMORY (today "up to 12.0 s of 30.0 s", 22 characters) and report how much shows. The limit in step 5 is set from that.

## Reported during execution (to investigate)

- **ENV: LENGTH does nothing** (reported 2026-11-13, from a screenshot of the plug-in in Live). Settings: ENV loop mode, LENGTH synced (1/32, 83 ms) and also unsynced, SOFT off, CATCH (sensitivity) 0%, ROUTING LOOP, WET/LOOP 100%, MIX dry 0% / fx 100%, WEAR WORN with AFTER LOOP on, DELAY with TAPE on, CLOCK 16 kHz, MEMORY 30 s, RECORD with 4 bars, a 16 s take. Turning LENGTH, synced or not, changes nothing audible.
  - First lead, not yet checked: in `loopTick` ENV slices only while the input's envelope is above `thr = expMap(1 - modLoop, 0.008, 0.35)`. CATCH at 0% puts the threshold at its highest (0.35), so the gate may never open, no slice plays, and LENGTH (the slice length) has nothing to act on. If so, the fix is a UI question (say so in the readout, or keep a floor on sensitivity), not the slicer.
  - Also check: whether ENV's gate listens to the input when ROUTING is LOOP, and whether a RECORD take changes anything.
  - Reproduce with a C++ test at these settings before changing anything.
  - Update the same day: moving LENGTH did nothing, then it started working again with no change. Treat it as intermittent: if it comes back, note what was done just before (a new take, a mode switch, RECORD closing, a project reload) and try the lead above in a test then.

## Decisions

One line each, with the reason.

- **LENGTH and the playhead:** keep `loopPos` in samples and rescale it by `Wnew / Wold` whenever the TAPE window's span changes because of LENGTH. Least disturbance: `State.pos`, `restoreLoop`, the editor playhead and the overdub head all keep reading samples.
- **Done in step 1, with a change agreed on the way:** TAPE's window is anchored to the newest audio, so rescaling alone made the playhead race through the take (worst step 0.85 against 0.2 before). The window now follows LENGTH at up to 3 samples a tick (`kLengthFollow`), so the playhead never runs faster than 4x (worst step 0.05). A full LENGTH turn on a 30 s take settles in about 8 s. The ENV slice wrap was tested with plain and synced LENGTH and is not reachable (LENGTH is smoothed before it gets there), so it was left alone.
- **END does not scale the playhead.** END is a hand gesture, slow, and scaling would make the loop's tail jump when you drag the end marker. Only the LENGTH-driven span (`W0`) triggers rescaling.
- **LENGTH-driven moves of the window's start skip the 25 ms START crossfade.** With the playhead rescaled, the absolute read position moves continuously (a pitch bend, as on the Digitakt), so there is no jump to hide. A chain of 25 ms fades restarted every 25 ms is what fast modulation produces today. START knob and END changes keep the crossfade.
- **ENV slice phase wraps with a true modulo.** Today `if (slicePh >= S) slicePh -= S` runs once per tick, so when S drops fast `(S - slicePh) / F2` goes hugely negative (about -240 for a 1 s to 10 ms drop) and is used as a gain. A burst the limiter squashes is very likely audible. The test in step 1 confirms before the fix.
- **One meaning for START:** counts from the oldest audio in every mode, using the ENV formula. Default moves to 1 (see question 2). At 1 the ENV formula clamps to the tail at any LENGTH, so TAPE with START untouched still follows the newest audio as LENGTH turns.
- **Migration is exact at load**, not approximate: old TAPE `v` becomes the new value that gives the same absolute start for the stored LENGTH, END and take length. 0 maps to exactly 1, and 1 to exactly 0. With no take in the state yet, use the pedal take length (`kLoopSamples`) and store the result.
- **Mode switches preserve the absolute playhead** (`window.start + loopPos`) when it lies inside the new window, else start at the window's start through the existing 25 ms crossfade. Boundary with the LENGTH rule: scale when the span changes with the mode unchanged; preserve when the mode changes.
- **RECORD reuses the hold path.** `footswitch()` already stores the history write point on press and calls `capture(written)` on a held release. RECORD stores the same point on a tap and closes the recording on the next event: a second tap, the bar target, or the MEMORY ceiling.
- **Auto-close is checked per core tick** inside `coreTick()` in both cores (where `hist` is written), not per block, so both cores close on the same sample and bar-locked loops are not late.
- **Bars are counted in host samples** (`hostCount`), not core ticks, so moving CLOCK mid-recording does not change the bar length. The bar target in host samples is fixed when recording starts.
- **The MEMORY ceiling wins over bars.** At the ceiling the recording closes and keeps what was played first. The LOOP panel readout says "8 BARS > MEMORY" before you record when the ceiling would cut it short.
- **Tape delay glide law:** a slew limit on the read head's speed (head speed held between 0.5x and 1.5x, so pitch bends at most 12 semitones down and 7 up), followed by a gentle one-pole on the slew so the bend has no corners. Sweeping TIME fully at that limit cannot overtake the write point: the head never moves faster than 1.5x and the line is longer than any delay.
- **Tape wow and flutter** from two deterministic sine LFOs with their own phases (the `wowPh` pattern), never `rng`. Depth about 0.15% wow at 0.5 Hz and 0.05% flutter at 6 Hz, scaled by `1 - hold`.
- **Tape saturation** in the feedback path only: `tanh` of drive 1.3 normalised, blended out by `full`, so a frozen line is rewritten exactly.
- **FREEZE with tape on:** wow, flutter and saturation fade with `hold`; TIME stops bending a frozen line (the target is latched at the frozen delay); the frozen read uses a whole-sample delay so recirculation has no interpolation loss.
- **Existing digital delay, frozen:** it reads at a fractional delay through Hermite interpolation and writes that back every pass, so a long freeze may dull slowly today. Measure it in step 4's freeze test. Report, do not fix, unless it fails the same test as tape.
- **Switching tapeDelay** crossfades the two read paths over 30 ms with a raised cosine.
- **Host value strings:** a new `hostValue(id, v)` returns short "value unit" strings; the existing `describe()` stays the prose for the editor readout and the web version. Mod depths get a string function ("+40%").
- **CC numbers for the new switches:** none. They are automatable parameters; the pedal's map has no equivalent, and spare CCs collide with controllers set up for the pedal.

## Step 0: baseline

**0a. Checks on the tree as it is** (already run in Phase A; rerun before step 1):

```bash
cmake --build native/build
ctest --test-dir native/build -C Release --output-on-failure
npm test
node tests/parity/compare.js native/build/brood_render
make check
make install
```

**0b. Finish WEAR after the loop** (only if question 1 says so), then commit:
- Parameter `wearPost` (bool, "Wear Post", default off) in `native/plugin/PluginProcessor.cpp` layout and `pullParams()` (`p.wearPost = param("wearPost") > 0.5f`).
- Editor: a small IN LOOP / AFTER LOOP switch under the WEAR stepper (`characterDesign`), active only on WORN. `native/plugin/PluginEditor.cpp`, `PluginEditor.h`.
- Web: add to the switches in `src/main.jsx` (`sanitize` already accepts it).
- JS test: the version removed earlier failed in its setup (silent loop even with the option off). Rewrite it using the same footswitch timing as the C++ test in `native/tests/dsp_tests.cpp` ("WORN after the loop") and get it passing.
- `docs/MANUAL.md`: describe the switch.

**0c. Record the baseline renders** before any DSP change:

```bash
mkdir -p build/baseline
for f in tests/parity/scenarios/*.txt; do native/build/brood_render "$f" "build/baseline/$(basename "$f" .txt).raw"; done
```

(Check `brood_render`'s arguments with `native/build/brood_render --help` first; if it writes WAV, store WAV.) Write `scripts/diff-baseline.sh` that re-renders every scenario and reports the largest sample difference against `build/baseline/`, per scenario. Every later step names which scenarios may differ; any other difference is a failure.

## Step 1: LENGTH modulation must not trap the playhead (Issue 4)

Files: `native/dsp/brood.h` (`loopTick`, `loopWindowAt`, new member `lastSpan`), `src/brood-worklet.js` (`loopTick`, `loopWindowAt`, `this.lastSpan`), tests in `native/tests/dsp_tests.cpp` and `test/dsp.test.js`, scenario `tests/parity/scenarios/length_mod.txt`.

1. **Tests first, both suites, both must fail before the fix.**
   - `"TAPE: fast LENGTH modulation keeps the playhead moving through the window"`: capture 4 s of a 220 Hz saw; TAPE; mod 1 routed to LENGTH, depth 0.8, triangle, rate for 25 Hz (`rampHertz`), then 20 Hz. Over 2 s, record `state().pos / state().winLen` every block: assert it visits every tenth of the window, and assert the output's largest sample-to-sample step is below 0.25 (a 220 Hz saw at the loop level steps by under 0.06; a wrap to a faded edge or a stuck repeat steps far more or goes silent, so also assert RMS over every 50 ms stays above 20% of the unmodulated RMS).
   - `"ENV: LENGTH dropping fast never gives a negative slice gain"`: ENV, threshold low, steady input so the gate stays open, LENGTH 1 then 0 in one block. Assert the loop output never exceeds 1.5x its peak before the drop.
2. **C++ fix in `loopTick()`:**
   - Compute the TAPE span `W0` (as in `loopWindowAt`) and keep `lastSpan`. If the mode is TAPE, the mode did not change, and `W0 != lastSpan`, set `loopPos *= W0 / lastSpan` (TAPE `W` is `floor(W0 * trim)`, so scale by the ratio of `W`s computed with the same trim). Also scale `dubPos` and `dubW`'s frame the same way, or keep the overdub on its own window as it does today and confirm with the existing overdub tests that nothing smears.
   - When the start moves only because the span changed, update `startA = startB = wantStart` directly instead of starting a crossfade. Keep the crossfade for START knob and END changes (track `lastWhere`, `lastTrim`).
   - ENV: replace `if (slicePh >= S) slicePh -= S;` with `slicePh = jsMod(slicePh, S);` and clamp the edge term to `std::max(0.0, ...)`.
3. **Same change in JS**, line for line.
4. **Scenario `length_mod.txt`:** TAPE capture, mod 1 to LENGTH at 25 Hz for 3 s, then ENV the same, then STRETCH the same, then an overdub during modulation.
5. **Checks:** both new tests pass; `npm test`; `ctest`; parity, all ok; `scripts/diff-baseline.sh`.
   - **May change:** `ramps.txt` and `mod_routes.txt` if they route LENGTH in TAPE or ENV (check which do: `grep -l "_length" tests/parity/scenarios/*.txt`). State the size of each change.
   - **Must stay identical:** everything else.

## Step 2: one meaning for START (Issue 3)

Files: `native/dsp/brood.h` (`loopWindowAt`, `loopTick`, `loopSpan`, new `lastMode`), `src/brood-worklet.js`, `native/plugin/PluginProcessor.cpp` (`start` default, `describe`, `setStateInformation`, `getStateInformation`), `native/plugin/PluginEditor.cpp` (take strip marker drag around line 551, and the LENGTH-holds-START logic around line 728), `src/main.jsx` (`BASE.start`, `describe('start')`, the strip at lines 414 to 532, `sanitize`, storage), `native/plugin/BroodPresets.h` (factory `apply` default), `docs/MANUAL.md`, `tests/parity/scenarios/start.txt`.

1. **Tests first:**
   - `"START means the same in every mode"`: capture 4 s; END 0.5; START 0.3; for each mode, the window's absolute start is the same within one sample.
   - `"switching loop mode keeps the playhead where it is in the take"`: TAPE playing, LENGTH full, END 0.6; switch to ENV mid-loop; absolute playhead before and after differs by under 2 samples when it lies inside both windows; the output has no step above the step-1 threshold.
   - `"old sets load sounding the same"` (C++ only, plug-in level in `native/tests/plugin_tests.cpp`): a state XML with `loopMode` TAPE, `start` 0.3, `length` 0.6 and a 3 s take restores to the same absolute window start as before migration.
2. **C++ core:** `loopWindowAt` uses the ENV formula for all modes. In TAPE, `W0` still comes from LENGTH and the clamp `min(travel, len - W0)` keeps the window inside the take. Remove the TAPE branch's `(1 - where) * room`. `loopSpan()` is unchanged.
3. **Mode switch:** in `loopTick`, when `p.loopMode != lastMode`, compute the old absolute playhead (`oldStart + loopPos`); if it lies inside the new window, set `loopPos = abs - newStart` and `startA = startB = newStart`; otherwise set `loopPos` to 0 and run the 25 ms START crossfade from the old start.
4. **JS core:** the same.
5. **State migration, native:** add a property `stateVersion` (current 2) in `getStateInformation`. In `setStateInformation`, if it's missing and `loopMode` is TAPE, rewrite `start` with the exact inverse described in the decisions. Write the conversion as a static function in `brood.h` (`migrateTapeStart(oldStart, length, loopEnd, lsrc)`) so JS can share the maths.
6. **State migration, web:** bump the stored key `brood-params-v1` to `brood-params-v2`; when only v1 exists, migrate `start` the same way (no take is stored, so use `kLoopSamples`). User presets `brood-user-presets-v2`: migrate on read, store a `v` field per preset.
7. **Default:** `start` default 1 in the parameter layout and `BASE.start = 1`. Factory presets set no START, so `apply()` gives them 1: identical to their old TAPE sound.
8. **Readouts and UI:** `describe('start')` in both: 1 is "the newest", 0 "the oldest", between them "X s from the oldest" when the take is known, else a percentage. The take strip markers drag with one direction in every mode (remove the `tape ? 1 - xs / room : xs / room` split in `src/main.jsx` line 477 and the matching code in `PluginEditor.cpp`). Remove the "LENGTH holds START" special case in the editor if the new formula makes it unnecessary; check with the editor's existing tests.
9. **`start.txt`:** add `0 param start 0` at the top so the ENV part keeps its sound, and convert the TAPE section's explicit values with `migrateTapeStart` for the LENGTH in force at each line. Its output will still change during the TAPE section where LENGTH moves with START between 0 and 1: under the new rule the window holds its absolute place instead of moving with LENGTH. That is intended; record the size of the change.
10. **Docs:** `docs/MANUAL.md`, START and the take strip.
11. **Checks:** new tests; `npm test`; `ctest`; parity ok; `scripts/diff-baseline.sh`.
    - **May change:** `start.txt` (TAPE section only), plus any scenario that switches `loopMode` mid-loop (playhead preservation): check with `grep -l "param loopMode" tests/parity/scenarios/*.txt` and list each.
    - **Must stay identical:** every scenario that never sets `start` and never changes `loopMode` while a loop plays.

## Step 3: realtime record mode (Issue 2)

Files: `native/dsp/brood.h` (`Params`, `footswitch`, `coreTick`, `capture`, `State`, new `Recording` struct), `src/brood-worklet.js`, `native/plugin/PluginProcessor.cpp` (two parameters, `pullParams`, pass `barQuarters`), `native/plugin/PluginEditor.cpp` and `.h` (LOOP panel controls, take strip copy), `src/main.jsx`, `native/tests/render.cpp` (new param names), `tests/parity/compare.js` and `tests/parity/render.js` only if the scenario format needs it, `docs/MANUAL.md`.

1. **Parameters:** `recMode` (choice: Capture, Record; default Capture; host name "Rec Mode") and `recBars` (choice: Free, 1, 2, 3, 4, 6, 8, 12, 16; default Free; host name "Rec Bars"). Core `Params`: `int recMode = 0; int recBars = 0; double barQuarters = 4;`. Processor sets `p.barQuarters = hostBarQuarters_.load()` each block.
2. **Tests first,** both suites:
   - `"RECORD: tap, play, tap keeps what was played after the first tap"`: silence, tap, 1 s of a 440 Hz tone, tap; the take is 1 s within 10 ms and its RMS matches the tone.
   - `"RECORD: hold records while held, plays on release"`.
   - `"RECORD: 2 bars at 120 bpm closes itself at 4 s and plays"`: the take length is 4 s of host time within one core sample, with CLOCK moved mid-recording.
   - `"RECORD: at the MEMORY ceiling it closes and keeps the start"`: MEMORY 2 s, record 3 s of a rising tone; the take is the first 2 s.
   - `"RECORD: with a loop playing, tap stops and hold overdubs"`: identical to Capture.
   - `"CAPTURE is unchanged"`: covered by the baseline diff.
3. **C++ core:**
   - New member `struct Recording { bool on = false; int histIdx = 0; uint32_t histCycle = 0; int64_t endAt = -1; int ceiling = 0; } rec;`.
   - `footswitch(1, down)` in RECORD with nothing playing and no recording: start one (store the write point, the bar target in host samples from `p.bpm` and `p.barQuarters` when `recBars > 0`, and `maxCaptureSamples()` as the ceiling). A down while recording closes it. A held release closes it. While a loop plays, the existing code runs unchanged.
   - Closing: `capture(written)` exactly as the held release does today. Because `capture()` copies the newest `written` samples, closing at the exact tick gives "the first part" for free.
   - In `coreTick()`, after `hist` is written: if recording and `written >= ceiling`, close. In `process()`'s host-sample loop: if `rec.endAt >= 0 && hostCount >= rec.endAt`, close. Both are per sample, so the two cores agree.
   - `state()`: LED shows Rec while `rec.on`.
4. **JS core:** the same.
5. **UI:** LOOP panel (01): a CAPTURE / RECORD stepper and a bars stepper beside MEMORY. Take strip empty copy: Capture "LISTENING. PRESS LOOP TO KEEP THE LAST MOMENT_"; Record "PRESS LOOP TO RECORD_"; while recording "RECORDING_" plus "2 BARS" or "FREE". Readout warns when bars exceed the ceiling. Web: the same in `src/main.jsx`.
6. **Scenarios** (new files): `rec_tap.txt`, `rec_hold.txt`, `rec_bars.txt` (with `bpm` set and CLOCK moving), `rec_ceiling.txt`, `rec_playing.txt`. The format already has `fs loop down/up` and `param`; add `param barQuarters` support to `native/tests/render.cpp` and the JS renderer only if needed.
7. **Checks:** new tests; `npm test`; `ctest`; parity ok; `scripts/diff-baseline.sh`.
    - **May change:** nothing.
    - **Must stay identical:** every existing scenario (all default to Capture).

## Step 4: tape delay mode (Issue 1)

Files: `native/dsp/brood.h` (`Delay`, `Params`, the call in `coreTick`), `src/brood-worklet.js` (`Delay` class), `native/plugin/PluginProcessor.cpp`, `PluginEditor.cpp`/`.h`, `src/main.jsx`, `native/tests/render.cpp`, `docs/MANUAL.md`.

1. **Parameter:** `tapeDelay` (bool, default off, host name "Tape Echo"). Core `Params::tapeDelay`. Passed into `Delay::tick` as an argument.
2. **Tests first,** both suites:
   - `"TAPE ECHO: sweeping TIME bends pitch and never steps"`: a 1 kHz tone into the delay, feedback 0.5, TIME swept 0.2 to 0.8 over 1 s. Assert the largest sample-to-sample step stays below twice the tone's own, and that during the sweep the echo's frequency moves (zero-crossing count differs from 1 kHz by more than 2%).
   - `"TAPE ECHO: FREEZE during a sweep holds a steady loop for 20 s"`: RMS of each second within 0.5 dB of the first; energy above 4 kHz within 1 dB of the first second.
   - `"TAPE ECHO: switching it under signal doesn't click"`.
   - `"TAPE ECHO: synced TIME lands on the division"`: after the glide the delay equals `syncSeconds` within one sample.
   - `"DIGITAL DELAY: a 20 s freeze holds its level and top end"`: report the result; it measures today's behaviour.
3. **C++ `Delay`:**
   - New state: `headD` (current read delay, double), `headV` (head velocity), `wowA`, `flutA` (LFO phases), `tapeMix` (0..1, smoothed switch), `frozenD`.
   - Tape read: target `delaySamples` (latched to `frozenD` while `hold > 0.5`); `headV` slews toward `(target - headD) * k` limited to ±0.5 per sample; `headD += headV` with a one-pole on `headV`; wow and flutter add `headD * (0.0015 sin + 0.0005 sin) * (1 - hold)`. Read with `readFrac` (already Hermite). While `full > 0.999`, round `headD` to a whole sample for the read.
   - Saturation: on the feedback term only, `tanh(1.3 x) / tanh(1.3)` blended by `tapeMix * (1 - full)`.
   - Output: `y = digital * (1 - w) + tape * w`, `w` a raised cosine of `tapeMix`, which moves over 30 ms.
   - Digital path untouched when `tapeMix == 0`, so default renders are identical.
4. **JS `Delay`:** the same.
5. **UI:** a TAPE switch in the WET CHANNEL column beside the wet SYNC switch, visible only in DELAY. Web: the same.
6. **Scenario** `echo_tape.txt` (not `delay_tape.txt`, which already means delay plus TAPE loop mode): TIME sweeps with tape on, FREEZE engaged mid-sweep, the switch toggled under signal, sync on with TIME stepping.
7. **Checks:** new tests; `npm test`; `ctest`; parity ok; `scripts/diff-baseline.sh`.
    - **May change:** nothing.
    - **Must stay identical:** every existing scenario.

## Step 5: short value strings for the host (Issue 5)

Files: `native/plugin/PluginProcessor.cpp` (new `hostValue`, every `withStringFromValueFunction`, mod depth parameters, `valueFromString`), `native/plugin/PluginProcessor.h`, `native/plugin/PluginEditor.cpp` (keeps calling `describe`), `src/main.jsx` (unchanged prose; optional short form for the web's small figures), `docs/MANUAL.md`, test `native/tests/plugin_tests.cpp`.

1. **Gate:** question 5's Push check sets the limit `L` (target 8 characters, at most 12).
2. **Test first:** `"every parameter's host string is short"`: for every parameter, at min, default, max and three points between, in every wet and loop mode, `getText(v, 64).length() <= L`, and `getValueForText(getText(v))` round-trips within 1% where a reverse mapping exists.
3. **`hostValue(id, v)`:** short strings in this style. Choice and bool parameters use their names, with "On" and "Off" for bools.

| id | Host name | Examples (min / default / max) | Notes |
|---|---|---|---|
| time (REVERB) | Time | 0.40 s / 2.31 s / Inf | divided by clock ratio as today |
| time (DELAY) | Time | 20 ms / 350 ms / 2.00 s | synced: "1/8 D" |
| time (SLIP) | Time | 50 ms / 280 ms / 1.50 s | |
| time (WEAVE) | Time | 0.20 s / 1.63 s / 2.80 s | synced: "1/4" |
| mix | Mix | Dry / 50/50 / Wet | middle values "70% Wet" |
| length (TAPE) | Length | 2% / 72% / 100% | share of the take; seconds when the take length is known |
| length (ENV) | Length | 10 ms / 72 ms / 1.00 s | synced: "1/16" |
| length (STRETCH) | Length | 10 ms / 66 ms / 500 ms | |
| modWet (REVERB) | Wet Mod | 0% / 58% / 100% | |
| modWet (DELAY) | Wet Mod | 0% / 58% / 100% | |
| modWet (SLIP) | Wet Mod | Rev 2.0x / 1.0x / 2.0x | |
| modWet (WEAVE) | Wet Mod | Long 0% / Long 58% / Short 100% | |
| clock | Clock | 2.0 kHz / 48.0 kHz / 64.0 kHz | |
| modLoop (ENV) | Loop Mod | 0% / 66% / 100% | |
| modLoop (TAPE) | Loop Mod | Rev 2.0x / 1.00x / 2.0x | |
| modLoop (STRETCH) | Loop Mod | Rev 0.50x / 1.00x / 2.0x | |
| balance | Wet/Loop | Wet / 50/50 / Loop | |
| memory | Memory | Pedal / Pedal / 30.0 s | |
| loopEnd | Loop End | 2% / 100% / 100% | |
| start | Loop Start | Oldest / Newest / Newest | between: "1.2 s" from the oldest when known, else % |
| tone | Tone | -100% / Open / Open | |
| fade | Fade | Pedal / Pedal / 100% | |
| width | Width | Mono / As built / Wide | between: "+40%", "-40%" |
| wetDiv, loopDiv | Wet Div, Loop Div | 1x / 1x / 8x slow | |
| character | Wear | Clean / Pedal / Worn | |
| wearNoise | Noise | Off / 100% / 100% | |
| loopDb | Loop Gain | -24 dB / 0 dB / +12 dB | |
| mod1Rate, mod2Rate | M1 Rate | 0.05 Hz / 0.45 / 20 Hz | synced: "4 bars" |
| mod1Wave | M1 Shape | Tri / Tri / Smth | |
| mod depths (14) | M1>Time and so on | -100% / 0% / +100% | new string function |
| recBars | Rec Bars | Free / Free / 16 bars | |
| bools | as named | Off / Off / On | |

   The exact numbers for each default come from running `getText` once the function exists; the table above sets the style, and the test enforces the length.
4. **Reverse mapping:** add `withValueFromStringFunction` for the plain number-and-unit parameters (time, length, clock, loopDb, memory, depths, percentages) so a host that lets you type a value accepts "350 ms" or "40%". For mode-dependent strings ("Rev 2.0x", "Long 30%") parse the number only and state it in a comment.
5. **Editor and web keep `describe()`.** No change to the readout wording.
6. **MIDI table:** make `docs/MANUAL.md` and the CC handler agree:
   - **CC 71** is documented as "Classic mode off/on" but sends 0 to WEAR = Clean, not Pedal. Fix in code: 0 gives Pedal (0.5), anything above 0 gives Worn (1.0). Update the table to "Wear: pedal or worn".
   - Remove the em-dash in the CC 105 row and anywhere else in the touched sections.
   - Finish the sentence above `## MIDI` ("Sets saved under the old product name still open: the plug-in IDs did not change,").
   - Replace "as in Silt" in the modulation section (around line 259) with the behaviour itself.
   - No CC for `tapeDelay`, `recMode`, `recBars`; say so in the paragraph after the table.
7. **Checks:** the new test; `ctest`; `make install`; then the Push 3 check (needs a full restart of Live).

## Final pass

```bash
cmake --build native/build
ctest --test-dir native/build -C Release --output-on-failure
npm test
node tests/parity/compare.js native/build/brood_render
scripts/diff-baseline.sh
make check
make install
native/build/plugin/editor_snapshot build/snapshot-final.png 1.5 loop
npm run dev   # then, in another terminal:
CHROMIUM_PATH=/path/to/chromium npm run test:browser
```

- Look at the snapshot: the new LOOP panel controls, the TAPE switch in DELAY, the take strip copy in both record modes.
- Update `docs/MANUAL.md` for every new control, and the README's feature list for RECORD and TAPE ECHO.
- Ask before committing. Suggested commits: one per step, in this order.

## Phase A findings for the record

- Baseline checks on the uncommitted tree: see the result reported with this plan.
- The JUCE processor already reads the host time signature into `hostBarQuarters_` (used by the Bench); step 3 passes it to the core.
- `readFrac` is Hermite, so the delay's existing reads keep their top end; the tape head can reuse it.
- `capture()` copies up to the whole buffer on the audio thread today. RECORD calls it exactly as often as Capture does (once per take), so it is no worse.

## After the five fixes: Arturia-style modulation

Requested on 14 November 2026, to pick up once the five fixes are done. Change how
modulation looks and works on the knobs so it is closer to Arturia's "click and drag to
mod a param" workflow. Reference: the user's screen recording of Arturia Efx Fragments
(`Screen Recording 2026-11-14 at 9.15.58 am.mov` on the Desktop). What it shows:

- **A handle on every knob.** A small yellow dot sits on the ring just above the knob.
  Dragging it up or down sets that knob's modulation amount; the knob's value doesn't move.
- **The amount is an arc on the ring.** The dot grows into a yellow arc running from the
  knob's current value, to one side or the other depending on the sign. Zero amount shrinks
  back to the dot.
- **Bipolar amounts with a live readout.** While dragging, a black tooltip beside the knob
  shows the amount in yellow, signed ("+28.8%", "-44.2%", "-6.64%"), or "+/-" for a
  two-sided amount ("+/-20.2%").
- **The knob itself is unchanged.** A plain drag on the knob still sets its value, with its
  own tooltip in the parameter's unit ("26.6 Hz", "48.8 %").
- **The status line explains it.** Hovering the handle shows a line at the bottom of the
  window naming the source and what it does to the parameter.

To work out before building: which source the handle drives (Brood has two, so a source
picker or one handle per source, with colours to tell them apart), how this replaces
dragging a source onto a knob, and whether the same handle goes into the kit so Silt
gets it too.
