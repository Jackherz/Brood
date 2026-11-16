# Handoff: step 6, the browser version rebuilt to match the plug-in

Repo: `~/Documents/AI/Brood-Source` (GitHub Jackherz/Brood). Work on `main` only; there are no
other branches and the owner wants it kept that way. Plan: `docs/plans/release-cleanup.md`,
section 7 (browser), section 5 (every control: name, place, face, help), 8.6 (input source).

## Where things stand

Steps 1 to 5 are done and pushed (main at 8c07b30, kit at d66b63f). The plug-in is the
reference: build it and look at it before starting.

```bash
cmake --build native/build --config Release --target editor_snapshot
native/build/plugin/editor_snapshot out.png 1.0 loop
```

(`editor_snapshot out.png <scale> [loop] [param=value ...]`, e.g. `wetMode=0.67 character=0.5`.)

The browser version is the old design: a pretend window, taglines, an on-page guide, a
"signal" column with the source, meters and switches, an IN > OUT card and key hints. Step 6
replaces its whole view. Keep, unchanged:

- `src/brood-worklet.js` (the DSP core; it must stay sample-for-sample with `native/dsp/brood.h`,
  checked by the parity test) and `src/audio-engine.js`;
- everything in `src/main.jsx` that isn't drawing: `BASE`, `PRESETS`, `sanitize`, `describe`,
  the migrations (`migrateStart`, `migrateRouting`, `migrateShapes`), the localStorage keys
  (`brood-params-v4`, `brood-user-presets-v4`, `brood-favourites-v1`), the engine wiring, file
  loading and the microphone.

Already decided (owner, 16 November):

- **Same names, same layout, same help, same fonts as the plug-in.** The pretend window,
  taglines, the on-page guide, the IN > OUT picture card and the key hints go.
- **Input source (8.6):** a source picker outside the panel, below it: microphone, a file,
  the demo. LOAD FILE lives there too. SOURCE and LOAD FILE exist only in the browser.
- **The Bench** (the plug-in's bottom row, which passes takes between Brood and Silt in a DAW)
  has nothing to do in a browser: leave it out. The panel then ends under the bottom strip
  (y 735 in design units, see below), and the source picker sits under that.
- **Size:** draw the plug-in's layout on a fixed design canvas (1200 wide; 820 tall in the
  plug-in, about 750 here without the Bench) and scale the whole canvas to the window
  (CSS `transform: scale()` on a 1200-wide box, or an equivalent), so nothing reflows. The
  owner wasn't asked about phones; on a narrow screen the panel is simply small. If you think
  a phone layout is needed, ask the owner before building one.
- The browser's control names were left stale on purpose until now (for example BALANCE is
  still "WET / LOOP" in `MOD_TARGET_NAMES`, ROUTING's middle is "BOTH"). They all change to the
  plug-in's names in this step.

## The reference, in the plug-in's source

Read these rather than guessing; every coordinate and sentence is there.

- `native/plugin/PluginEditor.cpp`
  - `resized()`: every rectangle, in design units (1200 x 820). Summary below.
  - `rebuildHoverHelp()`: the help sentence for every control (title and text). Copy them
    exactly; they're the owner's wording.
  - `kWhisper` (top of the file): each knob's help per mode.
  - `paint()`, `drawSection()`, `drawMasthead()`: headings, labels, figures, the strip.
  - `ThreeWay`, `Stepper`, `Footswitch`, `GainBox`, `StripKey`, `LoopView`, `ModChip`,
    `RouteList`: how each part looks and behaves.
  - `Look::drawRotarySlider` / `drawLinearSlider` (modulation rings on knobs, swing bars on
    BALANCE).
  - `wordmarkOutline`: the wordmark's outline points (see "Wordmark").
- `kit/include/kit/Look.h`: the frameless look: `drawRotarySlider` (an arc that fills to the
  value round a flat disc with a short pointer), `drawLinearSlider` (a rounded track that
  fills, a round thumb), `drawToggleButton` (pill switch; the "plate" kind is a small plate,
  lit when on), `drawButtonBackground` (plates), `drawReadout` (the dark screen),
  `drawSheet` / `fillSurface` (rounded paper, corner 6).
- `native/plugin/PluginProcessor.cpp` `describe()`: the readout text for every value. The
  browser has its own `describe()` in `main.jsx`; make its wording match.

### Layout (design units, origin top left)

- Masthead: 24, 24, 1152 x 72. The wordmark at the left, capitals 28 tall, then 18 gap, then
  "GLYPH AUDIO" / "MICRO-LOOPER + SPACE" in two lines (name face 17, muted). A 2 px ink rule
  along its foot. Preset bar (PATCH, arrows, the name, SAVE): 776, 30, 400 x 36.
- Main sheet: 24, 108, 828 x 548. Column centres: 162, 438, 714.
  - Take strip (the dark window): 38, 116, 800 x 100. CLEAR, UNDO and GAIN on its top line,
    at its right.
  - WET footswitch 248 x 46 centred on (162, 245); LOOP 176 x 46 at (590, 222); OVERDUB
    70 x 43 at (768, 222). WEAR stepper 138 x 24 centred on (438, 234); PRE LOOP / POST LOOP
    160 x 20 centred 24 below it.
  - Mode selectors at y 292, 248 x 28, centred on each column: REVERB DELAY SLIP WEAVE;
    IN, IN + LOOP, LOOP; ENV TAPE STRETCH. Column captions above them: WET CHANNEL, SHARED,
    MICRO-LOOPER.
  - Knobs 104 x 104: top row tops at 342, bottom row at 510, on the column centres. Name
    20 above each knob, value under it.
  - Under the knobs: FREEZE, TRAILS, WET DIV under the wet column; SMOOTH under CLOCK; LOOP
    DIV mirrored under the loop column. SYNC / TAPE (DELAY), SYNC (WEAVE), SYNC / SOFT (ENV)
    sit under the top knobs only in their modes. Exact rectangles in `resized()`.
- Side column: 864, 108, 312 wide. The screen 112 tall; 10 gap; LOOP section 113 tall; 10 gap;
  MODULATION to 656.
  - LOOP section (moss square): MEMORY slider and figure, BALANCE slider with LOOP and WET
    printed at its ends, CAPTURE / RECORD with BARS and AT TAP beside it in RECORD. BALANCE's
    readout ("LOOP 100 / WET 60") on the section's heading.
  - MODULATION section (ink square): two chips (number, route count, an on square, a trace of
    the last two seconds) and ROUTES. Clicking a chip opens its row beneath (SHAPE stepper,
    RATE slider and figure, UNI, SYNC, HOLD); clicking it again closes it. Drag a chip onto a
    knob or BALANCE to route it; drag up or down on a knob's ring to set depth; double-click a
    ring to remove it. The browser already has the routing logic (`ModPanel`, `Knob`).
- Bottom strip: 24, 668, 1152 x 67. Five even cells: LEVEL (the input level), TONE, FADE,
  WIDTH, NOISE, each a name, a figure at the right, and a slider under them.
- Source picker (browser only): under the bottom strip, inside the 1152 width.

### The screen

The dark window at the top of the side column shows a title, a value and a chip. While the
mouse rests on a control it shows that control's help (chip HELP, up to four lines); otherwise
the last thing moved and its value (chip LIVE), with "48.0 kHz  loop 1.33 s  cpu 4.5%" along
its foot. See `drawReadout` in the kit and where `BroodEditor` calls it.

### Colours (the plug-in's stock)

| Use | Colour |
| --- | --- |
| Page | `#efebe0` |
| Paper (sheets, sections) | `#f6f3ea` |
| Ink (text, knob faces' pointer, solid selections) | `#181815` |
| Muted text | `#5f5f57` |
| Rules, dotted leaders | `#bdb9ab` |
| Screen and take strip | `#181815`, text `#f6f3ea` |
| On (lights, lit switches) | `#c98f2e` |
| Accent | `#8f5f12` |
| LOOP section square | `#5d7046` |
| MOD 1, MOD 2 | `#b4452a`, `#2f6f8f` |

Plates are ink at 8 % (14 % on hover, 22 % pressed), corner 4 to 6. No frames, no hard shadows.

### Fonts

Same files as the plug-in, already in `assets/fonts/` after `bash scripts/fetch-fonts.sh`
(gitignored; never commit a .ttf, `make check` fails on it):

| Role | Face | Used for |
| --- | --- | --- |
| Screen | B612 Mono 400 | the screen, the take strip, CLEAR / UNDO / GAIN |
| Name | Overpass Medium (500) | control names, section headings, captions, buttons; tracked 0.06 em |
| Value | Chivo Mono 400 | figures on the paper: knob values, slider figures, steppers, footswitch keys |

The plug-in's sizes are JUCE heights. In CSS, `font-size = height / hem`, with hem
B612 Mono 1.215, Overpass 1.266, Chivo Mono 1.190. Names are drawn at 0.72 of the number in
the code (`nameFont(16)` is a height of 11.52), and values 2 under it (`font(16)` is 14). The
font trial page (`docs/plans/font-trial/index.html`) does exactly this conversion; reuse it.

Press Start 2P, VT323 and Space Mono go from the browser version. Remove them from
`scripts/fetch-fonts.sh`, `assets/fonts/` (their OFL texts too) and `THIRD_PARTY_NOTICES.md`
once nothing uses them.

### Wordmark

"BROOD" (no full stop) in the thin cut of Squarepusher with its D redrawn, as an outline,
never the font file: the font is freeware with no embedding terms. The points are in
`wordmarkOutline` in `native/plugin/PluginEditor.cpp` (font units, y up, capitals 360 tall,
3816 wide, even-odd fill). Turn them into one inline SVG path (flip y: `360 - y`), viewBox
`0 0 3816 360`, ink fill, 28 tall in design units. Credit is already in
`THIRD_PARTY_NOTICES.md`.

## What done looks like

- The browser panel and the plug-in snapshot look the same side by side, apart from the Bench
  and the source picker.
- Every control the plug-in has works: knobs (drag, double-click to reset), modulation
  routing and depth, the take strip (markers, window drag, double-click), CLEAR / UNDO,
  GAIN (drag, double-click, normalise), presets (step, browse, star, save, delete),
  footswitches (click and hold, keys A and L), OVERDUB, every switch and stepper, the help on
  the screen.
- Settings and user presets saved by the old page still load (the migrations are untouched).
- `npm test` (90), `node tests/parity/compare.js native/build/brood_render` (every line ok),
  `npx vite build`, `make check`.
- Checked in a browser at desktop width: no console errors, audio runs from the demo, the
  microphone and a file.

## Also affected: the website

jackherz.com embeds a patched build of this browser version (its own demo loops, a
site-integration patch; see the site repo's `brood-app/`). That patch is against an older
commit and will need redoing after this step. Don't touch the site in this step; tell the
owner it's due.

## House rules

- Australian English, no em dashes, no emojis. Never use the old pedal's name ("mood").
- Copy the owner's help wording from the plug-in; don't rewrite it.
- No generic web defaults: no Inter or Roboto, no gradients, no stock templates.
- Don't commit or push until the owner has seen it. Never upload packages to GitHub.
