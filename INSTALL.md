# Installing Brood on macOS

## From a release

Download `Brood-<version>-macOS.pkg` from the
[latest release](https://github.com/Jackherz/Brood/releases/latest). It's Universal
(Apple Silicon and Intel) and installs:

| Format | Installed to |
| --- | --- |
| VST3 | `Library/Audio/Plug-Ins/VST3/Brood.vst3` |
| Audio Unit | `Library/Audio/Plug-Ins/Components/Brood.component` |
| Standalone | `Applications/Brood.app` |

The installer asks whether to install for all users (`/Library`, `/Applications`) or
just you (the same folders in your home folder).

The package is free and isn't notarised by Apple, so macOS blocks it the first time:

1. Open the package. macOS says it can't verify it: click **Done** (not *Move to Bin*).
2. Open **System Settings > Privacy & Security** and scroll to *Security*. Beside the
   message about the Brood package, click **Open Anyway**, then confirm with your
   password or Touch ID.
3. The installer opens. Install, quit your DAW completely, reopen it and rescan.

You only allow it once per download. Brood's bundles are ad-hoc signed, which is all
macOS needs to run them; the installed plug-ins load without further steps.

## Building from source

Building locally means the plug-in matches your machine and macOS version exactly.

### 1. Install the tools

```sh
xcode-select --install     # Apple's compiler and SDK
brew install cmake         # CMake 3.22 or newer
```

### 2. Get the source

```sh
git clone --recurse-submodules https://github.com/Jackherz/Brood.git
cd Brood
```

`--recurse-submodules` brings in the panel kit at `kit/`. The interface fonts are
downloaded during the build by `scripts/fetch-fonts.sh`.

### 3. Build and install

```sh
make install
```

This compiles the VST3, Audio Unit and standalone app, signs them ad hoc, copies them
into your user plug-in folders and runs `auval`. The first build downloads JUCE 8.0.9
(about 100 MB) and takes several minutes; later builds are incremental. The build prints
the commit it came from (`==> Source 1c3fdc4`; `-dirty` means local edits).

| Format | Installed to |
| --- | --- |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Brood.vst3` |
| Audio Unit | `~/Library/Audio/Plug-Ins/Components/Brood.component` |
| Standalone | left in `native/build/plugin/Brood_artefacts/Release/Standalone/` |

To update later: `make update` pulls, updates the kit and installs again.

### 4. Find it in your DAW

Quit the DAW completely and reopen it, so it loads the new build. In Ableton Live, then
choose `Settings > Plug-Ins > Rescan` while holding **Option**. Brood is an effect: look
under *Plug-Ins > VST3 > Fx*, or search for "Brood". It registers as an Audio Unit
music effect (`aumf`) because it takes MIDI.

To play it without a DAW, `make run` builds and opens the standalone app.

## Building from VS Code

Open the folder in VS Code and trust it when asked (tasks need workspace trust). The
suggested *C/C++* and *CMake Tools* extensions are optional. **⌘⇧B** runs the default
task, `Brood: build + install`, the same as `make install`. `Terminal > Run Task…` has
the rest:

| Task | What it does |
| --- | --- |
| `build + install` (⌘⇧B) | Build, sign, install, run `auval` |
| `clean build + install` | The same after deleting `native/build`; use it after an interrupted build |
| `doctor (DAW cannot see it?)` | A full health report; changes nothing |
| `build universal + install` | One bundle with arm64 and x86_64 |
| `open the standalone app` | Build and play without a DAW |
| `run tests` | Native DSP tests and the JavaScript suite |
| `start the browser version` | The WebAudio version in a browser |

`F5` builds and starts the standalone app under the debugger.

## Troubleshooting

### The DAW doesn't show the plug-in

Run the health check first. It inspects the build, every standard plug-in folder, the
code signature, `auval`, and Live's own log and database:

```sh
make doctor          # make doctor-fix repairs signing and quarantine flags
```

Then work through these:

1. **Did the install happen?** Live reads `~/Library/Audio/Plug-Ins/VST3/` and
   `/Library/Audio/Plug-Ins/VST3/`. If neither holds `Brood.vst3`, run `make install`
   and read the first error, not the last line.
2. **Force a full rescan.** A normal rescan skips files it already rejected: hold
   **Option**. In Live 12, failed plug-ins show in the browser with a warning, and the
   reason is in `~/Library/Preferences/Ableton/Live <version>/Log.txt`.
3. **Check Live's VST3 folder setting.** With *Use VST3 Plug-In Custom Folder* on, Live
   scans only that folder.
4. **Match the architecture.** Live under Rosetta can't load an arm64-only plug-in, and
   an arm64 Live can't load an x86_64-only one. `make universal` builds both.

### Do I need an Apple Developer account?

No. A paid membership is for notarising builds you give to other people. On Apple
Silicon every executable needs at least an ad-hoc signature, which is local, free and
anonymous, and the build does it for you.

Two things often mistaken for needing an account:

- **Gatekeeper** blocks bundles that arrived in a ZIP or download. Bundles this build
  copies are never quarantined; for anything else run
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Brood.vst3`.
- **A stale signature**, after replacing files inside an installed bundle by hand.
  Re-sign with `codesign --force --sign - <bundle>`, or run `make doctor-fix`.

### VS Code shows a C# Dev Kit or .NET error

Brood has no C# and needs no .NET. The message comes from the C# Dev Kit extension
failing to find a .NET runtime and doesn't affect the build. Disable *C# Dev Kit*, *C#*
and *.NET Install Tool* for this workspace, or install `brew install --cask dotnet-sdk`
if you use C# elsewhere (then quit VS Code and reopen it from a terminal with `code .`).

## Building by hand

Command-line options, CMake targets and the tests are in
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
