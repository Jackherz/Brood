# Brood Mood

An unofficial, playable MOOD MKII-inspired stereo micro-looper and spatial-effects
plugin, with a black-and-white pixel-art interface. It includes a JUCE VST3
plugin and a browser version that runs the same DSP. There is also source for
JUCE standalone and macOS AU targets.

New here? [INSTALL.md](INSTALL.md) is the three-command version of the macOS
install; this README has the detail.

**This is an independent approximation, not the original pedal firmware or a
bit-exact emulation.** Chase Bliss's public firmware repository distributes
binary firmware rather than the MOOD DSP source. The algorithms and parameter
ranges here are interpretations of the published behaviour and have **not**
been calibrated against a physical pedal. It is not affiliated with Chase Bliss.

## Play it in a browser

Requires Node.js 20 or newer and a recent browser with AudioWorklet support.

```sh
npm ci
npm run dev
```

Open the URL printed by Vite. Choose **DEMO**, an audio **FILE**, or **LIVE IN**,
then press **START AUDIO**. Microphone access requires HTTPS or localhost and
browser permission. Use headphones with live input to avoid acoustic feedback.
The demo is generated locally; selected files and microphone audio stay in your
browser. For a production build, run `npm run build`.

Drag a knob vertically, use the mouse wheel or arrow keys, or double-click to
reset it. Tap **WET CHANNEL** to turn the effect on/off; hold it to freeze, or
throw the **FREEZE** switch under the hood to have the freeze arrive without a
held foot — the wet side comes up held and rings on its own until you let it go.
The looper is always listening: tap **MICRO-LOOPER** to capture the recent past,
tap again to stop, or hold while playing to overdub. Holding while listening
records a new loop for the duration of the hold, up to the buffer capacity.
The **LOOP TRIM** row under the waveform sets how much past the looper may keep
(**MEMORY**, up to 30 s; `PEDAL` is the hardware behaviour) and how much of the
captured loop plays (**END**). Where the loop starts is **LENGTH**'s job. The
waveform dims whatever the trim has left outside.
The A and L keys trigger the wet and loop switches when focus is not in another
control. **HOW TO PLAY** opens the in-app field guide.

Five factory patches set the controls; **SAVE PATCH** stores one additional
patch in local browser storage. A patch does not automatically engage either
channel. Captured audio is temporary and is not saved in a patch.

## Native plugin

Build it yourself from this checkout; that is the most reliable route, because the
result matches your machine and your macOS version exactly.

### Get the source

```sh
cd ~/Documents/AI
git clone -b arena/7dccc818-brood https://github.com/Jackherz/Brood.git Brood-Source
cd Brood-Source
open -a "Visual Studio Code" .
```

`open -a` is used because VS Code's `code` command is only on the `PATH` after
`⌘⇧P > Shell Command: Install 'code' command in PATH`; both open the same thing.

The `-b` selects the branch containing the working tree; a plain `git clone` gives
the upstream skeleton only.

**Do not use a `.patch` file for this.** A patch cannot carry binary files: the
transport drops the bytes and `git apply` stops with

```text
error: cannot apply binary patch to 'assets/fonts/VT323-Regular.ttf' without full index line
```

That is why the fonts are downloaded by `scripts/fetch-fonts.sh` at build time
rather than committed — `assets/fonts/*.ttf` is in `.gitignore`. Delete any old
patch so it cannot be applied by mistake: `rm -f ~/Documents/AI/Brood-Mood/*.patch`

### Install it from VS Code

1. **Install the tools once.** In Terminal:
   ```sh
   xcode-select --install          # Apple's compiler and SDK
   brew install cmake              # CMake 3.22 or newer
   ```
2. **Open the folder** — `File > Open Folder…`, choose `Brood-Source`, and trust
   it when VS Code asks (workspace trust is required to run tasks).
3. **Install the suggested extensions** — *C/C++* and *CMake Tools*. Optional:
   they add error squiggles and C++ debugging. Nothing else is needed, and no
   C#/.NET anything.
4. **Press ⌘⇧B.** The default build task is `Brood Mood: build + install`. It
   configures CMake, compiles, ad-hoc signs, copies the bundles into
   `~/Library/Audio/Plug-Ins/`, and runs `auval`. The first run downloads JUCE
   8.0.9 (about 100 MB) into `native/build/_deps`, then compiles for several
   minutes; later runs are incremental.
5. **Restart and rescan your DAW.** Quit Ableton Live completely, reopen it, then
   `Settings > Plug-Ins > Rescan` while holding Option. The effect appears under
   *Plug-Ins > VST3 > Fx* (search "Brood").

| Format | Installed to |
| --- | --- |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Brood Mood.vst3` |
| Audio Unit | `~/Library/Audio/Plug-Ins/Components/Brood Mood.component` |
| Standalone | left in `native/build/plugin/MoodEmu_artefacts/Release/Standalone/` |

Because you build locally, the bundle links against your own SDK and is never
quarantined, so no Gatekeeper prompt appears. Ad-hoc signing is automatic; set
`DEVELOPMENT_TEAM`/`CODE_SIGN_IDENTITY` yourself only if you intend to
distribute the build.

Everything else lives under `Terminal > Run Task…`:

| Task | What it does |
| --- | --- |
| `build + install` (⌘⇧B) | the one you want |
| `clean build + install` | same, after deleting `native/build` — use it after an interrupted build |
| `doctor (DAW cannot see it?)` | full health report, changes nothing |
| `build universal + install` | one bundle with arm64 + x86_64 |
| `open the standalone app` | build and play without a DAW |
| `run tests` | native DSP tests + JavaScript suite |
| `start the browser version` | the WebAudio version in a browser |

`F5` builds and starts the standalone app under the debugger.

### "Do I need an Apple Developer account?"

No. A paid Apple Developer Program membership (99 USD/year) is for **notarising**
a copy you distribute to other people. Building and running a plug-in on your own
Mac needs none of it, and this project never asks you to sign in.

macOS does require *some* valid signature, for a different reason: on Apple
Silicon every executable must carry at least an ad-hoc signature, and a DAW
refuses to load a bundle whose signature does not match its contents. Ad-hoc
signing (`codesign --sign -`) is local, free and anonymous.

`scripts/build-plugin.sh` already does this, immediately after building:

```text
==> Ad-hoc signing the bundles (no Apple Developer account needed)
    signed Brood Mood.vst3
    verified Brood Mood.vst3
```

Two situations that are often mistaken for "I need an Apple account":

- **Gatekeeper** — a bundle that arrived inside a ZIP or download carries the
  `com.apple.quarantine` flag and is blocked. Files this script copies itself are
  de-quarantined; for anything else, run
  `xattr -dr com.apple.quarantine "~/Library/Audio/Plug-Ins/VST3/Brood Mood.vst3"`.
- **A stale signature** — if you replaced files inside an installed bundle by
  hand, re-sign it: `codesign --force --sign - "<bundle>"`. `make doctor` detects
  this and `bash scripts/diagnose-plugin.sh --fix` repairs it.

### If Ableton Live (or another DAW) does not show the plug-in

Run the health check first; it inspects the build, every standard plug-in
folder, the code signature, `auval`, and Live's own `Log.txt` and database:

```sh
make doctor          # short form; runs scripts/diagnose-plugin.sh
```

Work through its FAIL lines, then these:

1. **Confirm the install actually happened.** Live reads
   `~/Library/Audio/Plug-Ins/VST3/` and `/Library/Audio/Plug-Ins/VST3/`. If
   neither contains `Brood Mood.vst3`, run `make install` and read the first
   error, not the last line.
2. **Force a full rescan.** Quit Live completely, reopen it, then
   `Settings > Plug-Ins > Rescan` while holding **Option** — a normal rescan
   skips files it already rejected. In Live 12, failed plug-ins appear in the
   browser under *Plug-Ins* with a warning; the reason is in
   `~/Library/Preferences/Ableton/Live <version>/Log.txt`.
3. **Check Live's VST3 folder setting.** In `Settings > Plug-Ins`, if *Use VST3
   Plug-In Custom Folder* is on, Live scans only that custom folder — turn it
   off (system folders) or point it at the folder holding the bundle.
4. **Match the architecture.** Live running under Rosetta
   (*Get Info > Open using Rosetta*) cannot load an arm64-only plug-in, and an
   arm64 Live cannot load an x86_64-only one. Build once with
   `bash scripts/build-plugin.sh --install --universal` to cover both.
5. **Look in the right place.** It is an effect: *Browser > Plug-Ins > VST3 >
   Fx*, or just type "Brood" into the browser search. Live's Audio Unit scan
   may also list `Brood Mood` under *Audio Units*.

### If VS Code shows a C# Dev Kit / .NET error

This project contains no C# and needs no .NET SDK. If a notification says
*"Install the missing components in the .NET folder…"* or *"C# Dev Kit will not
change your .NET installation"*, that is the **C# Dev Kit** extension starting in
this window and failing to find a .NET runtime. It is unrelated to Brood Mood and
does not block the build tasks.

- **You do not use C# in VS Code:** open the Extensions view, find *C# Dev Kit*
  (plus *C#* and *.NET Install Tool*), and choose **Disable (Workspace)**. The
  warning stops without affecting other projects.
- **You do use C# elsewhere:** install an SDK with `brew install --cask
  dotnet-sdk`, then quit VS Code and reopen it by running `code .` from that
  terminal so the new `dotnet` on your `PATH` is picked up — the notification's
  own advice, because *Reload Window* keeps the old environment.

Either way, `⌘⇧B` still builds the plugin.

### Build from the command line

The same thing without VS Code:

```sh
make install          # build, install into your user folders, run auval
make build            # build only
make universal        # one bundle with arm64 + x86_64 slices, then install
make doctor           # why is the plug-in not showing up?
make run              # build and open the standalone app
make test             # native DSP tests, then the JavaScript suite
make check            # repository self-check (no binaries, scripts parse, JSON valid)
make clean            # remove native/build
```

Those wrap the script; the equivalent explicit form is:

```sh
bash scripts/build-plugin.sh                 # build
bash scripts/build-plugin.sh --install       # build, install, and run auval
bash scripts/build-plugin.sh --install --universal
bash scripts/build-plugin.sh --clean --juce /path/to/JUCE
bash scripts/build-plugin.sh --test --install
```

Run it with `bash` so it works even if the executable bit was lost in transit.
The script needs Bash 3.2 or newer, which ships with macOS and every mainstream
Linux.

`--help` lists the options; arguments after `--` are passed to CMake verbatim.
The script needs CMake 3.22+, Git and a C++17 compiler, and on Linux it also
needs [JUCE's Linux dependencies](https://github.com/juce-framework/JUCE/blob/8.0.9/docs/Linux%20Dependencies.md).
To drive CMake by hand:

```sh
cmake -S native -B native/build -DMOOD_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --config Release --target MoodEmu_VST3
```

The bundle appears under `native/build/plugin/MoodEmu_artefacts/Release/VST3/`.
Targets: `MoodEmu_VST3`, `MoodEmu_Standalone`, and on macOS `MoodEmu_AU`. Set
`-DBROOD_BUILD_PLUGIN_TESTS=ON` for the headless processor tests and
`-DBROOD_BUILD_VST3_HOST_TESTS=ON` for the VST3 hosting smoke test.

### Prebuilt experiments (removed)

Earlier versions of this project shipped cross-compiled bundles
(`Brood-Mood-macOS-arm64-Experimental.zip`). They were arm64-only, had no
`moduleinfo.json`, were never opened in a DAW, and could not be transported
through the patch channel at all — the download button and the ZIP are gone.
Build locally with the steps above; that is the supported route.

Resting the mouse on any switch, stepper, slider or button that has no
explanation of its own makes the readout at the top of the side column explain it
— the six knobs are left out on purpose, since they already whisper. The browser
version says the same thing in its footer line.

The editor is a 900 × 712 panel that can be resized from 675 × 534 up to
1800 × 1280; the layout is drawn in design coordinates and scaled as a whole, so
the proportions never break and labels cannot collide when the window changes.
Everything is on that one page — pedal, side column and the row of options under
the hood — because a second page is a menu, and the pedal this follows has none. The
plugin normally processes the host track's input. The editor also offers a
generated demo and local file input. Plugin settings and wet on/off state are
stored in the DAW project; recorded loops, effect tails and loaded file audio are
not. The file source returns to live input after a project reload. The native
patch menu has the same five factory patches; use your DAW for user presets.

### MIDI

The native plugin accepts MIDI CC on any channel when the host routes MIDI to
the effect. Changes are applied at audio-block boundaries:

| CC | Control |
| --- | --- |
| 14–19 | Time, Mix, Length, wet Modify, Clock, loop Modify |
| 20 | Ramp speed (the pedal's own automation target) |
| 66, 67 | Bounce, Sweep (`0` off, `1–127` on) |
| 53, 54 | Wet and loop clock division |
| 71 | Classic mode (`0` off, `1–127` on) |
| 21–23 | Wet mode, routing, loop mode (`0–1` first, `2` second, `3–127` third) |
| 73–76, 78 | Spread, Dry Kill, Trails, Latch, Smooth (`0` off, `1–127` on) |
| 102, 103 | Loop and wet on/off (`0` off, `1–127` on) |
| 105 | Wet freeze — same as the FREEZE switch (`0` off, `1–127` on) |

CC 21 keeps the pedal's three-mode mapping exactly, so a hardware MOOD controller
never reaches the fourth mode by accident: **WEAVE** is selected in the editor or
by automating the `wetMode` parameter (value 3) instead. CC 20 is ramp speed and
66/67 are the two ramp dips, as on the pedal. The ramp destinations (RAMPS wet and
loop), the shape and polarity have no CC in the MOOD map — they live in the hidden
menu there — so automate the `rampWet`, `rampLoop`, `rampWave` and `rampUni`
parameters instead. Loop memory and END are host-only parameters too.

Tempo sync has no CC in the MOOD map; automate the `sync` parameter, or switch
it on in the editor.

The browser version does not receive MIDI. MIDI clock sync, expression/CV, synth
mode, MISO, true bypass, the direct micro-loop blend and the remaining hardware
DIP switches are not implemented. Automatable native parameters are
available to the host; the wet/loop footswitch parameters are momentary controls.

## The whispers

Every knob carries a sentence rather than a label. It should tell you which way to
turn without telling you what is inside — the mechanism stays a little hidden, the
consequence does not. The exact value is shown beside the knob as you move it, and
the field guide in the browser version lists the current mode's set.

| Knob | Reverb | Delay | Slip | Weave |
| --- | --- | --- | --- | --- |
| TIME | how long the room keeps breathing | how far back the echo remembers | how wide a slice of time is borrowed | how far back the threads reach |
| MODIFY | how much the surface smears | how much the echo wants to stay | which way, and how fast, time slides | long cloth, or short clipped stitches |

| Knob | Env | Tape | Stretch |
| --- | --- | --- | --- |
| LENGTH | how much of the moment gets caught | the size of the window that repeats | how wide each grain is stretched |
| MODIFY | how softly it must speak to be caught | which way the tape runs, and how fast | how far the grains drift from now |

Shared: **MIX** — *how much of what you hear is not here*; **CLOCK** — *the
heartbeat everything obeys*; **ROUTING** — *what the wet side is allowed to hear*.

## Sound model

- Wet modes: **Reverb**, **Delay**, **Slip** and **Weave**; loop modes: **Env**,
  **Tape** and **Stretch**. Three-way routing selects input, input + loop, or loop for the
  wet side. When the loop is listening, its recent history includes active wet
  audio; overdubs use the clean input.
- **WEAVE** is an addition, not a MOOD mode: a re-sampler in the spirit of the
  [Fieldtone Weaver](https://fieldtone.co). Three threads read a three-second
  memory at once, each with its own pitch (drawn from a root-sympathetic set of
  intervals), direction, grain size and dropout. TIME sets how far back the
  threads may reach; MODIFY walks the fabric from long overlapping cloth
  (texture) into short clipped stitches (rhythm), adding holes as it goes. The
  pattern is re-rolled from a seeded generator whenever the integer position of
  TIME or MODIFY changes, so a given knob position always draws the same fabric
  — chance you can return to, and no undo. Weaver's fixed capture window,
  Texture/Rhythm switch, root-aligned pitch set and per-element levels are the
  reference; its microphone, trigger outputs and CV are not modelled.
- **TEMPO SYNC** (the `SYNC` switch) turns the delay's TIME knob into a musical
  division: 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D, 1/2, 1/1. In the
  plugin the tempo comes from the DAW's playhead, every block, so tempo changes
  are followed; in the browser you set it on the TEMPO field. The division is
  real time, converted into core ticks at the current clock, so the echo stays on
  the beat when CLOCK moves. The readout shows the division and its milliseconds
  (a whole note at 120 BPM is 2 s, which the 2.1 s delay buffer clamps to 1.995 s).
- CLOCK selects 2–64 kHz steps, changing pitch, timing, bandwidth and loop
  capacity together. **SMOOTH** makes the clock continuous. The fixed 64,000
  sample history holds approximately 1–32 seconds depending on clock rate.
- **LOOP MEMORY** and **END** are our addition. On the pedal the capture window is
  whatever the 64,000-sample buffer holds at the current clock; MEMORY lifts that
  ceiling to a chosen length in *real time* (1–30 s, and the clock is clamped up
  if it could not fill the window). At the far left it reads `PEDAL` and behaves
  exactly like the hardware. END then sets how much of that window sounds before
  it wraps: it only ever moves the loop's *end*. The start point belongs to
  LENGTH — in TAPE the loop window is the most recent part of the capture, so
  turning LENGTH walks the start forward, exactly as the pedal does. At `1` the
  window is the whole take, so LENGTH reaches the very beginning of a 30 s
  capture; the travel is linear in the knob (each quarter turn moves the start a
  quarter of the way in), which is what makes a long take usable rather than a
  long wait on the last few seconds. (An earlier version had a separate START
  trim, which meant END silently dragged the start around with it; it is gone.) The waveform display dims everything outside the
  window. Both cores allocate for the longest possible loop up front (about
  32 MB), so MEMORY never reallocates on the audio thread.
- **WET / LOOP** is a crossfade, not two independent gains, and the loop's direct
  path is only ducked by as much as the wet path is audible. Earlier versions
  hard-muted the direct loop whenever the wet channel was on with routing BOTH or
  LOOP, so a balance fully to LOOP went silent; that is fixed and covered by
  tests in both implementations.
- **Under the hood** is the row along the bottom of the editor (browser: the
  block at the foot of the side panel): the options the pedal keeps behind both
  footswitches, with CLASSIC beside them in the browser's switch row. **TONE** blends the wet bus towards a 2.6 kHz hi-cut — fully right is
  bit-transparent, so it can stay open. **FADE** sets how much of the old loop
  survives each overdub pass: 0 is the pedal's slow erosion, 1 lets the new layer
  replace it entirely. **WIDTH** is mid/side on the effect bus only (the dry never
  moves); 0.5 is the image as built. **WET DIV** and **LOOP DIV** run one side on
  a divided clock — 2× or 4× slower — so the wet side's room and echo stretch out
  while the loop drops an octave or two, or the other way round. **CLASSIC**
  restores MK I manners: a noisy clock that hisses most at low settings, a loop
  that never sits still (wow and flutter), and a reverb that rings far longer.
- **THE RAMPS** are the pedal's function generators, one per channel, and the
  reason it is more than four effects. Each side has a destination — TIME, MIX,
  LENGTH, MODIFY (that channel's own modify knob), CLOCK or BALANCE — and a
  generator writes into it, added on top of where the knob sits and rebuilt from
  the knob every sample. Five shapes (triangle, square, sine, random, smooth
  random), one rate for both sides, from one cycle every twenty seconds up to
  eight a second. **UNI** keeps the swing above the knob instead of either side
  of it; **BOUNCE** reflects the phase so it never snaps back; **SWEEP** drops the
  shape and climbs, restarting from the bottom. In the editor the two destination
  cells step with their arrows, the rate is the RATE slider and the three
  switches sit in the same panel — on the pedal these are the dips, so the editor
  is the comfortable place to reach them.
- **TRAILS**, **DRY KILL**, **LATCH**, **SPREAD**, **SYNC** and wet/loop balance
  are modelled. Stereo input is kept separate with SPREAD off. The exact stereo
  movement, filters, transient response, control curves, reverb and grains differ
  from the hardware. Deliberate limiting when effects are active and resampling
  also affect the sound.

## Tests

```sh
npm test                            # JS DSP tests
cmake -S native -B native/build -DMOOD_BUILD_PLUGIN=ON \
  -DBROOD_BUILD_PLUGIN_TESTS=ON -DBROOD_BUILD_VST3_HOST_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --config Release --target dsp_tests mood_render plugin_tests vst3_host_tests
ctest --test-dir native/build -C Release --output-on-failure
node tests/parity/compare.js native/build/mood_render   # adjust path for multi-config builds
```

For optional browser end-to-end tests, run the Vite dev server in one terminal,
then point to an installed Chrome/Chromium executable in another:

```sh
CHROMIUM_PATH=/path/to/chromium npm run test:browser
```

The tests use generated WAV fixtures and a simulated microphone. The browser
smoke test expects the Vite **development** server at `http://127.0.0.1:5173/`;
set `BROOD_TEST_URL` if it uses a different address. These tests are not a
substitute for listening and testing in your own DAW.

## Licences and credits

Project code is GPLv3, including portions adapted from MoodEmu. JUCE 8 has
separate AGPLv3/commercial licensing, and the bundled fonts use the SIL Open
Font License. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution and details.
