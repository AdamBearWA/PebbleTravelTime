#include <pebble.h>

// ---------------------------------------------------------------------------
// Travel Time
//
//   * A large clock always shows the device's CURRENT LOCATION time. On modern
//     PebbleOS (Pebble Time 2 / emery) the device timezone tracks the phone, so
//     localtime() is always the true local time.
//
//   * Additional cities are configured from the phone (PebbleKit JS computes
//     each city's current UTC offset, DST included, via Intl) and are drawn
//     smaller in a list beneath the main clock.
//
//   * If an added city is in the same UTC offset as the device's current
//     location, it is a duplicate of the big clock and is hidden.
// ---------------------------------------------------------------------------

#define MAX_CITIES 10
#define NAME_LEN   24

// Persist storage keys
#define PK_HOME_CITY   2
#define PK_CITY_COUNT  3
#define PK_NAMES       4
#define PK_OFFSETS     5

static Window *s_window;
static Layer  *s_canvas;
static GPath  *s_heart_path = NULL;

// "01b · Quiet in colour": the single brand-blue accent (matched to the owner's
// blue watch band). Snaps to GColorVividCerulean on the 64-colour models; falls
// back to white on the 1-bit models (aplite/diorite) so they stay white-on-black.
#define ACCENT_COLOR PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite)
#define GREY_COLOR   PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)

// Custom Archivo fonts (bundled on the colour models only -- see package.json).
// On aplite/diorite these hold system-font fallbacks instead.
static GFont s_font_clock;   // Archivo Bold 68    (clock)
static GFont s_font_time;    // Archivo Bold 28    (city time, accent)
static GFont s_font_name;    // Archivo SemiBold 16 (city name, grey, UPPER)
static GFont s_font_hr;      // Archivo SemiBold 20 (heart-rate line, grey)

// Lower triangle of the heart icon (lobes are drawn as two circles). Sized to
// pair with the 20px HR value (~cap-height): the lobe radius below gives a
// ~24px-wide, ~18px-tall heart.
static const GPathInfo HEART_PATH_INFO = {
  .num_points = 3,
  .points = (GPoint[]) {{-12, 0}, {12, 0}, {0, 12}}
};
#define HEART_LOBE_R    6
#define HEART_HALF_W   12    // lobe centre (±6) + lobe radius (6) = ±12 from origin
#define HEART_W        24
#define HEART_TRI_DEPTH 12   // triangle tip extends this far below the lobe line

static char s_home_city[NAME_LEN] = "LOCAL";

static int  s_city_count = 0;
static char s_city_names[MAX_CITIES][NAME_LEN];
static int  s_city_offsets[MAX_CITIES];   // seconds east of UTC

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Format an HH:MM (or h:MM + a/p when in 12h mode) string from a struct tm.
static void fmt_time(const struct tm *t, char *buf, size_t len, bool with_ampm) {
  if (clock_is_24h_style()) {
    strftime(buf, len, "%H:%M", t);
  } else {
    char tmp[12];
    strftime(tmp, sizeof(tmp), "%I:%M", t);
    char *p = tmp;
    if (*p == '0') {          // strip leading zero ("08:30" -> "8:30")
      p++;
    }
    if (with_ampm) {
      snprintf(buf, len, "%s%s", p, (t->tm_hour < 12) ? "a" : "p");
    } else {
      snprintf(buf, len, "%s", p);
    }
  }
}

// The device's own current UTC offset, in seconds (day-boundary aware so it is
// correct for offsets all the way out to +/-14h). This is the offset of the
// big clock, used to detect city rows that merely duplicate it.
static int device_utc_offset(time_t now) {
  struct tm lt = *localtime(&now);
  struct tm gt = *gmtime(&now);

  int day_delta;
  if (lt.tm_year != gt.tm_year) {
    day_delta = (lt.tm_year > gt.tm_year) ? 1 : -1;
  } else {
    day_delta = lt.tm_yday - gt.tm_yday;   // -1, 0 or +1
  }

  int minutes = day_delta * 1440
              + (lt.tm_hour * 60 + lt.tm_min)
              - (gt.tm_hour * 60 + gt.tm_min);
  return minutes * 60;
}

// Snap a UTC offset (seconds) to the nearest whole minute. Real timezone
// offsets are always whole minutes; a stray sub-minute value (e.g. a phone
// rounding 28800 to 28799) would make a city's minute tick over a second or two
// out of step with the local clock, so the rows would briefly disagree with the
// big time before catching up on the next redraw.
static int snap_offset(int secs) {
  int r = secs % 60;
  if (r == 0) { return secs; }
  if (r < 0)  { r += 60; }            // normalise remainder to 0..59
  return (r < 30) ? (secs - r) : (secs - r + 60);
}

// Calendar-day difference of a city relative to home (-1, 0 or +1).
static int day_diff(const struct tm *city, const struct tm *home) {
  if (city->tm_year != home->tm_year) {
    return (city->tm_year > home->tm_year) ? 1 : -1;
  }
  if (city->tm_yday != home->tm_yday) {
    return (city->tm_yday > home->tm_yday) ? 1 : -1;
  }
  return 0;
}

// Draw a small heart centred (horizontally) on its lobes at `origin`.
static void draw_heart(GContext *ctx, GPoint origin) {
  graphics_context_set_fill_color(ctx, ACCENT_COLOR);
  gpath_move_to(s_heart_path, origin);
  gpath_draw_filled(ctx, s_heart_path);
  graphics_fill_circle(ctx, GPoint(origin.x - HEART_LOBE_R, origin.y), HEART_LOBE_R);
  graphics_fill_circle(ctx, GPoint(origin.x + HEART_LOBE_R, origin.y), HEART_LOBE_R);
}

// Most recent heart rate in BPM, or 0 if unavailable / no reading yet.
static int current_heart_rate(void) {
#if defined(PBL_HEALTH)
  HealthValue v = health_service_peek_current_value(HealthMetricHeartRateBPM);
  if (v > 0) {
    return (int) v;
  }
#endif
  return 0;
}

// ---------------------------------------------------------------------------
// Persistence (lets the face render correct names/offsets before JS connects)
// ---------------------------------------------------------------------------

static void save_state(void) {
  persist_write_string(PK_HOME_CITY, s_home_city);
  persist_write_int(PK_CITY_COUNT, s_city_count);
  persist_write_data(PK_NAMES, s_city_names, sizeof(s_city_names));
  persist_write_data(PK_OFFSETS, s_city_offsets, sizeof(s_city_offsets));
}

static void load_state(void) {
  if (persist_exists(PK_HOME_CITY)) {
    persist_read_string(PK_HOME_CITY, s_home_city, NAME_LEN);
  }
  if (persist_exists(PK_CITY_COUNT)) {
    s_city_count = persist_read_int(PK_CITY_COUNT);
  }
  if (persist_exists(PK_NAMES)) {
    persist_read_data(PK_NAMES, s_city_names, sizeof(s_city_names));
  }
  if (persist_exists(PK_OFFSETS)) {
    persist_read_data(PK_OFFSETS, s_city_offsets, sizeof(s_city_offsets));
  }
  if (s_city_count < 0)            s_city_count = 0;
  if (s_city_count > MAX_CITIES)   s_city_count = MAX_CITIES;

  // Snap any persisted offsets that pre-date the whole-minute fix, so a stale
  // sub-minute value can't desync the rows from the big clock before the phone
  // re-sends config.
  for (int i = 0; i < s_city_count; i++) {
    s_city_offsets[i] = snap_offset(s_city_offsets[i]);
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

static void canvas_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  const int   w = b.size.w;

  const time_t  now      = time(NULL);        // UTC on modern PebbleOS
  const struct tm local  = *localtime(&now);  // device / current-location time
  const int home_offset  = device_utc_offset(now);

  // Background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // --- Big current-location clock (hero, white) --------------------------
  // White is the key contrast fix. Archivo Bold 68 (tracking -2) on colour
  // models; ROBOTO_BOLD_SUBSET_49 fallback on aplite/diorite.
  char big[12];
  fmt_time(&local, big, sizeof(big), false);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, big, s_font_clock,
                     GRect(0, 8, w, 74),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // --- Heart-rate line: heart icon + "<bpm> BPM", centred as one group ----
  char hr[12];
  int bpm = current_heart_rate();
  if (bpm > 0) {
    snprintf(hr, sizeof(hr), "%d BPM", bpm);
  } else {
    strncpy(hr, "-- BPM", sizeof(hr));
  }
  const int hr_y = 78;                               // top of the HR text box
  const int hr_box_h = 28;                           // fits the 20px glyphs
  const GSize hr_sz = graphics_text_layout_get_content_size(
      hr, s_font_hr, GRect(0, 0, w, hr_box_h),
      GTextOverflowModeFill, GTextAlignmentLeft);
  const int hr_gap   = 6;
  const int group_w  = HEART_W + hr_gap + hr_sz.w;
  const int group_x  = (w - group_w) / 2;
  // Centre the heart on the value's cap-height (not the baseline) so the icon
  // and "72 BPM" read as one optical line. The cap band of a 20px glyph sits
  // roughly hr_y+13; the heart's own visual centre is its origin + (tri-lobe)/2.
  const int cap_center  = hr_y + 13;
  const int heart_cy    = cap_center - (HEART_TRI_DEPTH - HEART_LOBE_R) / 2;
  draw_heart(ctx, GPoint(group_x + HEART_HALF_W, heart_cy));
  graphics_context_set_text_color(ctx, GREY_COLOR);
  graphics_draw_text(ctx, hr, s_font_hr,
                     GRect(group_x + HEART_W + hr_gap, hr_y, hr_sz.w + 4, hr_box_h),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  // --- Hairline divider (thin blue) --------------------------------------
  const int divider_y = 112;
  graphics_context_set_stroke_color(ctx, ACCENT_COLOR);
  graphics_draw_line(ctx, GPoint(8, divider_y), GPoint(w - 8, divider_y));

  // --- City list (bottom-anchored) ---------------------------------------
  // 34px row pitch, 8px bottom margin (the emery reference puts rows at
  // y=124/158/192). Derived from bounds so it adapts to other screen sizes.
  const int row_pitch     = 34;
  const int name_w        = (w * 11) / 20;          // ~55% for the name
  const int bottom_margin = 8;
  const int list_top      = divider_y + 4;
  const int right         = w - 8;

  // Pass 1: collect the cities that will actually be shown -- skip any whose
  // offset duplicates the device's own time -- capped to what fits on screen.
  int visible[MAX_CITIES];
  int vis_n = 0;
  const int max_rows = (b.size.h - bottom_margin - list_top) / row_pitch;
  for (int i = 0; i < s_city_count && vis_n < max_rows; i++) {
    // Hide a city in the same zone as the device (it would duplicate the big
    // clock). Tolerant compare: distinct zones differ by >= 15 min, so anything
    // within a minute is "the same zone" and absorbs any rounding in the offset.
    int delta = s_city_offsets[i] - home_offset;
    if (delta < 0) { delta = -delta; }
    if (delta < 60) {
      continue;
    }
    visible[vis_n++] = i;
  }

  if (vis_n == 0) {
    graphics_context_set_text_color(ctx, GREY_COLOR);
    graphics_draw_text(ctx, "Add cities from\nthe phone settings",
                       s_font_name,
                       GRect(8, list_top + 6, w - 16, 60),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // Pass 2: draw bottom-aligned -- the last city sits at the bottom edge.
  int y = b.size.h - bottom_margin - vis_n * row_pitch;
  if (y < list_top) { y = list_top; }

  for (int k = 0; k < vis_n; k++) {
    const int i = visible[k];
    const time_t   city_epoch = now + s_city_offsets[i];
    const struct tm ct        = *gmtime(&city_epoch);

    // City name (left) -- UPPERCASE, grey. Baseline nudged down to sit on the
    // taller time's baseline.
    char nm[NAME_LEN];
    strncpy(nm, s_city_names[i], sizeof(nm) - 1);
    nm[sizeof(nm) - 1] = '\0';
    for (char *c = nm; *c; c++) {
      if (*c >= 'a' && *c <= 'z') { *c -= 32; }
    }
    graphics_context_set_text_color(ctx, GREY_COLOR);
    graphics_draw_text(ctx, nm, s_font_name,
                       GRect(8, y + 11, name_w - 8, row_pitch),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // City time (right, blue). The +1/-1 day marker (kept, per the owner) is
    // drawn small and grey to its right so it doesn't compete with the time.
    char tbuf[12];
    fmt_time(&ct, tbuf, sizeof(tbuf), true);
    const int dd = day_diff(&ct, &local);

    int mw = 0;
    if (dd != 0) {
      char mk[6];
      snprintf(mk, sizeof(mk), "%+d", dd);            // "+1" / "-1"
      GFont mfont = fonts_get_system_font(FONT_KEY_GOTHIC_14);
      GSize ms = graphics_text_layout_get_content_size(
          mk, mfont, GRect(0, 0, 40, 18), GTextOverflowModeFill, GTextAlignmentLeft);
      mw = ms.w + 3;                                  // marker + small gap
      graphics_context_set_text_color(ctx, GREY_COLOR);
      graphics_draw_text(ctx, mk, mfont,
                         GRect(right - ms.w, y + 13, ms.w + 2, 18),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }

    graphics_context_set_text_color(ctx, ACCENT_COLOR);
    graphics_draw_text(ctx, tbuf, s_font_time,
                       GRect(name_w, y, (right - mw) - name_w, row_pitch),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);

    y += row_pitch;
  }
}

// ---------------------------------------------------------------------------
// AppMessage (city config arrives from PebbleKit JS)
// ---------------------------------------------------------------------------

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *home = dict_find(iter, MESSAGE_KEY_HomeCity);
  if (home && home->value->cstring[0] != '\0') {
    strncpy(s_home_city, home->value->cstring, NAME_LEN - 1);
    s_home_city[NAME_LEN - 1] = '\0';
  }

  Tuple *names   = dict_find(iter, MESSAGE_KEY_CityNames);
  Tuple *offsets = dict_find(iter, MESSAGE_KEY_CityOffsets);

  if (names && offsets) {
    // Parse the two '|'-delimited lists. We split by hand rather than with
    // strtok(): on this SDK strtok() does not preserve its cursor across calls
    // (the first strtok(NULL,...) returns NULL), so only the first field would
    // ever be read. Buffers are static, not stack locals -- ~0.5 KB would
    // overflow the small stack the AppMessage callback runs on.
    static char nbuf[MAX_CITIES * NAME_LEN];
    static char obuf[256];
    strncpy(nbuf, names->value->cstring, sizeof(nbuf) - 1);
    nbuf[sizeof(nbuf) - 1] = '\0';
    strncpy(obuf, offsets->value->cstring, sizeof(obuf) - 1);
    obuf[sizeof(obuf) - 1] = '\0';

    int n = 0;
    for (char *p = nbuf; n < MAX_CITIES && *p; ) {
      char *start = p;
      while (*p && *p != '|') { p++; }
      if (*p == '|') { *p++ = '\0'; }      // terminate this field, advance
      strncpy(s_city_names[n], start, NAME_LEN - 1);
      s_city_names[n][NAME_LEN - 1] = '\0';
      n++;
    }

    int m = 0;
    for (char *p = obuf; m < MAX_CITIES && *p; ) {
      char *start = p;
      while (*p && *p != '|') { p++; }
      if (*p == '|') { *p++ = '\0'; }
      s_city_offsets[m] = snap_offset(atoi(start));
      m++;
    }

    s_city_count = (n < m) ? n : m;
    save_state();
    if (s_canvas) {
      layer_mark_dirty(s_canvas);
    }
  }
}

// ---------------------------------------------------------------------------
// Window / app lifecycle
// ---------------------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if (s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if ((event == HealthEventHeartRateUpdate || event == HealthEventSignificantUpdate)
      && s_canvas) {
    layer_mark_dirty(s_canvas);
  }
}
#endif

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  load_state();

  // Bundled Archivo fonts on the colour models; system-font fallbacks on the
  // 1-bit models (where the resources aren't compiled in -- see package.json).
#ifdef PBL_COLOR
  s_font_clock = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_BOLD_68));
  s_font_time  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_BOLD_28));
  s_font_name  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_16));
  s_font_hr    = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_20));
#else
  s_font_clock = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  s_font_time  = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_font_name  = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_font_hr    = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#endif

  s_heart_path = gpath_create(&HEART_PATH_INFO);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  // Inbox must hold HomeCity + the two '|'-delimited city lists plus dict
  // overhead; 512 bytes is ample. We never send to the phone, so the outbox
  // can be tiny.
  app_message_open(512, 64);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif
}

static void deinit(void) {
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  tick_timer_service_unsubscribe();
  app_message_deregister_callbacks();
  window_destroy(s_window);
  gpath_destroy(s_heart_path);
#ifdef PBL_COLOR
  fonts_unload_custom_font(s_font_clock);
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_name);
  fonts_unload_custom_font(s_font_hr);
#endif
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
