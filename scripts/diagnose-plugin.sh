#!/usr/bin/env bash
#
# Brood Mood — health check. Finds out why the plug-in is not appearing in a DAW.
#
#   bash scripts/diagnose-plugin.sh          report only, changes nothing
#   bash scripts/diagnose-plugin.sh --fix    also re-sign and de-quarantine bundles
#
# It inspects the build output, every place a VST3/AU can be installed, validates
# each bundle it finds, runs auval, and greps Ableton Live's own logs for the
# plug-in. Paste the whole report when asking for help.
#
set -uo pipefail

FIX=0
for arg in "$@"; do
  case "$arg" in
    --fix) FIX=1 ;;
    -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD="$ROOT/native/build"
PLUGIN_NAME="Brood Mood"
VST3_BUNDLE="$PLUGIN_NAME.vst3"
AU_BUNDLE="$PLUGIN_NAME.component"

hr() { printf '%s\n' "------------------------------------------------------------"; }
# A command with a deadline, logging to a file: `auval` has no timeout of its own and
# can wedge while the component registry re-scans. Polled rather than guarded by a
# background `sleep` killer, which would keep this script's output open afterwards.
DEADLINE_HIT=0
deadline_run() {   # deadline_run SECONDS LOGFILE COMMAND...
  local secs=$1 log=$2
  shift 2
  DEADLINE_HIT=0
  "$@" >"$log" 2>&1 &
  local child=$! ticks=0 limit=$((secs * 4))
  while kill -0 "$child" 2>/dev/null; do
    if [ "$ticks" -ge "$limit" ]; then
      kill -9 "$child" 2>/dev/null || true
      DEADLINE_HIT=1
      break
    fi
    sleep 0.25
    ticks=$((ticks + 1))
  done
  local rc=0
  wait "$child" 2>/dev/null || rc=$?
  return "$rc"
}

if [ "$(uname -s)" = Darwin ]; then
  mtime() { stat -f '%Sm' "$1" 2>/dev/null; }
  is_mac=1
else
  mtime() { stat -c '%y' "$1" 2>/dev/null; }
  is_mac=0
fi
section() { printf '\n== %s ==\n' "$1"; }
ok()   { printf '  ok    %s\n' "$1"; }
warn() { printf '  WARN  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; }
info() { printf '        %s\n' "$1"; }

ISSUES=0
issue() { ISSUES=$((ISSUES + 1)); }

# ----------------------------------------------------------------- 1. the host
section "Host"
printf '  uname      %s\n' "$(uname -srm)"
printf '  macOS      %s\n' "$(sw_vers -productVersion 2>/dev/null || echo 'n/a (not macOS)')"
printf '  CPU        %s\n' "$(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m)"
printf '  project    %s\n' "$ROOT"
printf '  date       %s\n' "$(date)"

# ------------------------------------------------------------- 2. toolchain
section "Toolchain"
for tool in cmake git; do
  if path=$(command -v "$tool" 2>/dev/null); then
    ok "$tool at $path ($("$tool" --version 2>/dev/null | head -1))"
  else
    bad "$tool not found on PATH"
    issue
  fi
done
if command -v xcode-select >/dev/null 2>&1; then
  if sdk=$(xcode-select -p 2>/dev/null); then
    ok "Xcode tools at $sdk"
  else
    bad "Xcode command line tools missing — run: xcode-select --install"
    issue
  fi
fi
if command -v c++ >/dev/null 2>&1; then
  info "c++ reports: $(c++ --version 2>/dev/null | head -1)"
fi
cxx_env=$(env | grep -E '^(CXX|CC|CXXFLAGS|CMAKE_[A-Z_]*)=.' | tr '\n' ' ')
[ -n "$cxx_env" ] && info "build environment overrides: $cxx_env"

# --------------------------------------------------------------- 3. artefacts
section "Build output"
if [ ! -d "$BUILD" ]; then
  bad "no build directory at $BUILD"
  info "the build task has not run, or it failed before configuring"
  issue
else
  printf '  build dir  %s\n' "$BUILD"
  if [ -f "$BUILD/CMakeCache.txt" ]; then
    ok "CMake configured"
    grep -E '^(CMAKE_BUILD_TYPE|CMAKE_OSX_ARCHITECTURES|MOOD_BUILD_PLUGIN):' "$BUILD/CMakeCache.txt" | sed 's/^/        /'
  else
    bad "CMake never finished configuring (no CMakeCache.txt)"
    info "re-run the build and send the first error, not the last lines"
    issue
  fi
  JUCE_DIR=$(find "$BUILD" -maxdepth 5 -type d \( -name 'JUCE-src' -o -name 'juce-src' \) 2>/dev/null | head -1)
  JUCE_OVERRIDE=$(grep -h '^FETCHCONTENT_SOURCE_DIR_JUCE:' "$BUILD/CMakeCache.txt" 2>/dev/null | cut -d= -f2)
  if [ -n "$JUCE_OVERRIDE" ]; then
    ok "JUCE from an existing checkout: $JUCE_OVERRIDE (--juce)"
  elif [ -n "$JUCE_DIR" ]; then
    ok "JUCE source present at $JUCE_DIR"
  else
    warn "no JUCE download under $BUILD — the fetch step may have failed"
  fi
  BUILT=$(find "$BUILD" -name '*.vst3' -maxdepth 6 -type d 2>/dev/null | head -5)
  if [ -n "$BUILT" ]; then
    while IFS= read -r b; do
      printf '  built      %s\n' "$b"
      printf '             modified %s\n' "$(mtime "$b")"
    done <<< "$BUILT"
  else
    bad "no .vst3 produced under $BUILD"
    info "the compile step failed; the error is earlier in the task output"
    issue
  fi
fi

# ------------------------------------------------------------- 4. inspection
check_bundle() {
  local bundle=$1 kind=$2
  printf '\n  %s\n' "$bundle"
  printf '             modified %s\n' "$(mtime "$bundle")"
  local bin
  bin=$(find "$bundle/Contents" -maxdepth 2 -type f \( -name "$PLUGIN_NAME" -o -name "$PLUGIN_NAME.so" \) 2>/dev/null | head -1)
  if [ "$kind" = vst3 ] && [ "$is_mac" = 1 ]; then
    [ -f "$bundle/Contents/Info.plist" ] && ok "Info.plist present" || { bad "Info.plist missing"; issue; }
    [ -f "$bundle/Contents/Resources/moduleinfo.json" ] && ok "moduleinfo.json present" \
      || warn "no moduleinfo.json (hosts must load the binary to read it; supported but slower)"
  fi
  if [ -f "$bin" ]; then
    ok "binary present ($(wc -c <"$bin" | tr -d ' ') bytes)"
    if command -v file >/dev/null 2>&1; then
      info "arch: $(file -b "$bin")"
    fi
    if command -v lipo >/dev/null 2>&1; then
      slices=$(lipo -archs "$bin" 2>/dev/null)
      info "slices: $slices"
      host=$(uname -m)
      case " $slices " in
        *" $host "*) ;;
        *) warn "no $host slice; a $host host cannot load this build (use --universal)"
           issue ;;
      esac
    fi
    if command -v codesign >/dev/null 2>&1; then
      if sig=$(codesign --verify --strict "$bundle" 2>&1); then
        ok "code signature verifies (ad-hoc is fine — no Apple Developer account needed)"
      elif [ "$FIX" = 1 ]; then
        info "re-signing ad-hoc"
        if codesign --force --timestamp=none --sign - "$bundle" >/dev/null 2>&1 \
           && codesign --verify --strict "$bundle" >/dev/null 2>&1; then
          ok "signed ad-hoc and now verifies"
        else
          bad "could not sign: $(printf '%s' "$sig" | head -1)"
          issue
        fi
      else
        bad "signature: $(printf '%s' "$sig" | head -1)"
        info "fix with: codesign --force --sign - \"$bundle\"   (or --fix)"
        issue
      fi
    fi
    if command -v xattr >/dev/null 2>&1; then
      if xattr -p com.apple.quarantine "$bundle" >/dev/null 2>&1; then
        if [ "$FIX" = 1 ]; then
          xattr -dr com.apple.quarantine "$bundle" 2>/dev/null
          if xattr -p com.apple.quarantine "$bundle" >/dev/null 2>&1; then
            bad "quarantine flag could not be removed"
            issue
          else
            ok "removed the quarantine flag"
          fi
        else
          bad "bundle is quarantined (Gatekeeper will block it)"
          info "fix with: xattr -dr com.apple.quarantine \"$bundle\"   (or --fix)"
          issue
        fi
      else
        ok "not quarantined"
      fi
    fi
    if command -v otool >/dev/null 2>&1; then
      info "linked against: $(otool -L "$bin" 2>/dev/null | tail -n +2 | awk '{print $1}' | grep -v '^/System\|^/usr/lib' | tr '\n' ' ' | sed 's/ *$//')"
    fi
  else
    bad "binary missing at $bin"
    issue
  fi
}

section "Installed plug-ins"
FOUND=0
for dir in \
  "$HOME/Library/Audio/Plug-Ins/VST3" \
  "/Library/Audio/Plug-Ins/VST3" \
  "$HOME/.vst3"
do
  if [ -d "$dir/$VST3_BUNDLE" ]; then
    FOUND=$((FOUND + 1))
    printf '  found      %s\n' "$dir/$VST3_BUNDLE"
    check_bundle "$dir/$VST3_BUNDLE" vst3
  fi
done
for dir in "$HOME/Library/Audio/Plug-Ins/Components" "/Library/Audio/Plug-Ins/Components"; do
  if [ -d "$dir/$AU_BUNDLE" ]; then
    FOUND=$((FOUND + 1))
    printf '  found      %s\n' "$dir/$AU_BUNDLE"
    check_bundle "$dir/$AU_BUNDLE" au
  fi
done
if [ "$FOUND" = 0 ]; then
  bad "no Brood Mood bundle installed in any standard folder"
  info "expected ~/Library/Audio/Plug-Ins/VST3/$VST3_BUNDLE"
  info "install with: bash scripts/build-plugin.sh --install"
  issue
fi

# ---------------------------------------------------------------- 5. auval
section "Audio Unit validation (auval)"
if command -v auval >/dev/null 2>&1; then
  # The type has to match what macOS filed the component as: a plug-in that takes
  # MIDI input is a music effect (aumf), not a plain effect (aufx), and validating
  # against the wrong one fails every time. Ask the registry, then try both.
  deadline_run 20 /tmp/brood-au-list.log auval -a || true
  registered=$(awk '$2 == "BrMd" { print $1, $2, $3; exit }' /tmp/brood-au-list.log 2>/dev/null || true)
  candidates="${registered:-}
aumf BrMd Broo
aufx BrMd Broo"
  passed=0
  tried=""
  out=""
  while IFS=' ' read -r au_type au_sub au_man; do
    if [ -z "$au_type" ] || [ -z "$au_sub" ] || [ -z "$au_man" ]; then continue; fi
    case " $tried " in *" $au_type $au_sub $au_man "*) continue ;; esac
    tried="$tried $au_type $au_sub $au_man"
    deadline_run 90 /tmp/brood-auval.log auval -v "$au_type" "$au_sub" "$au_man" || true
    out=$(cat /tmp/brood-auval.log 2>/dev/null || true)
    if printf '%s' "$out" | grep -q 'AU VALIDATION SUCCEEDED'; then
      ok "auval: the plug-in loads and passes validation ($au_type $au_sub $au_man)"
      passed=1
      break
    fi
  done <<< "$candidates"
  if [ "$passed" = 0 ]; then
    if [ "$DEADLINE_HIT" = 1 ]; then
      bad "auval did not answer within 90 s (the registry may be re-scanning)"
      info "retry with: make doctor"
    else
      bad "auval did not succeed"
      printf '%s\n' "$out" | grep -iE 'error|fail|crash|cannot|not found' | head -8 | sed 's/^/        /'
      info "if the AU folder has the bundle but auval cannot see it, run: killall -9 AudioComponentRegistrar"
    fi
    info "the VST3 is a separate format and is unaffected by this check"
    issue
  fi
else
  info "auval not available"
fi

# ------------------------------------------------------- 6. Ableton Live logs
section "Ableton Live"
if [ -d "$HOME/Library/Preferences/Ableton" ]; then
  for prefs in "$HOME"/Library/Preferences/Ableton/Live*/; do
    [ -d "$prefs" ] || continue
    printf '\n  %s\n' "$prefs"
    if [ -f "${prefs}Log.txt" ]; then
      printf '             Log.txt modified %s\n' "$(mtime "${prefs}Log.txt")"
      hits=$(grep -ic 'brood' "${prefs}Log.txt" 2>/dev/null || echo 0)
      if [ "${hits:-0}" -gt 0 ]; then
        ok "Live mentions Brood $hits time(s); last lines:"
        grep -i 'brood' "${prefs}Log.txt" | tail -8 | cut -c1-200 | sed 's/^/        /'
      else
        warn "Live's log never mentions Brood — it has not seen the bundle"
        info "scan errors in this log:"
        grep -iE 'vst3?.*(fail|error|crash|invalid|reject|blacklist)|cannot load.*vst' "${prefs}Log.txt" 2>/dev/null \
          | tail -6 | cut -c1-200 | sed 's/^/        /'
      fi
    else
      warn "no Log.txt (Live has not run since install, or never started)"
    fi
  done

  printf "\n  Does Live's plug-in database know about it?\n"
  if [ -d "$HOME/Library/Application Support/Ableton" ]; then
    dbhits=$(grep -rl --binary-files=text -m1 'Brood Mood' \
      "$HOME/Library/Application Support/Ableton" 2>/dev/null | head -5)
    if [ -n "$dbhits" ]; then
      ok "found in Live data:"
      printf '%s\n' "$dbhits" | sed 's/^/        /'
    else
      warn "not in Live's database either — Live has not scanned it"
    fi
  else
    info "no Ableton application support folder"
  fi

  printf '\n  VST3 folder settings in Live preferences:\n'
  found_pref=0
  for cfg in "$HOME"/Library/Preferences/Ableton/Live*/Preferences.cfg; do
    [ -f "$cfg" ] || continue
    found_pref=1
    grep -iE 'vst3?.*(folder|system|use)' "$cfg" 2>/dev/null | sed 's/^/        /'
  done
  [ "$found_pref" = 0 ] && info "no Preferences.cfg found"
else
  info "no Ableton preferences found — is Live installed for this user?"
fi

# -------------------------------------------------------------- 7. summary
section "Summary"
if [ "$ISSUES" = 0 ]; then
  ok "no blocking problems found: bundle installed, signature and auval fine"
  info "if Live still hides it: quit Live completely, reopen, then use"
  info "Preferences > Plug-Ins > Rescan (hold Option while clicking for a full rescan)."
else
  bad "$ISSUES problem(s) found — see the FAIL lines above"
fi
printf '\nA paid Apple Developer account is NOT needed to build, install and run this\n'
printf 'plug-in on your own Mac: locally built bundles need only an ad-hoc signature,\n'
printf 'which scripts/build-plugin.sh applies for you. Notarisation is only for\n'
printf 'handing a copy to somebody else.\n'
printf '\nReport finished. Paste this whole output, plus the first error from the build\n'
printf 'task, if you need help.\n'
