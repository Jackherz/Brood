# Brood Mood — install it on macOS

Three commands, then one keystroke in VS Code. Nothing needs to be downloaded
from a chat: everything comes from the repository.

## 1. Get the source

```sh
cd ~/Documents/AI
git clone -b arena/7dccc818-brood https://github.com/Jackherz/Brood.git Brood-Source
cd Brood-Source
open -a "Visual Studio Code" .
```

The `-b` matters: it selects the branch that contains the plugin. A plain
`git clone` gives the empty skeleton.

Do **not** apply a `.patch` file. Patches cannot carry binary files, so a tree
restored from one arrives without the interface fonts and fails to build. The
fonts are downloaded automatically by `scripts/fetch-fonts.sh` during the build.

## 2. Install the tools once

```sh
xcode-select --install     # Apple's compiler and SDK
brew install cmake         # CMake 3.22 or newer
```

## 3. Build and install

In VS Code: **⌘⇧B** (the default build task, `Brood Mood: build + install`).

Or from the terminal: `make install`.

That compiles the VST3, Audio Unit and standalone app, signs them ad-hoc (no
Apple Developer account needed), copies them into
`~/Library/Audio/Plug-Ins/` and runs `auval`.

## 4. Show it in Ableton Live

Quit Live completely, reopen it, then `Settings → Plug-Ins → Rescan` with
**Option** held. It is an effect: look under *Plug-Ins → VST3 → Fx*, or search
"Brood".

Still not there? `make doctor` prints a full report — build, install locations,
signature, `auval` and Live's own log. `make doctor-fix` repairs signing and
quarantine flags.

## Play it without a DAW

`make run` builds and opens the standalone app. The browser version is
`npm ci && npm run dev`.

---

Everything else — every task, the command-line flags, the troubleshooting
sections — is in [README.md](README.md).
