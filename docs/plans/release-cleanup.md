# Release clean-up: names, words, fonts and density

Agreed with the user on 16 November 2026, area by area. Nothing here is built yet. No code
changes until the user has reviewed this plan and settled the open questions in section 8.

Internal parameter IDs never change, so saved sets and presets keep loading. Where a change
alters what an old set would do (section 6), the state version goes from 3 to 4 and old sets
are converted.

## 1. The two blend controls

| | Now | After |
| --- | --- | --- |
| `mix` | MIX knob, SHARED column, readout "DRY 100% / FX 100%" | Unchanged |
| `balance` | WET/LOOP slider in the LOOP section, readout "50%" | Same place, same slider, renamed **BALANCE**. LOOP printed at the left end, WET at the right. Readout "LOOP 100 / WET 60". |

The control's direction does not change (left the loop alone, right the wet side alone; MIDI
CC 28 as the pedal). Every mention puts the two ends in that order: LOOP first, WET second.

## 2. Typography

- Press Start 2P is removed (it is browser-only today). The browser wordmark uses the
  plug-in's wordmark face for now.
- Brood stays retro, with a better retro face than VT323: B612 Mono, chosen in the trial.
- Values printed on the paper: Chivo Mono, chosen in the trial.
- All of it applies to Brood only. Silt and the shared kit's defaults stay as they are; Brood
  gets its own setting.

**Font trial (before any font code):** research candidates, then build one trial page showing
the real panel set in each, for three roles: the retro face (dark screen, take strip), names
and labels, and values on the paper. The user picks from that page. Fonts must have a licence
that allows embedding in a free plug-in. Not Inter or Roboto.

**Trial result (16 November 2026).** Done: research, licences and the fit check are in
`docs/plans/font-trial/candidates.md`, and the page is `docs/plans/font-trial/index.html`
(set D is the choice). The user picked:

| Role | Used for | Face | Weight | Size |
| --- | --- | --- | --- | --- |
| Wordmark | "BROOD." | Glyph Grotesque (unchanged) | Regular | 50 (was 48) |
| Retro | Dark screen, take strip status | B612 Mono | Regular (400) | as coded today |
| Label | Control names, section headings, switch captions | Overpass | Medium (500) | as coded today |
| Value | Readouts under knobs and beside sliders | Chivo Mono | Regular (400) | 2 under every coded size |

All four are SIL OFL 1.1, from the Google Fonts source repository; the licence files ship with
the plug-in like VT323's and Glyph Grotesque's do now. Sizes are JUCE heights. The new faces
have taller metrics than VT323, so they come out smaller at the same number; the sizes above
are what the trial page showed.

What comes with the fonts (from the trial's fit check):

- **Help on the dark screen goes to four lines.** In B612 Mono a sentence VT323 fits in three
  needs four. `maxLines` in `kit::drawReadout` goes from 3 to 4 for Brood; the screen has room
  (84 px, four lines at 17 take 68).
- **The WEAR help must get shorter.** It needs six lines in B612 Mono and is already cut today
  (five lines in VT323). For the help rewrite on the wording page.
- **The WEAR stepper's value cell gets about 8 px wider.** "PEDAL" is squashed to 83 per cent
  in Chivo Mono (92 per cent in VT323 today).
- **Overpass and Chivo Mono are variable fonts.** Cut static instances at the chosen weights
  for the plug-in (allowed by the OFL; neither has a Reserved Font Name), or confirm JUCE 8
  picks the weight from the variable file.
- **Labels stop depending on the system.** Today they are Lucida Grande on the Mac (JUCE's
  default sans, not embeddable) and a different face on Windows.
- Chivo Mono at 2 under fits the footswitch key "[ A ]" / "[ L ]" in its 36 px box; at the
  coded size it would be cut. "LOOK BACK" in Overpass is squashed to 98 per cent, hardly
  visible.

## 3. Words

| Words | Decision |
| --- | --- |
| "E-01" | Off the masthead and the Bench strip. Still Brood's tag when sending to the Bench, unseen. |
| "ONE CLOCK. TWO WORLDS." | Removed |
| "THE SIGNAL /" | Removed: the screen's title is just the control's name |
| 01 / 02 / 03 section chips | Removed; colour marks the sections (proposal in section 8) |
| Browser: "BROOD.EXE", "A SOUND EXPERIMENT", "A LITTLE MACHINE FOR HAPPY ACCIDENTS", the pretend window | Removed; the browser shows the plug-in's masthead |
| MIX whisper | Plainer, one per wet mode (drafts in section 5; see open question 8.1) |

## 4. Final names

One name per parameter, the same on the panel, in Live, in the browser and in the manual.
"Chars" is the length of the name Live and the Push show. The longest name Brood has today
is 11 characters ("Loop Switch"). Where Push 3 cuts a name off has not been measured
(open question 8.2), so anything over 11 has a short form ready.

| ID | Name in Live now | Final name | Chars | Short form if needed |
| --- | --- | --- | --- | --- |
| `time` | Time | Time | 4 | |
| `mix` | Mix | Mix | 3 | |
| `length` | Length | Length | 6 | |
| `modWet` | Wet Mod | Modify / Wet | 12 | Mod Wet |
| `clock` | Clock | Clock | 5 | |
| `modLoop` | Loop Mod | Modify / Loop | 13 | Mod Loop |
| `balance` | Wet/Loop | Balance | 7 | |
| `wetMode` | Wet Mode | Wet Mode | 8 | |
| `routing` | Routing | Routing | 7 | |
| `loopMode` | Loop Mode | Loop Mode | 9 | |
| `trails` | Trails | Trails | 6 | |
| `smooth` | Smooth | Smooth | 6 | |
| `freeze` | Freeze | Freeze | 6 | |
| `sync` | Wet Sync | Wet Sync | 8 | |
| `tapeDelay` | Tape Echo | Tape Echo | 9 | |
| `envSync` | ENV Sync | Env Sync | 8 | |
| `envSoft` | ENV Soft | Env Soft | 8 | |
| `wearPost` | Wear Post | Clean Take (proposal 8.4) | 10 | |
| `recMode` | Rec Mode | Loop Take (proposal 8.5) | 9 | |
| `recAtTap` | Rec At Tap | At Tap | 6 | |
| `loopDb` | Loop Gain | Loop Gain | 9 | |
| `wetSwitch`, `loopSwitch` | Wet Switch, Loop Switch | Unchanged | 10, 11 | |
| `overdub`, `clear`, `undo` | Overdub, Clear, Undo | Unchanged | | |
| Mod sources | Mod 1 Shape, Rate, Uni, Sync, Hold... | Unchanged, minus Sweep and Bounce (section 6) | | |
| Hood | Tone, Fade, Width, Wet Div, Loop Div, Noise | Unchanged | | |

On the panel the two MODIFY knobs keep a name per mode:

| Knob | Mode | Now | After |
| --- | --- | --- | --- |
| Wet MODIFY | REVERB / DELAY / SLIP / WEAVE | SMEAR / FEEDBACK / SPEED / STITCH | SMEAR / FEEDBACK / **RATE** / STITCH |
| Loop MODIFY | ENV / TAPE / STRETCH | CATCH / SPEED / STRETCH | **SENSITIVITY** / SPEED / STRETCH |

SENSITIVITY is the one name for the ENV knob: title, help and readout. CATCH goes. With the
wet knob renamed, SPEED only ever means the loop's tape speed.

SLIP's knob is RATE: how fast, and which way, the borrowed slices play (half speed at the
centre to double at the ends, in semitone steps, backwards on the left half). Its readout is a
signed rate with no "x": "RATE +0.63" forwards, "RATE -0.63" backwards. Settled 16 November;
the rest of the MODIFY names are kept.

## 5. Every control: name, place, face, help

Help text is the current text unless marked. The user wants to rewrite a lot of it: the
wording page (https://claude.ai/artifact/7x9JosPxKLi6e23YJByJjC) will be updated to these names
once they're settled, and whatever is written there replaces the text below. Nothing has been
entered on it yet.

### Masthead

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| BROOD. | Top left, no chip beside it | Wordmark | none |
| GLYPH AUDIO / MICRO-LOOPER + SPACE | Beside the wordmark | Label | none |
| PATCH (name, arrows, SAVE) | Top right | Label | click the name to search, star and audition sounds. the arrows step through them. SAVE keeps what you hear. |

### Take strip

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| The window (waveform, START, END) | Top, full width of the main panel | Retro | drag a marker to trim what plays, or inside to slide the window along the take. double-click opens the whole take. moves crossfade, so they do not click. |
| CLEAR | In the strip | Retro | empties the take. undo brings it back. |
| UNDO | In the strip | Retro | one step back: a clear, a new take, or the last overdub layer. press again to redo. |
| GAIN | In the strip, right | Retro | the loop's own level. drag or scroll, shift for fine. double-click for 0 dB, right-click to normalise. |

### Footswitch row

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| WET | Left | Label | tap for the wet side, hold to freeze it where it stands. key A. |
| WEAR | Centre, with nothing under it until CLEAN TAKE appears on WORN | Label, value in Value | one line from clean to worn. clean: fuller top, far fewer artifacts, the wet side a little later. worn: the MK I in the room, a noisy clock, a restless loop and a longer reverb. |
| CLEAN TAKE (was AFTER LOOP) | Under WEAR, only on WORN | Label | worn is heard but not recorded: the loop keeps a clean take, so turning WEAR back gives it back clean. a little less grit than worn in the loop. |
| LOOP | Right | Label | tap to catch the recent past, tap again to stop, hold to overdub. key L. |
| OVERDUB | Beside LOOP | Label | latches: layers into the playing loop until you switch it off. needs a loop playing. |

### Wet channel (left column)

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| Wet mode: REVERB, DELAY, SLIP, WEAVE | Top of column | Label | four ways of treating time: reverb, delay, slip, weave. |
| TIME | First knob | Label, readout in Value | reverb: how long the room keeps breathing. delay: how far back the echo remembers. slip: how wide a slice of time is borrowed. weave: how far back the threads reach. |
| SYNC (DELAY, WEAVE) | Under TIME | Label | TIME becomes a division of the beat: in DELAY the echo lands on the grid, in WEAVE so does how far back the threads reach. |
| TAPE (DELAY) | Under TIME | Label | a tape echo: turning TIME glides the head, so the echoes bend in pitch, with a little wow and flutter and saturation. off, TIME crossfades and the pitch stays put. |
| SMEAR / FEEDBACK / SLIDE / STITCH | Second knob | Label, readout in Value | how much the surface smears. / how much the echo wants to stay. / which way, and how fast, time slides. / long cloth, or short clipped stitches. |
| FREEZE | Bottom of column | Label | the hold gesture without the holding: the wet side comes up frozen and rings on its own. |
| TRAILS | Bottom of column | Label | the tails keep falling after you switch the wet side off. |
| **WET DIV** (moved from the hood) | Bottom of column, a small stepper beside TRAILS | Label, value in Value | the wet side runs on a divided clock, so the room and the echo stretch out. |

### Shared (centre column)

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| Routing: IN, IN + LOOP, LOOP | Top of column | Label | in: the wet side hears the input, and the loop keeps it. in + loop: it hears the input and the loop, and the loop keeps it. loop: it hears only the loop, and the loop keeps only the input. |
| MIX | First knob | Label, readout in Value | **Draft, per wet mode.** reverb: your dry sound against the room and the loop. delay: your dry sound against the echoes and the loop. slip: your dry sound against the slipped slices and the loop. weave: your dry sound against the threads and the loop. Each ends: left is dry alone, right is Brood alone, the middle is both in full. |
| CLOCK | Second knob (the dark one) | Label, readout in Value | the heartbeat everything obeys. |
| SMOOTH | Under CLOCK | Label | the sample rate slides instead of stepping, so CLOCK bends pitch as it turns. |

### Micro-looper (right column)

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| Loop mode: ENV, TAPE, STRETCH | Top of column | Label | three ways of holding a moment: envelope, tape, stretch. |
| LENGTH | First knob | Label, readout in Value | env: how much of the moment gets caught. tape: the size of the window that repeats. stretch: how wide each grain is stretched. |
| SYNC (ENV) | Under LENGTH | Label | the envelope slices by a division of the beat instead of a free length, so the loop stutters in time. |
| SOFT (ENV) | Under LENGTH | Label | the slices fade in and out instead of cutting, and a new one takes over from the last, so nothing clicks. a rounder, less percussive stutter. |
| SENSITIVITY / SPEED / STRETCH | Second knob | Label, readout in Value | how softly it must speak to be caught. / which way the tape runs, and how fast. / how far the grains drift from now. |
| **LOOP DIV** (moved from the hood) | Bottom of column, a small stepper mirroring WET DIV | Label, value in Value | the loop runs on a divided clock: lower, longer, further from the other side. |

### Side: dark screen

Title shows the control's name alone (no "THE SIGNAL /"), with LIVE or HELP. Retro face.

### Side: LOOP section (colour mark, no number)

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| MEMORY | First row | Label, value in Value | in LOOK BACK, how much of the recent past a tap keeps. hard left is the pedal's own buffer, which shrinks as CLOCK drops. RECORD ignores it: the bars or your second tap decide, up to 32 s. |
| BALANCE (was WET/LOOP) | Second row, LOOP at the left end, WET at the right | Label, readout "LOOP 100 / WET 60" in Value | **New draft.** the loop against the wet side. left: the loop alone. right: the wet side alone. in the middle, both in full. drop a source here: its bar is the swing, the small tab is where it is now. |
| LOOK BACK / RECORD (was CAPTURE / RECORD, proposal 8.5) | Third row | Label | look back keeps the recent past when you tap. record starts when you tap and plays when you tap again, or records for as long as you hold. |
| BARS (RECORD only) | Third row | Label, value in Value | **Corrected.** free records until the next tap. a number records that many bars, then plays. (The old last sentence, "longer than MEMORY allows, MEMORY wins", is no longer true.) |
| AT TAP (RECORD only) | Third row | Label | off: a bar count starts on the next bar line while the host plays, so loops land on the grid. on: it starts the moment you tap. |

### Side: MODULATION section (colour mark, no number)

At rest only the two chips and ROUTES show. Clicking a chip shows its row of controls
beneath; clicking it again hides them.

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| MOD 1, MOD 2 chips | Always visible | Label | click to show its controls. drag it onto any knob or BALANCE, or drag up or down around a knob, to route it. the square turns it on or off. |
| ROUTES | Always visible | Label | every route in one list, with its depth. |
| Shape: TRI, SQR, SIN, **SAW**, RND, SMTH | Shown for the open chip | Label, value in Value | click to step the shape: triangle, square, sine, sawtooth, random, smooth random. |
| RATE | Shown for the open chip | Label, value in Value | how slowly this source turns. with SYNC on it becomes a span of the beat. |
| UNI | Shown for the open chip | Label | the swing sits above the knob instead of either side of it. |
| SYNC | Shown for the open chip | Label | the rate becomes a division of the beat: eight bars down to a sixteenth. |
| HOLD | Shown for the open chip | Label | this source stops where it is and its knobs stay pushed there. CC 52 holds both. |
| SWEEP, BOUNCE | **Removed** (section 6) | | |

### Side: INPUT section

| Control | Place | Face | Help |
| --- | --- | --- | --- |
| LEVEL | See open question 8.7 | Label, value in Value | how hard the input hits the machine. past the middle it's driven into old tape: rounder, duller, a little hiss. |
| IN / OUT meters | **Removed** | | |
| SOURCE, LOAD FILE | **Removed** for good from the plug-in (already hidden); their help and code go | | |

### Bottom strip

Four sliders, always open, each with more room than now.

| Control | Face | Help |
| --- | --- | --- |
| TONE | Label, value in Value | a hi-cut on the wet side. right is open, left is soft and far away. |
| FADE | Label, value in Value | how much of the old take an overdub erases. left keeps the pedal's slow erosion, right replaces it. |
| WIDTH | Label, value in Value | how far apart the two speakers sit: left is mono, the middle is the image as built, right opens the sides. |
| NOISE | Label, value in Value | how much of the worn clock's hiss is let through. only heard with WEAR on WORN; left is silent. |

### The Bench

Unchanged apart from "E-01" leaving the strip: status, the source menu (help: what SEND sends:
the loop, or the exact output for that many bars, from the next bar line), SEND, TAKE, FOLDER.

## 6. Changes that touch old sets (state version 4)

- **SWEEP becomes the SAW shape.** A set with SWEEP on loads as SAW, whatever shape it had
  (SWEEP ignored the shape).
- **BOUNCE is removed.** A set with SWEEP and BOUNCE loads as TRI at half the rate, which is
  the same curve. A set with BOUNCE on another shape loses it: no audible change on TRI, RND
  and SMTH; on SQR the two halves swap order; on SIN the turnaround corners go.
- **Adding a sixth shape moves the saved shape values.** The shape is stored as a position
  along a row of five; with six, every saved value is converted so each set keeps its shape.
- The same conversion runs for presets, the factory patches and the browser's saved state.
- Both cores change together; parity scenarios that use SWEEP or BOUNCE are rewritten.

## 7. Browser

Same names, same layout, same help, same fonts as the plug-in. The pretend window and
taglines go. The on-page guide, the IN > OUT picture card and the key hints go (but see open
question 8.6 for the input source).

## 8. Open questions before any code

1. **MIX whisper.** Both "one plainer line" and "a line per wet mode" were ticked. The plan
   assumes plainer and per mode. Say if it should be one line for all modes.
   **Settled 16 November: one line per wet mode, as drafted in section 5.**
2. **Push 3 name length.** With Brood loaded on the Push, check whether "Modify / Loop"
   (13 characters) shows in full. If not, the short forms in section 4 are used.
3. **Wet knob in SLIP.** Proposed SLIDE (its help already says "time slides"). Alternatives:
   DRIFT, RATE. **Settled 16 November: RATE, readout "RATE +0.63" / "RATE -0.63" (section 4). All
   other MODIFY names stay: SMEAR, FEEDBACK, STITCH, SENSITIVITY, SPEED, STRETCH.**
4. **AFTER LOOP.** Proposed CLEAN TAKE (on: the loop keeps a clean take). Alternatives:
   KEEP CLEAN, WEAR AFTER. **Settled 16 November: a PRE LOOP / POST LOOP choice, PRE by
   default (wear is recorded into the loop). POST is today's "on". Default unchanged.**
5. **CAPTURE / RECORD.** Proposed LOOK BACK / RECORD, with the parameter called Loop Take in
   Live. Alternatives: RETRO / RECORD, or keep CAPTURE / RECORD. **Settled 16 November: keep
   CAPTURE / RECORD.**
6. **Browser input.** No browser extras were kept, but the browser has no track to listen to,
   so it needs some source. Proposed: keep a single small source control (demo phrase or a
   file) outside the panel, below it.
   **Settled 16 November: a source picker outside the panel, below it: microphone, a file,
   or the demo phrase.** SOURCE and LOAD FILE stay in the browser only (confirmed 16 November, after
   step 3); the plug-in has neither, and the standalone app plays the demo phrase.
7. **INPUT section.** Without meters it holds one slider. Proposed: LEVEL joins the bottom
   strip as its first slider and the INPUT section goes, leaving LOOP and MODULATION on the side.
   **Settled 16 November: as proposed.**
8. **Section colours.** Proposed: LOOP moss green, MODULATION left in ink (its red and blue
   chips are its colour), INPUT (if it stays) slate. Ochre stays reserved for "on". Shown as a
   small square before each heading. To be judged on the font trial page.
   **Settled 16 November: as shown on the trial page (LOOP moss green, MODULATION ink).**
9. **Bottom strip's label.** It's no longer hidden, so "UNDER THE HOOD" may not fit. Keep it,
   or drop the label.
   **Settled 16 November: drop it.**

## 9. Order of work once approved

1. Font research and the trial page (nothing in the plug-in changes). Done, see section 2.
2. Names and words (sections 1, 3, 4, 5): the plug-in editor, the host names, the manual, the
   README picture, the wording page. The browser's names come with its rebuild in step 6
   (decided 16 November: it is too far behind to patch now). **Done 16 November (not committed):**
   host names (Modify / Wet, Modify / Loop, Balance, Env Sync, Env Soft, At Tap; Wear Post shows
   Pre or Post), RATE and SENSITIVITY on the panel, BALANCE with LOOP and WET at its ends and its
   readout on the LOOP heading (host value `100/60`, as Live allows 8 characters), PRE LOOP / POST
   LOOP under WEAR, MIX help per mode, E-01, "ONE CLOCK. TWO WORLDS." and "THE SIGNAL /" gone,
   screen titles are the control's name, the manual, the README picture and the wording page.
3. Layout: DIV steppers, four-slider strip, modulation chips-first, meters and E-01 out,
   section colours. **Done 16 November (not committed):** WET DIV and LOOP DIV steppers at the
   column feet; the bottom strip is LEVEL, TONE, FADE, WIDTH, NOISE with no label; INPUT,
   its meters, SOURCE and LOAD FILE are gone (the standalone plays the demo phrase); LOOP and
   MODULATION carry colour squares instead of numbers; MODULATION shows its chips and ROUTES
   until a chip is clicked.
4. SAW in, SWEEP and BOUNCE out, state version 4, both cores, parity, tests.
5. Fonts, from the trial's result, Brood only: B612 Mono, Overpass, Chivo Mono, with help at
   four lines and the wider WEAR cell (section 2).
6. Browser brought to match.

Each step: `ctest`, `npm test`, `make check`, parity, `make install`, and a snapshot for the
user to look at before the next step.
