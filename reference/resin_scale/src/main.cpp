// ============================================================
//  Resin Scale — ESP32-S3 DevKitC + HX711 load cell
//  scale.local | Captive portal WiFi setup | OTA | Calibration
//
//  HX711 wiring:
//    DOUT → GPIO 4      SCK → GPIO 5
//    VCC  → 3V3         GND → GND
//  HX711 RATE pin: tied to GND on most breakout boards (10 samples/s).
//  Lifting it to VCC (80 samples/s) makes the pour display noticeably
//  snappier — the filter below adapts automatically.
//
//  BOOT button (GPIO 0):
//    hold 3 s on power-up     → clear saved WiFi, launch captive portal
//    short press while running → tare
//
//  Status LED (on-board RGB, GPIO 38 on DevKitC-1 v1.1; v1.0 boards use 48):
//    blinking blue → hotspot / captive-portal mode
//    solid green   → connected to WiFi
//    blinking red  → HX711 not responding (check wiring)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Update.h>
#include <esp_arduino_version.h>

#define FW_VERSION  "1.0.0"

// ── HX711 ───────────────────────────────────────────────────
#define PIN_HX_DOUT         4
#define PIN_HX_SCK          5
#define HX_TIMEOUT_MS       1000    // no conversion in this long → HX711 "offline"

// ── Weight filter ───────────────────────────────────────────
// Readings are averaged over a sliding window while the load is steady.
// A reading that jumps more than FILTER_SNAP_G away from the running mean
// resets the window, so the display tracks a pour with minimal lag and
// settles to a quiet average as soon as the pour stops.
#define FILTER_WINDOW       10
#define FILTER_SNAP_G       0.5f
#define STABLE_BAND_G       0.3f    // window spread (max-min) below this → "stable"
#define STABLE_MIN_SAMPLES  5
#define AVG_SAMPLES         10      // samples averaged for tare / calibration
#define DEFAULT_CAL_FACTOR  420.0f  // raw counts per gram — placeholder until calibrated

// ── Network ─────────────────────────────────────────────────
#define AP_SSID             "Scale-Setup"
#define MDNS_NAME           "scale"
#define DNS_PORT            53
#define STA_RETRY_MS        60000   // hotspot fallback: retry saved WiFi this often
#define STA_RETRY_WINDOW_MS 15000   // …for this long each attempt

// ── Status LED ──────────────────────────────────────────────
// On-board WS2812 RGB. DevKitC-1 v1.1 = GPIO 38, v1.0 and most clones = GPIO 48
// (clones often also need their "RGB" solder jumper bridged). This is only the
// default — the pin is changeable from the Admin page and saved in NVS.
#define PIN_STATUS_LED      38

// ── Boot button ─────────────────────────────────────────────
#define BOOT_PIN            0
#define BOOT_HOLD_MS        3000

// Partition label must match the first column of partitions.csv
#define LFS_LABEL           "littlefs"

// Keys the browser may store through /api/store → /<key>.json on LittleFS
static const char* STORE_KEYS[] = { "presets", "parts" };
#define STORE_MAX_BYTES     16384

// Scale UI — web/index.html, linked into the firmware by
// board_build.embed_txtfiles (NUL-terminated).
extern const char INDEX_HTML[] asm("_binary_web_index_html_start");

// ── Globals ─────────────────────────────────────────────────
DNSServer   dnsServer;
WebServer   server(80);
Preferences prefs;

bool        apMode      = false;
bool        lfsOk       = false;
String      savedSsid, savedPass;
String      scanJson    = "[]";   // networks found at boot, for the setup page

// ── Scale state ─────────────────────────────────────────────
static float    calFactor   = DEFAULT_CAL_FACTOR; // raw counts per gram (sign = cell orientation)
static int32_t  calZero     = 0;      // raw reading at 0 g, saved by calibration
static bool     bootTare    = true;   // tare automatically on power-up
static int32_t  zeroOffset  = 0;      // current tare point (raw)
static float    tareSumG    = 0;      // running total of every tare's shift (g) — lets the
                                      // page keep a mix continuous across mid-pour tares
static int32_t  hxRaw       = 0;      // latest single reading
static uint32_t hxLastMs    = 0;
static uint32_t hxSeq       = 0;
static bool     hxOk        = false;

static float    winBuf[FILTER_WINDOW];
static int      winN = 0, winHead = 0;
static float    winMeanRaw  = 0;
static bool     weightStable = false;

// Averaging jobs — tare & calibration collect AVG_SAMPLES fresh readings
// without blocking the loop (the web server keeps answering meanwhile).
enum AvgJob : uint8_t { JOB_NONE, JOB_TARE, JOB_CAL_ZERO, JOB_CAL_SPAN };
static AvgJob   job         = JOB_NONE;
static int64_t  jobAcc      = 0;
static int      jobN        = 0;
static float    jobSpanG    = 0;
static bool     bootTarePending = false;
static String   calMsg      = "";

static bool     _otaStarted = false;

// ============================================================
//  HTML — PROGMEM
// ============================================================

// ── Captive portal WiFi setup ────────────────────────────────
const char CAPTIVE_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Scale WiFi Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;
     display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}
.card{background:#16213e;border-radius:12px;padding:32px;width:100%;max-width:340px;
      box-shadow:0 8px 32px rgba(0,0,0,.4)}
h1{font-size:1.4rem;margin-bottom:24px;text-align:center;color:#4fc3f7}
label{display:block;font-size:.85rem;margin-bottom:6px;color:#90caf9}
input,select{width:100%;padding:10px 14px;border-radius:8px;border:1px solid #0f3460;
      background:#0f3460;color:#fff;font-size:1rem;margin-bottom:18px}
button{width:100%;padding:12px;background:#0288d1;border:none;border-radius:8px;
       color:#fff;font-size:1rem;cursor:pointer}
button:hover{background:#0277bd}
.hint{margin-top:16px;font-size:.85rem;text-align:center;color:#607d8b;line-height:1.5}
.back{display:block;text-align:center;margin-bottom:18px;color:#546e7a;
      text-decoration:none;font-size:.85rem}
.back:hover{color:#90caf9}
.show{display:flex;align-items:center;gap:8px;font-size:.8rem;color:#607d8b;
      margin:-10px 0 18px}
.show input{width:auto;margin:0}
</style></head><body>
<div class="card">
  <a class="back" href="/">&#8592; Use scale without WiFi</a>
  <h1>&#9878; Scale WiFi Setup</h1>
  <form method="POST" action="/wifi-save">
    <label>Network (SSID)</label>
    <select id="nets" onchange="if(this.value)ssid.value=this.value">
      <option value="">Scanning&hellip;</option></select>
    <input type="text" id="ssid" name="ssid" placeholder="or type a network name" required>
    <label>Password</label>
    <input type="password" id="pass" name="pass" placeholder="WiFi password">
    <label class="show"><input type="checkbox"
      onchange="pass.type=this.checked?'text':'password'"> Show password</label>
    <button type="submit">Connect &amp; Save</button>
  </form>
  <p class="hint">Device will restart and connect.<br>Then visit <strong>http://scale.local</strong></p>
</div>
<script>
fetch('/api/scan').then(r=>r.json()).then(l=>{
  const s=document.getElementById('nets');
  s.innerHTML='<option value="">'+(l.length?'Choose a network…':'No networks found')+'</option>'+
    l.map(n=>{const o=document.createElement('option');o.value=n.ssid;
      o.textContent=n.ssid+'  ('+n.rssi+' dBm'+(n.open?', open':'')+')';return o.outerHTML;}).join('');
}).catch(()=>{});
</script>
</body></html>
)html";

// ── Saved / restarting confirmation ─────────────────────────
const char SAVED_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title>
<style>
body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;
     display:flex;align-items:center;justify-content:center;min-height:100vh;text-align:center;padding:16px}
h1{color:#4fc3f7;margin-bottom:12px}p{color:#90caf9;line-height:1.6}strong{color:#fff}
</style></head><body>
<div>
  <h1>&#9878; Credentials Saved</h1>
  <p>Device is restarting&hellip;<br>
  Connect to your WiFi and visit<br><strong>http://scale.local</strong></p>
  <p style="color:#607d8b;font-size:.85rem;margin-top:14px">If it can't connect, the
  <strong>Scale-Setup</strong> hotspot comes back so you can try again.</p>
</div>
</body></html>
)html";

// ── Admin panel (calibration + OTA + WiFi) ───────────────────
const char ADMIN_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Admin &mdash; scale.local</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,sans-serif;background:#1a1a2e;color:#eee;
     min-height:100vh;padding:24px 16px;max-width:640px;margin:0 auto}
nav{margin-bottom:20px}nav a{color:#546e7a;text-decoration:none;font-size:.9rem}
nav a:hover{color:#4fc3f7}
h1{color:#4fc3f7;font-size:1.35rem;margin-bottom:4px}
.sub{color:#607d8b;font-size:.82rem;margin-bottom:28px}
.sec{background:#16213e;border-radius:12px;padding:22px;margin-bottom:18px}
h2{color:#90caf9;font-size:.95rem;margin-bottom:14px;text-transform:uppercase;
   letter-spacing:.05em}
p.t{font-size:.83rem;color:#80cbc4;margin-bottom:12px;line-height:1.5}
input[type=file]{color:#90caf9;font-size:.88rem;display:block;margin-bottom:10px;max-width:100%}
input[type=number],input[type=text]{padding:9px 12px;border-radius:7px;border:1px solid #0f3460;
     background:#0f3460;color:#fff;font-size:.9rem;width:130px}
.btn{display:inline-block;padding:9px 18px;background:#0288d1;border:none;
     border-radius:7px;color:#fff;font-size:.88rem;cursor:pointer;text-decoration:none}
.btn:hover{background:#0277bd}
.btn:disabled{opacity:.45;cursor:default}
.danger{background:#c62828}.danger:hover{background:#b71c1c}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:flex-end;margin-bottom:10px}
.lbl{color:#90caf9;font-size:.8rem;display:block;margin-bottom:4px}
.step{display:flex;gap:12px;padding:14px 0;border-top:1px solid #0f3460}
.step:first-of-type{border-top:none;padding-top:4px}
.num{flex:0 0 26px;height:26px;border-radius:50%;background:#0f3460;color:#4fc3f7;
     display:flex;align-items:center;justify-content:center;font-weight:700;font-size:.85rem}
.live{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:16px}
.live div{background:#0a1628;border-radius:8px;padding:10px;text-align:center}
.live b{display:block;font-size:1.25rem;color:#4fc3f7;font-variant-numeric:tabular-nums}
.live span{font-size:.68rem;color:#546e7a;text-transform:uppercase;letter-spacing:.06em}
.prog{background:#0f3460;border-radius:4px;height:8px;margin-top:10px;
      overflow:hidden;display:none}
.prog-bar{height:100%;background:#4fc3f7;width:0%;transition:width .2s}
.st{margin-top:8px;font-size:.83rem;color:#80cbc4;min-height:1.2em}
table{width:100%;border-collapse:collapse;font-size:.85rem}
td{padding:6px 4px;border-bottom:1px solid #0f3460;word-break:break-all}
td:first-child{color:#546e7a;width:40%}
.chk{display:flex;align-items:center;gap:8px;font-size:.85rem;color:#90caf9;margin-top:14px}
</style></head><body>
<nav><a href="/">&#8592; Scale</a></nav>
<h1>&#9881; Admin Panel</h1>
<p class="sub">scale.local &middot; firmware <span id="fwv">&hellip;</span></p>

<div class="sec">
  <h2>Calibration</h2>
  <div class="live">
    <div><b id="lG">&mdash;</b><span>grams</span></div>
    <div><b id="lRaw">&mdash;</b><span>raw</span></div>
    <div><b id="lSt">&mdash;</b><span>status</span></div>
  </div>
  <div class="step"><div class="num">1</div><div>
    <p class="t">Remove everything from the platform, wait for <em>stable</em>, then set zero.</p>
    <button class="btn" id="bZero" onclick="calZero()">Set Zero</button>
  </div></div>
  <div class="step"><div class="num">2</div><div>
    <p class="t">Place a known reference weight (the heavier the better &mdash;
      ideally 25&ndash;100% of the load cell's rating) and enter its mass.</p>
    <div class="row">
      <label><span class="lbl">Reference mass (g)</span>
        <input type="number" id="spanG" step="0.01" min="0" placeholder="e.g. 500"></label>
      <button class="btn" id="bSpan" onclick="calSpan()">Calibrate</button>
    </div>
  </div></div>
  <p class="st" id="calSt"></p>
  <div class="step" style="border-top:1px solid #0f3460;margin-top:6px"><div class="num">&#9998;</div><div>
    <p class="t">Or set the calibration factor directly (raw counts per gram).</p>
    <div class="row">
      <input type="number" id="fac" step="0.001">
      <button class="btn" onclick="setFactor()">Save Factor</button>
    </div>
  </div></div>
  <label class="chk"><input type="checkbox" id="bootTare" onchange="setBootTare()">
    Tare automatically at power-up (off = use the saved zero from calibration)</label>
</div>

<div class="sec">
  <h2>OTA Firmware Update</h2>
  <p class="t">Upload <code>.pio/build/esp32-s3-devkitc-1/firmware.bin</code>.
     The scale web page is built into the firmware, so this updates it too.</p>
  <input type="file" id="otaBin" accept=".bin">
  <button class="btn" onclick="doOTA()">Flash Firmware</button>
  <div class="prog" id="otaProg"><div class="prog-bar" id="otaBar"></div></div>
  <p class="st" id="otaSt"></p>
</div>

<div class="sec">
  <h2>WiFi</h2>
  <p class="t">The scale works straight off its own <strong>Scale-Setup</strong> hotspot &mdash;
     joining a WiFi network lets you reach it at http://scale.local instead.</p>
  <a class="btn" href="/wifi-setup">Connect to a WiFi Network</a>
  <button class="btn danger"
    onclick="if(confirm('Clear saved WiFi credentials and restart?'))location.href='/wifi-reset'">
    Reset WiFi Credentials
  </button>
  <p class="st">Reset restarts the device back into its own hotspot.</p>
</div>

<div class="sec">
  <h2>Status LED</h2>
  <p class="t">Pin for the on-board RGB LED. <b>Test</b> saves it and flashes the LED white for 3&nbsp;s.
     Usually 38 (DevKitC-1 v1.1) or 48 (v1.0 and most clones &mdash; some need the
     &ldquo;RGB&rdquo; solder jumper on the board bridged).</p>
  <div class="row">
    <label><span class="lbl">GPIO</span><input type="number" id="ledPin" min="1" max="48"></label>
    <button class="btn" onclick="testLed()">Save &amp; Test</button>
  </div>
  <p class="st" id="ledSt"></p>
</div>

<div class="sec">
  <h2>Device Info</h2>
  <table id="info"></table>
</div>

<script>
const $=id=>document.getElementById(id);
function post(url,body){
  return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body||''});
}
function xhr(method,url,body,onProg,onDone){
  const x=new XMLHttpRequest();
  x.timeout=120000; // 2 min — large firmware files need time to write to flash
  if(onProg)x.upload.onprogress=e=>{if(e.lengthComputable)onProg(e.loaded/e.total);};
  x.onload=()=>onDone(x.status,x.responseText);
  x.onerror=()=>onDone(0,'Connection lost');
  x.ontimeout=()=>onDone(0,'Request timed out');
  x.open(method,url);x.send(body);
}
let busy=false;
function poll(){
  fetch('/api/cal').then(r=>r.json()).then(d=>{
    $('lG').textContent=d.ok?d.g.toFixed(2):'—';
    $('lRaw').textContent=d.ok?d.raw:'—';
    $('lSt').textContent=!d.ok?'NO HX711':d.job?'MEASURING':d.stable?'STABLE':'SETTLING';
    $('lSt').style.color=!d.ok?'#ef5350':d.stable&&!d.job?'#4caf50':'#ffb74d';
    if(document.activeElement!==$('fac'))$('fac').value=d.factor;
    $('bootTare').checked=d.boot_tare;
    busy=!!d.job;$('bZero').disabled=busy;$('bSpan').disabled=busy;
    if(d.msg)$('calSt').textContent=d.msg;
  }).catch(()=>{});
}
function calZero(){post('/api/cal/zero').then(()=>{$('calSt').textContent='Measuring zero…';poll();});}
function calSpan(){
  const g=+$('spanG').value;
  if(!(g>0)){$('calSt').textContent='Enter the reference mass first.';return;}
  post('/api/cal/span','grams='+g).then(r=>r.text().then(t=>{$('calSt').textContent=r.ok?'Measuring reference…':t;poll();}));
}
function setFactor(){
  post('/api/cal/factor','factor='+(+$('fac').value)).then(r=>r.text()).then(t=>$('calSt').textContent=t);
}
function setBootTare(){post('/api/cal/boot-tare','on='+($('bootTare').checked?1:0));}
function doOTA(){
  const f=$('otaBin').files[0];if(!f)return;
  $('otaProg').style.display='block';$('otaSt').textContent='Uploading firmware…';
  const fd=new FormData();fd.append('firmware',f,f.name);
  xhr('POST','/api/ota',fd,p=>$('otaBar').style.width=(p*100)+'%',(s,r)=>{
    $('otaSt').textContent=s===200?'Done! Device is restarting…':'Error: '+r;
    if(s===200)setTimeout(()=>location.reload(),8000);
  });
}
function loadInfo(){
  fetch('/api/info').then(r=>r.json()).then(d=>{
    $('fwv').textContent=d.fw;
    const up=d.uptime,h=Math.floor(up/3600),m=Math.floor(up%3600/60);
    const rows=[
      ['Mode',d.ap?'Hotspot (Scale-Setup)':'WiFi client'],
      ['Network',d.ssid||'—'],['IP address',d.ip],
      ['Signal',d.ap?'—':d.rssi+' dBm'],
      ['Firmware',d.fw+' ('+d.built+')'],
      ['Uptime',h+'h '+m+'m'],['Free heap',(d.heap/1024).toFixed(0)+' KB'],
      ['LittleFS',d.lfs_total?d.lfs_used+' / '+d.lfs_total+' KB used':'not mounted'],
      ['HX711',d.hx_ok?'OK':'not responding'],
    ];
    const t=$('info');t.innerHTML='';
    rows.forEach(([k,v])=>{const tr=t.insertRow();tr.insertCell().textContent=k;tr.insertCell().textContent=v;});
  }).catch(()=>{});
}
function testLed(){
  post('/api/led','pin='+(+$('ledPin').value)).then(r=>r.text()).then(t=>$('ledSt').textContent=t);
}
fetch('/api/led').then(r=>r.json()).then(d=>$('ledPin').value=d.pin).catch(()=>{});
poll();loadInfo();
setInterval(poll,400);setInterval(loadInfo,5000);
</script>
</body></html>
)html";

// ============================================================
//  HX711 driver
//  24-bit two's-complement read, then one extra SCK pulse to select
//  channel A / gain 128 for the next conversion. The bit-bang runs in a
//  critical section: if SCK stays high for >60 µs (e.g. preempted by the
//  WiFi task mid-read) the HX711 powers down and the reading is garbage.
// ============================================================
static portMUX_TYPE hxMux = portMUX_INITIALIZER_UNLOCKED;

static inline bool hxReady() { return digitalRead(PIN_HX_DOUT) == LOW; }

static int32_t hxReadRaw() {
    uint32_t v = 0;
    portENTER_CRITICAL(&hxMux);
    for (int i = 0; i < 24; i++) {
        digitalWrite(PIN_HX_SCK, HIGH);
        delayMicroseconds(1);
        v = (v << 1) | (uint32_t)digitalRead(PIN_HX_DOUT);
        digitalWrite(PIN_HX_SCK, LOW);
        delayMicroseconds(1);
    }
    digitalWrite(PIN_HX_SCK, HIGH);   // 25th pulse → channel A, gain 128
    delayMicroseconds(1);
    digitalWrite(PIN_HX_SCK, LOW);
    delayMicroseconds(1);
    portEXIT_CRITICAL(&hxMux);
    if (v & 0x800000) v |= 0xFF000000; // sign-extend
    return (int32_t)v;
}

static void setupHX711() {
    // Pull-up on DOUT: an unplugged HX711 reads "never ready" rather than
    // a stream of zeros, so it's reported as missing instead of as 0 g.
    pinMode(PIN_HX_DOUT, INPUT_PULLUP);
    pinMode(PIN_HX_SCK, OUTPUT);
    digitalWrite(PIN_HX_SCK, LOW);
    Serial.printf("HX711: DOUT=GPIO%d  SCK=GPIO%d\n", PIN_HX_DOUT, PIN_HX_SCK);
}

// ============================================================
//  Scale — calibration storage, filtering, tare
// ============================================================
static void loadCalibration() {
    prefs.begin("scale", true);
    calFactor = prefs.getFloat("factor", DEFAULT_CAL_FACTOR);
    calZero   = prefs.getInt("zero", 0);
    bootTare  = prefs.getBool("bootTare", true);
    prefs.end();
    if (!isfinite(calFactor) || fabsf(calFactor) < 0.001f) calFactor = DEFAULT_CAL_FACTOR;
    zeroOffset = calZero;
    Serial.printf("Calibration: factor=%.4f  zero=%ld  bootTare=%d\n",
        calFactor, (long)calZero, bootTare);
}

static void saveCalibration() {
    prefs.begin("scale", false);
    prefs.putFloat("factor", calFactor);
    prefs.putInt("zero", calZero);
    prefs.putBool("bootTare", bootTare);
    prefs.end();
}

static inline float rawToGrams(float raw) { return (raw - (float)zeroOffset) / calFactor; }
static inline float weightGrams()          { return rawToGrams(winMeanRaw); }

static void startJob(AvgJob j, float spanG = 0) {
    job = j; jobAcc = 0; jobN = 0; jobSpanG = spanG;
    if (j != JOB_TARE) calMsg = "";
}

static void finishJob() {
    int32_t avg = (int32_t)(jobAcc / jobN);
    if (job == JOB_TARE || job == JOB_CAL_ZERO)
        tareSumG += ((float)avg - (float)zeroOffset) / calFactor;
    switch (job) {
        case JOB_TARE:
            zeroOffset = avg;
            Serial.printf("Tare → offset %ld\n", (long)avg);
            break;
        case JOB_CAL_ZERO:
            zeroOffset = calZero = avg;
            saveCalibration();
            calMsg = "✓ Zero set (raw " + String(avg) + "). Now place the reference weight.";
            Serial.printf("Cal zero → %ld\n", (long)avg);
            break;
        case JOB_CAL_SPAN: {
            float delta = (float)avg - (float)zeroOffset;
            // Anything under ~1000 counts is noise — a real weight on a
            // 1–20 kg cell at gain 128 moves the reading by far more.
            if (fabsf(delta) < 1000.0f) {
                calMsg = "✘ Reading barely changed (" + String((long)delta) +
                         " counts) — is the weight on the platform and zero set?";
            } else {
                calFactor = delta / jobSpanG;
                calZero   = zeroOffset;
                saveCalibration();
                calMsg = "✓ Calibrated: factor " + String(calFactor, 4) +
                         " counts/g. Saved.";
                Serial.printf("Cal span %.2f g → factor %.4f\n", jobSpanG, calFactor);
            }
            break;
        }
        default: break;
    }
    job = JOB_NONE;
}

static void pushSample(int32_t raw) {
    hxRaw    = raw;
    hxLastMs = millis();
    hxSeq++;

    if (job != JOB_NONE) {
        jobAcc += raw;
        if (++jobN >= AVG_SAMPLES) finishJob();
    }

    float snapRaw = FILTER_SNAP_G * fabsf(calFactor);
    if (winN == 0 || fabsf((float)raw - winMeanRaw) > snapRaw) {
        winN = 0;           // load changed — restart the average from here
        winHead = 0;
    }
    winBuf[winHead] = (float)raw;
    winHead = (winHead + 1) % FILTER_WINDOW;
    if (winN < FILTER_WINDOW) winN++;

    float sum = 0, mn = winBuf[0], mx = winBuf[0];
    for (int i = 0; i < winN; i++) {
        float v = winBuf[i];
        sum += v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    winMeanRaw   = sum / winN;
    weightStable = winN >= STABLE_MIN_SAMPLES &&
                   (mx - mn) <= STABLE_BAND_G * fabsf(calFactor);
}

static void scaleServiceLoop() {
    if (hxReady()) {
        int32_t raw = hxReadRaw();
        // 0x7FFFFF / 0x800000 are the HX711's saturation codes — drop them
        if (raw != 0x7FFFFF && raw != (int32_t)0xFF800000) pushSample(raw);
    }
    hxOk = hxSeq > 0 && millis() - hxLastMs < HX_TIMEOUT_MS;

    // First few conversions after power-up drift while the HX711's
    // internal reference settles — tare once they've passed.
    if (bootTarePending && hxSeq >= 5) {
        bootTarePending = false;
        startJob(JOB_TARE);
    }
}

// ============================================================
//  Boot button — short press = tare
// ============================================================
static void bootButtonLoop() {
    static bool     armed     = false;  // must see it released once (it may still
    static bool     down      = false;  // be held from the power-up WiFi-reset check)
    static uint32_t downSince = 0;
    bool pressed = digitalRead(BOOT_PIN) == LOW;
    if (!armed) { if (!pressed) armed = true; return; }
    if (pressed && !down) { down = true; downSince = millis(); }
    else if (!pressed && down) {
        down = false;
        uint32_t held = millis() - downSince;
        if (held >= 40 && held < 2000) {
            Serial.println("BOOT button → tare");
            startJob(JOB_TARE);
        }
    }
}

// ============================================================
//  Status LED
// ============================================================
static uint8_t  ledPin      = PIN_STATUS_LED;
static uint32_t ledLast     = 0xFFFFFFFF;
static uint32_t ledTestUntil = 0;

static void loadLedPin() {
    prefs.begin("dev", true);
    ledPin = prefs.getUChar("ledPin", PIN_STATUS_LED);
    prefs.end();
}

static void setLed(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (c == ledLast) return;          // RMT write only on change
    ledLast = c;
    neopixelWrite(ledPin, r, g, b);
}

static void setLedPin(uint8_t pin) {
    if (pin == ledPin) return;
    neopixelWrite(ledPin, 0, 0, 0);    // blank the old pin before moving
    ledPin  = pin;
    ledLast = 0xFFFFFFFF;
    prefs.begin("dev", false);
    prefs.putUChar("ledPin", pin);
    prefs.end();
}

static void ledLoop() {
    bool blinkOn = (millis() / 500) % 2;
    if (millis() < ledTestUntil)       setLed(blinkOn ? 255 : 0, blinkOn ? 255 : 0, blinkOn ? 255 : 0);
    else if (!hxOk && millis() > 3000) setLed(blinkOn ? 40 : 0, 0, 0);
    else if (apMode)                   setLed(0, 0, blinkOn ? 40 : 0);
    else                               setLed(0, 30, 0);
}

// ============================================================
//  Web helpers
// ============================================================
static String jsonEscape(const String& s) {
    String o;
    o.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

static void sendJson(const String& body) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", body);
}

static bool storeKeyValid(const String& k) {
    for (auto s : STORE_KEYS) if (k == s) return true;
    return false;
}

// ============================================================
//  Routes — registered in both AP and STA mode, so the scale is
//  fully usable straight off its own hotspot with no WiFi network.
// ============================================================
void registerRoutes() {
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", INDEX_HTML);
    });
    server.on("/admin", HTTP_GET, []() {
        server.send_P(200, "text/html", ADMIN_HTML);
    });

    // ── WiFi setup ───────────────────────────────────────────
    server.on("/wifi-setup", HTTP_GET, []() {
        server.send_P(200, "text/html", CAPTIVE_HTML);
    });
    server.on("/wifi-save", HTTP_POST, []() {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");
        ssid.trim();
        if (ssid.length()) {
            prefs.begin("wifi", false);
            prefs.putString("ssid", ssid);
            prefs.putString("pass", pass);
            prefs.end();
            server.send_P(200, "text/html", SAVED_HTML);
            delay(1500);
            ESP.restart();
        } else {
            server.sendHeader("Location", "/wifi-setup");
            server.send(302);
        }
    });
    server.on("/wifi-reset", HTTP_GET, []() {
        prefs.begin("wifi", false);
        prefs.clear();
        prefs.end();
        server.send(200, "text/plain", "WiFi credentials cleared. Restarting...");
        delay(500);
        ESP.restart();
    });
    server.on("/api/scan", HTTP_GET, []() { sendJson(scanJson); });

    // ── Weight ───────────────────────────────────────────────
    server.on("/api/weight", HTTP_GET, []() {
        char buf[200];
        snprintf(buf, sizeof buf,
            "{\"g\":%.3f,\"tare_sum\":%.3f,\"stable\":%s,\"taring\":%s,\"ok\":%s,"
            "\"seq\":%lu,\"ap\":%s}",
            weightGrams(), tareSumG,
            weightStable ? "true" : "false",
            job == JOB_TARE ? "true" : "false",
            hxOk ? "true" : "false",
            (unsigned long)hxSeq,
            apMode ? "true" : "false");
        sendJson(buf);
    });
    server.on("/api/tare", HTTP_POST, []() {
        if (!hxOk) { server.send(503, "text/plain", "HX711 not responding"); return; }
        if (job == JOB_NONE || job == JOB_TARE) startJob(JOB_TARE);
        server.send(200, "text/plain", "OK");
    });

    // ── Calibration ──────────────────────────────────────────
    server.on("/api/cal", HTTP_GET, []() {
        char buf[200];
        snprintf(buf, sizeof buf,
            "{\"g\":%.3f,\"raw\":%ld,\"factor\":%.4f,\"zero\":%ld,\"stable\":%s,"
            "\"ok\":%s,\"job\":%s,\"boot_tare\":%s,",
            weightGrams(), (long)hxRaw, calFactor, (long)calZero,
            weightStable ? "true" : "false", hxOk ? "true" : "false",
            job != JOB_NONE ? "true" : "false", bootTare ? "true" : "false");
        sendJson(String(buf) + "\"msg\":\"" + jsonEscape(calMsg) + "\"}");
    });
    server.on("/api/cal/zero", HTTP_POST, []() {
        if (!hxOk) { server.send(503, "text/plain", "HX711 not responding"); return; }
        startJob(JOB_CAL_ZERO);
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/cal/span", HTTP_POST, []() {
        float g = server.arg("grams").toFloat();
        if (!hxOk)  { server.send(503, "text/plain", "HX711 not responding"); return; }
        if (g <= 0) { server.send(400, "text/plain", "Reference mass must be > 0"); return; }
        startJob(JOB_CAL_SPAN, g);
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/cal/factor", HTTP_POST, []() {
        float f = server.arg("factor").toFloat();
        if (!isfinite(f) || fabsf(f) < 0.001f) {
            server.send(400, "text/plain", "✘ Factor must be a non-zero number");
            return;
        }
        calFactor = f;
        saveCalibration();
        server.send(200, "text/plain", "✓ Factor saved: " + String(f, 4));
    });
    server.on("/api/cal/boot-tare", HTTP_POST, []() {
        bootTare = server.arg("on") == "1";
        saveCalibration();
        server.send(200, "text/plain", "OK");
    });

    // ── Presets / parts library (LittleFS JSON, opaque to firmware) ──
    server.on("/api/store", HTTP_GET, []() {
        String key = server.arg("key");
        if (!storeKeyValid(key)) { server.send(400, "text/plain", "bad key"); return; }
        String path = "/" + key + ".json";
        if (!lfsOk || !LittleFS.exists(path)) { sendJson("[]"); return; }
        File f = LittleFS.open(path, "r");
        server.sendHeader("Cache-Control", "no-store");
        server.streamFile(f, "application/json");
        f.close();
    });
    server.on("/api/store", HTTP_POST, []() {
        String key  = server.arg("key");
        String data = server.arg("data");
        if (!storeKeyValid(key))            { server.send(400, "text/plain", "bad key"); return; }
        if (!lfsOk)                         { server.send(500, "text/plain", "LittleFS not mounted"); return; }
        if (data.length() > STORE_MAX_BYTES){ server.send(413, "text/plain", "too large"); return; }
        File f = LittleFS.open("/" + key + ".json", "w");
        if (!f) { server.send(500, "text/plain", "write failed"); return; }
        f.print(data);
        f.close();
        server.send(200, "text/plain", "OK");
    });

    // ── Status LED pin (settable, for boards that differ) ────
    server.on("/api/led", HTTP_GET, []() {
        sendJson("{\"pin\":" + String(ledPin) + "}");
    });
    server.on("/api/led", HTTP_POST, []() {
        int pin = server.arg("pin").toInt();
        // Keep off the pins already in use (BOOT, HX711, USB D-/D+)
        if (pin < 1 || pin > 48 || pin == PIN_HX_DOUT || pin == PIN_HX_SCK ||
            pin == 19 || pin == 20 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
            server.send(400, "text/plain", "✘ Invalid pin");
            return;
        }
        setLedPin((uint8_t)pin);
        ledTestUntil = millis() + 3000;
        server.send(200, "text/plain", "✓ LED on GPIO " + String(pin) + " — flashing white for 3 s");
    });

    // ── Device info ──────────────────────────────────────────
    server.on("/api/info", HTTP_GET, []() {
        char buf[420];
        snprintf(buf, sizeof buf,
            "{\"fw\":\"%s\",\"built\":\"%s %s\",\"ap\":%s,\"ssid\":\"%s\",\"ip\":\"%s\","
            "\"rssi\":%d,\"uptime\":%lu,\"heap\":%u,\"lfs_total\":%u,\"lfs_used\":%u,"
            "\"hx_ok\":%s}",
            FW_VERSION, __DATE__, __TIME__,
            apMode ? "true" : "false",
            jsonEscape(apMode ? String(AP_SSID) : WiFi.SSID()).c_str(),
            (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(),
            apMode ? 0 : WiFi.RSSI(),
            (unsigned long)(millis() / 1000),
            (unsigned)ESP.getFreeHeap(),
            lfsOk ? (unsigned)(LittleFS.totalBytes() / 1024) : 0u,
            lfsOk ? (unsigned)(LittleFS.usedBytes() / 1024) : 0u,
            hxOk ? "true" : "false");
        sendJson(buf);
    });

    // ── OTA ──────────────────────────────────────────────────
    server.on("/api/ota", HTTP_POST,
        []() {
            if (!_otaStarted || Update.hasError()) {
                server.send(500, "text/plain",
                    _otaStarted ? Update.errorString() : "No firmware received");
            } else {
                server.send(200, "text/plain", "Update complete. Restarting...");
                delay(300);
                ESP.restart();
            }
        },
        []() {
            HTTPUpload& up = server.upload();
            if (up.status == UPLOAD_FILE_START) {
                _otaStarted = true;
                Serial.printf("OTA start: %s  (%u bytes free heap)\n",
                    up.filename.c_str(), ESP.getFreeHeap());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
                    Serial.println(Update.errorString());
            } else if (up.status == UPLOAD_FILE_WRITE) {
                if (Update.write(up.buf, up.currentSize) != up.currentSize)
                    Serial.println(Update.errorString());
                // Flash writes stall the cache for both cores — a short
                // yield keeps the TCP window open during the upload.
                delay(5);
            } else if (up.status == UPLOAD_FILE_END) {
                if (Update.end(true)) Serial.printf("OTA done: %u bytes\n", up.totalSize);
                else                  Serial.println(Update.errorString());
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                Update.abort();
                Serial.println("OTA aborted");
            }
        }
    );
}

// ============================================================
//  WiFi scan (for the setup page's network list)
//  Done once at boot, before the hotspot comes up — scanning later
//  would hop the radio off the AP channel and drop connected phones.
// ============================================================
void scanNetworks() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int n = WiFi.scanNetworks();
    Serial.printf("WiFi scan: %d networks\n", n);
    String out = "[";
    int added = 0;
    for (int i = 0; i < n && added < 20; i++) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;
        bool dup = false;
        for (int j = 0; j < i; j++) if (WiFi.SSID(j) == ssid) { dup = true; break; }
        if (dup) continue;
        if (added++) out += ',';
        out += "{\"ssid\":\"" + jsonEscape(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
               ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    out += "]";
    scanJson = out;
    WiFi.scanDelete();
}

// ============================================================
//  AP Mode (captive portal)
// ============================================================
void startAP() {
    apMode = true;
    scanNetworks();
    // AP+STA so the saved network can be retried in the background
    // (see staRetryLoop) without tearing the hotspot down.
    WiFi.mode(WIFI_AP_STA);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    // Core 3.x's DHCP server doesn't advertise a DNS server unless told to —
    // without it clients never query us and captive-portal detection never fires.
    WiFi.softAPConfig(apIP, apIP, subnet, IPAddress((uint32_t)0), apIP);
#else
    WiFi.softAPConfig(apIP, apIP, subnet);
#endif
    WiFi.softAP(AP_SSID);
    delay(300);

    Serial.printf("AP started — SSID: \"%s\"  IP: %s\n", AP_SSID, apIP.toString().c_str());
    Serial.println("Connect to \"Scale-Setup\" and browse to http://192.168.4.1");

    dnsServer.start(DNS_PORT, "*", apIP);

    registerRoutes();

    // Captive-portal probes (and anything unknown) land on WiFi setup —
    // the page has a link to skip straight to the scale.
    auto redir = []() {
        server.sendHeader("Location", "http://192.168.4.1/wifi-setup", true);
        server.send(302, "text/plain", "");
    };
    server.on("/generate_204",        redir);
    server.on("/gen_204",             redir);
    server.on("/hotspot-detect.html", redir);
    server.on("/library/test/success.html", redir);
    server.on("/ncsi.txt",            redir);
    server.on("/connecttest.txt",     redir);
    server.on("/redirect",            redir);
    server.onNotFound(redir);

    server.begin();
    Serial.println("Captive portal started.");
}

// While stuck on the hotspot with saved credentials (e.g. the router was
// still booting after a power cut), periodically retry the saved network —
// but only while nobody is connected to the hotspot, because the STA
// connect attempt hops the radio and would drop them. On success, reboot
// cleanly into STA mode.
static void staRetryLoop() {
    static bool     trying   = false;
    static uint32_t tryStart = 0;
    static uint32_t nextTry  = STA_RETRY_MS;
    if (!apMode || !savedSsid.length()) return;

    if (!trying) {
        if (millis() >= nextTry && WiFi.softAPgetStationNum() == 0) {
            Serial.printf("[AP] retrying saved network \"%s\"\n", savedSsid.c_str());
            WiFi.begin(savedSsid.c_str(), savedPass.c_str());
            trying = true;
            tryStart = millis();
        }
    } else if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[AP] saved network is back — restarting into WiFi mode");
        delay(200);
        ESP.restart();
    } else if (millis() - tryStart > STA_RETRY_WINDOW_MS || WiFi.softAPgetStationNum() > 0) {
        WiFi.disconnect();
        trying  = false;
        nextTry = millis() + STA_RETRY_MS;
    }
}

// ============================================================
//  STA Mode
// ============================================================
bool connectWiFi() {
    prefs.begin("wifi", true);
    savedSsid = prefs.getString("ssid", "");
    savedPass = prefs.getString("pass", "");
    prefs.end();

    if (!savedSsid.length()) {
        Serial.println("No saved WiFi credentials — captive portal mode");
        return false;
    }

    Serial.printf("Saved SSID found: \"%s\" — connecting", savedSsid.c_str());
    WiFi.mode(WIFI_STA);
    // Modem sleep misses inbound multicast (mDNS queries) — keep it awake.
    WiFi.setSleep(false);
    WiFi.setHostname(MDNS_NAME);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(250);
        scaleServiceLoop();   // keep the HX711 warm while we wait
        if ((millis() - t) % 1000 < 250) Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected!  IP: %s  RSSI: %d dBm\n",
            WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }
    Serial.printf("Connection failed (status %d) — falling back to captive portal\n",
        WiFi.status());
    return false;
}

void startSTA() {
    registerRoutes();
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    server.begin();

    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS  : http://%s.local\n", MDNS_NAME);
    } else {
        Serial.println("mDNS start failed — use IP address directly");
    }
    Serial.printf("Direct: http://%s\n", WiFi.localIP().toString().c_str());
}

// ============================================================
//  Setup / loop
// ============================================================
static bool bootHeldForReset() {
    pinMode(BOOT_PIN, INPUT_PULLUP);
    if (digitalRead(BOOT_PIN) != LOW) return false;
    Serial.println("BOOT held — keep holding 3 s to clear WiFi credentials");
    uint32_t t = millis();
    while (digitalRead(BOOT_PIN) == LOW) {
        if (millis() - t >= BOOT_HOLD_MS) return true;
        setLed((millis() / 100) % 2 ? 40 : 0, 20, 0);
        delay(10);
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n=== Resin Scale v%s ===\n", FW_VERSION);

    loadLedPin();
    if (bootHeldForReset()) {
        prefs.begin("wifi", false);
        prefs.clear();
        prefs.end();
        Serial.println("WiFi credentials cleared.");
    }

    lfsOk = LittleFS.begin(true, "/littlefs", 10, LFS_LABEL);
    Serial.printf("LittleFS: %s\n", lfsOk ? "mounted" : "FAILED");

    setupHX711();
    loadCalibration();
    bootTarePending = bootTare;

    if (connectWiFi()) startSTA();
    else               startAP();
}

void loop() {
    scaleServiceLoop();
    if (apMode) dnsServer.processNextRequest();
    server.handleClient();
    bootButtonLoop();
    staRetryLoop();
    ledLoop();
    delay(1);
}
