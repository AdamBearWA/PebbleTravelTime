# Handoff: Travel Time watchface — "Quiet · in colour" (01b) redesign

## Overview
This package specifies a visual redesign of the **Travel Time** Pebble watchface. The current
face shows a large current-location clock, a heart-rate line, a divider, and a bottom-anchored
list of city times. The redesign — internally called **"01b · Quiet in colour"** — keeps the
same information and structure but fixes the contrast, rhythm, and hierarchy problems and adds a
single brand-blue accent (matched to the owner's blue watch band).

The goal of this task is to implement 01b in the **existing C / Pebble SDK codebase** so it
builds and runs on the watch.

## About the design files
The file in this bundle — `Watchface Redesign.dc.html` — is a **design reference created in
HTML**, not production code. It is an interactive board showing the current face plus three
redesign directions; **the direction to implement is the card labelled "01b · Quiet in colour"**
(the others — Current, 01 Quiet mono, 02 Accent, 03 Departures — are context only, do not build
them).

Do **not** port the HTML/CSS. Recreate the 01b layout in the watchface's real environment:
**`src/c/main.c`** drawing code (Pebble SDK, C, drawn with `graphics_*` calls on a `Layer`).
The HTML is only a faithful picture of the intended result.

> The HTML board renders each watch screen at **200 × 228** (real emery pixels) and then displays
> it at **1.8× for legibility on a desktop**. **Ignore the 1.8× — every pixel value in this
> README is already in real watch pixels.** A `200 × 228` element in the mock = `200 × 228` on
> the watch, 1:1.

## Fidelity
**High-fidelity.** Colours, type sizes, and positions below are final and exact (in watch pixels
on the `emery` / Pebble Time 2 target). Recreate them faithfully, with the platform caveats noted
under "Platform constraints" (custom fonts must be bundled; colours snap to the 64-colour palette;
smaller/round models need the existing adaptive logic preserved).

---

## Target codebase & reference files
The watchface source lives in the user's local repo (the folder attached to this project,
`Bears.Pebble.TravelWatchFace/`). Relevant files:

| File | Role | Touch? |
|------|------|--------|
| `src/c/main.c` | All rendering (the `canvas_update` layer proc). **This is where 01b is implemented.** | **Yes** |
| `package.json` | App manifest + `messageKeys` + **`resources.media`** (where bundled fonts are declared). | **Yes** (add fonts) |
| `src/pkjs/index.js` | PebbleKit JS — settings page + city offsets. Design-agnostic. | No |
| `wscript` | Build script. | No |
| add `resources/fonts/` | New folder for the bundled `.ttf` files. | **Yes** (create) |

---

## The 01b design — screen spec

Single screen (the watchface). Black background. Three zones, top → bottom: **clock**,
**heart-rate line**, **hairline divider**, **city list** (bottom-anchored).

All coordinates are in **emery pixels** with origin at top-left of the 200 × 228 screen.

### Layout (emery reference, 200 × 228)

| Element | x / width | y (top) | Align | Notes |
|---|---|---|---|---|
| **Clock** `16:19` | full width (0–200) | **8** | centre | hero element |
| **Heart-rate line** `♥ 72 BPM` | full width, centred | **80** | centre | heart icon + value, single baseline |
| **Hairline divider** | x 8 → 192 (w 184) | **110** | — | 1px, dim blue |
| **City row 1** (Brisbane 18:19) | x 8, w 184 | **124** | name left / time right | baseline-aligned |
| **City row 2** (Sydney 18:19) | x 8, w 184 | **158** | name left / time right | 34px row pitch |
| **City row 3** (Wanaka 20:19) | x 8, w 184 | **192** | name left / time right | last row sits ~14px off bottom |

Side margin is **8px** (the previous design used 14px; tightening it is what lets the type grow).
The list is **bottom-anchored** — keep the existing approach of anchoring to `b.size.h` and laying
rows upward, rather than hard-coding `y = 124/158/192`. Those numbers are the emery result of a
**34px row pitch, 8px bottom margin, divider at y≈110**.

### Typography

The design uses **Archivo** (Google Fonts, OFL — free to embed). Pebble has no Archivo system
font, so it must be **bundled as resource fonts at fixed sizes** (see package.json below).

| Element | Font | Weight | Size (px) | Tracking | Case | Colour |
|---|---|---|---|---|---|---|
| Clock `16:19` | Archivo | Bold (700) | **68** | **−2** | — | white |
| City time `18:19` | Archivo | Bold (700) | **28** | 0 | — | **blue** |
| City name `BRISBANE` | Archivo | SemiBold (600) | **16** | **+2** | **UPPERCASE** | grey |
| HR `72 BPM` | Archivo | SemiBold (600) | **20** | +1 | — | grey (icon = accent) |

- All numerals must be **tabular** (so the clock and times don't jitter minute-to-minute). Archivo's
  default figures are proportional — set the font's tabular feature when subsetting if your tool
  supports it, or accept that HH:MM only changes width slightly; the columns are right/left aligned
  so it stays acceptable.
- **Tracking** is applied at font-compile time via the resource's `"trackingAdjust"` (px). Use
  `-2` for the clock and `2` for the names/HR. Pebble can't adjust tracking at runtime.
- City names are drawn **UPPERCASE**. Either uppercase the string in C (`toupper` loop) or feed an
  already-uppercased label — don't rely on a CSS transform.

### Colours & 64-colour mapping

`emery` (and basalt/chalk) render 64 colours (2 bits/channel). The design hexes snap to these
Pebble palette constants:

| Token | Design hex | Pebble `GColor` (colour models) | Monochrome models (aplite/diorite) |
|---|---|---|---|
| Background | `#000000` | `GColorBlack` | `GColorBlack` |
| Clock | `#FFFFFF` | `GColorWhite` | `GColorWhite` |
| **Accent (band blue)** | `#28A0E8` | **`GColorVividCerulean`** (`#00AAFF`, nearest 6-bit) | `GColorWhite` |
| City name / HR grey | `#8C8C8C` | `GColorLightGray` (`#AAAAAA`) | `GColorWhite` |
| Hairline | blue @ 30% | `GColorVividCerulean` (Pebble has no per-pixel alpha — draw it solid; a single dim 1px line reads fine) | `GColorWhite` |

The accent (city times, heart icon, hairline) is **the only colour** on the face — everything else
is white/grey on black. On 1-bit models fall back to white for all of it (guard with
`#ifdef PBL_COLOR`, as the existing code already does).

### Content / copy
- Clock: device local time, `HH:MM` (24h) or `h:MM` (12h) — reuse the existing `fmt_time()`.
- HR line: `"<bpm> BPM"` when a reading exists, `"-- BPM"` when not. (Current code shows the bare
  number / `--`; the redesign appends the `BPM` unit.) Heart icon to the **left** of the value, the
  whole `icon + text` group **centred** on the screen.
- City rows: name (left) + `HH:MM` (right). **Time only** — see decision note on the day marker.

---

## Implementation steps (`src/c/main.c`)

1. **Add custom-font globals + lifecycle.** Load the bundled fonts once in `init()` (or
   `window_load`) and unload in `deinit()`:
   ```c
   static GFont s_font_clock;   // Archivo Bold 68
   static GFont s_font_time;    // Archivo Bold 28
   static GFont s_font_name;    // Archivo SemiBold 16
   static GFont s_font_hr;      // Archivo SemiBold 20

   // in init():  (NB: the SDK function is fonts_load_custom_font, not fonts_load_resource_font)
   s_font_clock = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_BOLD_68));
   s_font_time  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_BOLD_28));
   s_font_name  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_16));
   s_font_hr    = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_20));

   // in deinit():
   fonts_unload_custom_font(s_font_clock); /* ...and the others */
   ```

2. **Clock** — replace `FONT_KEY_ROBOTO_BOLD_SUBSET_49` with `s_font_clock`, draw white, top y=8,
   full width, centre-aligned. The contrast fix (white, not the current grey) is the single most
   important change.
   ```c
   graphics_context_set_text_color(ctx, GColorWhite);
   graphics_draw_text(ctx, big, s_font_clock, GRect(0, 8, w, 74),
                      GTextOverflowModeFill, GTextAlignmentCenter, NULL);
   ```

3. **Heart-rate line** — center the `heart icon + " 72 BPM"` group on one baseline at y≈78,
   using **`s_font_hr` (Archivo SemiBold 20)** — do NOT fall back to a small/thin system font here;
   it must read at the same weight as the city names.
   - Format the string: `snprintf(hr, sizeof hr, "%d BPM", bpm)` or `strcpy(hr, "-- BPM")`.
   - Measure it with `graphics_text_layout_get_content_size(...)` using `s_font_hr`, add the heart
     width + a small gap, then compute the left x so the whole group is centred.
   - Draw the heart with the existing `draw_heart()` but (a) **scale it up** to pair with 20px text
     (≈ the value's cap-height — bump `HEART_LOBE_R` and the triangle for this line) and (b)
     **vertically centre it on the text's cap-height**, not below the baseline, so icon and value sit
     on one optical line. Colour it the accent: set fill to
     `PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite)` (it is currently `GColorRed`).
   - Draw the text in `GColorLightGray` with `s_font_hr`.

4. **Hairline** — `graphics_draw_line(ctx, GPoint(8, 110), GPoint(w-8, 110))` with stroke colour
   `PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite)`. (Replaces the current full-white divider.)

5. **City list** — keep the existing duplicate-hiding + bottom-anchor logic; only change the
   styling/metrics:
   - Row pitch **34px**, bottom margin **8px**, list starts just below the divider.
   - Name: `s_font_name`, **uppercased**, `GColorLightGray`, left-aligned at x=8.
   - Time: `s_font_time`, accent colour `PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite)`,
     right-aligned, ending at x=w−8.
   - Keep the empty-state ("Add cities from the phone settings") but restyle to match (grey, Archivo).

6. **Trigger redraws** — unchanged (minute tick + health events already mark the layer dirty).

> Keep everything derived from `layer_get_bounds()` as the current code does. The emery numbers
> above are the reference; on basalt/aplite/diorite (144×168) and chalk (180×180 round) the
> bottom-anchored list and centred clock still work, but you'll want smaller bundled font sizes per
> platform (see cross-platform note).

---

## `package.json` — bundle the fonts

Drop the four `.ttf` weights into `resources/fonts/` and declare them. Subset with
`characterRegex` to keep flash small (clock & time need only digits + colon; names/HR need
letters). Use `trackingAdjust` for the design's letter-spacing.

```jsonc
"resources": {
  "media": [
    {
      "type": "font",
      "name": "FONT_ARCHIVO_BOLD_68",
      "file": "fonts/Archivo-Bold.ttf",
      "characterRegex": "[0-9:]",
      "trackingAdjust": -2,
      "targetPlatforms": ["basalt", "chalk", "emery"]
    },
    {
      "type": "font",
      "name": "FONT_ARCHIVO_BOLD_28",
      "file": "fonts/Archivo-Bold.ttf",
      "characterRegex": "[0-9: ap+-]",
      "targetPlatforms": ["basalt", "chalk", "emery"]
    },
    {
      "type": "font",
      "name": "FONT_ARCHIVO_SEMIBOLD_16",
      "file": "fonts/Archivo-SemiBold.ttf",
      "characterRegex": "[A-Za-z .'-]",
      "trackingAdjust": 2,
      "targetPlatforms": ["basalt", "chalk", "emery"]
    },
    {
      "type": "font",
      "name": "FONT_ARCHIVO_SEMIBOLD_20",
      "file": "fonts/Archivo-SemiBold.ttf",
      "characterRegex": "[0-9A-Za-z -]",
      "trackingAdjust": 1,
      "targetPlatforms": ["basalt", "chalk", "emery"]
    }
  ]
}
```
- Download Archivo from Google Fonts (OFL): `Archivo-Bold.ttf`, `Archivo-SemiBold.ttf`.
- The font sizes are encoded in the resource `name` by convention; the actual rasterised size comes
  from the suffix you request — in PebbleKit the size is taken from the font file at the pixel size
  named, so name them clearly and load the matching `RESOURCE_ID_*`. (If your SDK version requires
  an explicit size field, set it to 68 / 28 / 16 / 15 respectively.)

### Flash budget / fallback
If subset fonts push the build over the resource budget on `aplite` (smallest), **exclude aplite
from the font targets** and let it fall back to system fonts there:
- Clock fallback: `FONT_KEY_ROBOTO_BOLD_SUBSET_49` (white — still fixes the contrast issue, just
  smaller than 68px).
- Name fallback: `FONT_KEY_GOTHIC_18_BOLD`; time: `FONT_KEY_GOTHIC_24_BOLD`; HR: `FONT_KEY_GOTHIC_24_BOLD`.
Guard the font handles so the face works whether or not the custom fonts loaded.

---

## Cross-platform notes
- **Colour vs mono**: gate every accent through `PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite)`
  so aplite/diorite render a clean white-on-black version (no colour). The heart's existing
  `#ifdef PBL_COLOR` pattern is the model.
- **Round (chalk, 180×180)**: the bottom-anchored list can clip in the rounded corners — inset the
  list horizontally a little more on `PBL_ROUND`, or reduce to the rows that fit. The existing
  `max_rows` calculation already caps this.
- **Smaller rectangular (basalt/aplite/diorite, 144×168)**: scale the bundled font sizes down
  (e.g. clock ~46, time ~22, name ~14, HR ~13) via per-platform resource entries, or accept the
  emery sizes will be large. Keep positions bounds-relative.

---

## Decisions / open questions for the implementer
1. **Day marker (`+1` / `-1`)** — the current code appends a day-offset marker to a city's time when
   its calendar date differs from home. The 01b mock shows **time only** (per the owner's "city +
   time only" choice). Owner chose to **keep the marker**, drawn small and grey to the right of the
   blue time so it doesn't compete. **Implemented in `FONT_KEY_GOTHIC_14` (system), not Archivo** —
   none of the four bundled Archivo subsets include digits + `+`/`-` at a small size, and adding a
   fifth subset purely for the marker isn't worth the flash.
2. **`BPM` unit** — the mock shows `72 BPM` / `-- BPM`. If width is tight on 144px models, the bare
   number is an acceptable fallback.
3. **Tabular figures** — confirm the bundled Archivo renders fixed-width digits; if not, the
   right/left alignment of the columns still keeps it tidy.

---

## Acceptance checklist
- [ ] Clock is **white** (not grey), ~68px, centred near the top.
- [ ] Heart-rate line is a single centred `♥ 72 BPM` group, heart in band-blue, grey text.
- [ ] One thin **blue** hairline divider (not full white).
- [ ] City names UPPERCASE grey (Archivo SemiBold 16), times **blue** (Archivo Bold 28),
      right-aligned, ~34px pitch, bottom-anchored.
- [ ] 8px side margins; type fills the screen edge-to-edge.
- [ ] Builds for `emery` + `basalt` + `chalk`; degrades to white-on-black on `aplite`/`diorite`.
- [ ] No regression in city duplicate-hiding, offsets, persistence, or the settings flow.

---

## Files in this bundle
- `Watchface Redesign.dc.html` — the design reference board. Open in a browser; build the
  **"01b · Quiet in colour"** card only.
- `support.js` — runtime needed for the HTML reference to render (do not ship; reference only).
