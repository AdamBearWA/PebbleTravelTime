# Appstore listing — Travel Time

Copy these fields into the developer portal (dev-portal.rebble.io / Core Devices
portal) when publishing. Screenshots live in `store/screenshots/`; the release
binary is `build/PebbleTravelTime.pbw`.

## Core fields

| Field | Value |
|---|---|
| Title | **Travel Time** |
| Type / Category | Watchface |
| UUID | `d1f9c2a4-7b3e-4f6a-9c2d-8e5a1b0f3c77` |
| Developer | AdamBearWA |
| Version | 1.0.0 |
| Binary (`.pbw`) | `build/PebbleTravelTime.pbw` |
| Source URL | _(optional — add your repo URL if public)_ |

## Tagline (short blurb)

> Your local time, your cities, and your heartbeat — at a glance.

## Description

Travel Time keeps you grounded across time zones. A big, high-contrast clock
shows your current local time, your live heart rate sits just beneath it, and a
clean list of city times — anywhere in the world — fills the rest of the face.

Add cities by searching the full time-zone database right from your phone (no
fixed list): type "Tokyo", "New York", "Paris"… tap to add, rename to taste,
remove with a tap. City times are DST-aware and stay correct as you travel.

**Features**

- Large, white local-time clock (24h or 12h — follows your watch setting)
- Live heart rate with a band-blue heart that grows to fill the space when you
  are tracking fewer cities
- Search & add *any* city / time zone from the phone settings, and rename them
  freely
- DST-aware city times, with a subtle **+1 / −1** marker when a city is on a
  different calendar day
- Automatically hides a city that is in your current time zone, so it never
  just duplicates the big clock
- A single brand-blue accent on colour watches; crisp white-on-black on the
  black-and-white models
- Built for every Pebble — rectangular, round, B&W and colour

Perfect for travellers, remote teams, and anyone with people in other time
zones.

## Screenshots

**Managed in the dashboard, not via `pebble publish`.** The appstore API only
appends uploaded screenshots (the CLI hardcodes `replaceScreenshots:false` with
no override), so publishing them on every release duplicates them. The publish
workflow therefore skips screenshots; upload/replace them on the dashboard.

Default (blue accent) — one per platform, native resolution:

| Platform | Device | File | Size |
|---|---|---|---|
| aplite  | Pebble / Pebble Steel        | `store/screenshots/aplite.png`  | 144×168 |
| basalt  | Pebble Time / Time Steel     | `store/screenshots/basalt.png`  | 144×168 |
| chalk   | Pebble Time Round            | `store/screenshots/chalk.png`   | 180×180 |
| diorite | Pebble 2                     | `store/screenshots/diorite.png` | 144×168 |
| emery   | Pebble Time 2                | `store/screenshots/emery.png`   | 200×228 |
| gabbro  | Pebble (round, colour, 260)  | `store/screenshots/gabbro.png`  | 260×260 |

Accent-colour variants for the **colour** platforms (basalt, chalk, emery,
gabbro): `store/screenshots/<platform>-red.png` and `-grey.png`. The chalk and
gabbro variants are round (transparent corners), retinted from the round images
(`<platform>-round.png`) to match those watches. The 1-bit models (aplite,
diorite) render white-on-black with no accent, so they have no colour variants.

## Suggested keywords / tags

travel, time zone, world clock, cities, heart rate, dual time, dst
