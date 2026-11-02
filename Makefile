# Brood — short, copy-paste friendly entry points.
#
#   make update     pull the latest source, then build and install
#   make install    build the VST3 (+ AU and standalone on macOS) and install it
#   make doctor     check why a DAW cannot see the plug-in
#   make build      build without installing
#   make run        build and launch the standalone app
#   make web        run the browser version
#   make test       native DSP tests, then the JavaScript suite
#   make check      repository self-check (no binaries, scripts parse, …)
#   make doctor-fix re-sign and de-quarantine installed bundles
#   make clean      remove native/build
#
# These wrap scripts/build-plugin.sh and scripts/diagnose-plugin.sh so the
# commands stay short (and survive chat and terminal link-ification).

SHELL := /bin/bash
SCRIPTS := scripts
BUILD := native/build

.PHONY: help update install build universal run doctor doctor-fix clean test check web

help:
	@awk 'NR <= 15 && /^#/' Makefile | sed 's/^# *//'

# One command for "get the newest source and install it": the build prints the
# commit it came from, so a stale checkout can never be mistaken for a stale bug.
update:
	@git -C "$(CURDIR)" pull --ff-only
	@$(MAKE) --no-print-directory install

install:
	@bash $(SCRIPTS)/build-plugin.sh --install

build:
	@bash $(SCRIPTS)/build-plugin.sh

universal:
	@bash $(SCRIPTS)/build-plugin.sh --install --universal

run:
	@bash $(SCRIPTS)/build-plugin.sh
	@open "$(BUILD)/plugin/Brood_artefacts/Release/Standalone/Brood.app"

doctor:
	@bash $(SCRIPTS)/diagnose-plugin.sh

doctor-fix:
	@bash $(SCRIPTS)/diagnose-plugin.sh --fix

check:
	@bash $(SCRIPTS)/check-repo.sh

clean:
	@rm -rf "$(BUILD)"
	@echo "removed $(BUILD)"

test:
	@if [ ! -d "$(BUILD)" ]; then bash $(SCRIPTS)/build-plugin.sh >/dev/null; fi
	@$(MAKE) --no-print-directory -C . _native-test
	@npm test

_native-test:
	@cmake --build "$(BUILD)" --config Release --target dsp_tests >/dev/null
	@"$(BUILD)/dsp_tests"

web:
	@npm run dev
