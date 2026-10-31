# Brood Mood

An unofficial, playable MOOD MKII-inspired stereo micro-looper and spatial-effects
plugin, with a black-and-white pixel-art interface. It includes a JUCE VST3
plugin and a browser version that runs the same DSP. There is also source for
JUCE standalone and macOS AU targets.

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
reset it. Tap **WET CHANNEL** to turn the effect on/off; hold it to freeze.
The looper is always listening: tap **MICRO-LOOPER** to capture the recent past,
tap again to stop, or hold while playing to overdub. Holding while listening
records a new loop for the duration of the hold, up to the buffer capacity.
The A and L keys trigger the wet and loop switches when focus is not in another
control. **HOW TO PLAY** opens the in-app field guide.

Five factory patches set the controls; **SAVE PATCH** stores one additional
patch in local browser storage. A patch does not automatically engage either
channel. Captured audio is temporary and is not saved in a patch.

## Native plugin

Build it yourself from this checkout; that is the most reliable route, because the
result matches your machine and your macOS version exactly.

### Install it from VS Code

Once the source folder exists (see the step below), the whole install is one
keystroke.

1. **Open the folder** — `File > Open Folder…` and choose `Brood-Source`, or from
   a terminal inside it: `code .`
2. **Trust the folder** if VS Code asks (workspace trust is required to run
   tasks).
3. **Install the suggested extensions** when the notification appears — *C/C++*
   and *CMake Tools*. Optional: they give error squiggles and C++ debugging.
   Nothing else is required, and no C#/.NET anything.
4. **Press ⌘⇧B.** The default build task is `Brood Mood: build + install`; it
   configures CMake, compiles, ad-hoc signs, copies the bundles into
   `~/Library/Audio/Plug-Ins/`, and runs `auval`. First run downloads JUCE and
   takes several minutes; later runs are incremental.
5. **Quit Ableton Live completely and reopen it**, then
   `Settings > Plug-Ins > Rescan` while holding Option. The effect appears under
   *Plug-Ins > VST3 > Fx* (search "Brood").

Everything else lives in the same menu: `Terminal > Run Task…`

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

### Get the source

```sh
cd ~/Documents/AI
git clone -b arena/7dccc818-brood https://github.com/Jackherz/Brood.git Brood-Source
cd Brood-Source
open -a "Visual Studio Code" .
```

`open -a` is used because VS Code's `code` command is only on the `PATH` after
`⌘⇧P > Shell Command: Install 'code' command in PATH`; both open the same thing.

The `-b` selects the branch containing the working tree. A plain `git clone`
gives the upstream skeleton only.

**Do not use a `.patch` file for this.** A patch cannot carry binary files: the
transport drops the bytes and `git apply` stops with

```text
error: cannot apply binary patch to 'assets/fonts/VT323-Regular.ttf' without full index line
```

That is why the fonts are downloaded by `scripts/fetch-fonts.sh` at build time
instead of being committed — `assets/fonts/*.ttf` is in `.gitignore`. If you have
an old patch lying around, delete it so it cannot be applied by mistake:

```sh
rm -f ~/Documents/AI/Brood-Mood/*.patch
```

### Install it from VS Code

Once the source folder exists (see the step below), the whole install is one
keystroke.

1. **Open the folder** — `File > Open Folder…` and choose `Brood-Source`, or from
   a terminal inside it: `code .`
2. **Trust the folder** if VS Code asks (workspace trust is required to run
   tasks).
3. **Install the suggested extensions** when the notification appears — *C/C++*
   and *CMake Tools*. Optional: they give error squiggles and C++ debugging.
   Nothing else is required, and no C#/.NET anything.
4. **Press ⌘⇧B.** The default build task is `Brood Mood: build + install`; it
   configures CMake, compiles, ad-hoc signs, copies the bundles into
   `~/Library/Audio/Plug-Ins/`, and runs `auval`. First run downloads JUCE and
   takes several minutes; later runs are incremental.
5. **Quit Ableton Live completely and reopen it**, then
   `Settings > Plug-Ins > Rescan` while holding Option. The effect appears under
   *Plug-Ins > VST3 > Fx* (search "Brood").

Everything else lives in the same menu: `Terminal > Run Task…`

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

### First: turn the patch into a working checkout

The `.patch` file is a diff, not the project. A folder holding only
`01a1919c-….patch` has no `Makefile`, no `scripts/` and no source, so every
build command fails there. Restore the tree once, in a new folder:

```sh
cd ~/Documents/AI
git clone https://github.com/Jackherz/Brood.git Brood-Source
cd Brood-Source
git apply --whitespace=nowarn ../Brood-Mood/*.patch
ls                 # Makefile, native, scripts, src … now exist
```

Then build from inside `Brood-Source`.

**Use `git apply`, not `patch`.** The patch carries three binary font files, and
`patch` prints `git binary diffs are not supported` and skips them; the build then
fails because the UI embeds those fonts. If you already restored a tree that way,
re-run `git apply` on a fresh clone rather than trying to repair it.

If `git apply` reports that the patch does not apply, the clone has moved on;
check out the commit the patch was made against and retry:

```sh
git checkout bf2e30a83954ebbf7ae79e6768e84778b48ccde5
git apply --whitespace=nowarn ../Brood-Mood/*.patch
```

### Build and install in VS Code (macOS, recommended)

1. **Install the tools once.** In Terminal:
   ```sh
   xcode-select --install          # Apple's compiler and SDK
   brew install cmake              # CMake 3.22 or newer
   ```
   In VS Code, install the extensions it suggests (`ms-vscode.cpptools` and
   `ms-vscode.cmake-tools`); they are listed in `.vscode/extensions.json`.
2. **Open this folder** in VS Code (`File > Open Folder`).
3. **Run the build task.** Press `⌘⇧B`, or use `Terminal > Run Task…` and pick
   `Brood Mood: build + install`. The first run downloads JUCE 8.0.9 (about
   100 MB) into `native/build/_deps`, then compiles.
4. **Rescan your DAW.** Ableton Live: `Settings > Plug-Ins > Rescan`. REAPER:
   `Options > Preferences > Plug-ins > VST > Re-scan`. Quit and reopen the DAW
   if the plug-in still does not appear.
5. **Check it loaded.** The script runs `auval` automatically after installing,
   which instantiates the Audio Unit and catches load or signing problems. You
   can also play the DSP without any DAW: run the task
   `Brood Mood: open the standalone app`.

The task installs the whole bundles to:

| Format | Folder |
| --- | --- |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Brood Mood.vst3` |
| Audio Unit | `~/Library/Audio/Plug-Ins/Components/Brood Mood.component` |
| Standalone | left in `native/build/plugin/MoodEmu_artefacts/Release/Standalone/` |

Other VS Code tasks: `build only`, `build universal + install` (one bundle with
arm64 **and** x86_64 slices), `clean`, `run tests`, and `start the browser
version`. `F5` starts the standalone app under the debugger (see
`.vscode/launch.json`).

Because you build locally, macOS does not quarantine the bundle and it links
against your own SDK, so no `xattr` step and no Gatekeeper prompt are needed.
Set `DEVELOPMENT_TEAM`/`CODE_SIGN_IDENTITY` yourself only if you intend to
distribute the build.

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

Run it with `bash` so it works even if the executable bit was lost in transit
(an archive or patch can drop it). `chmod +x scripts/build-plugin.sh` also lets
you use `./scripts/build-plugin.sh`. The script needs Bash 3.2 or newer, which
ships with macOS and every mainstream Linux.

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

### Prebuilt experiments

`Brood-Mood-macOS-arm64-Experimental.zip` and
`Brood-Mood-Linux-x86_64-Experimental.zip`, if provided with this checkout, are
cross-compiled bundles for a quick try. The macOS one is arm64 only and has no
`moduleinfo.json`; the Linux one needs glibc 2.35+, `GLIBCXX_3.4.30`, FreeType,
Fontconfig and X11. Neither has been opened in a DAW on the target platform, and
neither is a substitute for building locally.

The plugin normally processes the host track's input. The editor also offers a
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
| 21–23 | Wet mode, routing, loop mode (`0–1` first, `2` second, `3–127` third) |
| 73–76, 78 | Spread, Dry Kill, Trails, Latch, Smooth (`0` off, `1–127` on) |
| 102, 103 | Loop and wet on/off (`0` off, `1–127` on) |
| 105 | Wet freeze (`0` off, `1–127` on) |

The browser version does not receive MIDI. MIDI clock/sync, expression, synth
mode, MISO, Classic, Bounce/Sweep ramping and the remaining hardware DIP switches
or hidden-menu features are not implemented. Automatable native parameters are
available to the host; the wet/loop footswitch parameters are momentary controls.

## Sound model

- Wet modes: **Reverb**, **Delay** and **Slip**; loop modes: **Env**, **Tape**
  and **Stretch**. Three-way routing selects input, input + loop, or loop for the
  wet side. When the loop is listening, its recent history includes active wet
  audio; overdubs use the clean input.
- CLOCK selects 2–64 kHz steps, changing pitch, timing, bandwidth and loop
  capacity together. **SMOOTH** makes the clock continuous. The fixed 64,000
  sample history holds approximately 1–32 seconds depending on clock rate.
- **TRAILS**, **DRY KILL**, **LATCH**, **SPREAD** and wet/loop balance are
  modelled. Stereo input is kept separate with SPREAD off. The exact stereo
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
