# Developing Brood

Building from the command line, running the tests, and how the repository is laid out.

## Layout

```
native/dsp/      the C++ DSP engine, shared by the plug-in and the tests
native/plugin/   the JUCE plug-in (VST3, AU, standalone) and its editor
native/tests/    native DSP, plug-in and VST3 hosting tests, the renderer and editor_snapshot
src/             the browser version: the same DSP in JavaScript, in an AudioWorklet
test/            JavaScript DSP tests
tests/           the native/JavaScript parity harness and the browser smoke test
scripts/         build, diagnose and repository checks
kit/             the Glyph Audio panel kit (submodule)
docs/            the manual, the open-source plan and the pedal-series research
```

## Build from the command line

Everything the VS Code tasks do, from a terminal:

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

`native/build/plugin/editor_snapshot out.png [scale] [loop] [id=value ...]` (built with
`-DBROOD_BUILD_PLUGIN_TESTS=ON`) renders the editor to a PNG without a DAW; `loop`
catches a few seconds of the demo first so the waveform has a take.

There are no prebuilt bundles: build locally, which matches your machine and macOS
version exactly.

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
