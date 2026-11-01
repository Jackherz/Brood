#!/usr/bin/env bash
#
# Brood Mood — build the JUCE plug-in from source, then optionally install it.
#
#   ./scripts/build-plugin.sh                 # build VST3 (+ AU and standalone on macOS)
#   ./scripts/build-plugin.sh --install       # build, then copy into your plug-in folders
#   ./scripts/build-plugin.sh --install --test
#
# Options:
#   --install        Copy the built bundles into your user plug-in folders.
#   --universal      macOS only: build arm64 + x86_64 in one universal bundle.
#   --juce PATH      Use an existing JUCE checkout instead of downloading 8.0.9.
#   --clean          Delete the build directory before configuring.
#   --test           Run the DSP and plug-in test suites after building.
#   --no-sign        macOS only: skip ad-hoc code signing of the bundles.
#   --jobs N         Parallel compile jobs (default: number of CPUs).
#   --dry-run        Print the commands without running them.
#   -- ARGS...       Pass the remaining arguments to CMake verbatim.
#   -h, --help       Show this help.
#
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD="$ROOT/native/build"

# Which source this build came from. A warning report only means something if
# the tree it came from is known: `-dirty` means local edits, `unknown` means a
# folder that is not a clone (a downloaded zip), an old hash an old checkout.
SRC_VERSION=$(git -C "$ROOT" describe --always --dirty 2>/dev/null || echo "unknown (not a git checkout)")

INSTALL=0
UNIVERSAL=0
CLEAN=0
RUN_TESTS=0
DRY_RUN=0
SIGN=1
JOBS=$( (getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4) )
EXTRA_ARGS=()
JUCE_PATH=${JUCE_PATH:-}

usage() {
  cat <<'EOF'
Brood Mood - build the JUCE plug-in from source, then optionally install it.

  ./scripts/build-plugin.sh                 # build VST3 (+ AU and standalone on macOS)
  ./scripts/build-plugin.sh --install       # build, then copy into your plug-in folders

Options:
  --install        Copy the built bundles into your user plug-in folders.
  --universal      macOS only: build arm64 + x86_64 in one universal bundle.
  --juce PATH      Use an existing JUCE checkout instead of downloading 8.0.9.
  --clean          Delete the build directory before configuring.
  --test           Run the DSP and plug-in test suites after building.
  --no-sign        macOS only: skip ad-hoc code signing of the bundles.
  --jobs N         Parallel compile jobs (default: number of CPUs).
  --dry-run        Print the commands without running them.
  -- ARGS...       Pass the remaining arguments to CMake verbatim.
  -h, --help       Show this help.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --install)   INSTALL=1 ;;
    --universal) UNIVERSAL=1 ;;
    --clean)     CLEAN=1 ;;
    --test)      RUN_TESTS=1 ;;
    --dry-run)   DRY_RUN=1 ;;
    --no-sign)   SIGN=0 ;;
    --juce)      JUCE_PATH=${2:?--juce needs a path}; shift ;;
    --jobs)      JOBS=${2:?--jobs needs a number}; shift ;;
    -h|--help)   usage; exit 0 ;;
    --)          shift; EXTRA_ARGS=("$@"); break ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

run() {
  if [ "$DRY_RUN" = 1 ]; then
    printf '+ %s\n' "$*"
  else
    "$@"
  fi
}

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "error: $1 not found. $2" >&2
    exit 1
  }
}

case "$(uname -s)" in
  Darwin) OS=macos ;;
  Linux)  OS=linux ;;
  *) echo "error: this script supports macOS and Linux only." >&2; exit 1 ;;
esac

# -------------------------------------------------------------------- fonts
# The panel fonts are binary and therefore fetched, not committed. Best effort:
# a missing font degrades the typeface, it does not stop the build.
if [ "$DRY_RUN" = 0 ]; then
  if ! bash "$ROOT/scripts/fetch-fonts.sh" --quiet; then
    echo "note: could not fetch assets/fonts/*.ttf — building with a system font." >&2
    echo "      The browser panel needs them: bash scripts/fetch-fonts.sh" >&2
  fi
else
  bash "$ROOT/scripts/fetch-fonts.sh" --check --quiet || \
    echo "note: assets/fonts/*.ttf missing (would be fetched now)" >&2
fi

# ---------------------------------------------------------------- prerequisites
if [ "$OS" = macos ]; then
  if ! xcode-select -p >/dev/null 2>&1; then
    echo "error: the Xcode command line tools are missing. Run:" >&2
    echo "         xcode-select --install" >&2
    exit 1
  fi
  need cmake "Install it with: brew install cmake"
  need git "Install it with: xcode-select --install"
else
  need cmake "Install CMake 3.22 or newer (and the JUCE Linux dependencies)."
  need git "Install git."
  need pkg-config "Install pkg-config plus the JUCE Linux dependencies."
fi

cmake_version=$(cmake --version | head -1 | sed 's/[^0-9.]*\([0-9][0-9.]*\).*/\1/')
case "$cmake_version" in
  2.*|3.[0-9].*|3.1[0-9].*|3.2[01].*)
    echo "warning: CMake $cmake_version is older than the required 3.22." >&2 ;;
esac

# --------------------------------------------------------------------- configure
if [ "$CLEAN" = 1 ]; then
  echo "==> Removing $BUILD"
  run rm -rf "$BUILD"
fi

configure_args=(
  -S "$ROOT/native"
  -B "$BUILD"
  -DMOOD_BUILD_PLUGIN=ON
  -DCMAKE_BUILD_TYPE=Release
)
# Anything after `--` is passed to CMake verbatim, e.g.
#   ./scripts/build-plugin.sh -- -DCMAKE_CXX_FLAGS=-DJUCE_USE_XCURSOR=0
if [ ${#EXTRA_ARGS[@]} -gt 0 ]; then
  configure_args+=("${EXTRA_ARGS[@]}")
fi

if [ -n "$JUCE_PATH" ]; then
  configure_args+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$(cd "$JUCE_PATH" && pwd)")
fi

if [ "$OS" = macos ]; then
  if [ "$UNIVERSAL" = 1 ]; then
    configure_args+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
  fi
  configure_args+=(-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0)
else
  if [ "$UNIVERSAL" = 1 ]; then
    echo "warning: --universal only applies to macOS; ignoring it." >&2
    UNIVERSAL=0
  fi
  # Headless-ish Linux builds skip the optional X11 extensions and ALSA, which
  # keeps the dependency list small. Drop these if you want audio I/O in the
  # standalone app on a normal desktop.
  if [ "${BROOD_LINUX_FULL_DESKTOP:-0}" != 1 ]; then
    # JUCE's juceaide host helper is bootstrapped by a nested CMake run that only
    # inherits CXXFLAGS from the environment, so set both.
    linux_defs="-DJUCE_ALSA=0 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XSHM=0 -DJUCE_USE_XRENDER=0 -DJUCE_USE_XCURSOR=0"
    configure_args+=("-DCMAKE_CXX_FLAGS=$linux_defs ${CXXFLAGS:-}")
    export CXXFLAGS="$linux_defs ${CXXFLAGS:-}"
  fi
fi

echo "==> Source ${SRC_VERSION}"
echo "==> Configuring (JUCE 8.0.9 is downloaded on the first run)"
run cmake "${configure_args[@]}"

# ------------------------------------------------------------------------- build
targets=(MoodEmu_VST3)
if [ "$OS" = macos ]; then
  targets+=(MoodEmu_AU MoodEmu_Standalone)
fi

echo "==> Building ${targets[*]}"
run cmake --build "$BUILD" --config Release --parallel "$JOBS" --target "${targets[@]}"

# An interrupted build (Ctrl-C, closed terminal, killed task) can leave a
# zero-byte binary that the next invocation considers up to date. Catch it here
# rather than installing a bundle no host can load.
VST3="$BUILD/plugin/MoodEmu_artefacts/Release/VST3/Brood Mood.vst3"
if [ "$DRY_RUN" = 0 ]; then
  if [ ! -d "$VST3" ] || [ -z "$(find "$VST3" -type f -size +0 -name 'Brood Mood*' 2>/dev/null | head -1)" ]; then
    echo "error: the VST3 bundle at" >&2
    echo "         $VST3" >&2
    echo "       is missing or has an empty binary. A previous build was probably" >&2
    echo "       interrupted. Re-run with --clean:" >&2
    echo "         bash scripts/build-plugin.sh --clean --install" >&2
    exit 1
  fi
fi
AU="$BUILD/plugin/MoodEmu_artefacts/Release/AU/Brood Mood.component"
APP="$BUILD/plugin/MoodEmu_artefacts/Release/Standalone/Brood Mood.app"

echo
echo "Build finished. Artefacts:"
echo "  VST3       $VST3"
[ "$OS" = macos ] && echo "  Audio Unit $AU"
[ "$OS" = macos ] && echo "  Standalone $APP"
echo

# ----------------------------------------------------------------------- signing
# macOS on Apple Silicon only runs code that carries at least an ad-hoc
# signature, and a host refuses to load a bundle whose signature does not match
# its contents. Xcode signs automatically; Ninja/Makefiles do not, so sign each
# bundle here. This needs no Apple Developer account: ad-hoc ("-") signatures are
# for code that stays on this machine. A paid account is only needed to notarise
# a copy you hand to somebody else.
if [ "$OS" = macos ] && [ "$SIGN" = 1 ]; then
  if command -v codesign >/dev/null 2>&1; then
    echo "==> Ad-hoc signing the bundles (no Apple Developer account needed)"
    for bundle in "$VST3" "$AU" "$APP"; do
      [ -e "$bundle" ] || continue
      if [ "$DRY_RUN" = 1 ]; then
        echo "  codesign --force --sign - \"$bundle\""
        continue
      fi
      if run codesign --force --timestamp=none --sign - "$bundle" 2>/tmp/brood-codesign.log; then
        echo "    signed $(basename "$bundle")"
      else
        echo "    warning: could not sign $(basename "$bundle")" >&2
        sed 's/^/      /' /tmp/brood-codesign.log >&2 || true
      fi
    done
    if [ "$DRY_RUN" = 0 ]; then
      for bundle in "$VST3" "$AU" "$APP"; do
        [ -e "$bundle" ] || continue
        if codesign --verify --strict "$bundle" >/dev/null 2>&1; then
          echo "    verified $(basename "$bundle")"
        else
          echo "    warning: $(basename "$bundle") fails codesign --verify" >&2
        fi
      done
    fi
  else
    echo "note: codesign not found; skipping ad-hoc signing." >&2
  fi
  echo
fi

# ----------------------------------------------------------------------- install
if [ "$INSTALL" = 1 ]; then
  if [ "$OS" = macos ]; then
    VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
    AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
  else
    VST3_DIR="${VST3_DIR:-$HOME/.vst3}"
    AU_DIR=""
  fi

  install_bundle() {
    src=$1
    dest=$2
    if [ ! -e "$src" ]; then
      echo "warning: $src was not built; skipping." >&2
      return 0
    fi
    echo "==> Installing $(basename "$src") into $dest"
    run mkdir -p "$dest"
    if [ "$DRY_RUN" = 0 ]; then
      rm -rf "$dest/$(basename "$src")"
    fi
    if [ "$OS" = macos ]; then
      run ditto "$src" "$dest/$(basename "$src")"
      # A bundle that arrived in a ZIP or download carries com.apple.quarantine
      # and Gatekeeper blocks it — that is what people usually hit when they are
      # told they "need an Apple Developer account". Files this script installed
      # itself must not carry that flag.
      if [ "$DRY_RUN" = 0 ] && command -v xattr >/dev/null 2>&1; then
        xattr -dr com.apple.quarantine "$dest/$(basename "$src")" 2>/dev/null || true
      fi
    else
      run cp -R "$src" "$dest/$(basename "$src")"
    fi
  }

  install_bundle "$VST3" "$VST3_DIR"
  if [ "$OS" = macos ]; then
    install_bundle "$AU" "$AU_DIR"
  fi

  cat <<EOF

Installed. Now rescan plug-ins in your DAW:

  Ableton Live   Settings > Plug-Ins > Rescan
  REAPER         Options > Preferences > Plug-ins > VST > Re-scan
  Bitwig         Settings > Locations > Plug-ins > Rescan
  Logic Pro      Logic scans Audio Units on launch (Auval)

If a plug-in does not appear, quit the DAW completely and reopen it.
EOF

  if [ "$OS" = macos ] && command -v auval >/dev/null 2>&1 && [ "$DRY_RUN" = 0 ]; then
    echo
    echo "==> Validating the Audio Unit with auval (this also catches signing issues)"
    # An Audio Unit is published under the type it really registers as. This plug-in
    # asks for MIDI input, so macOS files it as a *music effect* (`aumf`), not a plain
    # effect (`aufx`) — validating against the wrong type fails every time, which
    # looks exactly like a broken plug-in. So ask the registry what it filed first.
    AU_TRIPLE=$(auval -a 2>/dev/null | awk '$2 == "BrMd" { print $1, $2, $3; exit }' || true)
    AU_OK=0
    for triple in ${AU_TRIPLE:-} "aumf BrMd Broo" "aufx BrMd Broo"; do
      set -- $triple
      [ -n "${1:-}" ] || continue
      if auval -v "$1" "$2" "$3" >/tmp/brood-auval.log 2>&1; then
        echo "    auval: $1 $2 $3 loads and passes validation."
        AU_OK=1
        break
      fi
    done
    if [ "$AU_OK" = 0 ]; then
      echo "    auval could not validate the AU; the lines that matter:" >&2
      grep -iE 'error|fail|cannot|not found' /tmp/brood-auval.log 2>/dev/null | head -8 | sed 's/^/      /' >&2 || true
      echo "    full log: /tmp/brood-auval.log" >&2
      echo "    the VST3 is unaffected — in Ableton, load the VST3" >&2
    fi
  fi
fi

# ------------------------------------------------------------------------- tests
if [ "$RUN_TESTS" = 1 ]; then
  echo
  echo "==> Running native DSP tests"
  run cmake --build "$BUILD" --config Release --parallel "$JOBS" --target dsp_tests
  run "$BUILD/dsp_tests"
  echo "==> Running JS DSP tests"
  run npm --prefix "$ROOT" ci
  run npm --prefix "$ROOT" test
fi
