#!/usr/bin/env bash
# Repository self-check. Catches the failure modes that made this project hard to
# build the first time, before anything is committed or shipped:
#
#   * a binary file that is not ignored (a patch cannot carry it, so a copy of
#     this repo restored from a patch arrives without it and fails to build)
#   * a shell script that does not parse
#   * invalid JSON in .vscode/ or package.json
#   * README instructions that tell people to apply a patch
#   * required entry points going missing
#
#   bash scripts/check-repo.sh
#
# Exit code is the number of problems, so `make check` fails loudly.
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT" || exit 1
PROBLEMS=0
ok()   { printf '  ok    %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; PROBLEMS=$((PROBLEMS + 1)); }
info() { printf '        %s\n' "$1"; }

printf '\n== Required files ==\n'
for f in README.md Makefile LICENSE .gitignore \
         scripts/build-plugin.sh scripts/diagnose-plugin.sh scripts/fetch-fonts.sh \
         .vscode/tasks.json native/CMakeLists.txt native/plugin/CMakeLists.txt \
         src/main.jsx src/brood-worklet.js native/dsp/brood.h; do
  if [ -e "$f" ]; then ok "$f"; else bad "missing $f"; fi
done

printf '\n== No unignored binaries ==\n'
# Anything git would add: untracked and not ignored, plus modified tracked files.
binaries=""
while IFS= read -r f; do
  [ -n "$f" ] || continue
  [ -f "$f" ] || continue
  case "$f" in docs/images/*) continue ;; esac   # README screenshots: never part of a build
  if LC_ALL=C grep -Iq . < "$f" 2>/dev/null; then :; else binaries="$binaries $f"; fi
done <<EOF
$(git ls-files -o --exclude-standard 2>/dev/null; git ls-files -m 2>/dev/null)
EOF
if [ -z "$binaries" ]; then
  ok "no untracked or modified binary files"
else
  bad "binary file(s) would enter the repo:$binaries"
  info "Add them to .gitignore and fetch them at build time instead."
fi

printf '\n== Fonts are fetched, not committed ==\n'
if grep -q '^assets/fonts/\*\.ttf$' .gitignore; then
  ok ".gitignore excludes assets/fonts/*.ttf"
else
  bad ".gitignore does not exclude assets/fonts/*.ttf"
fi
if git ls-files --error-unmatch assets/fonts/VT323-Regular.ttf >/dev/null 2>&1; then
  bad "a .ttf is tracked — patches will break again"
else
  ok "no .ttf is tracked"
fi

printf '\n== Shell scripts parse ==\n'
for f in scripts/*.sh; do
  if bash -n "$f" 2>/dev/null; then ok "$f"; else bad "$f has a syntax error"; fi
done

printf '\n== JSON parses ==\n'
if command -v python3 >/dev/null 2>&1; then
  for f in package.json .vscode/tasks.json .vscode/launch.json .vscode/settings.json .vscode/extensions.json; do
    [ -f "$f" ] || continue
    if python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$f" >/dev/null 2>&1; then
      ok "$f"
    else
      bad "$f is not valid JSON"
    fi
  done
else
  info "python3 not found; skipped JSON checks"
fi

printf '\n== README does not send people down the patch route ==\n'
if grep -qE '^\s*git apply|git apply --whitespace' README.md; then
  bad "README still tells people to run git apply"
else
  ok "no git apply instructions in README"
fi
if grep -q 'arena/7dccc818-brood' README.md; then
  ok "README names the branch to clone"
else
  info "README does not name a branch — update 'Get the source' if the branch changes"
fi

printf '\n== Build task is the default ==\n'
if grep -q '"isDefault": true' .vscode/tasks.json; then
  ok "tasks.json declares a default build task (⌘⇧B)"
else
  bad "no default build task"
fi

printf '\n== Summary ==\n'
if [ "$PROBLEMS" = 0 ]; then
  printf '  clean\n\n'
else
  printf '  %s problem(s)\n\n' "$PROBLEMS"
fi
exit "$PROBLEMS"
