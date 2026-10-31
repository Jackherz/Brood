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

The bundled VT323 (Peter Hull), Press Start 2P (CodeMan38), and Space Mono
(Colophon Foundry) fonts are licensed under the SIL Open Font License 1.1.
Their individual notices and licence texts are in `assets/fonts/`.
