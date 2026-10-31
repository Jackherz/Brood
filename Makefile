# Brood Mood — short, copy-paste friendly entry points.
#
#   make install    build the VST3 (+ AU and standalone on macOS) and install it
#   make doctor     check why a DAW cannot see the plug-in
#   make build      build without installing
#   make run        build and launch the standalone app
#   make web        run the browser version
#   make test       native DSP tests, then the JavaScript suite
#   make clean      remove native/build
#
# These wrap scripts/build-plugin.sh and scripts/diagnose-plugin.sh so the
# commands stay short (and survive chat and terminal link-ification).

SHELL := /bin/bash
SCRIPTS := scripts
BUILD := native/build

.PHONY: help install build universal run doctor clean test web

help:
	@sed -n '2,12p' Makefile | sed 's/^# \{0,1\}//'

install:
	@bash $(SCRIPTS)/build-plugin.sh --install

build:
	@bash $(SCRIPTS)/build-plugin.sh

universal:
	@bash $(SCRIPTS)/build-plugin.sh --install --universal

run:
	@bash $(SCRIPTS)/build-plugin.sh
	@open "$(BUILD)/plugin/MoodEmu_artefacts/Release/Standalone/Brood Mood.app"

doctor:
	@bash $(SCRIPTS)/diagnose-plugin.sh

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
