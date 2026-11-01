# The pedal series: candidates

> Status: **research only.** Nothing here is built, scheduled, or promised. This is
> the companion to [FUTURE-PEDALS.md](FUTURE-PEDALS.md): that file holds the
> packaging decision (a series of separate plug-ins in one repository, decided
> 2026-11-01) and the three pedals already studied in depth. This file holds the
> nine added to the list on 2026-11-01 — eight by Chase Bliss, one by Hologram
> Electronics — with what each one would actually cost us.
>
> The open-source code that would help with each of them — the `pedalzombie` data
> model for panels, DaisySP and Airwindows as a vocabulary of cheap blocks,
> chowdsp_wdf for circuit models, Beads for granular, Faust for writing a block once
> — is in [OPEN-SOURCE.md](OPEN-SOURCE.md), with licences and what each is *not*.

## How to read this

- **Effort** is in *rounds* — one shipped slice: DSP in both cores, a parity
  scenario at `0.00e+0`, the panel, and the docs. A round is roughly a day of work
  on the evidence of rounds 1–14, plus whatever the new block costs.
- **Reuse** means blocks that already exist in `native/dsp/mood.h` and
  `src/mood-worklet.js`: `hist`, `capture`, the looper, `Reverb`, `Delay`, `Slip`,
  `Weave`, `LowPass`/`LowPass4`/`Allpass`, the grain engine in `Slip`/`Weave`
  (STRETCH), the frequency shifter, the envelope follower, the ramp generators, and
  CLASSIC's wow/flutter/hiss.
- **Guardrail** is where a pedal collides with a standing rule: one page and no
  menu diving, no clicks, no control shrunk to fit, and both cores bit-identical.
- **Analogue pedals are models, not clones.** Clean, Brothers AM and Big Time's
  preamp and limiter are real circuits. "As close to the original as possible" is a
  claim we can keep for digital pedals; for these it becomes "a model of a circuit",
  and that distinction belongs in the README the day one of them ships.
- Every Chase Bliss pedal here is 9 V centre-negative, stereo on TRS, with MIDI via
  a MIDIbox and two face presets plus 122 over MIDI. Big Time is the exception: 1 A,
  5-pin MIDI, ten face presets, Automatone format. Microcosm is 9 V, 400 mA, TRS
  MIDI, sixteen user presets.

## At a glance

| Pedal | Year | Archetype | The block it forces | Effort | Verdict |
| --- | --- | --- | --- | --- | --- |
| Reverse Mode C | 2023 | multidirectional delay | reverse read head | **2** | cheapest big sound |
| Clean | 2024 | creative compressor | VCA-style dynamics | **2** | cheapest honest one |
| Generation Loss MKII | 2022 | tape machine | tape transport | **2** | MOOD-adjacent |
| Lossy | 2023 | codec artifacts | spectral bands | **2–3** | cheap subset first |
| Onward | 2024 | dynamic sampler | envelope-triggered grains | **3–4** | reuses MOOD's heart |
| Brothers AM | 2025 | dual analogue drive | oversampled nonlinearity | **2–3** | changes what we claim |
| Big Time | 2026 | hybrid rack delay | 3.2 min memory | **5–6** | memory policy first |
| Lost + Found | 2025 | twin multi-effect | mode tables | **6–8** | content, not architecture |
| Microcosm | 2020 | granular multi-effect | 11 algorithms | **6–9** | biggest; needs the groundwork |

---

## Reverse Mode C (2023)

**What it is.** A tribute to the "Octave Tap" mode of the 2008 Empress Superdelay,
made with Empress: a delay of three voices — forward, backward, and up an octave —
that you blend, split apart and sequence. Small Batch release, 16 dips, ~270 mA.

**Front panel.** Knobs **TIME** (max 4.19 s, 8.38 s at half rate), **MIX (ramp)**,
**FEEDBACK**, **OFFSET** (a second time control that pulls the three voices apart),
**BALANCE** (left half solos or blends neighbouring voices, right half plays all
three), **FILTER** (dual high-pass/low-pass). Toggles **MOD CONTROL** (sync / off /
free), **MOD TYPE** (vibrato / tremolo / frequency shift), **SEQUENCE** (cycles the
voices, continuous or input-triggered). Bypass held halves the sample rate; tap/hold
on the other footswitch captures the moment as an infinite loop. Hidden options:
sequencer rate, ramping waveform, modulation shape.

**Blocks.** Three read heads off one delay line with per-voice time ratios; a reverse
head; a feedback loop that stays stable at maximum (the pedal promises infinite
build-up without squealing); a dual filter; vibrato, tremolo and a frequency shifter;
a voice sequencer; half-rate playback; hold-to-loop.

**Reuse.** `Delay`, `LowPass4`, the ramps' phase machinery, the frequency shifter
already written for MOOD's WEAVE, and the freeze/hold path.

**New.** A genuine reverse read head (STRETCH has `dir = ±1` but not a reversed delay
line with crossfaded turnarounds), and a feedback limiter that stays musical at 100 %.

**Effort: 2.** The sound is enormous and the parts are nearly all here.

**Guardrail.** Half-rate mode is a clock change; our core clock is fixed, so this
becomes a read-rate change inside the block. That is a defensible reinterpretation
and should be documented as one.

---

## Clean (2024)

**What it is.** A "creative compressor": two-stage dynamics — compressor, then a
variable limiter that sags — with a dynamic EQ and a swell. The one utility pedal on
the list, and the one that makes the rest of a chain sound better.

**Front panel.** Knobs **SENSITIVITY (ramp)**, **WET**, **DRY**, **ATTACK**
(0.5–300 ms), **RELEASE** (fast / user / slow), **PHYSICS** (subtle-wobbly /
normal / twitchy-unstable), **EQ** (one knob, noon is bypass, three characters chosen
by the **MODE** toggle: shifty / manual / modulated). A fourth footswitch, **AUX**,
is the swell. Hidden: swell in, swell out, gate threshold,
gate release, user release, envelope type (analogue / adaptive / combo), spread
routing, envelope balance. Dips: MISO, spread, latch, sidechain, **noise gate**,
**motion**, **swell aux**, **dusty** (the limiter becomes a crumbly overdrive).

**Blocks.** Feedforward/feedback-blended detection, three envelope-follower types,
VCA-style gain with attack/release ballistics, a limiter stage, sag, a three-mode
dynamic EQ, a gate, a swell, and the dusty nonlinearity.

**Reuse.** The envelope follower from ENV mode, `LowPass4` for the EQ, the gain
smoothing already used everywhere.

**New.** Ballistics that feel like a VCA compressor rather than a follower, and a
sag law. The dusty mode is the first place we would need oversampling.

**Effort: 2.** Cheap, useful, and honest — a compressor is easier to model
convincingly than a fuzz.

**Guardrail.** PHYSICS and the envelope type are second-layer controls, and Clean's
own answer is three-way toggles, not a menu. Same treatment here: they belong on the
panel as cells.

---

## Generation Loss MKII (2022)

**What it is.** The tape machine: wow, flutter, magnetic saturation, dropouts and
snags, twelve measured EQ profiles, and mechanical noise. Descended from Cooper FX's
Generation Loss.

**Front panel.** Knobs **WOW**, **VOLUME (ramp)**, **MODEL (LP)** — a rotary stepping
through twelve EQ profiles sampled from real hardware (CPR-3300 generations, Portamax,
CAM-8, Dictatron, Fishy 60, MS-Walker, AMU-2, M-PEX) — **FLUTTER**, **SATURATE (GEN)**,
**FAILURE (HP)**. Toggles **AUX** (tape stop / filter / fail), **DRY** (none / small /
unity), **NOISE** (none / mild / heavy). Hidden: mechanical noise level and colour
(hum through VCR noise), CLASSIC mode quirks. Dips: WOW, FLUTTER, SAT/GEN,
FAILURE/HP, MODEL/LP, bounce, random, sweep, polarity, CLASSIC, MISO, spread, dry
type, drop bypass, snag bypass.

**Blocks.** Slow random pitch modulation (wow), fast random pitch and amplitude
modulation (flutter), a hysteresis saturator, twelve EQ curves, a dropout scheduler
(drops) and a pitch-hitch scheduler (snags), sample-rate reduction (GEN), resonant
LP/HP filters, mechanical noise, tape stop, freeze.

**Reuse.** CLASSIC mode's wow/flutter/hiss, the TONE filter, `LowPass4`, the
`tapeSpeed` ramp, and the grain read for snags.

**New.** The twelve-profile table (measured, or approximated and labelled as such), a
dropout/snag scheduler, and a hysteresis saturator.

**Effort: 2.** The most MOOD-adjacent pedal on the list, and the one most likely to
sound convincing on the first pass.

**Guardrail.** GEN is sample-rate reduction — decimation inside the block, not a
change of our fixed core clock. Document it.

---

## Lossy (2023)

**What it is.** Made with Goodhertz: the sound of digital audio that has been shrunk
and compromised — codec artifacts, packet dropouts, spectral smearing, and a broken
reverb.

**Front panel.** Knobs **FILTER**, **FREQ**, **GLOBAL** (a macro that is the amount
of processing, in place of a mix), **VERB**, **SPEED**, **LOSS**. Toggles **SLOPE**
(6 / 24 / 96 dB), **PACKETS** (loss / repeat / clean), **MODE** (inverse / standard /
jitter). Bypass held engages the gate; the freeze footswitch freezes spectrally and
evolves as you play. Hidden: gate cutoff, freezer balance, verb decay, limiter
threshold, auto gain, loss gain, weighting (dark / neutral / bright). The reverb can
be moved pre or post with a dip.

**Blocks.** A band-analysis core with three characters — *standard* (the low bit-rate
MP3 sound), *inverse* (only the bands the codec threw away), *jitter* (unstable
spectral freeze) — plus packet loss (random mutes) and packet repeat (frozen smears),
a three-slope band-pass, a gate, a limiter with auto gain, and a reverb.

**Reuse.** The reverb, the gate idea from Onward's neighbours, `Allpass` and the
filters for the band-pass, `hist` for packet repeat.

**New.** The spectral core. Standard and inverse need a band split — an FFT or a
filterbank — and jitter needs a freeze that follows playing. This is the same problem
as Bad Mood's Soup and Lost + Found's spectral modulator: **an FFT that is
bit-identical in JavaScript and C++**, which is the single most expensive piece of
DSP in this whole dossier.

**Cheap subset first:** bit-depth and sample-rate reduction, packet loss and repeat,
gate, band-pass, reverb — no FFT. That is roughly 60 % of the pedal's character and
would ship in one round; the spectral modes land later or not at all.

**Effort: 2–3** with the subset-first plan, more if the spectral core is attempted
properly.

---

## Onward (2024)

**What it is.** A dynamic sampler: two channels that capture what you play based on
how you play it — **Freeze** (sustained) and **Glitch** (rhythmic) — with an octave
voice, deliberate errors, and ducking.

**Front panel.** Knobs **SIZE** (length of the glitch, also the timing of the whole
pedal), **BALANCE (mix)**, **OCTAVE** (down / up / off), **ERROR** (how likely and how
intense), **SUSTAIN** (how long a sample holds), **TEXTURE** (bit reduction / off /
gritty overdrive). Toggles **error type** (timing / condition / playback), **fade**
(long / user / short), **animate** (vibrato / off / chorus). Footswitches engage each
channel; holding one locks its sample while the other stays dynamic. Hidden:
sensitivity, balance, duck depth, user fade, routing, error blend, tilt EQ. Dips:
MISO, spread, latch, **sidechain**, **duck**, **reverse**, **½ speed**, **manual**.
CCs: toggles 21–23, dips 61–68 and 71–78, footswitches 102–109, ramp 52, subdivision
53, dry kill 57, trails 58, tap 93.

**Blocks.** Two envelope-triggered sample heads with hysteresis; grain playback with
an octave-down or octave-up voice; error modes (sample size, mutes, rate shifts,
speed and direction instability); ducking and sidechain; bit reduction and an
overdrive for texture; vibrato and chorus animation.

**Reuse.** `hist`, the grain scheduler from STRETCH, read-rate octaves, `engaged` and
the peak follower for sensitivity, the ramp engine for the auto-scan behaviour.

**New.** Level-triggered sample capture per channel with its own hysteresis, a second
independent sample head, and an error state machine that stays deterministic (a
seeded RNG — we already have per-generator seeds for exactly this reason).

**Effort: 3–4.** The natural second pedal for MOOD players, and the closest to what
MOOD already is.

**Guardrail.** Sensitivity, duck depth, user fade and routing are second-layer
controls; per our rule they belong on the panel. Onward has eight customise dips that would
become eight cells — a real layout problem, but a solvable one.

---

## Brothers AM (2025)

**What it is.** Made with Analog Man: a King of Tone-derived two-channel drive with a
Beano-style treble booster in place of the original Brothers' stacking options. The
only analogue overdrive on the list, and the one that tests the whole premise.

**Front panel.** Per channel **GAIN**, **VOLUME**, **TONE**, plus a hidden **PRESENCE**
(hold that channel's footswitch, turn its TONE). A three-way **mode** per channel
(boost / overdrive / distortion) and a three-way **treble booster** (full / off /
lite). Dips: **HI GAIN** per channel, **MOTOBYP**, **PRESENCE LINK**, **MASTER**
(volume 2 becomes a master), **BANK** (second preset bank), EXP/CV, MIDI/EXT.

**Blocks.** Two independent, stackable clippers — a clean boost, a soft symmetric
overdrive and a harder distortion — with a tone control, a presence trim, and a
treble booster (input high-pass into a class-A-ish boost).

**Reuse.** Almost nothing. This is the one pedal where our existing blocks buy us
only the plumbing.

**New.** Non-linearity that does not alias. At a 64 kHz core, a hard clipper folds;
Brothers AM needs 4–8× oversampling, in both cores, with the same filter — a new
shared block, and the highest CPU per sample of anything in this dossier.

**Effort: 2–3** for a convincing model, but see the verdict: effort is not the only
axis here.

**Guardrail — the important one.** This is an analogue circuit whose feel depends on
what it is plugged into. Shipping it means saying "model" out loud in the README, and
it means the series is not only lo-fi digital boxes. That is a *good* thing for the
project's identity, but it should be a conscious step, not an accident.

---

## Big Time (2026)

**What it is.** Made with Electronic Audio Experiments, in the Automatone format: an
early-80s rack delay rebuilt as a hybrid — stereo analogue preamp, stereo analogue
limiter inside the feedback loop, digital delay, modulation, diffusion and a looper
with 3.2 minutes of memory. Six motorised faders.

**Front panel.** Faders **COLOR** (preamp gain into saturation), **TIME**, **CLUSTER**
(extra taps and diffusion), **TILT EQ**, **FEEDBACK**, **WET**. Buttons **SCALE**
(quantised, Thermae-like time), **MOTION**, **MODE** (mod 3–46 ms / short 46–736 ms /
long / loop), **VOICING**, **STATE** (limiter role), **SHIFT** (alt layer:
**TEXTURE**, **RATE**, **DEPTH**, **CROSSOVER**, **DIFFUSE**). **0.5X** drops to
12-bit and halves the sample rate; **+12 dB** bumps the preamp. Play/dub in loop mode.
Ten face presets, 127 over MIDI, 5-pin MIDI in and thru, stereo balanced or
unbalanced, 1 A.

**Blocks.** A saturating preamp; a three-level limiter in the feedback path; a delay
with musical quantisation; multi-tap cluster; diffusion; tilt EQ with a crossover;
motion LFO; a fidelity switch; and a 3.2-minute stereo looper.

**Reuse.** `Delay`, the diffusion-ish smearing in `Weave`, the tilt filter, our
looper and its START/END/crossfade machinery.

**New.** Memory: 3.2 minutes stereo is roughly six times our 30 s take, so ~22 MB per
core as int16 — the same policy question as Habit, only worse. Plus motorised faders
in the UI, and Thermae-style quantised delay time.

**Effort: 5–6.** A second-generation pedal for us.

**Guardrail.** Faders are not a problem in principle — the panel already has a slider
cell for START — but six faders plus five buttons plus a shift layer is a bigger
panel than anything we have drawn, and the 900 × 712 sheet would have to grow.

---

## Lost + Found (2025)

**What it is.** Chase Bliss's first true multi-effect: two channels, twelve effect
modes arranged as six pairs, blendable in parallel or series, with a compressor/
saturator at the end. "A little museum of the rare and strange."

**Front panel.** Knobs **TIME**, **MIX**, **MODIFY** (bidirectional: left half is
effect A, right half is effect B, noon is neither — and the glue stage alone),
**BLEND** (which channel is louder in parallel, or the second effect's mix in series).
Three-way per-channel **effect type** selectors. Three-way **ROUTING** (parallel,
L→R, L←R). Dips: MISO, spread, latch, **UNSYNC**, **TRAILS**, **BANK**, **L SWAP**,
**R SWAP**. Hidden: **ALT** (a third parameter per mode), two-sided **EQ**, **SPILL**,
spread routing.

**The twelve modes** (six pairs, each with an ALT parameter):

1. **Reverb** — slow-verb (wobbly, shoegaze) / ambient reverb.
2. **Pitch** — orchestral swell / pitch repeater.
3. **Warp** — pinging phaser (modify = poles) / spectral modulator (modify = depth).
4. **Delay** — tape echo (modify = feedback, alt = stability) / grain tumbler
   (modify = feedback, alt = grain shape).
5. **Synth** — impulse synthesizer (ports, texture, attack/release) / sympathetic
   resonator (onset, feedback, octave).
6. **Bend** — ensemble expander (voices) / gen lite (drops and snags).

**Reuse.** Reverb, delay, the allpass chain for the phaser, the grain engine for the
tumbler, tape for gen lite.

**New.** A modal resonator bank, an impulse synth, a spectral modulator, an ensemble,
the routing matrix, and — the interesting control idea — **a continuous blend between
two effect personalities on one knob**. That last one is genuinely novel and would be
worth copying for its own sake.

**Effort: 6–8.** Expensive for *content*, not architecture: twelve modes × three
parameters × two channels is a table, not a breakthrough — which is exactly the
argument for panels-as-data.

**Guardrail.** Twelve modes want a mode display, not a menu; the two-channel
structure wants a symmetrical panel. Both are compatible with our rules, but this is
the pedal that would force the panel system to become data.

---

## Microcosm (Hologram Electronics, 2020)

**What it is.** A granular multi-effect: eleven algorithms in four families, each
with four variations (44 total), plus a sixty-second phrase looper, a reverb, a
resonant filter and pitch modulation.

**Front panel.** Knobs **ACTIVITY** (the macro for whichever algorithm is running),
**SHAPE** (window; shift = modulation rate), **FILTER** (resonant low-pass; shift =
resonance), **MIX** (shift = effect level), **TIME** (tap tempo and divisions),
**REPEATS** (shift = modulation depth), **SPACE** (four reverbs; shift = decay),
**LOOP LEVEL** (shift = fade). **Preset selector** encoder. Three footswitches:
tap / record-play-dub, on / variation, hold / stop-erase. Sixteen user presets.

**The eleven** —

- **Micro Loop** — *Mosaic* (overlapping loops at different speeds), *Seq* (loops
  rearranged into new rhythms), *Glide* (short overlapping loops shifting in pitch).
- **Granules** — *Haze* (clusters of micro-loops as an immediate wash), *Tunnel*
  (cyclical micro-loops, drone-like), *Strum* (chains of recent note onsets).
- **Glitch** — *Blocks* (input-triggered glitches), *Interrupt* (signal interrupted
  by pitch-shifted bursts), *Arp* (samples sequenced into arpeggios).
- **MultiDelay** — *Pattern* (four rhythmic tap patterns), *Warp* (taps filtered and
  pitch-shifted).

**Looper.** 60 s, with pre-FX / looper-only / burst / quantise modes, reverse
playback, undo/redo on a long press, and erase.

**Reuse.** The most of any pedal here: our `hist`, capture, looper, `Reverb`,
filters and grain scheduling are the same primitives Microcosm is built from.

**New.** The eleven algorithms and their variations — a content problem — plus preset
storage and a global configuration menu (input mode, looper shape, MIDI thru/clock,
input level, MIDI channel, hold style, bypass style, looper operation) that in our
version has to become panel cells, because that is the rule.

**Effort: 6–9.** The biggest item in this file. It is also the best stress test of
"pedals as data": if the panel system and the block contract are right, Microcosm is
eleven tables and a switch.

**Guardrail.** Its global menu is precisely the menu diving we removed in round 11.
A Microcosm bank would be a conscious reinterpretation, not a port. It is also not a
Chase Bliss pedal — the series stays "unofficial studies of instruments we own",
never a manufacturer's lineup.

---

## What the list tells us

1. **The blocks cluster; the pedals do not.** Five shared blocks would carry most of
   this list:
   - **oversampled nonlinearity** — Brothers AM, Clean's dusty mode, Big Time's
     preamp and limiter, Onward's texture, Gen Loss saturation;
   - **spectral band processing** — Lossy, Lost + Found's spectral modulator, and
     Bad Mood's Soup; one FFT block that is bit-identical in both cores pays for
     three pedals;
   - **a reverse read head** — Reverse Mode C, Onward's reverse dip, Microcosm's
     reverse;
   - **a generalised granular engine** — Onward, Microcosm, Lost + Found's grain
     tumbler, Big Time's cluster, grown out of STRETCH;
   - **tape transport** — Gen Loss MKII, Big Time's motion, MOOD's CLASSIC: wow,
     flutter, dropouts, snags, saturation, one block instead of three copies.
2. **The cheapest three are Reverse Mode C, Clean and Generation Loss MKII** — each
   roughly 80 % existing blocks, one round each plus a round of tuning.
3. **The expensive ones are expensive for content, not architecture.** Lost + Found
   is twelve modes; Microcosm is forty-four variations. That is a table-shaped
   problem, and it is the strongest argument for panels-as-data in the whole
   dossier.
4. **The analogue pedal is the fork in the road.** Everything else is DSP in a
   digital path; Brothers AM means committing to oversampling and to the word
   "model" in the README.
5. **Memory is the recurring wall.** Big Time wants 3.2 minutes, Habit wants three,
   Microcosm sixty seconds. One written policy (int16 or a lower reel rate, and
   never in the saved state) settles all three.
6. **Three control idioms we do not have yet**, all worth stealing: a continuous A/B
   blend on one knob (Lost + Found's MODIFY), a macro that is "the amount of
   processing" (Lossy's GLOBAL, Microcosm's ACTIVITY), and a bank of faders (Big
   Time). The first two are cheap and would improve our own panels.

## Recommended order, if the series is picked up

1. **Reverse Mode C** — nearly every block is here, and it proves the reverse head.
2. **Clean** — a utility that makes the other pedals better, and cheap to do honestly.
3. **Generation Loss MKII** — the tape transport block, which three later pedals need.
4. **Lossy** — the first spectral decision; ship the cheap subset, then argue about
   the FFT.
5. **Onward** — the sampler that reuses MOOD's heart; do it once the grain engine has
   been generalised.
6. **Microcosm** or **Lost + Found** — the content-heavy pair, and only after
   panels-as-data exists.
7. **Big Time** — after the memory policy is written down.
8. **Brothers AM** — whenever the oversampling block is wanted. Cheap in effort but
   expensive in *positioning*: it should land once the series' identity as digital
   studies is established, not before.
9. Bad Mood, Habit and blooper stay where they are, in
   [FUTURE-PEDALS.md](FUTURE-PEDALS.md).

## Sources (fetched 2026-11-01)

- Reverse Mode C: <https://manuals.plus/chase-bliss/reverse-mode-c-manual>,
  <https://www.manualslib.com/manual/3323157/Chase-Bliss-Audio-Reverse-Mode-C.html>,
  <https://guitar.com/news/gear-news/chase-bliss-reverse-mode-c-empress-effects/>.
- Clean: <https://manuals.plus/chase-bliss/cln01-clean-stereo-compressor-manual>,
  <https://www.manualslib.com/manual/3615947/Chasebliss-Clean.html>,
  <https://www.elektronauts.com/t/chase-bliss-clean-the-creative-compressor-pedal/223147>.
- Generation Loss MKII:
  <https://www.manualslib.com/manual/2832198/Chase-Bliss-Audio-Generation-Loss-Mkii.html>,
  <https://midi.guide/d/chase-bliss/generation-loss-mkii/>,
  <https://guitar.com/reviews/effects-pedal/chase-bliss-generation-loss-mkii-review/>.
- Lossy: <https://www.chasebliss.com/lossy>,
  <https://midi.guide/d/chase-bliss/lossy/>,
  <https://www.engadget.com/chase-bliss-and-goodhertzs-lossy-is-a-pedal-that-makes-your-guitar-sound-like-a-crappy-mp3-141611147.html>.
- Onward: <https://manuals.plus/chase-bliss/onward-dynamic-sampler-manual>,
  <https://www.guitarpedalx.com/news/gpx-blog/chase-blisss-latest-onward-dynamic-sampling-device-is-a-really-smart-tom-majeski-pet-project--extrapolation-from-his-former-cooper-fx-outward-pedal>,
  Onward MIDI manual (Chase Bliss PDF).
- Brothers AM: <https://www.chasebliss.com/s/Brothers-AM_Manual_Pedal_Chase-Bliss.pdf>,
  <https://www.premierguitar.com/gear/reviews/chase-bliss-brothers-am-review>,
  <https://www.guitarpedalx.com/news/gpx-blog/the-much-anticipated-chase-bliss-x-analogman-collaboration-is-with-us-an-updated-brothers-variant-covering-the-analog-man-kot-and-beano-boost>.
- Big Time: <https://manuals.plus/chase-bliss/big-time-pedal-manual>,
  <https://www.gearnews.com/chase-bliss-big-time/>,
  <https://guitarbomb.com/blog/chase-bliss-big-time/>,
  <https://www.synthtopia.com/content/2026/05/04/chase-bliss-introduces-big-time-hybrid-echo-pedal/>.
- Lost + Found: <https://manuals.plus/chase-bliss/lost-plus-found-multi-fx-pedal-manual>,
  <https://device.report/manual/17479209>,
  <https://synthanatomy.com/2025/08/chase-bliss-lost-found-a-multi-fx-pedal-packed-with-strange-but-inspiring-algorithms.html>,
  <https://pedalfilter.com/chase-bliss/lost-found>.
- Microcosm: <https://www.manualslib.com/manual/1852211/Hologram-Microcosm.html>,
  <https://waveformmagazine.com/waveform-reviews/microcosm-hologram-electronics/>,
  <https://www.guitarworld.com/reviews/hologram-electronics-microcosm-review>,
  <https://www.guitarpedalx.com/news/gpx-blog/hologram-electronics-reveals-the-microcosm---the-most-mature-and-feature-rich-granular-synthesis-multi-fx-unit-to-date>.
