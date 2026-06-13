// ---------------------------------------------------------------------------
// Travel Time - PebbleKit JS
//
// Responsibilities:
//   * Provide a settings page (self-contained data: URI, no hosting needed)
//     for picking which cities to show.
//   * Compute each selected city's CURRENT UTC offset (DST-aware, via Intl)
//     plus the phone's own timezone name, and push them to the watch.
// ---------------------------------------------------------------------------

// Master list of selectable cities.
//   tz: IANA timezone (used for accurate, DST-aware offsets via Intl)
//   fb: fallback offset in MINUTES east of UTC, used only if Intl/timeZone is
//       unavailable on the device (ignores DST).
var CITIES = [
  { name: 'Honolulu',     tz: 'Pacific/Honolulu',     fb: -600 },
  { name: 'Los Angeles',  tz: 'America/Los_Angeles',  fb: -480 },
  { name: 'Denver',       tz: 'America/Denver',       fb: -420 },
  { name: 'Chicago',      tz: 'America/Chicago',      fb: -360 },
  { name: 'New York',     tz: 'America/New_York',     fb: -300 },
  { name: 'Sao Paulo',    tz: 'America/Sao_Paulo',    fb: -180 },
  { name: 'London',       tz: 'Europe/London',        fb: 0 },
  { name: 'Paris',        tz: 'Europe/Paris',         fb: 60 },
  { name: 'Berlin',       tz: 'Europe/Berlin',        fb: 60 },
  { name: 'Cairo',        tz: 'Africa/Cairo',         fb: 120 },
  { name: 'Moscow',       tz: 'Europe/Moscow',        fb: 180 },
  { name: 'Dubai',        tz: 'Asia/Dubai',           fb: 240 },
  { name: 'Karachi',      tz: 'Asia/Karachi',         fb: 300 },
  { name: 'Mumbai',       tz: 'Asia/Kolkata',         fb: 330 },
  { name: 'Dhaka',        tz: 'Asia/Dhaka',           fb: 360 },
  { name: 'Bangkok',      tz: 'Asia/Bangkok',         fb: 420 },
  { name: 'Singapore',    tz: 'Asia/Singapore',       fb: 480 },
  { name: 'Hong Kong',    tz: 'Asia/Hong_Kong',       fb: 480 },
  { name: 'Beijing',      tz: 'Asia/Shanghai',        fb: 480 },
  { name: 'Perth',        tz: 'Australia/Perth',      fb: 480 },
  { name: 'Tokyo',        tz: 'Asia/Tokyo',           fb: 540 },
  { name: 'Seoul',        tz: 'Asia/Seoul',           fb: 540 },
  { name: 'Brisbane',     tz: 'Australia/Brisbane',   fb: 600 },
  { name: 'Sydney',       tz: 'Australia/Sydney',     fb: 600 },
  { name: 'Auckland',     tz: 'Pacific/Auckland',     fb: 720 },
  { name: 'Wanaka',       tz: 'Pacific/Auckland',     fb: 720 }
];

// Cities selected on first run, before the user opens settings.
var DEFAULT_SELECTION = ['Perth', 'Brisbane', 'Sydney', 'Wanaka'];

// --- Intl safety detection --------------------------------------------------
//
// The Pebble emulator's JS engine (pypkjs / STPyV8) ships without working ICU
// data: constructing an Intl.DateTimeFormat HARD-CRASHES the whole JS process
// (an out-of-memory abort, NOT a catchable exception), so a try/catch can't
// save us and no config is ever sent. Detect that environment up front via the
// watch model -- the emulator always reports a "qemu_*" model -- and avoid Intl
// there, falling back to each city's fixed offset. Real phones keep full,
// DST-aware accuracy via Intl.
var USE_INTL = true;          // assume a real device until told otherwise
var INTL_CHECKED = false;

function detectIntlSafety() {
  if (INTL_CHECKED) { return; }
  INTL_CHECKED = true;
  try {
    var info = Pebble.getActiveWatchInfo ? Pebble.getActiveWatchInfo() : null;
    if (info && typeof info.model === 'string' && info.model.indexOf('qemu') === 0) {
      USE_INTL = false;       // emulator: ICU/Intl is unsafe here
      console.log('Travel Time: emulator detected (' + info.model + ') - using fixed offsets, no Intl');
    }
  } catch (e) {
    // getActiveWatchInfo unavailable (older runtime): assume a real device.
  }
}

// --- offset computation -----------------------------------------------------

// Current UTC offset (seconds east of UTC) for an IANA timezone. DST-aware via
// Intl on real devices; falls back to the fixed offset when Intl is unsafe.
function offsetSeconds(tz, fbMinutes) {
  if (USE_INTL) {
    try {
      var now = new Date();
      var dtf = new Intl.DateTimeFormat('en-US', {
        timeZone: tz, hour12: false,
        year: 'numeric', month: 'numeric', day: 'numeric',
        hour: 'numeric', minute: 'numeric', second: 'numeric'
      });
      var p = {};
      dtf.formatToParts(now).forEach(function (part) { p[part.type] = part.value; });
      var hour = parseInt(p.hour, 10) % 24;   // Intl can emit "24" at midnight
      var asUTC = Date.UTC(p.year, p.month - 1, p.day, hour, p.minute, p.second);
      // Round to the whole minute: real TZ offsets are always whole minutes,
      // and asUTC (truncated to the second) vs now.getTime() (carries ms) would
      // otherwise drift by a second, breaking the watch's "same as local" test.
      return Math.round((asUTC - now.getTime()) / 60000) * 60;
    } catch (e) {
      /* fall through to fixed offset */
    }
  }
  return (fbMinutes || 0) * 60;              // fallback: fixed offset, no DST
}

// The phone's own timezone name -> a short label for the big clock.
function homeCityLabel() {
  if (USE_INTL) {
    try {
      var tz = Intl.DateTimeFormat().resolvedOptions().timeZone; // e.g. "Australia/Sydney"
      if (tz) {
        return tz.split('/').pop().replace(/_/g, ' ');
      }
    } catch (e) { /* fall through */ }
  }
  return 'LOCAL';
}

// --- selection storage ------------------------------------------------------

function loadSelection() {
  try {
    var raw = localStorage.getItem('cities');
    if (raw) { return JSON.parse(raw); }
  } catch (e) { /* ignore */ }
  return DEFAULT_SELECTION.slice();
}

function saveSelection(names) {
  try { localStorage.setItem('cities', JSON.stringify(names)); } catch (e) { /* ignore */ }
}

// --- send to watch ----------------------------------------------------------

function sendConfig() {
  detectIntlSafety();           // decide Intl-vs-fixed-offset before any Intl use
  var selected = loadSelection();
  var names = [];
  var offs = [];

  selected.forEach(function (name) {
    for (var i = 0; i < CITIES.length; i++) {
      if (CITIES[i].name === name) {
        names.push(CITIES[i].name);
        offs.push(String(offsetSeconds(CITIES[i].tz, CITIES[i].fb)));
        break;
      }
    }
  });

  var dict = {
    HomeCity: homeCityLabel(),
    CityCount: names.length,
    CityNames: names.join('|'),
    CityOffsets: offs.join('|')
  };

  Pebble.sendAppMessage(dict, function () {
    console.log('Travel Time: config sent (' + names.length + ' cities)');
  }, function (e) {
    console.log('Travel Time: send failed - ' + JSON.stringify(e));
  });
}

// --- settings page (self-contained) -----------------------------------------

function buildConfigPage(selected) {
  var cityJson = JSON.stringify(CITIES.map(function (c) { return c.name; }));
  var selJson = JSON.stringify(selected);

  var html =
    '<!DOCTYPE html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>Travel Time</title><style>' +
    'body{font-family:-apple-system,Roboto,Helvetica,sans-serif;margin:0;background:#f4f4f4;color:#222}' +
    'header{background:#000;color:#fff;padding:16px;font-size:20px;font-weight:600}' +
    'p.hint{padding:10px 16px;margin:0;color:#666;font-size:13px}' +
    'ul{list-style:none;margin:0;padding:0}' +
    'li{display:flex;align-items:center;padding:14px 16px;border-bottom:1px solid #ddd;background:#fff}' +
    'li label{flex:1;font-size:17px}' +
    'input[type=checkbox]{width:22px;height:22px;margin-right:14px}' +
    'button{position:fixed;bottom:0;left:0;right:0;border:0;background:#ff4700;color:#fff;' +
    'font-size:18px;padding:16px;width:100%}' +
    'ul{margin-bottom:64px}' +
    '</style></head><body>' +
    '<header>Travel Time</header>' +
    '<p class="hint">Choose the cities to show beneath your local time. A city in your current timezone is hidden automatically.</p>' +
    '<ul id="list"></ul>' +
    '<button id="save">Save</button>' +
    '<script>' +
    'var CITIES=' + cityJson + ';' +
    'var SEL=' + selJson + ';' +
    'var list=document.getElementById("list");' +
    'CITIES.forEach(function(name){' +
    'var li=document.createElement("li");' +
    'var cb=document.createElement("input");cb.type="checkbox";cb.value=name;' +
    'cb.checked=SEL.indexOf(name)>=0;cb.id="c_"+name;' +
    'var lb=document.createElement("label");lb.textContent=name;lb.htmlFor=cb.id;' +
    'li.appendChild(cb);li.appendChild(lb);list.appendChild(li);});' +
    // On a real phone the return URL is "pebblejs://close#"; the emulator
    // passes its own via a ?return_to= query param. Honour whichever is given.
    'function getReturnTo(){var m=/[?&]return_to=([^&#]+)/.exec(location.href);' +
    'return m?decodeURIComponent(m[1]):"pebblejs://close#";}' +
    'document.getElementById("save").addEventListener("click",function(){' +
    'var chosen=[];CITIES.forEach(function(name){' +
    'if(document.getElementById("c_"+name).checked){chosen.push(name);}});' +
    'location.href=getReturnTo()+encodeURIComponent(JSON.stringify(chosen));});' +
    '</script></body></html>';

  return 'data:text/html,' + encodeURIComponent(html);
}

// --- event wiring -----------------------------------------------------------

Pebble.addEventListener('ready', function () {
  console.log('Travel Time: PebbleKit JS ready');
  sendConfig();
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(buildConfigPage(loadSelection()));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) { return; }            // user cancelled
  try {
    var chosen = JSON.parse(decodeURIComponent(e.response));
    if (Array.isArray(chosen)) {
      saveSelection(chosen);
      sendConfig();
    }
  } catch (err) {
    console.log('Travel Time: bad config response - ' + err);
  }
});
