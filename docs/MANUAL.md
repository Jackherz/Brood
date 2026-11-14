# Brood manual

What every control does, in the plug-in and the browser version, and how the sound is
made. For installing, see [INSTALL.md](../INSTALL.md); for building and testing, see
[DEVELOPMENT.md](DEVELOPMENT.md).

- [Playing](#playing)
- [The plug-in's panel](#the-plug-ins-panel)
- [MIDI](#midi)
- [What each knob says](#what-each-knob-says)
- [Sound model](#sound-model)
- [The sheet](#the-sheet)
- [Not implemented](#not-implemented)

## Playing

Drag a knob vertically, use the mouse wheel or arrow keys, or double-click to
reset it. Tap **WET CHANNEL** to turn the effect on/off; hold it to freeze, or
throw the **FREEZE** switch to have the freeze arrive without a
held foot — the wet side comes up held and rings on its own until you let it go.
The looper is always listening: tap **MICRO-LOOPER** to capture the recent past,
tap again to stop, or hold while playing to overdub. Holding while listening
records a new loop for the duration of the hold, up to the buffer capacity.
The **LOOP TRIM** row under the waveform sets how much past the looper may keep
(**MEMORY**, up to 30 s; `PEDAL` is the hardware behaviour), where the window
stops (**END**) and where it begins (**START**). END is an end marker and START a
start marker: each moves itself, so the window can be trimmed from either side.
The waveform dims whatever the trim has left outside. (In the plug-in, START and
END are markers you drag on the waveform itself; see below.)
The A and L keys trigger the wet and loop switches when focus is not in another
control. **HOW TO PLAY** opens the in-app field guide.

Five factory presets set the controls. Click the name in the patch box for the preset
browser: search, categories and stars, the same as the plug-in's. **SAVE** stores what
you hear under a name in local browser storage (the plug-in keeps its presets as files). A patch does not automatically engage either
channel. Captured audio is temporary and is not saved in a patch.

## The plug-in's panel

Resting the mouse on any knob, switch, stepper, slider or button makes the readout
at the top of the side column explain it. The browser version says the same thing
in its footer line, and keeps each knob's whisper on the panel.

The editor is a 1200 × 820 panel that can be resized from 840 × 574 up to
2000 × 1367; the layout is drawn in design coordinates and scaled as a whole —
the controls included, so the text inside a field or a cell grows with the window
instead of staying at its pixel size — and labels cannot collide when the window
changes. Everything is on that one page, because a second page is a menu, and the
pedal this follows has none. It reads in three tiers:

1. **The pedal**: the take along the top, with its START and END markers; under it the
   two footswitches, the six knobs under three column names (WET CHANNEL, SHARED,
   MICRO-LOOPER) and the mode selectors. FREEZE, TRAILS and PRE-LOOP / POST-LOOP sit
   under the wet column, SMOOTH under CLOCK, and WEAR between the two footswitches.
   With WEAR on WORN, an **AFTER LOOP** switch appears under it. Off, worn is recorded into
   the loop as it always was. On, worn is heard but not recorded: the loop keeps a take as
   clean as PEDAL's (overdubs don't follow the wobble, and the hiss stays out of the take),
   so turning WEAR back gives the loop back clean. It has a little less grit than worn in
   the loop, because the rough conversion on the way in is what a loop records. In the
   browser version it sits beside CLASSIC.
2. **The side column**: the readout, then three numbered panels in the order you reach
   for them while playing: `01 LOOP` (MEMORY, WET/LOOP and the loop switch), `02 MODULATION`
   and `03 INPUT`, which is set once per session.

   **CAPTURE or RECORD** sets what the loop switch does with nothing playing. CAPTURE is
   the pedal's way: a tap keeps the recent past, up to MEMORY. RECORD is a regular
   looper: a tap starts recording what you play and a second tap plays it, or hold the
   switch for as long as you play and let go to hear it. In RECORD, **BARS** chooses FREE
   (until the next tap) or 1 to 16 bars, after which it plays by itself; a tap before
   then still ends it early. With bars chosen and the host playing, recording waits for
   the next bar line so the loop lands on the grid; **AT TAP** starts it the moment you
   tap instead. With the host stopped, or in the browser version, it always starts at
   the tap. Bars are counted at the host's tempo and bar length (the last tempo it sent,
   else 120 bpm, in 4/4), so moving CLOCK while recording doesn't change them. If MEMORY
   runs out first, the recording ends there and keeps what was played first. With a
   loop playing, a tap stops it and a hold overdubs, in both modes.

   **OVERDUB**, beside the LOOP switch, latches: switched on, it layers into the playing
   loop until you switch it off, so a controller that can't hold a button (Push's knobs
   and buttons, say) can overdub. It's greyed out unless a loop is playing, and it
   switches itself off when the loop stops. A hold on LOOP still overdubs while OVERDUB is
   on, and letting go doesn't end the latched layer. FADE sets how much of the take
   survives each pass, as for a hold.

   **CLEAR** and **UNDO** sit on the take strip's heading, beside GAIN. CLEAR empties the
   take; a playing loop fades out first, so it doesn't click. UNDO steps back once: it
   brings back a cleared take, goes back to the take before a new one (or to nothing, after
   the first), or takes off the last overdub layer. Press it again to redo; it reads REDO
   when that's what it will do. Undoing a clear or a take leaves the loop stopped, ready
   for a tap; undoing a layer keeps a playing loop playing (it dips for a moment while
   the take is swapped). The undo step isn't saved with your project, so a reopened set
   starts with nothing to undo, and a cleared take isn't saved at all.
3. **Under the hood**: the six sliders along the bottom. The last, **NOISE**, sets how much
   of the worn clock's hiss is let through; it only does anything with WEAR on WORN.

**Before there's a take, the strip shows what's coming in.** The input scrolls past with
the newest audio at the right edge, over the window a tap would keep (MEMORY's length,
so a slow CLOCK scrolls more slowly and holds more). It shows what the looper hears: the
wet side too when it's PRE-LOOP, and WEAR's worn sound when WEAR is before the loop, but
nothing that comes after the looper. In CAPTURE all of it is bright, since a tap keeps
it. In RECORD it stays dim until you tap. While a recording or a CAPTURE hold runs, a
START rule marks where the take begins and rides left as it grows, and the heading
counts the seconds kept. The same live view takes over from a take you've stopped
while a new one records over it (RECORD recording or waiting for the bar line, or a
CAPTURE hold), and comes back after CLEAR. Otherwise, with a take caught, the strip
shows the take, so START and END can still be edited, and an overdub redraws as you lay
it down.

**START and END are markers on the waveform.** Grab one and drag it, all the way to
either end of the take in every mode, or drag inside the window to slide it along
the take; a press outside the window brings the nearer marker there. Moving START keeps
the end where it is, END moves only itself, and a double-click opens the whole take
again. In TAPE a marker that needs more room moves LENGTH with it, and turning LENGTH
leaves a moved START where it is. A window dragged against the end of the take keeps
following the newest audio. Each marker is a
full-height rule with a tab naming it — START's hangs from the top, END's from the
bottom, so they stay readable even when the window is a sliver.

**WET/LOOP shows its modulation.** Drop a source on it and its swing appears as a bar in
the source's colour, a small tab moves to where the sound has it now, and the figure at the
end of the row follows it.

**Values sit where you are looking.** Resting on or turning a knob shows the exact
value under it, in ink; the modulation rate prints its period (or,
synced, its span of the beat) at the end of its row; MEMORY, WET/LOOP and the hood
sliders carry their figures already. The knobs have no ring: five marks are the
scale, and they turn to ink on hover. Each tempo **SYNC** switch sits directly
under the knob it governs, and only in the modes that can use it.

The plugin normally processes the host track's input. The editor also offers a
generated demo and local file input. Plugin settings, wet on/off state **and the
recorded loops** are stored in the DAW project — the two takes travel with the
state as gzipped 16-bit PCM and are swapped back in on the audio thread after a
reload, so a bounce or a reopened session plays the loop you were hearing rather
than an empty buffer. Effect tails and loaded file audio are not saved. The file
source returns to live input after a project reload. The patch box in the masthead
has the same five factory presets as the browser, and a factory preset is a whole
state: every control goes back to its default before it sets its own. Click the name
for the preset browser, the same one every plug-in in the range uses: search, category
keys and stars; clicking a preset plays it, KEEP (Return) keeps it, CANCEL (Esc) puts
back the sound you had, and right-clicking a user preset renames, files or removes it.
User presets are `.broodpreset` files in `~/Library/Audio/Presets/Glyph Audio/Brood/` on macOS, so
every project and every instance shares them. The four user slots from round 18 become
presets called User 1 to User 4 the first time this version opens. A preset never
engages a channel.

**Drop an audio file** on the window, from Finder or anywhere that drags files, and it becomes the loop, at the looper's clock.

**The Bench** along the bottom passes a moment between plug-ins, either way: SEND writes
what the loop is playing to Brood's own folder (`~/Music/Glyph Audio/Brood/` until you choose another), an instrument in the range (Silt) shows it
and takes it as its sample, and TAKE makes what Silt sent the loop here. FOLDER opens it in
the Finder, chooses another folder, or picks a category, which is a folder inside it (make a
Guitar one, and what you send goes there until you change it). Each plug-in has its own
folder and categories, sending between them works whichever you choose, and everything sent
stays there. The menu beside SEND can send the
exact output instead: 4, 8 or 16 bars from the next bar line (at once if the host is
stopped). Silt keeps the first 30 seconds of anything longer.

Sets saved under the old product name still open: the plug-in IDs did not change,
and the old state tag is read as the new one.

## MIDI

The native plugin accepts MIDI CC on any channel when the host routes MIDI to
the effect. Changes are applied at audio-block boundaries:

| CC | Control |
| --- | --- |
| 14–19 | Time, Mix, Length, wet Modify, Clock, loop Modify |
| 20 | Rate of both modulation sources |
| 52 | Hold both modulation sources (`0` running, `1–127` held) |
| 66, 67 | Bounce, Sweep (`0` off, `1–127` on) |
| 53, 54 | Wet and loop clock division |
| 71 | WEAR: pedal (`0`) or worn (`1–127`) |
| 21–23 | Wet mode, routing, loop mode (`0–1` first, `2` second, `3–127` third) |
| 28 | WET/LOOP balance (the pedal's LEVEL BALANCE) |
| 75, 78 | Trails, Smooth (`0` off, `1–127` on) |
| 102, 103 | Loop and wet on/off (`0` off, `1–127` on) |
| 105 | Wet freeze, the same as the FREEZE switch (`0` off, `1–127` on) |

CC 21 keeps the pedal's three-mode mapping exactly, so a hardware controller set up for the pedal
never reaches the fourth mode by accident: **WEAVE** is selected in the editor or
by automating the `wetMode` parameter (value 3) instead. CC 20 is the sources' rate and
66/67 are BOUNCE and SWEEP, on both sources. CC 73, 74 and 76 used to carry
SPREAD, DRY KILL and LATCH; those controls are gone (WIDTH does the first, MIX the
second, the FREEZE switch the third), so the pedal's controllers for them do
nothing now. Routes and depths have no CC: automate the `mod1_time`-style depth parameters instead. NOISE has no CC either. Loop memory and END are host-only parameters too.

Tempo sync has no CC in the pedal's map; automate the `sync` parameter, or switch
it on in the editor. TAPE, CAPTURE / RECORD, BARS and AT TAP have none either: the
pedal's map has nothing for them, and a spare number could clash with a controller set
up for the pedal. Automate `tapeDelay`, `recMode`, `recBars` and `recAtTap` instead.
OVERDUB, CLEAR and UNDO have no CC for the same reason; they're host parameters
(`overdub`, `clear`, `undo`), so Push and other controllers that map plug-in parameters
reach them. CLEAR and UNDO act when switched on and set themselves back to off.

What a host or controller shows for each value is kept to 8 characters, a number and its
unit where there is one (`2.31 s`, `426 ms`, `1/8D`, `16.0 kHz`, `1.00x`, `-6.0 dB`,
`+40%`), so it fits a controller's display such as Push's. The editor's readout keeps
the fuller wording. Hosts that let you type a value accept the same form.

## What each knob says

Every knob has a sentence as well as a label. It should tell you which way to
turn without telling you what is inside — the mechanism stays a little hidden, the
consequence does not. In the plug-in it shows in the readout while you rest on the knob
and goes to screen readers as its description; in the browser version it sits under the
knob, the exact value replaces it while you touch it, and the field guide lists the
current mode's set.

| Knob | Reverb | Delay | Slip | Weave |
| --- | --- | --- | --- | --- |
| TIME | how long the room keeps breathing | how far back the echo remembers | how wide a slice of time is borrowed | how far back the threads reach |
| MODIFY | how much the surface smears | how much the echo wants to stay | which way, and how fast, time slides | long cloth, or short clipped stitches |

| Knob | Env | Tape | Stretch |
| --- | --- | --- | --- |
| LENGTH | how much of the moment gets caught | the size of the window that repeats | how wide each grain is stretched |
| MODIFY | how softly it must speak to be caught | which way the tape runs, and how fast | how far the grains drift from now |

Shared: **MIX** — *how much of what you hear is not here*; **CLOCK** — *the
heartbeat everything obeys*; **ROUTING** — *what the wet side is allowed to hear*.

## Sound model

- **Getting in and out of the core clock.** The engine runs at its own clock (the
  CLOCK knob), so every host sample is resampled on the way in and on the way out.
  On the way in each core tick reads the input at its own moment on a Hermite
  curve — until round 18 it took the latest host sample, which at 48 kHz repeated
  one sample in four and recorded a stair-step jitter into every loop. On the way
  out a Hermite curve runs through the last four ticks, and both sides are filtered
  by 6th-order Butterworth low-passes at 0.45 × the clock (they were 4th order).
  Every delay line, grain and loop read is Hermite-interpolated too. **CLASSIC**
  keeps the old path — held input, straight-line output, 4th-order filters — as
  part of its grit. The output limiter now bends from 0.7 instead of 0.9, to the
  same ceiling, so loud tails are rounded rather than flattened.
- **Stereo.** Each engine is mono in and stereo out, and there is one per input
  channel. The left engine supplies the left output and the right engine the right
  one — on a mono track the two engines are identical, so what you hear is one
  engine's full stereo: the reverb's two sides, WEAVE's panned threads and WIDTH.
  (Until round 17 both outputs came from the engines' *left* sides, which made all
  of that dual mono on any mono source.)
- Wet modes: **Reverb**, **Delay**, **Slip** and **Weave**; loop modes: **Env**,
  **Tape** and **Stretch**. Three-way routing selects input, input + loop, or loop for the
  wet side. When the loop is listening, its recent history includes active wet
  audio; overdubs use the clean input.
- **WEAVE** is an addition, not one of the pedal's modes: a re-sampler in the spirit of the
  [Fieldtone Weaver](https://fieldtone.co). Three threads read a three-second
  memory at once, each with its own pitch (drawn from a root-sympathetic set of
  intervals), direction, grain size and dropout. TIME sets how far back the
  threads may reach; MODIFY walks the fabric from long overlapping cloth
  (texture) into short clipped stitches (rhythm), adding holes as it goes. The
  pattern is re-rolled from a seeded generator whenever the integer position of
  TIME or MODIFY changes, so a given knob position always draws the same fabric
  — chance you can return to, and no undo. Weaver's fixed capture window,
  Texture/Rhythm switch, root-aligned pitch set and per-element levels are the
  reference; its microphone, trigger outputs and CV are not modelled.
- **TAPE**, beside SYNC under TIME in DELAY, makes the delay a tape echo. Off, a new TIME
  crossfades between two fixed read heads and the pitch stays put. On, one head glides
  to the new time, so the echoes bend in pitch the way a tape echo's do (an octave down
  or a fifth up at most), with a little wow and flutter and a gentle saturation in the
  repeats. While FREEZE holds the echo, TIME is ignored and the head reads whole
  samples, so a frozen echo stays exactly as it was for as long as it's held; TIME
  takes effect again when FREEZE lets go. Synced, the head glides between divisions and
  lands on the beat. Switching TAPE under sound crossfades over 30 ms.
- **TEMPO SYNC** is three switches, each under the knob it governs and each drawn
  only in the modes that can use it. `SYNC` under TIME turns the delay's TIME knob
  into a musical division — 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D, 1/2,
  1/1 — and does the same for WEAVE's reach; it is absent in REVERB and SLIP,
  which have nothing to put on the beat. `SYNC` under LENGTH (in ENV) makes the
  envelope's slice a division of the beat instead of a free length, and appears
  only in ENV. The modulation panel has its own SYNC for the rate (eight bars to a
  sixteenth). All three are independent: a synced delay can run against a free
  envelope. In the plugin the tempo comes from the DAW's playhead, every block, so
  tempo changes are followed; in the browser you set it on the TEMPO field. The
  division is real time, converted into core ticks at the current clock, so the
  echo stays on the beat when CLOCK moves. The readout shows the division and its
  milliseconds (a whole note at 120 BPM is 2 s, which the 2.1 s delay buffer clamps
  to 1.995 s).
- CLOCK selects 2–64 kHz steps, changing pitch, timing, bandwidth and loop
  capacity together. **SMOOTH** lets it rest between those steps and glide
  through them, without changing the rate of any step. The fixed 64,000
  sample history holds approximately 1–32 seconds depending on clock rate.
- **LOOP MEMORY** and **END** are our addition. On the pedal the capture window is
  whatever the 64,000-sample buffer holds at the current clock; MEMORY lifts that
  ceiling to a chosen length in *real time* (1–30 s, and the clock is clamped up
  if it could not fill the window). At the far left it reads `PEDAL` and behaves
  exactly like the hardware. LENGTH sets how big the window is — in TAPE it is the
  most recent part of the capture, so turning LENGTH walks the start forward, and
  at `1` the window is the whole take, reaching the very beginning of a 30 s
  capture; the travel is linear in the knob (each quarter turn moves the start a
  quarter of the way in), which is what makes a long take usable rather than a
  long wait on the last few seconds. When LENGTH moves, the playhead keeps its place
  as a share of the window, and the window follows LENGTH at up to four times
  playback speed, so even fast, deep LENGTH modulation bends the loop's pitch (two
  octaves up at most) rather than jumping or clicking. On a long take a big turn of
  LENGTH takes a moment to settle: about 8 s for a full turn on a 30 s take. **END** is an end marker: it says where the
  window stops playing, and dragging it moves the end marker and nothing else —
  the window grows or shrinks from the right while START stays where you put it.
  **START** says where that window begins inside the capture, which is what ENV and
  STRETCH were missing — with END alone you could only ever hear the front of a
  take. It is measured against the take rather than against the room END leaves,
  so the two markers do not drag each other around; START only gives ground when
  the window would otherwise run past the end of the take (and a window that fills
  the take cannot move at all, because there is nowhere to slide to). START means the
  same in every loop mode: 0 is the oldest audio and 1 the newest, and it reads `THE
  OLDEST`, a percentage, or `THE NEWEST`. Its default is 1, so TAPE plays the newest
  audio as it always has, and at 1 the window keeps against the end of the take as
  LENGTH turns. Switching loop mode keeps the playhead where it is in the take when the
  new window holds it, and crossfades it in when it doesn't. Sets and presets saved
  before START worked this way are rewritten when they load, so they sound as they
  did. Moving it is **not** a jump: the read head crossfades from where the loop
  was playing to where it now begins over 25 ms, weighted by a raised cosine, and
  a knob still being dragged simply starts the next fade when the last one lands.
  A test sweeps the whole travel while the loop plays and checks the worst
  sample-to-sample step stays within the signal's own slope. The waveform display
  dims everything outside the window. Both cores allocate for the longest possible
  loop up front (about 32 MB), so MEMORY never reallocates on the audio thread.
- **SOFT** sits beside SYNC under LENGTH, in ENV only. The slices fade in and out over
  20 ms instead of 4, a new slice takes over from the last rather than jumping the read
  head, and a moving LENGTH glides, so nothing clicks. It's a rounder, less percussive
  stutter; off is the sharp cut the pedal makes. Both cores agree
  (`tests/parity/scenarios/env_soft.txt`).
- **PRE-LOOP / POST-LOOP**, in the wet column (BEFORE THE LOOP in the browser version): PRE-LOOP puts the wet side ahead of the looper: input,
  then wet, then loop. Catches and overdubs keep the wet sound, and the Bench's SEND
  carries it. It hears only the input then, so ROUTING stands aside. POST-LOOP, the loop stays
  clean and the wet side plays over it (`tests/parity/scenarios/wet_pre.txt`).
- **HI-Q**, in the switches, uses longer filters for the crossings into and out of CLOCK: far
  fewer artifacts and a fuller top at low clocks. The wet side and the loop run a little later
  (eight clock ticks, so 1 ms at 8 kHz and 4 ms at 2 kHz); the dry side isn't delayed, so
  no latency is reported to the host. CLASSIC ignores it (`tests/parity/scenarios/hi_q.txt`).
- **GAIN**, in the LOOP panel's heading, is the loop's own level, -24 to +12 dB: drag up or down, Shift
  for fine, double-click for 0 dB. The waveform grows and shrinks with it, and SEND sends the loop at this level.
- **MODULATION** is two sources, each with its own rate, shape and UNI, BOUNCE, SWEEP,
  SYNC and HOLD. Drag a source onto any knob or WET/LOOP to route it. Around a knob, the rings are
  handles: drag up or down to set that source's depth (negative inverts), and
  where there's no ring yet it makes one for the selected source. Alt-drag does the same, double-click a ring to remove that source, Delete on a clicked knob removes both,
  right-click for the list,
  and ROUTES lists them all. CC 20, 52, 66 and 67 act on both sources
  (`tests/parity/scenarios/mod_routes.txt`).
- **ENV** slices when it hears the input: LENGTH and MODIFY act on those slices, so with
  the loop playing and nothing coming in above MODIFY's threshold, they have nothing to
  move. The knob says so while you touch it.
- **WET / LOOP** blends two complete worlds rather than fading one into the
  other: the loop keeps its own level across the whole knob and the wet side adds
  on top, so the middle hears both. It used to duck the direct loop against the
  wet level, which meant the middle of the travel was the wet side only — and in
  SLIP and WEAVE, which hand back little of the loop, the loop vanished until the
  knob was nearly at LOOP. The duck is gone; both cores agree bit for bit
  (`tests/parity/scenarios/balance.txt`) and the level is pinned by tests in both
  implementations.
- **The switches** sit with what they act on: FREEZE and TRAILS under the wet column,
  SMOOTH under CLOCK, WEAR between the footswitches. Under the hood is sliders only. SYNC left the panel in round 16 to sit with the knob it governs,
  so it can disappear in the modes that have no use for the beat. LATCH went
  because the FREEZE switch does the same job without having to hold anything (the
  one thing it does that nothing else now can: latching the loop's overdub hold,
  so a tap leaves you recording on top — that is a held gesture again). DRY KILL
  went because MIX at the top of its travel already removes the dry, and SPREAD
  went into WIDTH.
- **Under the hood** is the row along the bottom of the editor (browser: the
  block at the foot of the side panel): the options the pedal keeps behind both
  footswitches. **TONE** blends the wet bus towards a 2.6 kHz hi-cut — fully right is
  bit-transparent, so it can stay open. **FADE** sets how much of the old loop
  survives each overdub pass: 0 is the pedal's slow erosion (about 3 % a pass), 1
  lets the new layer replace it entirely. It only does anything *while you are
  overdubbing*, and overdubbing is the hold gesture: press and hold **MICRO-LOOPER**
  / the LOOP footswitch while a loop plays, and play over the top. Holding it while
  the looper is still listening records a new loop instead. **WIDTH** took over
  from the removed SPREAD switch: fully left is mono, 0.5 is the image as built,
  and past the middle a decorrelated copy of the wet bus is pushed out into the
  sides. It acts on the effect bus only — the dry never moves. **WET DIV** and **LOOP DIV** run one side on
  a divided clock — 2× or 4× slower — so the wet side's room and echo stretch out
  while the loop drops an octave or two, or the other way round. **CLASSIC**
  restores MK I manners: a noisy clock that hisses most at low settings, a loop
  that never sits still (wow and flutter), and a reverb that rings far longer.
- **MODULATION** (the pedal's ramps) is the pedal's function generators, one per channel, and the
  reason it is more than four effects. Each side has a destination — OFF, then
  TIME, MIX, LENGTH, MODIFY (that channel's own modify knob), CLOCK or BALANCE —
  and a generator writes into it, added on top of where the knob sits and rebuilt
  from the knob every sample. **OFF** is the first stop and the pedal's own name
  for it, so there is no separate "still" word to mistake for a setting: an OFF
  cell is drawn as an empty frame, a running one as a filled display. Five shapes (triangle, square, sine, random, smooth
  random), one rate for both sides, from one cycle every twenty seconds up to
  eight a second. **UNI** keeps the swing above the knob instead of either side
  of it; **BOUNCE** reflects the phase so it never snaps back; **SWEEP** drops the
  shape and climbs, restarting from the bottom. In the editor the two destination
  cells step from anywhere on the cell — only the painted back arrow steps back —
  and every cell offers its full list, so the readout and the hover text name
  exactly the stops that exist. The rate is the RATE slider and the three switches sit in the same panel — on the pedal these
  are the dips, so the editor is the comfortable place to reach them. Hovering a
  cell in the editor writes what pressing it will do into the readout.
- **TRAILS**, **SMOOTH**, the three tempo switches and wet/loop balance are
  modelled. WIDTH, the
  hidden stereo control, is where the pedal's SPREAD lives: fully left is mono,
  the middle is the image as built, and only past the middle does the sides open.
  The exact stereo movement, filters, transient response, control curves, reverb
  and grains differ from the hardware. Deliberate limiting when effects are active
  and resampling also affect the sound.

## The sheet

Both panels are drawn to one small system, the way a product styleguide works:
spacing snaps to 4 / 8 / 16 / 24 / 32, type to five sizes, and elevation is
always a hard offset — 1 px for a control, 2 px for a panel, 3 px for the
readout — never a blur. Rules carry the structure: ink for frames and controls,
hairline grey for grouping. The only textures are a 2 px halftone band on every
panel head and a 20 px dot grid under the paper, both built from the paper's own
colour, so the cream-and-ink palette is untouched.

That system is read the way a shipping manifest is read, because the machine is
one: every panel opens with a numbered block, its name, a dotted leader and a
figure (`02 INPUT SOURCE ···· 2 IN / 2 OUT`); every knob carries a number beside
its name; the two level meters are ten countable cells rather than a bar; the
readout is an alert box with a status chip, a hard frame and a blinking block
where a cursor would be; the switches sit as three boxed cells; and the title bar
ends with what the host is actually running us at (`48.0 kHz / 2 IN`). Small
registration marks sit in the margins, and `ONE CLOCK. TWO WORLDS.` is set as a
caption with a leader either side.

The knobs are flat discs with a hairline rim and a thin pointer, five marks around
the outside and a hard offset shadow. The wet channel's two knobs are black, the
shared and loop knobs are pale, the same way a pedal marks the primary control.
Nothing is rounded, nothing is dashed, and nothing is a tint of a colour that is

## Not implemented

The browser version does not receive MIDI. MIDI clock sync, expression/CV, synth
mode, MISO, true bypass and the remaining hardware
DIP switches are not implemented. Automatable native parameters are
