#!/bin/bash
# Brood: a Universal (Apple Silicon + Intel) installer package for macOS, with the
# VST3, the Audio Unit and the standalone app. Run by the release workflow, or by hand:
#
#   bash scripts/package-macos.sh [VERSION]      # -> release/Brood-VERSION-macOS.pkg
#
# The bundles carry ad-hoc signatures only (no Apple Developer account), so the
# package isn't notarised: macOS asks once, in System Settings > Privacy & Security,
# before it will open it. Files the Installer puts in place aren't quarantined, so
# hosts load the plug-ins without further steps.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="${1:-$(git describe --tags --always --dirty)}"
VERSION="${VERSION#v}"
BUILD="$ROOT/native/build-release"
OUT="$ROOT/release"
WORK="$OUT/work"
PKG="$OUT/Brood-$VERSION-macOS.pkg"

[ "$(uname -s)" = Darwin ] || { echo "error: macOS only" >&2; exit 1; }

echo "==> Building Brood $VERSION, Universal"
BROOD_BUILD_DIR="$BUILD" bash scripts/build-plugin.sh --universal

ARTEFACTS="$BUILD/plugin/Brood_artefacts/Release"
rm -rf "$WORK" && mkdir -p "$WORK/root/Library/Audio/Plug-Ins/VST3" "$WORK/root/Library/Audio/Plug-Ins/Components" "$WORK/root/Applications"
ditto "$ARTEFACTS/VST3/Brood.vst3" "$WORK/root/Library/Audio/Plug-Ins/VST3/Brood.vst3"
ditto "$ARTEFACTS/AU/Brood.component" "$WORK/root/Library/Audio/Plug-Ins/Components/Brood.component"
ditto "$ARTEFACTS/Standalone/Brood.app" "$WORK/root/Applications/Brood.app"

echo "==> Checking every binary is Universal and signed"
for bundle in "$WORK"/root/Library/Audio/Plug-Ins/*/* "$WORK/root/Applications/Brood.app"; do
  binary="$bundle/Contents/MacOS/$(defaults read "$bundle/Contents/Info.plist" CFBundleExecutable)"
  archs="$(lipo -archs "$binary")"
  case "$archs" in *arm64*x86_64*|*x86_64*arm64*) ;; *) echo "error: $binary is only $archs" >&2; exit 1 ;; esac
  codesign --verify --strict "$bundle"
  echo "    $(basename "$bundle"): $archs, signed"
done

# Installed bundles stay where the package puts them: without this, Installer moves an
# update to wherever it finds an older copy (a developer's build folder, say).
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist" >/dev/null
i=0
while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/components.plist" >/dev/null 2>&1; do
  /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$WORK/components.plist" 2>/dev/null || true
  /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/components.plist"
  i=$((i + 1))
done

pkgbuild --root "$WORK/root" --component-plist "$WORK/components.plist" \
  --identifier audio.brood.pkg --version "$VERSION" --install-location / "$WORK/Brood.pkg"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Brood $VERSION</title>
    <domains enable_localSystem="true" enable_currentUserHome="true"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <license file="LICENSE"/>
    <choices-outline><line choice="brood"/></choices-outline>
    <choice id="brood" title="Brood"><pkg-ref id="audio.brood.pkg"/></choice>
    <pkg-ref id="audio.brood.pkg" version="$VERSION">Brood.pkg</pkg-ref>
</installer-gui-script>
XML
mkdir -p "$WORK/resources" && cp LICENSE "$WORK/resources/LICENSE"

productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
  --package-path "$WORK" "$PKG"
rm -rf "$WORK"
echo "==> $PKG ($(du -h "$PKG" | cut -f1))"
