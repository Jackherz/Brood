# Brood — install it on macOS

Three commands, then one keystroke in VS Code. Nothing needs to be downloaded
from a chat: everything comes from the repository.

## 1. Get the source

```sh
cd ~/Documents/AI
git clone https://github.com/Jackherz/Brood.git Brood-Source
cd Brood-Source
open -a "Visual Studio Code" .
```

Everything is on `main`, so a plain clone is all you need.

Do **not** apply a `.patch` file. Patches cannot carry binary files, so a tree
restored from one arrives without the interface fonts and fails to build. The
fonts are downloaded automatically by `scripts/fetch-fonts.sh` during the build.

## 2. Install the tools once

```sh
xcode-select --install     # Apple's compiler and SDK
brew install cmake         # CMake 3.22 or newer
```

## 3. Build and install

In VS Code: **⌘⇧B** (the default build task, `Brood: build + install`).

Or from the terminal, when you already have the source: `make install`.

To fetch the newest source first — the one command to reach for after a new round —
run `make update`. It pulls fast-forward, then builds and installs.

Either way the build starts by printing the commit it came from
(`==> Source 1c3fdc4`), so a stale checkout can never be mistaken for a bug: a
`-dirty` suffix means local edits, and `unknown (not a git checkout)` means the
folder was copied rather than cloned.

That compiles the VST3, Audio Unit and standalone app, signs them ad-hoc (no
Apple Developer account needed), copies them into
`~/Library/Audio/Plug-Ins/` and runs `auval` — with a deadline, and with the
component type the plug-in actually registers as (it takes MIDI input, so macOS
files it as a music effect, `aumf`). A failed or timed-out `auval` is reported but
never fails the install, because the bundles are already in place by then.

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
