# Open source we can lean on

> Status: **research note, 2026-11-01.** Nothing here is adopted. This is the
> shortlist of existing open-source code that would actually buy us something on the
> nine pedals in [PEDAL-SERIES.md](PEDAL-SERIES.md), with the licence of each — and
> the licence matters, because the project is GPLv3 today and *may* one day want a
> closed-source build.

Found while answering one question: what already exists that we could stand on,
instead of writing every block twice by hand?

## Decision: if this is ever sold, it is sold open source

Recorded 2026-11-01, so it does not have to be re-argued later.

**Lane A — sell it as open source. This is the plan.** The price buys the pedal, the
documentation and the updates; the source stays published under GPLv3, which is what
the repository already is. Consequences, none of them a problem:

- **JUCE is fine on its AGPLv3 path.** No commercial JUCE licence is needed while the
  whole plug-in is AGPL/GPL-compatible and the source ships. That alone is the single
  largest cost avoided.
- **Every dependency already on the list stays usable** — DaisySP, Airwindows,
  Beads/stmlib, chowdsp_wdf, Signalsmith Stretch, `pedalzombie` are MIT or BSD and
  only need attribution; Signalsmith's and Airwindows' own terms ask for nothing more
  than credit. The GPLv3 pieces (MoodEmu as ancestor, CHOWTapeModel, GRNLR,
  Dragonfly, SooperLooper, Faust's compiler) can be linked **only** under Lane A, and
  that is now the lane we are in.
- **GPLv3 does not stop anyone from reselling it, or from forking it.** That is
  accepted, not overlooked: for a pedal the value is the sound, the panel and the
  support, not the source text.
- **What a sale would actually need:** a reproducible `make install`, the notices in
  [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) kept current, the source available
  at the same place the binary is downloaded from, and a licence line in the manual.
  That is a payment page and a source archive, nothing structural.

**Lane B — a closed build — is a fallback, not a plan.** It means replacing the
MoodEmu-derived core with a clean-room rewrite, buying a JUCE commercial licence, and
renaming everything (product name, `audio.brood.mood` bundle id, the `BrMd` plug-in
code, the "unofficial, inspired by" disclaimer in `native/plugin/CMakeLists.txt`).
If it ever happens it must happen **before the first customer**, because a rename
breaks every saved Ableton Live set. Nothing here is on that path today.

One thing open sourcing does not fix: **"Brood Mood" / "MOOD" are Chase Bliss's
trademarks and trade dress**, which is a naming question rather than a code-licence
question, and is answered separately by the rename in Lane B.

## Our own ancestor, and what to check before anything else

The repo's first cores came from **MoodEmu** (MaxTeicheira/Cba-guitar-pedal-,
GPLv3), at commit `4ba376b8`. Two things follow:

- **Check upstream for anything since.** It is the same architecture we have, so a
  fix there may be a fix we want — particularly in the FDN reverb and the clock
  domain. Pulling it back is cheap; re-deriving it is not.
- **Its README is honest about the same problem we have**, listing which behaviours
  are documented and which were assumed. That table is worth keeping current in our
  own docs, because it is the difference between an emulation and a wish.

## The most useful find: `pedalzombie` (MIT) — panels as data, already written

**mkaerger/pedalzombie** is a set of browser MIDI editors for Chase Bliss pedals,
and it is MIT. It covers **MOOD MKII, Bad Mood, Habit, Blooper, Lost + Found,
Onward, Clean, Lossy, Brothers AM, Reverse Mode C, Generation Loss MKII** and Warped
Vinyl MKII — almost exactly our candidate list.

Its architecture is the thing we have been planning for two rounds: *"A pedal is
data. `src/pedals/<pedal>.ts` declares its knobs, toggles, dip switches, footswitches
(press and hold), hidden options, presets, tap tempo and LEDs. The engine knows
nothing about specific pedals."* Adding a pedal is one file; the UI renders whatever
the definition says.

**What it buys us:** the single largest saving identified so far — the parameter
tables themselves. Names, ranges, value semantics (that "0–1 first, 2 second" CC
coercion we hand-wrote for MOOD), hidden options, dip defaults, CC numbers. That is
exactly the "panels as data" groundwork in
[FUTURE-PEDALS.md](FUTURE-PEDALS.md#groundwork-that-pays-for-itself-whatever-happens-next),
written and tested against the real pedals.

**What it does not buy us:** any audio. It sends MIDI; it does not model sound. And
its UI is a drawing of the pedal, which is precisely the trade dress we should not
copy — take the data model and the value rules, keep our own sheet.

## Parity by construction: Faust and Elementary

Our biggest recurring cost is that every feature is written twice and then held to
`0.00e+0`. Two projects attack that at the root:

- **Faust** (GRAME; GPL compiler, LGPL libraries with an exception) is a DSP language
  whose one source compiles to C++, C, LLVM IR and **WebAssembly**. Generated code
  is self-contained, sample-accurate and deterministic. Writing a new block once and
  compiling it to both cores would make parity true by construction rather than by
  discipline. The catch is that the browser side becomes wasm rather than readable
  JavaScript, and every flag (`-single` vs double, `-ftz`, `-es`) has to be pinned
  and recorded, or the same "identical rounding" discipline moves one level up.
- **Elementary Audio** (MIT) is a JavaScript graph DSL with a native C++ API: one
  graph, rendered on both sides. Closer to our current shape, and permissive, but it
  would replace the hand-written worklet rather than extend it.

**Verdict:** not for existing code. For a *new* block that is expensive to write twice
— an FFT, an oversampled clipper, a wavefolder — Faust is the strongest option on
this page, and worth a one-block experiment before we hand-write an FFT in two
languages.

## Per pedal: what exists, and whether it fits us

| Pedal | Prior art | Licence | What it buys |
| --- | --- | --- | --- |
| Generation Loss MKII | **CHOWTapeModel** (Jatin Chowdhury) | GPL-3.0 (one review site says MIT — verify in-repo) | Jiles–Atherton magnetic hysteresis, wow/flutter, dropouts, bias — a published, paper-backed tape model |
| | **Airwindows** ToTape, Flutter, Wow | MIT | small per-sample tape/blip functions, trivially mirrored in JS |
| | **DaisySP** `Decimate`, `Overdrive` | MIT | rate reduction and drive in a few dozen readable lines |
| Clean | **Airwindows** ButterComp, Density, Pressure5 | MIT | compressor/limiter ballistics that sound like circuits |
| | **DaisySP** `Limiter`, `Crossfade`, `Svf` | MIT | clean, tiny building blocks |
| | **DaisySP** `AdEnv`, `AdsrEnvelope` | MIT | envelope followers to compare ballistics against |
| Lossy | **Airwindows** DeRez / DeRez2, BitGlitter | MIT | an *analogue-flavoured* bit and rate crusher, with µ-law nonlinearity — much closer to Lossy's character than a plain quantiser |
| | **SpectMorph** | LGPL-2.1+ | spectral morphing/smearing, the closest open thing to Lossy's spectral modes |
| | **smbPitchShift** (Bernsee) | free with attribution | STFT frame plumbing worth reading before writing our own |
| Onward | **Beads** (Mutable Instruments) | MIT (open-sourced late 2026) | the most complete open granular engine there is: grains, freeze, feedback, quality modes |
| | **Clouds** / **stmlib** (Mutable) | MIT | the original granular processor, plus their DSP primitives |
| | **GRNLR** | GPL-3.0 | a JUCE granular instrument with a tutorial worth reading |
| Reverse Mode C | **kHs Delay**, **CHOWMatrix**, **Zita-at1** | GPL | delay and time-modulation references; ours already covers most of this ground, and the reverse head is the only genuinely new part |
| Lost + Found | **DaisySP** `Resonator`, `ModalVoice`, `Chorus` | MIT | sympathetic resonator, impulse synth, ensemble — three of the twelve modes |
| | **Mutable Elements / Rings** | MIT | modal and resonator engines in readable C++ |
| | **SpectMorph** | LGPL-2.1+ | the spectral modulator |
| Brothers AM | **chowdsp_wdf** | BSD-3 | wave digital filter library: real circuit models, diode clippers, the academically-cited approach |
| | **ChowCentaur** | BSD-3 | a Klon-style transparent overdrive built with WDF — the nearest published architecture to a King of Tone stage |
| | **BYOD** (Bring Your Own Distortion) | GPL-3.0 | a modular distortion lab for trying circuit topologies |
| | **NAM** + **RTNeural** | MIT (NAM), BSD-3 (RTNeural, verify) | the *other* route: a trained neural capture of the pedal. Cheap to ship, but a capture is a photograph of one unit at one setting — not a clone, and not portable to JS without a port of the inference layer |
| Big Time | **CHOWMatrix**, **kHs Delay** | GPL | multi-tap delay topology |
| | **Faust** | GPL/LGPL | a limiter with oversampling, written once |
| Microcosm | **Beads / Clouds** (MIT) | MIT | the closest thing to Microcosm's granular families that exists openly |
| | **DaisySP** `GranularPlayer` | MIT | a small, readable grain scheduler |
| blooper / Habit / Bad Mood | **Beads**, **SooperLooper** (GPL), **CHOWTapeModel** | mixed | granular playback, loop state machines (quantise, undo, one-shot), tape character |

## The vocabulary libraries, in one place

- **DaisySP** (MIT) — a small, MIT, pedal-oriented library with exactly the pieces
  we keep needing: granular player, modal voices, resonators, limiter, decimate,
  overdrive, one-pole filters, looper utility. Every file is short enough to mirror
  line-for-line in JavaScript, which is the constraint that matters here.
- **Airwindows** (MIT) — several hundred single-function effects, one file each,
  per-sample C++. The best source of "cheap blocks" for tape, compression, bitcrush,
  delay, reverb and tilt EQ, and the author has explicitly blessed reuse with credit.
- **chowdsp_wdf** (BSD-3) and its siblings — circuit modelling when we get to drive.
- **Signalsmith Stretch** (MIT, available as C++ *and* JS/WASM) — pitch and time, for
  the orchestral swell and pitch repeater modes.
- **Mutable Instruments eurorack** (MIT) — Clouds, Beads, Elements, Rings, stmlib.
- **Faust** (GPL compiler, LGPL libs) and **Elementary** (MIT) — for writing a block
  once and running it in both cores.

## The licence trap, spelled out

Two facts decide what we may take:

1. **The project is GPLv3 today** (it derives from MoodEmu, GPLv3). So GPL-licensed
   references — CHOWTapeModel, GRNLR, kHs, Dragonfly, SooperLooper — are compatible
   *now*. They would become incompatible the day a closed-source build is wanted.
2. **Everything permissive (MIT / BSD / LGPL-with-care) is safe in both lanes.**
   DaisySP, Airwindows, chowdsp_wdf, ChowCentaur, Signalsmith, Beads/Clouds, NAM,
   pedalzombie and Elementary are all in this class.

So the rule to write down before adopting anything: **prefer MIT/BSD references, and
when we take a GPL one, record it per file** so a later Lane B decision has a list to
replace rather than an archaeology project. MIT and BSD also require their notices to
travel with the binary — `THIRD_PARTY_NOTICES.md` is where they go, as it already
does for JUCE, the VST3 SDK and the fonts.

## What I would actually do with this, in order

1. **Adopt `pedalzombie`'s data model** for panels-as-data. MIT, our exact pedal
   list, and it is the groundwork round we keep deferring. Data only: names, ranges,
   value rules, CC numbers. No audio, no look.
2. **Treat DaisySP and Airwindows as the vocabulary** for cheap blocks, porting the
   per-sample functions to JavaScript by hand (they are small enough that this is a
   translation, not a rewrite).
3. **Try Faust on exactly one hard block** — the spectral one, or an oversampled
   clipper — before committing to hand-written FFTs in two languages.
4. **Use chowdsp_wdf / ChowCentaur as the reference architecture** for Brothers AM,
   and consciously reject NAM: a capture is not the thing we said we were making.
5. **Diff against upstream MoodEmu** once, and keep our delta documented.

## Sources (fetched 2026-11-01)

- MoodEmu: <https://github.com/MaxTeicheira/Cba-guitar-pedal->
- pedalzombie: <https://github.com/mkaerger/pedalzombie>
- max-bliss (GPL-3.0 M4L controllers): <https://github.com/jehrich/max-bliss> ·
  ChaseBlissEditor: <https://github.com/ElliotGarbus/ChaseBlissEditor>
- CHOWTapeModel: <https://github.com/jatinchowdhury18/AnalogTapeModel> ·
  <https://synthanatomy.com/2020/07/chow-tape-model-free-open-source-tape-saturation-plugin-for-pc-mac.html>
- chowdsp_wdf: <https://github.com/Chowdhury-DSP/chowdsp_wdf> ·
  paper: <https://arxiv.org/abs/2210.12554> · ChowCentaur:
  <https://github.com/AviateAudio/ChowDSP_ChowCentaur>
- DaisySP: <https://github.com/electro-smith/DaisySP> ·
  <https://electro-smith.github.io/DaisySP/>
- Airwindows: <https://www.airwindows.com/> · DeRez (MIT, analog bitcrusher):
  <https://www.airwindows.com/derez-vst/>
- Beads open-sourced (MIT):
  <https://synthanatomy.com/2026/10/mutable-instruments-beads-is-now-open-source-the-clouds-reinvention.html>
  · Mutable Instruments: <https://github.com/pichenettes/eurorack>
- GRNLR: <https://github.com/passivist/GRNLR> · SpectMorph:
  <https://github.com/swesterfeld/spectmorph> · SooperLooper:
  <https://sonosaurus.com/sooperlooper/download.html> · Dragonfly Reverb:
  <https://github.com/michaelwillis/dragonfly-reverb>
- Signalsmith Stretch (MIT, JS/WASM): <https://www.npmjs.com/package/signalsmith-stretch>
- Faust: <https://faustdoc.grame.fr/manual/architectures/> ·
  <https://en.wikipedia.org/wiki/FAUST_(programming_language)> · faustwasm (LGPL-3.0):
  <https://www.npmjs.com/package/@grame/faustwasm>
- Elementary Audio (MIT): <https://github.com/elemaudio/elementary>
- Neural Amp Modeler (MIT): <https://pypi.org/project/neural-amp-modeler/>
