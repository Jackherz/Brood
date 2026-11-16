# Brood font trial: candidates

For the four type roles in `docs/plans/release-cleanup.md` section 2. The trial page is
`index.html` in this folder; serve the folder (for example `python3 -m http.server` inside it)
so the local fonts load, then open it in a browser.

## How the candidates were checked

- **Licence.** Every Google Fonts candidate was checked in its own folder in the Google Fonts
  source repository (`github.com/google/fonts/tree/main/ofl/<name>`), which is what the
  specimen page is built from: `METADATA.pb` says `license: "OFL"` and the folder's `OFL.txt`
  was read for its copyright line. Departure Mono is not on Google Fonts: its release zip
  (v1.500) ships an SIL OFL 1.1 `LICENSE`, and the project README lists the font as OFL and
  only the website code as MIT. Every candidate below is SIL OFL 1.1, which allows bundling
  in a free, distributed plug-in as long as the licence travels with it and the font is not
  sold on its own.
- **Metrics and digits.** Each font file was downloaded and read with fontTools: ascent plus
  descent per em (which sets its size in JUCE, see below), cap height, whether its default
  digits are all one width, and whether it has a `tnum` feature. Every candidate has all the
  characters the panel uses (A to Z, a to z, 0 to 9, `% / + - . : [ ] _`).
- **Fit.** The trial page puts each face on a mock of the panel at real sizes and box widths
  (taken from `resized()`, `paint()` and `drawReadout`) and flags any text that would be
  squashed (JUCE's `drawFittedText` narrows text down to a floor) or cut. Its "check every
  candidate" button runs all of them and confirms each font really loaded; a deliberately
  wrong font name is included and is reported as FELL BACK, so the check does catch it.

**How sizes work, and why it matters.** JUCE 8 sizes a font so its ascent plus descent equals
the height asked for. VT323 and Glyph Grotesque have ascent plus descent of exactly 1 em, so
"15" means 15 px. Most candidates are 1.2 to 1.45 em, so at the same number they render 15 to
30 per cent smaller. The page shows them that way by default ("plug-in height"), and has a
"same cap height as today" mode for comparing at equal visual size. Either way, the final
sizes get set per face when the fonts go in.

## 1. Retro (dark screen, take strip)

| Face | Licence | Source | Why it suits Brood, and its weakness |
| --- | --- | --- | --- |
| VT323 (today) | OFL | [Google Fonts](https://fonts.google.com/specimen/VT323) | Baseline. Very narrow (about 0.4 em a character), which is why the help fits; thin, low-contrast strokes and a small cap height (0.56 em) are why it reads weak. |
| Departure Mono | OFL | [departuremono.com](https://departuremono.com), [GitHub](https://github.com/rektdeckard/departure-mono) | Pixel-drawn mono with real terminal character and firm strokes; tabular digits and `tnum`. Wide (0.64 em): help that takes three lines in VT323 needs four. Pixel grid is 11 px, so it is crispest at 11 or 22 px. Not on Google Fonts (copied into `fonts/`). |
| Share Tech Mono | OFL (Reserved Font Name "Share") | [Google Fonts](https://fonts.google.com/specimen/Share+Tech+Mono) | Squared, LCD-like technical mono, very clear at 15 to 17 px. Help needs four lines. Single weight. Reserved name means a subset or edited copy must be renamed. |
| B612 Mono | OFL | [Google Fonts](https://fonts.google.com/specimen/B612+Mono) | Drawn for Airbus cockpit screens: made for legibility on dark displays, tall cap height (0.76 em). Reads more instrument than retro. The widest here (help needs four lines). |
| DotGothic16 | OFL | [Google Fonts](https://fonts.google.com/specimen/DotGothic16) | Dot-matrix Latin on a 16 px grid: the most "pixel screen" of the set, and keeps three-line help. Tall metrics (1.45 em) make it render small at today's numbers; it wants +1 or +2, and is only truly crisp at 16 or 32 CSS px. 2 MB file (it carries Japanese). |
| Handjet | OFL | [Google Fonts](https://fonts.google.com/specimen/Handjet) | Built from dot and square elements, condensed, so it is the only new face that fits even the long WEAR help in three lines. Variable font: the plug-in would need a static instance at the chosen weight. Can look stylised at small sizes. |
| Doto | OFL | [Google Fonts](https://fonts.google.com/specimen/Doto) | Pure dot-matrix, strongly retro. Dots blur at 15 px; needs a heavy weight (800 in the trial) to hold up. Help needs four lines. Variable font (weight and roundness), so a static instance would be cut for the plug-in. |
| Pixelify Sans | OFL | [Google Fonts](https://fonts.google.com/specimen/Pixelify+Sans) | Friendly pixel sans, legible. Digits are proportional and it has no `tnum`, so live values on the screen jiggle. Softer and less "machine" than the others. |

## 2. Label (names, headings, switch captions)

Today's label face is not a chosen font: JUCE 8.0.9 on the Mac maps the default sans to
Lucida Grande, an Apple system font that cannot be embedded, and Windows gets a different
face. Any face below makes the panel look the same on both.

| Face | Licence | Source | Why it suits Brood, and its weakness |
| --- | --- | --- | --- |
| Barlow Semi Condensed | OFL | [Google Fonts](https://fonts.google.com/specimen/Barlow+Semi+Condensed) | Rounded-DIN grotesk, the language of signage and equipment panels; tracks well in small caps and the semi-condensed width gives every box room. Digits proportional by default, `tnum` available. Could serve Value with `tnum` on. |
| Archivo Narrow | OFL | [Google Fonts](https://fonts.google.com/specimen/Archivo+Narrow) | Narrow grotesk with a printed, editorial edge; the most room in tight boxes. Tall metrics (1.35 em) make it small at today's numbers (wants +2). Digits tabular by default, so it can serve Value too. |
| IBM Plex Sans Condensed | OFL (Reserved Font Name "Plex") | [Google Fonts](https://fonts.google.com/specimen/IBM+Plex+Sans+Condensed) | Engineered, slightly mechanical caps; clean at 11 px. Tabular digits by default, so it can serve Value. Widely used, so less distinctive. |
| Overpass | OFL | [Google Fonts](https://fonts.google.com/specimen/Overpass) | After Highway Gothic, the road-sign face: clear tracked capitals with a utilitarian character. Pairs with Overpass Mono for values. "LOOK BACK" is a hair over its cell (98 per cent squash). |
| Atkinson Hyperlegible Next | OFL | [Google Fonts](https://fonts.google.com/specimen/Atkinson+Hyperlegible+Next) | Drawn for low-vision readers: distinct letter shapes at the smallest sizes. Small cap height (0.67 em) wants +2. Pairs with Atkinson Hyperlegible Mono. |
| Familjen Grotesk | OFL | [Google Fonts](https://fonts.google.com/specimen/Familjen+Grotesk) | Grotesk with quirky, ink-trap-like details: has a voice without being loud. Tabular digits by default. Smallest cap height here (0.65 em). |

## 3. Value (readouts on the paper)

All six dedicated faces have one-width digits by default. JUCE 8.0.9 can also switch on `tnum`
(`FontOptions::withFeatureEnabled`), which is what makes Barlow Semi Condensed usable here.

| Face | Licence | Source | Why it suits Brood, and its weakness |
| --- | --- | --- | --- |
| Overpass Mono | OFL | [Google Fonts](https://fonts.google.com/specimen/Overpass+Mono) | Plain, even, very readable figures; same family as Overpass. |
| B612 Mono | OFL | [Google Fonts](https://fonts.google.com/specimen/B612+Mono) | Cockpit readout figures, unambiguous 0 and 8. Wide: "[ A ]" and "PEDAL" do not fit their boxes. |
| Departure Mono | OFL | [departuremono.com](https://departuremono.com) | Carries the screen's character onto the paper, so one face covers Retro and Value. Pixel face at sizes off its 11 px grid. |
| IBM Plex Mono | OFL (Reserved Font Name "Plex") | [Google Fonts](https://fonts.google.com/specimen/IBM+Plex+Mono) | Sober, engineered; the narrowest of the true monos here. Common. |
| Chivo Mono | OFL | [Google Fonts](https://fonts.google.com/specimen/Chivo+Mono) | Grotesk-flavoured mono with a little warmth; good figures. |
| Atkinson Hyperlegible Mono | OFL | [Google Fonts](https://fonts.google.com/specimen/Atkinson+Hyperlegible+Mono) | Most legible figures; pairs with Atkinson Hyperlegible Next. Widest in cap-height mode ("PEDAL" cut). |

Faces that can serve both Label and Value: **Archivo Narrow** and **IBM Plex Sans Condensed**
(tabular by default), **Barlow Semi Condensed** (with `tnum`). All three are in the page's
Value picker.

## 4. Wordmark

| Face | Licence | Source | Note |
| --- | --- | --- | --- |
| Glyph Grotesque (today) | OFL (renamed Terminal Grotesque) | `kit/fonts` | Baseline; keep unless one below wins on the page. |
| Doto | OFL | [Google Fonts](https://fonts.google.com/specimen/Doto) | Heavy dot-matrix "BROOD." ties the wordmark to the screen. |
| Jersey 10 | OFL | [Google Fonts](https://fonts.google.com/specimen/Jersey+10) | Condensed pixel display face, sports-scoreboard warmth. Small cap height (0.54 em) wants a larger size. |

## What the fit check found

At default sizes (the page's "check every candidate", both size modes agree):

- **Help text.** With a sentence VT323 sets in three lines (the SYNC help), Departure Mono,
  Share Tech Mono, B612 Mono and Doto need four, even at minus 2 px; DotGothic16, Handjet and
  Pixelify Sans keep three. The screen's text area is 84 px tall, so four lines at 17 px (68 px)
  fit; only `maxLines = 3` in `kit::drawReadout` stops it. The page has a 3 / 4 lines switch.
- **The WEAR help is cut today.** It needs five lines in VT323 and is cut even squashed. Only
  Handjet fits it. Worth knowing for the help rewrite.
- **Footswitch key "[ A ]" / "[ L ]".** Its 36 px box is too narrow for every monospaced Value
  face (needs 37 to 43 px). Widen the box or write "[A]".
- **WEAR stepper value "PEDAL".** Its value area is about 33 px; VT323 is already squashed to
  92 per cent there. The monos squash to 73 to 79 per cent; B612 Mono at plug-in height and
  Atkinson Hyperlegible Mono at equal cap height are cut. The condensed sans faces squash to 83
  to 92 per cent.
- **"LOOK BACK".** The new name is squashed in its 60 px cell in Lucida Grande today (89 per
  cent). At plug-in heights only Overpass is also squashed (98 per cent); at equal cap height
  Overpass, Atkinson Next and Familjen are squashed to about 90 per cent and Archivo Narrow to
  98. Barlow Semi Condensed and Plex Sans Condensed fit it in both modes.
- Knob names, knob readouts, take strip, screen title and live value, bottom strip and side
  captions fit in every candidate.

## Three recommended sets

| Set | Retro | Label | Value | Rationale |
| --- | --- | --- | --- | --- |
| A. Departure | Departure Mono | Barlow Semi Condensed 500 | Departure Mono | One pixel-born mono for every figure, so the screen and the paper read as one machine, with equipment-panel caps that fit every box; needs help at 4 lines and a wider key box. |
| B. Overpass | Share Tech Mono | Overpass 500 | Overpass Mono | A clear LCD face on the screen and one signage family on the paper: the most readable and least fussy; needs help at 4 lines. |
| C. Atkinson | DotGothic16 | Atkinson Hyperlegible Next 500 | Atkinson Hyperlegible Mono | The truest dot-matrix screen that still keeps three-line help, with the most legible labels and figures; give every role +1 or +2 px, as all three render small at today's numbers. |

## The owner's pick (16 November 2026)

Set D on the trial page:

| Role | Face | Weight | Size |
| --- | --- | --- | --- |
| Retro | B612 Mono | 400 | as coded |
| Label | Overpass | 500 | as coded |
| Value | Chivo Mono | 400 | 2 px under every coded size |
| Wordmark | Glyph Grotesque (unchanged) | 400 | 2 px over (50 instead of 48) |

Fit at these sizes, checked with both size modes and both help texts:

- Chivo Mono at minus 2 fits the footswitch key "[ A ]" / "[ L ]" (it is cut at 0 px).
- The WEAR stepper value "PEDAL" is squashed to 83 per cent (85 at equal cap height). That's
  already the case today at 92 per cent. Widening that cell by about 8 px would clear it.
- "LOOK BACK" is squashed to 98 per cent, hardly visible (89 per cent at equal cap height,
  the same as Lucida Grande today).
- The SYNC help needs four lines and fits once `maxLines` in `kit::drawReadout` goes from 3 to
  4; the screen has room for four lines at 17 px. The WEAR help needs six lines and is cut
  either way, so it needs shortening in the help rewrite. It is already cut today with VT323.
- At value size 16 minus 2, Chivo Mono's capitals are about 8.1 px tall, against 9.0 px for
  VT323 at 16. Smaller, but Chivo Mono's strokes are firmer.

Runner-up for Retro: **Handjet**, the only new face that fits everything at today's sizes,
including the WEAR help. Worth a look if keeping three-line help matters more than character.

## Rejected

- **Silkscreen**: its lowercase is small capitals, and help text is lowercase; also wide.
- **Sixtyfour**: about twice VT323's width ("SENSITIVITY" is 11 em); nothing would fit.
- **Workbench**: scan-line display face with a 0.88 em cap height, too big and busy at 15 px.
- **Jersey 10 for Retro**: a display face, 0.54 em cap height; kept only as a wordmark option.
- **Press Start 2P**: removed by the clean-up plan.
- **Space Mono, Space Grotesk, JetBrains Mono, Instrument Sans, Geist**: stock-template looks.
  Space Mono also has very tall metrics (1.48 em).
- **Inter, Roboto**: excluded by the brief.
- **Martian Mono**: the widest mono looked at (8.25 em for "SENSITIVITY"); not trialled.
- **Oldschool PC Font Pack (Mx437 and friends)**: CC BY-SA 4.0, and how share-alike applies to
  a font bundled in a plug-in is unclear; not "similarly permissive" enough.
- **Monocraft**: reads as a specific game's lettering.

## Not verified

- Licences were checked in the Google Fonts source repository, not on the rendered specimen
  pages (those are built from the same `METADATA.pb` and `OFL.txt`). Departure Mono's was
  checked in its release zip and README.
- Metrics were read from the repository files; the files Google Fonts serves to the page are
  built from the same sources but were not compared byte for byte.
- Lucida Grande's metrics were measured in the browser, not read from the file.
- Doto, Handjet and Pixelify Sans (and several Label and Value faces) are variable fonts. How
  JUCE 8 picks a weight from a variable font was not tested; the safe route is to cut a static
  instance at the chosen weight (allowed by the OFL, but a modified font must not keep a
  Reserved Font Name; none of the variable candidates has one).
- Pixel faces were judged at the trial's scale, not at each of the plug-in's eighth steps, where
  they may go soft between their native sizes.
- Role choices the plan does not spell out, made for the mock: "TAP / HOLD" and "[ A ]" in
  Value; the masthead lines in Label at the PATCH size; side figures ("PEDAL", "2.0s", the hood
  captions) in Value at 14; the BALANCE readout "LOOP 100 / WET 60" placed on the LOOP heading
  line (where INPUT's figure used to sit), as a proposal; LOOP's moss-green section mark from
  open question 8.8; the side column ends after MODULATION (INPUT gone, per 8.7).
