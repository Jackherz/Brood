# Third-party notices

The initial JavaScript and C++ DSP cores, native JUCE processor/editor scaffold,
and their tests were adapted from [MoodEmu](https://github.com/MaxTeicheira/Cba-guitar-pedal-),
commit `4ba376b8`, by that project's contributors. MoodEmu is licensed under the
GNU General Public License, version 3. The full license is in `LICENSE`.
This project modifies that code. It does **not** include or use Chase Bliss
firmware, and it is not affiliated with or endorsed by Chase Bliss.

The native build fetches [JUCE 8.0.9](https://github.com/juce-framework/JUCE/tree/8.0.9)
separately. JUCE modules are offered under AGPLv3 or a commercial JUCE licence;
see `licenses/JUCE-LICENSE.md` and `licenses/JUCE-AGPL-3.0.txt`. If using JUCE
under the AGPL, the combined native program is subject to the applicable AGPL
terms in addition to the GPL terms of the project code. JUCE includes the VST3
SDK and other third-party components; its licence file lists them. See also
`licenses/VST3-SDK-LICENSE.txt` for the VST3 SDK's licensing options.

The plug-ins' VT323 and Glyph Grotesque (Terminal Grotesque by Raphael Bastide,
renamed) are also under that licence; their texts are in `kit/fonts/`.

Brood's panel faces, B612 Mono (The B612 Project Authors), Overpass Medium (The Overpass
Project Authors) and Chivo Mono (The Chivo Project Authors), are embedded in the plug-in and
bundled with the browser version under the SIL Open Font License 1.1. Their licence texts are
in `assets/fonts/`. The font binaries themselves are not committed: `scripts/fetch-fonts.sh`
downloads them at build time and verifies each against a pinned SHA-256, which also keeps this
repository free of binary files.

Brood's wordmark is drawn from the letters of Squarepusher (thin cut), a freeware font by
Derik Revenge of Flak Networks, after fluid's Euphoric, with its D redrawn. Only the outlines
of "BROOD" are in the plug-in and the browser version, not the font. Credit as the font asks: Derik Revenge and
Flak Networks, and fluid, creator of Euphoric.

The demo loops the browser version and the standalone app play (`assets/demos/`) are recordings
by Jack Herz. They are not part of the source, are not committed, and are not covered by the
GPL: `scripts/fetch-demos.sh` fetches them at build time and checks each against a pinned
SHA-256.
