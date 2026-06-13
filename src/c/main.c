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

// "01b · Quiet in colour": the single brand-blue accent (matched to the owner's
// blue watch band). Snaps to GColorVividCerulean on the 64-colour models; falls
// back to white on the 1-bit models (aplite/diorite) so they stay white-on-black.
#define ACCENT_COLOR PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite)
#define GREY_COLOR   PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)

// Custom Archivo fonts (bundled on the colour models only -- see package.json).
// On aplite/diorite these hold system-font fallbacks instead. The heart-rate
// font is an array, one per scaling tier (see s_hr[] below).
static GFont s_font_clock;   // clock
static GFont s_font_time;    // city time, accent
static GFont s_font_name;    // city name, grey, UPPERCASE

// --- Per-platform type sizes, layout metrics, heart size, HR scaling -------
// emery (200x228) is the design reference; basalt/aplite/diorite (144x168) and
// round chalk (180x180) get scaled-down fonts and compressed spacing. On emery
// and chalk the heart-rate line ALSO grows when fewer cities are shown (more
// free space): three tiers, picked at draw time, with the divider floating up
// to the shortened, bottom-anchored list. Positions combine with
// layer_get_bounds() at draw time.
//
//   *_RID       : bundled Archivo resource per role (colour models)
//   HR_RID_M/L  : medium / large heart-rate fonts for the scaling tiers
//   HEART_*_M/L : medium / large heart lobe radius & triangle-tip depth
//   LAY_LIST_BOTTOM : y the city list bottom-anchors to (screen bottom, or
//                     higher on round so rows clear the narrow corners)
//   HR_SCALE platforms use LAY_CLK_BOT (clock-zone bottom) + LAY_HR_MIN
//   (smallest HR-zone height); the others use fixed LAY_HR_TOP/LAY_DIV_Y/CAP.
#if defined(PBL_PLATFORM_EMERY)
  #define HR_SCALE 1
  #define CLK_RID   RESOURCE_ID_FONT_ARCHIVO_BOLD_68
  #define TIME_RID  RESOURCE_ID_FONT_ARCHIVO_BOLD_28
  #define NAME_RID  RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_16
  #define HR_RID    RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_20
  #define HR_RID_M  RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_28
  #define HR_RID_L  RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_36
  #define HEART_R     6
  #define HEART_TRI   12
  #define HEART_R_M   9
  #define HEART_TRI_M 17
  #define HEART_R_L   12
  #define HEART_TRI_L 22
  #define LAY_CLK_TOP 8
  #define LAY_CLK_BOT 76
  #define LAY_HR_MIN  30
  #define LAY_ROW     34
  #define LAY_SIDE    8
  #define LAY_NAME_DY 11
  #define LAY_LIST_BOTTOM 220
#elif defined(PBL_PLATFORM_CHALK)
  #define HR_SCALE 1
  #define CLK_RID   RESOURCE_ID_FONT_ARCHIVO_BOLD_54
  #define TIME_RID  RESOURCE_ID_FONT_ARCHIVO_BOLD_24
  #define NAME_RID  RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_14
  #define HR_RID    RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_20
  #define HR_RID_M  RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_28
  #define HR_RID_L  RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_36
  #define HEART_R     5
  #define HEART_TRI   10
  #define HEART_R_M   8
  #define HEART_TRI_M 15
  #define HEART_R_L   11
  #define HEART_TRI_L 20
  #define LAY_CLK_TOP 8
  #define LAY_CLK_BOT 64
  #define LAY_HR_MIN  18
  #define LAY_ROW     28
  #define LAY_SIDE    22      // round: keep rows inside the circle
  #define LAY_NAME_DY 8
  #define LAY_LIST_BOTTOM 146 // round: anchor above the narrow bottom corners
#elif defined(PBL_PLATFORM_BASALT)
  #define HR_SCALE 0
  #define CLK_RID  RESOURCE_ID_FONT_ARCHIVO_BOLD_44
  #define TIME_RID RESOURCE_ID_FONT_ARCHIVO_BOLD_22
  #define NAME_RID RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_14
  #define HR_RID   RESOURCE_ID_FONT_ARCHIVO_SEMIBOLD_15
  #define HEART_R     5
  #define HEART_TRI   9
  #define LAY_CLK_TOP 6
  #define LAY_HR_TOP  54
  #define LAY_HR_CAP  9
  #define LAY_DIV_Y   80
  #define LAY_ROW     26
  #define LAY_SIDE    7
  #define LAY_NAME_DY 8
  #define LAY_LIST_BOTTOM 162
#else   /* aplite / diorite -- 1-bit, 144x168, system fonts (no Archivo bundled) */
  #define HR_SCALE 0
  #define HEART_R     5
  #define HEART_TRI   9
  #define LAY_CLK_TOP 6
  #define LAY_HR_TOP  54
  #define LAY_HR_CAP  11
  #define LAY_DIV_Y   80
  #define LAY_ROW     26
  #define LAY_SIDE    7
  #define LAY_NAME_DY 5
  #define LAY_LIST_BOTTOM 162
#endif

#if HR_SCALE
  #define NTIER 3
  static const int TIER_LOBE[NTIER] = { HEART_R, HEART_R_M, HEART_R_L };
  static const int TIER_TRI[NTIER]  = { HEART_TRI, HEART_TRI_M, HEART_TRI_L };
#else
  #define NTIER 1
  static const int TIER_LOBE[NTIER] = { HEART_R };
  static const int TIER_TRI[NTIER]  = { HEART_TRI };
#endif

// Heart paths (lower triangle; lobes are circles) and heart-rate fonts -- one
// per HR tier -- built/loaded in init().
static GPath    *s_heart[NTIER];
static GPoint    s_heart_pts[NTIER][3];
static GPathInfo s_heart_info[NTIER];
static GFont     s_hr[NTIER];

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

// Draw a heart (accent colour) centred horizontally on its lobes at `origin`,
// using the given lobe radius and pre-built triangle path (one per HR tier).
static void draw_heart(GContext *ctx, GPoint origin, int lobe_r, GPath *path) {
  graphics_context_set_fill_color(ctx, ACCENT_COLOR);
  gpath_move_to(path, origin);
  gpath_draw_filled(ctx, path);
  graphics_fill_circle(ctx, GPoint(origin.x - lobe_r, origin.y), lobe_r);
  graphics_fill_circle(ctx, GPoint(origin.x + lobe_r, origin.y), lobe_r);
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

  // --- Collect the visible cities first (the HR line + divider size to the
  //     space they leave) -------------------------------------------------
  const int row_pitch = LAY_ROW;
  const int left  = LAY_SIDE;
  const int right = w - LAY_SIDE;
  GFont mfont = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Capacity: the list bottom-anchors to LAY_LIST_BOTTOM; list_top can rise no
  // higher than the floor (which reserves the clock zone + a minimum HR zone on
  // the scaling platforms, or sits just below the fixed divider otherwise).
#if HR_SCALE
  const int list_floor = LAY_CLK_BOT + LAY_HR_MIN + 4;
#else
  const int list_floor = LAY_DIV_Y + 4;
#endif
  const int max_rows = (LAY_LIST_BOTTOM - list_floor) / row_pitch;

  // Pass 1: skip cities whose offset duplicates the device's own time; cap to
  // capacity; note whether any visible row needs a +/-1 day marker.
  int visible[MAX_CITIES] = {0};
  int vis_n = 0;
  for (int i = 0; i < s_city_count && vis_n < max_rows; i++) {
    int delta = s_city_offsets[i] - home_offset;
    if (delta < 0) { delta = -delta; }
    if (delta < 60) { continue; }
    visible[vis_n++] = i;
  }

  // Bottom-anchor the (shortened) list; the empty state reserves ~2 rows of
  // space for its message. Clamp so it never rises into the clock/HR zone.
  int list_top = LAY_LIST_BOTTOM - (vis_n > 0 ? vis_n : 2) * row_pitch;
  if (list_top < list_floor) { list_top = list_floor; }

  // On the scaling platforms the divider floats up to the list; fixed otherwise.
#if HR_SCALE
  const int divider_y = list_top - 4;
#else
  const int divider_y = LAY_DIV_Y;
#endif

  // --- Big current-location clock (hero, white) --------------------------
  char big[12];
  fmt_time(&local, big, sizeof(big), false);
  graphics_context_set_text_color(ctx, GColorWhite);
#if HR_SCALE
  const int clk_h = LAY_CLK_BOT - LAY_CLK_TOP;
#else
  const int clk_h = LAY_HR_TOP - LAY_CLK_TOP;
#endif
  graphics_draw_text(ctx, big, s_font_clock,
                     GRect(0, LAY_CLK_TOP, w, clk_h),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // --- Heart-rate line: heart + "<bpm> BPM" as one centred group ---------
  // On emery/chalk the tier (font + heart size) grows with the free space, and
  // is stepped down if needed so the line always fits the HR-zone height.
  char hr[12];
  int bpm = current_heart_rate();
  if (bpm > 0) { snprintf(hr, sizeof(hr), "%d BPM", bpm); }
  else         { strncpy(hr, "-- BPM", sizeof(hr)); }

#if HR_SCALE
  const int hr_zone_top = LAY_CLK_BOT;
  const int hr_zone_h   = divider_y - LAY_CLK_BOT;
  int free_rows = max_rows - vis_n;
  if (free_rows < 0) { free_rows = 0; }
  int tier = (free_rows >= 2) ? 2 : free_rows;
  GSize hr_sz = graphics_text_layout_get_content_size(
      hr, s_hr[tier], GRect(0, 0, w, 80), GTextOverflowModeFill, GTextAlignmentLeft);
  while (tier > 0 && hr_sz.h > hr_zone_h) {        // shrink until it fits
    tier--;
    hr_sz = graphics_text_layout_get_content_size(
        hr, s_hr[tier], GRect(0, 0, w, 80), GTextOverflowModeFill, GTextAlignmentLeft);
  }
  const int text_y   = hr_zone_top + (hr_zone_h - hr_sz.h) / 2;
  const int heart_cy = text_y + hr_sz.h / 2 - (TIER_TRI[tier] - TIER_LOBE[tier]) / 2;
#else
  const int tier = 0;
  GSize hr_sz = graphics_text_layout_get_content_size(
      hr, s_hr[tier], GRect(0, 0, w, 80), GTextOverflowModeFill, GTextAlignmentLeft);
  const int text_y   = LAY_HR_TOP;
  const int heart_cy = LAY_HR_TOP + LAY_HR_CAP;
#endif
  const int lobe    = TIER_LOBE[tier];
  const int heart_w = 4 * lobe;
  const int hr_gap  = lobe;
  const int group_x = (w - (heart_w + hr_gap + hr_sz.w)) / 2;
  draw_heart(ctx, GPoint(group_x + heart_w / 2, heart_cy), lobe, s_heart[tier]);
  graphics_context_set_text_color(ctx, GREY_COLOR);
  graphics_draw_text(ctx, hr, s_hr[tier],
                     GRect(group_x + heart_w + hr_gap, text_y, hr_sz.w + 4, hr_sz.h + 6),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  // --- Hairline divider (thin blue) --------------------------------------
  graphics_context_set_stroke_color(ctx, ACCENT_COLOR);
  graphics_draw_line(ctx, GPoint(left, divider_y), GPoint(right, divider_y));

  if (vis_n == 0) {
    graphics_context_set_text_color(ctx, GREY_COLOR);
    graphics_draw_text(ctx, "Add cities from\nthe phone settings",
                       s_font_name,
                       GRect(left, divider_y + 6, right - left, 60),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // Reserve exactly the measured "HH:MM" width on the right for the time; each
  // name gets the remainder (minus that row's day marker, if any) and
  // ellipsizes on a single line. Keeps the time unclipped even at 144px, and
  // only the row with a marker gives up name width.
  const int time_w = graphics_text_layout_get_content_size(
      "00:00", s_font_time, GRect(0, 0, w, 60),
      GTextOverflowModeFill, GTextAlignmentLeft).w;
  const int name_lh = graphics_text_layout_get_content_size(
      "Ag", s_font_name, GRect(0, 0, w, 60),
      GTextOverflowModeFill, GTextAlignmentLeft).h;

  // Pass 2: draw the rows from the (bottom-anchored) list top downward.
  int y = list_top;

  for (int k = 0; k < vis_n; k++) {
    const int i = visible[k];
    const time_t   city_epoch = now + s_city_offsets[i];
    const struct tm ct        = *gmtime(&city_epoch);

    // Time string + this row's +/-1 day-marker width (0 if same day).
    char tbuf[12];
    fmt_time(&ct, tbuf, sizeof(tbuf), true);
    const int dd = day_diff(&ct, &local);
    char mk[6] = "";
    int mw = 0;
    if (dd != 0) {
      snprintf(mk, sizeof(mk), "%+d", dd);            // "+1" / "-1"
      mw = graphics_text_layout_get_content_size(
               mk, mfont, GRect(0, 0, 40, 18),
               GTextOverflowModeFill, GTextAlignmentLeft).w + 3;
    }
    int name_end = right - time_w - 4 - mw;
    if (name_end < left + 24) { name_end = left + 24; }

    // City name (left) -- UPPERCASE, grey, single line, baseline-nudged.
    char nm[NAME_LEN];
    strncpy(nm, s_city_names[i], sizeof(nm) - 1);
    nm[sizeof(nm) - 1] = '\0';
    for (char *c = nm; *c; c++) {
      if (*c >= 'a' && *c <= 'z') { *c -= 32; }
    }
    graphics_context_set_text_color(ctx, GREY_COLOR);
    graphics_draw_text(ctx, nm, s_font_name,
                       GRect(left, y + LAY_NAME_DY, name_end - left, name_lh),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Day marker (small grey) at the far right, then the time (blue) left of it.
    if (mk[0]) {
      graphics_context_set_text_color(ctx, GREY_COLOR);
      graphics_draw_text(ctx, mk, mfont,
                         GRect(right - (mw - 3), y + LAY_NAME_DY + 2, mw, 18),
                         GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    }
    graphics_context_set_text_color(ctx, ACCENT_COLOR);
    graphics_draw_text(ctx, tbuf, s_font_time,
                       GRect(name_end, y, (right - mw) - name_end, row_pitch),
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
  s_font_clock = fonts_load_custom_font(resource_get_handle(CLK_RID));
  s_font_time  = fonts_load_custom_font(resource_get_handle(TIME_RID));
  s_font_name  = fonts_load_custom_font(resource_get_handle(NAME_RID));
  s_hr[0]      = fonts_load_custom_font(resource_get_handle(HR_RID));
#if HR_SCALE
  s_hr[1]      = fonts_load_custom_font(resource_get_handle(HR_RID_M));
  s_hr[2]      = fonts_load_custom_font(resource_get_handle(HR_RID_L));
#endif
#else
  s_font_clock = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  s_font_time  = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_font_name  = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_hr[0]      = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
#endif

  for (int t = 0; t < NTIER; t++) {
    s_heart_pts[t][0] = GPoint(-2 * TIER_LOBE[t], 0);
    s_heart_pts[t][1] = GPoint( 2 * TIER_LOBE[t], 0);
    s_heart_pts[t][2] = GPoint(0, TIER_TRI[t]);
    s_heart_info[t].num_points = 3;
    s_heart_info[t].points = s_heart_pts[t];
    s_heart[t] = gpath_create(&s_heart_info[t]);
  }

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
  for (int t = 0; t < NTIER; t++) {
    gpath_destroy(s_heart[t]);
  }
#ifdef PBL_COLOR
  fonts_unload_custom_font(s_font_clock);
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_name);
  for (int t = 0; t < NTIER; t++) {
    fonts_unload_custom_font(s_hr[t]);
  }
#endif
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
