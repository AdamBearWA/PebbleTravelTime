# Travel Time — Pebble Time 2 watchface

A world-clock watchface for the **Pebble Time 2** (and other Pebble models).

- A large clock always shows your **current location** time (the device's local
  time, which on modern PebbleOS tracks your phone's timezone).
- Add as many **other cities** as you like from the phone settings page; they
  appear in a smaller list beneath the main clock, each with a `+1` / `-1` day
  marker when their calendar date differs from yours.
- If an added city happens to be in **your current timezone**, it's a duplicate
  of the big clock, so it's **automatically hidden**.
- A **heart icon + current BPM** is shown below the date (uses the Pebble Time 2
  heart-rate sensor; shows `--` until a reading is available).

## How it works

| Piece | Role |
|-------|------|
| `src/c/main.c` | Renders the face. Big clock uses `localtime()`; each city is `gmtime(now + offset)`. Computes the device's own UTC offset to detect & hide duplicates. Reads heart rate via `HealthMetricHeartRateBPM`. Persists the city list so the face is correct before the phone connects. |
| `src/pkjs/index.js` | PebbleKit JS. Hosts the settings page, computes each city's **current** UTC offset (DST-aware, via `Intl.DateTimeFormat`), and sends names + offsets to the watch over `AppMessage`. |
| `package.json` | App manifest. Targets `emery` (Pebble Time 2, 200×228 colour) plus the other platforms. Declares the `messageKeys`. |

City offsets are recomputed by the phone every time the watchface launches
(the `ready` event) and whenever you change the city selection, so DST changes
are picked up automatically.

## Settings

Open the Pebble mobile app → the watchface → **Settings**. Tick the cities you
want and tap **Save**. The list defaults to New York / London / Tokyo on first
run. The settings page is fully self-contained (served as a `data:` URI) — no
web hosting required.

## Building

Requires the Pebble SDK / `pebble` CLI from Core Devices
(<https://developer.repebble.com>).

```sh
# from this directory
pebble build

# run in the Pebble Time 2 emulator
pebble install --emulator emery --logs

# or install to a physical watch over the phone
pebble install --phone <PHONE_IP> --logs
```

The compiled `build/travel-time.pbw` can also be side-loaded via the mobile app.

## Notes / possible extensions

- Cities are shown in the order they appear in the master list. Drag-to-reorder
  could be added to the settings page.
- The master city list (`CITIES` in `index.js`) is easy to extend — each entry
  is just an IANA timezone plus a fixed-offset fallback.
- `MAX_CITIES` (in `main.c`) caps the list at 10; the screen fits ~5 rows on
  `emery`, fewer on smaller models.
