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

// Accent colour. Selectable from the settings page (a palette of Pebble Time 2
// band colours); defaults to the brand blue (GColorVividCerulean). Sent to the
// watch as a 0xRRGGBB integer. Only applies on the colour models.
var DEFAULT_ACCENT = '#00AAFF';

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
// Intl on real devices; falls back to `fbSec` (a snapshot offset in seconds,
// computed in the settings page where Intl works) when Intl is unsafe here.
function offsetSeconds(tz, fbSec) {
  if (USE_INTL && tz) {
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
      /* fall through to the snapshot offset */
    }
  }
  return fbSec || 0;                         // fallback: snapshot offset, no live DST
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
//
// A selection is an array of { name, tz, off }:
//   name : the label shown on the watch (editable -- defaults to the zone city)
//   tz   : IANA timezone id (used to recompute the live, DST-aware offset)
//   off  : snapshot offset in seconds, computed in the settings page; used as
//          the fallback when Intl is unavailable (e.g. the emulator).

// Map a curated-list city name to a {name,tz,off} entry (for defaults + the
// migration of the old name-only saved format).
function cityFromName(name) {
  for (var i = 0; i < CITIES.length; i++) {
    if (CITIES[i].name === name) {
      return { name: CITIES[i].name, tz: CITIES[i].tz, off: CITIES[i].fb * 60 };
    }
  }
  return null;
}

function defaultSelection() {
  var out = [];
  DEFAULT_SELECTION.forEach(function (n) {
    var c = cityFromName(n);
    if (c) { out.push(c); }
  });
  return out;
}

function loadSelection() {
  var raw = null;
  try { raw = localStorage.getItem('cities'); } catch (e) { /* ignore */ }
  if (raw == null) { return defaultSelection(); }      // first run only
  try {
    var arr = JSON.parse(raw);
    if (!Array.isArray(arr)) { return defaultSelection(); }
    if (arr.length && typeof arr[0] === 'object' && arr[0] && arr[0].tz) {
      return arr;                                       // new {name,tz,off} format
    }
    // Migrate the old format (array of plain city-name strings).
    var out = [];
    arr.forEach(function (n) { var c = cityFromName(n); if (c) { out.push(c); } });
    return out;                                         // may be [] if user cleared all
  } catch (e) {
    return defaultSelection();
  }
}

function saveSelection(sel) {
  try { localStorage.setItem('cities', JSON.stringify(sel)); } catch (e) { /* ignore */ }
}

function loadAccent() {
  try { var a = localStorage.getItem('accent'); if (a) { return a; } } catch (e) { /* ignore */ }
  return DEFAULT_ACCENT;
}

function saveAccent(hex) {
  try { localStorage.setItem('accent', hex); } catch (e) { /* ignore */ }
}

// --- send to watch ----------------------------------------------------------

function sendConfig() {
  detectIntlSafety();           // decide Intl-vs-snapshot before any Intl use
  var selected = loadSelection();
  var names = [];
  var offs = [];

  selected.forEach(function (c) {
    if (!c || !c.name) { return; }
    names.push(c.name);
    offs.push(String(offsetSeconds(c.tz, c.off)));   // live via Intl, else snapshot
  });

  var dict = {
    HomeCity: homeCityLabel(),
    CityCount: names.length,
    CityNames: names.join('|'),
    CityOffsets: offs.join('|'),
    AccentColor: parseInt(loadAccent().slice(1), 16)   // '#00AAFF' -> 0x00AAFF
  };

  Pebble.sendAppMessage(dict, function () {
    console.log('Travel Time: config sent (' + names.length + ' cities)');
  }, function (e) {
    console.log('Travel Time: send failed - ' + JSON.stringify(e));
  });
}

// --- settings page (self-contained) -----------------------------------------
//
// Search the full IANA timezone database (Intl.supportedValuesOf) for any city,
// tap to add, rename inline, remove with x. Falls back to the curated list on
// browsers without supportedValuesOf. Note: no apostrophes in any page text --
// the page source is embedded inside a single-quoted JS string here.

function buildConfigPage(selected, accent) {
  // Escape a JSON blob so it can sit safely inside the single-quoted page source.
  function esc(s) { return s.replace(/\\/g, '\\\\').replace(/'/g, "\\'"); }
  var selJson   = esc(JSON.stringify(selected));
  var accJson   = esc(JSON.stringify(accent || DEFAULT_ACCENT));
  var fbZonesJson = esc(JSON.stringify(CITIES.map(function (c) {
    return { name: c.name, tz: c.tz };
  })));

  var html =
    '<!DOCTYPE html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>Travel Time</title><style>' +
    'body{font-family:-apple-system,Roboto,Helvetica,sans-serif;margin:0;background:#f4f4f4;color:#222}' +
    'header{background:#000;color:#fff;padding:16px;font-size:20px;font-weight:600}' +
    'p.hint{padding:10px 16px;margin:0;color:#666;font-size:13px}' +
    '.sec{padding:8px 16px 4px;font-size:12px;color:#888;text-transform:uppercase;letter-spacing:.05em}' +
    'ul{list-style:none;margin:0;padding:0}' +
    'li{display:flex;align-items:center;padding:12px 16px;border-bottom:1px solid #e2e2e2;background:#fff}' +
    '.sel li .nm{flex:1;font-size:17px;border:0;background:transparent;color:#111;padding:4px 0}' +
    '.sel li .tz{font-size:12px;color:#999;margin:0 10px;white-space:nowrap}' +
    '.sel li .rm{border:0;background:#eee;color:#c00;font-size:18px;width:30px;height:30px;border-radius:15px;position:static}' +
    '.res li{cursor:pointer}' +
    '.res li .nm{flex:1;font-size:17px}' +
    '.res li .tm{font-size:13px;color:#0a86c8;margin-left:10px}' +
    '#q{box-sizing:border-box;width:calc(100% - 32px);margin:8px 16px;padding:12px;font-size:16px;' +
    'border:1px solid #ccc;border-radius:8px}' +
    '#save{position:fixed;bottom:0;left:0;right:0;border:0;background:#0a86c8;color:#fff;font-size:18px;padding:16px;width:100%}' +
    '#results{margin-bottom:72px}' +
    '.empty{padding:10px 16px;color:#aaa;font-size:14px}' +
    '.bands{display:flex;flex-wrap:wrap;gap:12px;padding:12px 16px;background:#fff;border-bottom:1px solid #e2e2e2}' +
    '.sw{width:38px;height:38px;border-radius:19px;border:3px solid #fff;box-shadow:0 0 0 1px #ccc;cursor:pointer}' +
    '.sw.on{border-color:#111;box-shadow:0 0 0 2px #111}' +
    '</style></head><body>' +
    '<header>Travel Time</header>' +
    '<p class="hint">Search for any city or timezone and tap to add it. Tap a name to rename it. A city in your current timezone is hidden on the watch. Up to 10.</p>' +
    '<div class="sec">Showing on watch</div><ul id="sel" class="sel"></ul>' +
    '<div class="sec">Accent colour</div><div id="bands" class="bands"></div>' +
    '<input id="q" type="search" placeholder="Search cities (Tokyo, New York, Paris)" autocomplete="off">' +
    '<ul id="results" class="res"></ul>' +
    '<button id="save">Save</button>' +
    '<script>' +
    'var SEL=JSON.parse(\'' + selJson + '\');' +
    'var FB=JSON.parse(\'' + fbZonesJson + '\');' +
    'var MAX=10;' +
    'function disp(tz){return tz.split("/").pop().replace(/_/g," ");}' +
    'function allZones(){try{if(Intl.supportedValuesOf){return Intl.supportedValuesOf("timeZone").map(function(z){return {tz:z,name:disp(z)};});}}catch(e){}return FB;}' +
    'var ZL=allZones();' +
    'function offFor(tz){try{var n=new Date();var f=new Intl.DateTimeFormat("en-US",{timeZone:tz,hour12:false,year:"numeric",month:"numeric",day:"numeric",hour:"numeric",minute:"numeric",second:"numeric"});var p={};f.formatToParts(n).forEach(function(x){p[x.type]=x.value;});var h=parseInt(p.hour,10)%24;var u=Date.UTC(p.year,p.month-1,p.day,h,p.minute,p.second);return Math.round((u-n.getTime())/60000)*60;}catch(e){return 0;}}' +
    'function nowIn(tz){try{return new Intl.DateTimeFormat([],{timeZone:tz,hour:"2-digit",minute:"2-digit"}).format(new Date());}catch(e){return "";}}' +
    'var selEl=document.getElementById("sel"),resEl=document.getElementById("results"),qEl=document.getElementById("q");' +
    'var ACC=JSON.parse(\'' + accJson + '\');' +
    // The Pebble Time 2 ships in four case/band combos (Black/Grey, Silver/Grey,
    // Black/Red, Silver/Blue) -> three distinct band colours. Blue is the default.
    'var BANDS=[{n:"Blue",h:"#00AAFF"},{n:"Red",h:"#FF0000"},{n:"Grey",h:"#AAAAAA"}];' +
    'var bandsEl=document.getElementById("bands");' +
    'function renderBands(){bandsEl.innerHTML="";BANDS.forEach(function(bd){var s=document.createElement("div");s.className="sw"+(bd.h.toLowerCase()===ACC.toLowerCase()?" on":"");s.style.background=bd.h;s.title=bd.n;s.addEventListener("click",function(){ACC=bd.h;renderBands();});bandsEl.appendChild(s);});}' +
    'function renderSel(){selEl.innerHTML="";if(!SEL.length){var d=document.createElement("div");d.className="empty";d.textContent="No cities yet. Search below to add some.";selEl.appendChild(d);}' +
    'SEL.forEach(function(c,i){var li=document.createElement("li");' +
    'var nm=document.createElement("input");nm.className="nm";nm.value=c.name;nm.maxLength=20;' +
    'nm.addEventListener("input",function(){SEL[i].name=nm.value;});' +
    'var tz=document.createElement("span");tz.className="tz";tz.textContent=disp(c.tz);' +
    'var rm=document.createElement("button");rm.className="rm";rm.textContent="\\u00d7";' +
    'rm.addEventListener("click",function(){SEL.splice(i,1);renderSel();renderRes();});' +
    'li.appendChild(nm);li.appendChild(tz);li.appendChild(rm);selEl.appendChild(li);});}' +
    'function has(tz){return SEL.some(function(s){return s.tz===tz;});}' +
    'function renderRes(){var q=qEl.value.trim().toLowerCase();resEl.innerHTML="";if(!q){return;}' +
    'var hits=[];for(var i=0;i<ZL.length&&hits.length<40;i++){var z=ZL[i];if(has(z.tz))continue;' +
    'if((z.name+" "+z.tz).toLowerCase().indexOf(q)>=0)hits.push(z);}' +
    'if(!hits.length){var d=document.createElement("div");d.className="empty";d.textContent="No matches. Try a major city or region.";resEl.appendChild(d);return;}' +
    'hits.forEach(function(z){var li=document.createElement("li");' +
    'var nm=document.createElement("span");nm.className="nm";nm.textContent=z.name;' +
    'var tm=document.createElement("span");tm.className="tm";tm.textContent=nowIn(z.tz);' +
    'li.appendChild(nm);li.appendChild(tm);' +
    'li.addEventListener("click",function(){if(SEL.length>=MAX){alert("Up to "+MAX+" cities.");return;}' +
    'SEL.push({name:z.name,tz:z.tz,off:offFor(z.tz)});qEl.value="";renderSel();renderRes();qEl.focus();});' +
    'resEl.appendChild(li);});}' +
    'qEl.addEventListener("input",renderRes);' +
    'function getReturnTo(){var m=/[?&]return_to=([^&#]+)/.exec(location.href);return m?decodeURIComponent(m[1]):"pebblejs://close#";}' +
    'document.getElementById("save").addEventListener("click",function(){' +
    'var out=SEL.map(function(c){return {name:(c.name||disp(c.tz)).slice(0,20),tz:c.tz,off:offFor(c.tz)};});' +
    'location.href=getReturnTo()+encodeURIComponent(JSON.stringify({cities:out,accent:ACC}));});' +
    'renderSel();renderBands();' +
    '</script></body></html>';

  return 'data:text/html,' + encodeURIComponent(html);
}

// --- event wiring -----------------------------------------------------------

Pebble.addEventListener('ready', function () {
  console.log('Travel Time: PebbleKit JS ready');
  sendConfig();
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(buildConfigPage(loadSelection(), loadAccent()));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) { return; }            // user cancelled
  try {
    var parsed = JSON.parse(decodeURIComponent(e.response));
    var chosen, accent;
    if (Array.isArray(parsed)) {
      chosen = parsed;                          // old cities-only payload
    } else if (parsed && typeof parsed === 'object') {
      chosen = parsed.cities;
      accent = parsed.accent;
    }
    if (Array.isArray(chosen)) { saveSelection(chosen); }
    if (accent) { saveAccent(accent); }
    sendConfig();
  } catch (err) {
    console.log('Travel Time: bad config response - ' + err);
  }
});
