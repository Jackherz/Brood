# Brood

An unofficial, playable, pedal-inspired stereo micro-looper and spatial-effects
plugin, with a black-and-white pixel-art interface. It includes a JUCE VST3
plugin and a browser version that runs the same DSP. There is also source for
JUCE standalone and macOS AU targets.

New here? [INSTALL.md](INSTALL.md) is the three-command version of the macOS
install; this README has the detail.

**This is an independent approximation, not the original pedal firmware or a
bit-exact emulation.** Chase Bliss's public firmware repository distributes
binary firmware rather than the pedal's DSP source. The algorithms and parameter
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
throw the **FREEZE** switch to have the freeze arrive without a
held foot — the wet side comes up held and rings on its own until you let it go.
The looper is always listening: tap **MICRO-LOOPER** to capture the recent past,
tap again to stop, or hold while playing to overdub. Holding while listening
records a new loop for the duration of the hold, up to the buffer capacity.
The **LOOP TRIM** row under the waveform sets how much past the looper may keep
(**MEMORY**, up to 30 s; `PEDAL` is the hardware behaviour), where the window
stops (**END**) and where it begins (**START**). END is an end marker and START a
start marker: each moves itself, so the window can be trimmed from either side.
The waveform dims whatever the trim has left outside. (In the plug-in, START and
END are markers you drag on the waveform itself; see below.)
The A and L keys trigger the wet and loop switches when focus is not in another
control. **HOW TO PLAY** opens the in-app field guide.

Five factory presets set the controls. Click the name in the patch box for the preset
browser: search, categories and stars, the same as the plug-in's. **SAVE** stores what
you hear under a name in local browser storage (the plug-in keeps its presets as files). A patch does not automatically engage either
channel. Captured audio is temporary and is not saved in a patch.

## Native plugin

Build it yourself from this checkout; that is the most reliable route, because the
result matches your machine and your macOS version exactly.

### Get the source

```sh
cd ~/Documents/AI
git clone https://github.com/Jackherz/Brood.git Brood-Source
cd Brood-Source
open -a "Visual Studio Code" .
```

`open -a` is used because VS Code's `code` command is only on the `PATH` after
`⌘⇧P > Shell Command: Install 'code' command in PATH`; both open the same thing.
Everything lives on `main`.

**Do not use a `.patch` file for this.** A patch cannot carry binary files: the
transport drops the bytes and `git apply` stops with

```text
error: cannot apply binary patch to 'assets/fonts/VT323-Regular.ttf' without full index line
```

That is why the fonts are downloaded by `scripts/fetch-fonts.sh` at build time
rather than committed — `assets/fonts/*.ttf` is in `.gitignore`. Delete any old
patch so it cannot be applied by mistake: `rm -f ~/Documents/AI/Brood/*.patch`

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
4. **Press ⌘⇧B.** The default build task is `Brood: build + install`. It
   configures CMake, compiles, ad-hoc signs, copies the bundles into
   `~/Library/Audio/Plug-Ins/`, and runs `auval`. The first run downloads JUCE
   8.0.9 (about 100 MB) into `native/build/_deps`, then compiles for several
   minutes; later runs are incremental.
5. **Restart and rescan your DAW.** Quit Ableton Live completely, reopen it, then
   `Settings > Plug-Ins > Rescan` while holding Option. The effect appears under
   *Plug-Ins > VST3 > Fx* (search "Brood").

| Format | Installed to |
| --- | --- |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Brood.vst3` |
| Audio Unit | `~/Library/Audio/Plug-Ins/Components/Brood.component` |
| Standalone | left in `native/build/plugin/Brood_artefacts/Release/Standalone/` |

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
    signed Brood.vst3
    verified Brood.vst3
```

Two situations that are often mistaken for "I need an Apple account":

- **Gatekeeper** — a bundle that arrived inside a ZIP or download carries the
  `com.apple.quarantine` flag and is blocked. Files this script copies itself are
  de-quarantined; for anything else, run
  `xattr -dr com.apple.quarantine "~/Library/Audio/Plug-Ins/VST3/Brood.vst3"`.
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
   neither contains `Brood.vst3`, run `make install` and read the first
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
   may also list `Brood` under *Audio Units*.

### If VS Code shows a C# Dev Kit / .NET error

This project contains no C# and needs no .NET SDK. If a notification says
*"Install the missing components in the .NET folder…"* or *"C# Dev Kit will not
change your .NET installation"*, that is the **C# Dev Kit** extension starting in
this window and failing to find a .NET runtime. It is unrelated to Brood and
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
cmake -S native -B native/build -DBROOD_BUILD_PLUGIN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --config Release --target Brood_VST3
```

The bundle appears under `native/build/plugin/Brood_artefacts/Release/VST3/`.
Targets: `Brood_VST3`, `Brood_Standalone`, and on macOS `Brood_AU`. Set
`-DBROOD_BUILD_PLUGIN_TESTS=ON` for the headless processor tests and
`-DBROOD_BUILD_VST3_HOST_TESTS=ON` for the VST3 hosting smoke test.

### Prebuilt experiments (removed)

Earlier versions of this project shipped cross-compiled bundles
(`Brood-macOS-arm64-Experimental.zip`). They were arm64-only, had no
`moduleinfo.json`, were never opened in a DAW, and could not be transported
through the patch channel at all — the download button and the ZIP are gone.
Build locally with the steps above; that is the supported route.

Resting the mouse on any switch, stepper, slider or button that has no
explanation of its own makes the readout at the top of the side column explain it
— the six knobs are left out on purpose, since they already whisper. The browser
version says the same thing in its footer line.

The editor is a 1200 × 820 panel that can be resized from 840 × 574 up to
2000 × 1367; the layout is drawn in design coordinates and scaled as a whole —
the controls included, so the text inside a field or a cell grows with the window
instead of staying at its pixel size — and labels cannot collide when the window
changes. Everything is on that one page, because a second page is a menu, and the
pedal this follows has none. It reads in three tiers:

1. **The pedal** — the patch in the masthead, the six knobs under three column
   names (WET CHANNEL, SHARED, MICRO-LOOPER), the mode selectors and both
   footswitches.
2. **The side column** — the readout, then four numbered panels in the order you
   reach for them while playing: `01 LOOP` (the waveform with its START and END
   markers, MEMORY, WET/LOOP), `02 MODULATION`, `03 SWITCHES` (TRAILS, SMOOTH,
   FREEZE, CLASSIC) and `04 INPUT`, which is set once per session.
3. **Under the hood** — the five sliders along the bottom.

**START and END are markers on the waveform.** Press anywhere on it and the nearer
marker comes to the mouse; drag it. Moving START keeps the end where it is, END
moves only itself, and a double-click opens the whole take again. Each marker is a
full-height rule with a tab naming it — START's hangs from the top, END's from the
bottom, so they stay readable even when the window is a sliver.

**Values sit where you are looking.** Resting on or turning a knob replaces its
whisper with the exact value, in ink; the modulation rate prints its period (or,
synced, its span of the beat) at the end of its row; MEMORY, WET/LOOP and the hood
sliders carry their figures already. The knobs have no ring: five marks are the
scale, and they turn to ink on hover. Each tempo **SYNC** switch sits directly
under the knob it governs, and only in the modes that can use it.

`native/build/plugin/editor_snapshot out.png [scale] [loop] [id=value ...]` (built
with `-DBROOD_BUILD_PLUGIN_TESTS=ON`) renders the editor to a PNG without a DAW;
`loop` catches a few seconds of the demo first so the waveform has a take.

The plugin normally processes the host track's input. The editor also offers a
generated demo and local file input. Plugin settings, wet on/off state **and the
recorded loops** are stored in the DAW project — the two takes travel with the
state as gzipped 16-bit PCM and are swapped back in on the audio thread after a
reload, so a bounce or a reopened session plays the loop you were hearing rather
than an empty buffer. Effect tails and loaded file audio are not saved. The file
source returns to live input after a project reload. The patch box in the masthead
has the same five factory presets as the browser, and a factory preset is a whole
state: every control goes back to its default before it sets its own. Click the name
for the preset browser, the same one every plug-in in the range uses: search, category
keys and stars; clicking a preset plays it, KEEP (Return) keeps it, CANCEL (Esc) puts
back the sound you had, and right-clicking a user preset renames, files or removes it.
User presets are `.broodpreset` files in `~/Library/Audio/Presets/Brood/` on macOS, so
every project and every instance shares them. The four user slots from round 18 become
presets called User 1 to User 4 the first time this version opens. A preset never
engages a channel.

**The Bench** along the bottom passes a moment between plug-ins, either way: SEND writes
what the loop is playing to Brood's own folder (`~/Music/Bench/Brood/` until you choose another), an instrument in the range (Silt) shows it
and takes it as its sample, and TAKE makes what Silt sent the loop here. FOLDER opens it in
the Finder, chooses another folder, or picks a category, which is a folder inside it (make a
Guitar one, and what you send goes there until you change it). Each plug-in has its own
folder and categories, sending between them works whichever you choose, and everything sent
stays there. The menu beside SEND can send the
exact output instead: 4, 8 or 16 bars from the next bar line (at once if the host is
stopped). Silt keeps the first 30 seconds of anything longer.

Sets saved under the old product name still open: the plug-in IDs did not change,
and the old state tag is read as the new one.

### MIDI

The native plugin accepts MIDI CC on any channel when the host routes MIDI to
the effect. Changes are applied at audio-block boundaries:

| CC | Control |
| --- | --- |
| 14–19 | Time, Mix, Length, wet Modify, Clock, loop Modify |
| 20 | Rate of both modulation sources |
| 52 | Hold both modulation sources (`0` running, `1–127` held) |
| 66, 67 | Bounce, Sweep (`0` off, `1–127` on) |
| 53, 54 | Wet and loop clock division |
| 71 | Classic mode (`0` off, `1–127` on) |
| 21–23 | Wet mode, routing, loop mode (`0–1` first, `2` second, `3–127` third) |
| 75, 78 | Trails, Smooth (`0` off, `1–127` on) |
| 102, 103 | Loop and wet on/off (`0` off, `1–127` on) |
| 105 | Wet freeze — same as the FREEZE switch (`0` off, `1–127` on) |

CC 21 keeps the pedal's three-mode mapping exactly, so a hardware controller set up for the pedal
never reaches the fourth mode by accident: **WEAVE** is selected in the editor or
by automating the `wetMode` parameter (value 3) instead. CC 20 is the sources' rate and
66/67 are BOUNCE and SWEEP, on both sources. CC 73, 74 and 76 used to carry
SPREAD, DRY KILL and LATCH; those controls are gone (WIDTH does the first, MIX the
second, the FREEZE switch the third), so the pedal's controllers for them do
nothing now. Routes and depths have no CC: automate the `mod1_time`-style depth parameters instead. Loop memory and END are host-only parameters too.

Tempo sync has no CC in the pedal's map; automate the `sync` parameter, or switch
it on in the editor.

### The sheet

Both panels are drawn to one small system, the way a product styleguide works:
spacing snaps to 4 / 8 / 16 / 24 / 32, type to five sizes, and elevation is
always a hard offset — 1 px for a control, 2 px for a panel, 3 px for the
readout — never a blur. Rules carry the structure: ink for frames and controls,
hairline grey for grouping. The only textures are a 2 px halftone band on every
panel head and a 20 px dot grid under the paper, both built from the paper's own
colour, so the cream-and-ink palette is untouched.

That system is read the way a shipping manifest is read, because the machine is
one: every panel opens with a numbered block, its name, a dotted leader and a
figure (`02 INPUT SOURCE ···· 2 IN / 2 OUT`); every knob carries a number beside
its name; the two level meters are ten countable cells rather than a bar; the
readout is an alert box with a status chip, a hard frame and a blinking block
where a cursor would be; the switches sit as three boxed cells; and the title bar
ends with what the host is actually running us at (`48.0 kHz / 2 IN`). Small
registration marks sit in the margins, and `ONE CLOCK. TWO WORLDS.` is set as a
caption with a leader either side.

The knobs are flat discs with a hairline rim and a thin pointer, five marks around
the outside and a hard offset shadow. The wet channel's two knobs are black, the
shared and loop knobs are pale, the same way a pedal marks the primary control.
Nothing is rounded, nothing is dashed, and nothing is a tint of a colour that is
not in the palette.

The browser version does not receive MIDI. MIDI clock sync, expression/CV, synth
mode, MISO, true bypass and the remaining hardware
DIP switches are not implemented. Automatable native parameters are
available to the host; the wet/loop footswitch parameters are momentary controls.

## The whispers

Every knob carries a sentence rather than a label. It should tell you which way to
turn without telling you what is inside — the mechanism stays a little hidden, the
consequence does not. The exact value replaces it while you rest on or turn the knob, and
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

- **Getting in and out of the core clock.** The engine runs at its own clock (the
  CLOCK knob), so every host sample is resampled on the way in and on the way out.
  On the way in each core tick reads the input at its own moment on a Hermite
  curve — until round 18 it took the latest host sample, which at 48 kHz repeated
  one sample in four and recorded a stair-step jitter into every loop. On the way
  out a Hermite curve runs through the last four ticks, and both sides are filtered
  by 6th-order Butterworth low-passes at 0.45 × the clock (they were 4th order).
  Every delay line, grain and loop read is Hermite-interpolated too. **CLASSIC**
  keeps the old path — held input, straight-line output, 4th-order filters — as
  part of its grit. The output limiter now bends from 0.7 instead of 0.9, to the
  same ceiling, so loud tails are rounded rather than flattened.
- **Stereo.** Each engine is mono in and stereo out, and there is one per input
  channel. The left engine supplies the left output and the right engine the right
  one — on a mono track the two engines are identical, so what you hear is one
  engine's full stereo: the reverb's two sides, WEAVE's panned threads and WIDTH.
  (Until round 17 both outputs came from the engines' *left* sides, which made all
  of that dual mono on any mono source.)
- Wet modes: **Reverb**, **Delay**, **Slip** and **Weave**; loop modes: **Env**,
  **Tape** and **Stretch**. Three-way routing selects input, input + loop, or loop for the
  wet side. When the loop is listening, its recent history includes active wet
  audio; overdubs use the clean input.
- **WEAVE** is an addition, not one of the pedal's modes: a re-sampler in the spirit of the
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
- **TEMPO SYNC** is three switches, each under the knob it governs and each drawn
  only in the modes that can use it. `SYNC` under TIME turns the delay's TIME knob
  into a musical division — 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D, 1/2,
  1/1 — and does the same for WEAVE's reach; it is absent in REVERB and SLIP,
  which have nothing to put on the beat. `SYNC` under LENGTH (in ENV) makes the
  envelope's slice a division of the beat instead of a free length, and appears
  only in ENV. The modulation panel has its own SYNC for the rate (eight bars to a
  sixteenth). All three are independent: a synced delay can run against a free
  envelope. In the plugin the tempo comes from the DAW's playhead, every block, so
  tempo changes are followed; in the browser you set it on the TEMPO field. The
  division is real time, converted into core ticks at the current clock, so the
  echo stays on the beat when CLOCK moves. The readout shows the division and its
  milliseconds (a whole note at 120 BPM is 2 s, which the 2.1 s delay buffer clamps
  to 1.995 s).
- CLOCK selects 2–64 kHz steps, changing pitch, timing, bandwidth and loop
  capacity together. **SMOOTH** makes the clock continuous. The fixed 64,000
  sample history holds approximately 1–32 seconds depending on clock rate.
- **LOOP MEMORY** and **END** are our addition. On the pedal the capture window is
  whatever the 64,000-sample buffer holds at the current clock; MEMORY lifts that
  ceiling to a chosen length in *real time* (1–30 s, and the clock is clamped up
  if it could not fill the window). At the far left it reads `PEDAL` and behaves
  exactly like the hardware. LENGTH sets how big the window is — in TAPE it is the
  most recent part of the capture, so turning LENGTH walks the start forward, and
  at `1` the window is the whole take, reaching the very beginning of a 30 s
  capture; the travel is linear in the knob (each quarter turn moves the start a
  quarter of the way in), which is what makes a long take usable rather than a
  long wait on the last few seconds. **END** is an end marker: it says where the
  window stops playing, and dragging it moves the end marker and nothing else —
  the window grows or shrinks from the right while START stays where you put it.
  **START** says where that window begins inside the capture, which is what ENV and
  STRETCH were missing — with END alone you could only ever hear the front of a
  take. It is measured against the take rather than against the room END leaves,
  so the two markers do not drag each other around; START only gives ground when
  the window would otherwise run past the end of the take (and a window that fills
  the take cannot move at all, because there is nowhere to slide to). It reads `THE
  OLDEST` / `THE NEWEST` at zero and a percentage after that; in TAPE, which plays
  the newest audio, turning it *reaches back*. Moving it is **not** a jump: the read head crossfades from where the loop
  was playing to where it now begins over 25 ms, weighted by a raised cosine, and
  a knob still being dragged simply starts the next fade when the last one lands.
  A test sweeps the whole travel while the loop plays and checks the worst
  sample-to-sample step stays within the signal's own slope. The waveform display
  dims everything outside the window. Both cores allocate for the longest possible
  loop up front (about 32 MB), so MEMORY never reallocates on the audio thread.
- **SOFT** sits beside SYNC under LENGTH, in ENV only. The slices fade in and out over
  20 ms instead of 4, a new slice takes over from the last rather than jumping the read
  head, and a moving LENGTH glides, so nothing clicks. It's a rounder, less percussive
  stutter; off is the sharp cut the pedal makes. Both cores agree
  (`tests/parity/scenarios/env_soft.txt`).
- **BEFORE THE LOOP**, in the wet column, puts the wet side ahead of the looper: input,
  then wet, then loop. Catches and overdubs keep the wet sound, and the Bench's SEND
  carries it. It hears only the input then, so ROUTING stands aside. Off, the loop stays
  clean and the wet side plays over it (`tests/parity/scenarios/wet_pre.txt`).
- **GAIN** on the waveform is the loop's own level, -24 to +12 dB: drag up or down, Shift
  for fine, double-click for 0 dB. The waveform grows and shrinks with it, and SEND sends the loop at this level.
- **MODULATION** is two sources, each with its own rate, shape and UNI, BOUNCE, SWEEP,
  SYNC and HOLD. Drag a source onto any knob or WET/LOOP to route it. Around a knob, the rings are
  handles as in Silt: drag up or down to set that source's depth (negative inverts), and
  where there's no ring yet it makes one for the selected source. Alt-drag does the same, right-click a knob to remove a route,
  and ROUTES lists them all. CC 20, 52, 66 and 67 act on both sources
  (`tests/parity/scenarios/mod_routes.txt`).
- **ENV** slices when it hears the input: LENGTH and MODIFY act on those slices, so with
  the loop playing and nothing coming in above MODIFY's threshold, they have nothing to
  move. The knob says so while you touch it.
- **WET / LOOP** blends two complete worlds rather than fading one into the
  other: the loop keeps its own level across the whole knob and the wet side adds
  on top, so the middle hears both. It used to duck the direct loop against the
  wet level, which meant the middle of the travel was the wet side only — and in
  SLIP and WEAVE, which hand back little of the loop, the loop vanished until the
  knob was nearly at LOOP. The duck is gone; both cores agree bit for bit
  (`tests/parity/scenarios/balance.txt`) and the level is pinned by tests in both
  implementations.
- **The switches** are TRAILS, SMOOTH, FREEZE and CLASSIC, one row in the
  `03 SWITCHES` panel; FREEZE and CLASSIC came up from under the hood, which is now
  sliders only. SYNC left the panel in round 16 to sit with the knob it governs,
  so it can disappear in the modes that have no use for the beat. LATCH went
  because the FREEZE switch does the same job without having to hold anything (the
  one thing it does that nothing else now can: latching the loop's overdub hold,
  so a tap leaves you recording on top — that is a held gesture again). DRY KILL
  went because MIX at the top of its travel already removes the dry, and SPREAD
  went into WIDTH.
- **Under the hood** is the row along the bottom of the editor (browser: the
  block at the foot of the side panel): the options the pedal keeps behind both
  footswitches. **TONE** blends the wet bus towards a 2.6 kHz hi-cut — fully right is
  bit-transparent, so it can stay open. **FADE** sets how much of the old loop
  survives each overdub pass: 0 is the pedal's slow erosion (about 3 % a pass), 1
  lets the new layer replace it entirely. It only does anything *while you are
  overdubbing*, and overdubbing is the hold gesture: press and hold **MICRO-LOOPER**
  / the LOOP footswitch while a loop plays, and play over the top. Holding it while
  the looper is still listening records a new loop instead. **WIDTH** took over
  from the removed SPREAD switch: fully left is mono, 0.5 is the image as built,
  and past the middle a decorrelated copy of the wet bus is pushed out into the
  sides. It acts on the effect bus only — the dry never moves. **WET DIV** and **LOOP DIV** run one side on
  a divided clock — 2× or 4× slower — so the wet side's room and echo stretch out
  while the loop drops an octave or two, or the other way round. **CLASSIC**
  restores MK I manners: a noisy clock that hisses most at low settings, a loop
  that never sits still (wow and flutter), and a reverb that rings far longer.
- **MODULATION** (the pedal's ramps) is the pedal's function generators, one per channel, and the
  reason it is more than four effects. Each side has a destination — OFF, then
  TIME, MIX, LENGTH, MODIFY (that channel's own modify knob), CLOCK or BALANCE —
  and a generator writes into it, added on top of where the knob sits and rebuilt
  from the knob every sample. **OFF** is the first stop and the pedal's own name
  for it, so there is no separate "still" word to mistake for a setting: an OFF
  cell is drawn as an empty frame, a running one as a filled display. Five shapes (triangle, square, sine, random, smooth
  random), one rate for both sides, from one cycle every twenty seconds up to
  eight a second. **UNI** keeps the swing above the knob instead of either side
  of it; **BOUNCE** reflects the phase so it never snaps back; **SWEEP** drops the
  shape and climbs, restarting from the bottom. In the editor the two destination
  cells step from anywhere on the cell — only the painted back arrow steps back —
  and every cell offers its full list, so the readout and the hover text name
  exactly the stops that exist. The rate is the RATE slider and the three switches sit in the same panel — on the pedal these
  are the dips, so the editor is the comfortable place to reach them. Hovering a
  cell in the editor writes what pressing it will do into the readout.
- **TRAILS**, **SMOOTH**, the three tempo switches and wet/loop balance are
  modelled. WIDTH, the
  hidden stereo control, is where the pedal's SPREAD lives: fully left is mono,
  the middle is the image as built, and only past the middle does the sides open.
  The exact stereo movement, filters, transient response, control curves, reverb
  and grains differ from the hardware. Deliberate limiting when effects are active
  and resampling also affect the sound.

## Tests

```sh
npm test                            # JS DSP tests
cmake -S native -B native/build -DBROOD_BUILD_PLUGIN=ON \
  -DBROOD_BUILD_PLUGIN_TESTS=ON -DBROOD_BUILD_VST3_HOST_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --config Release --target dsp_tests brood_render plugin_tests vst3_host_tests
ctest --test-dir native/build -C Release --output-on-failure
node tests/parity/compare.js native/build/brood_render   # adjust path for multi-config builds
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

Project code is GPLv3, including portions adapted from MoodEmu (see THIRD_PARTY_NOTICES.md). JUCE 8 has
separate AGPLv3/commercial licensing, and the bundled fonts use the SIL Open
Font License. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for attribution and details.

**If this is ever sold, it is sold open source** — the source stays published under
GPLv3 and JUCE stays on its AGPLv3 path, so no commercial JUCE licence is needed. The
reasoning, and the closed-build fallback that would have to happen before the first
customer, are in [docs/OPEN-SOURCE.md](docs/OPEN-SOURCE.md).

## Where this could go

This is one pedal, emulated carefully. The direction now settled is a **series of
separate plug-ins in one repository** — one `juce_add_plugin` per pedal, sharing this
engine, the parity harness and the design sheet — because a pedal's panel is its own
shape, its memory should exist only in the instances that need it, and two MIDI maps
never have to share a device.

Nine candidates are researched. Reverse Mode C, Clean, Generation Loss MKII, Lossy,
Onward, Brothers AM, Big Time and Lost + Found (Chase Bliss), plus Hologram's
Microcosm, each with its front panel, the blocks it would force, what this codebase
already owns, an effort in rounds, and where it collides with the one-page rule — in
[docs/PEDAL-SERIES.md](docs/PEDAL-SERIES.md). The packaging decision and the three
pedals studied earlier — blooper, Habit and a third, grumpier one — are in
[docs/FUTURE-PEDALS.md](docs/FUTURE-PEDALS.md).

None of it is scheduled. The cheapest three are Reverse Mode C, Clean and Generation
Loss MKII, at roughly one round each because most of their blocks already exist here.
The open-source code those pedals would lean on — panel definitions, DSP vocabulary,
circuit models, granular engines — is catalogued with licences in
[docs/OPEN-SOURCE.md](docs/OPEN-SOURCE.md).
