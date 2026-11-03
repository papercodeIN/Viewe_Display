// ============================================================
// VIEWE UEDX80480050E-WB 5" ESP32-S3 - WiFi NAS (port of the
// Elecrow CrowPanel 2.8" ESP32_NAS project)
//
// Display: 800x480 RGB parallel LCD (ST7262) via Arduino_GFX.
//   Arduino_GFX blits through a PSRAM framebuffer; the panel
//   scans it out continuously, exactly like the Stream Deck port
//   in this repo. Status screens are redrawn only on state change.
// Storage: onboard microSD slot on the FSPI bus (CS 10, MOSI 11,
//   SCK 12, MISO 13 - from the vendor schematic TF1 section).
// Server: ESPAsyncWebServer - browse/upload/download/delete files,
//   WiFi setup portal in AP fallback, OTA firmware updates.
//
// Pin map sources: VIEWE schematic (Schematic/5.0.png), the
// vendor ESP32_Display_Panel board definition, and the
// hardware-tested Bluetooth_Stream_Deck port in this repo.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Arduino_GFX_Library.h>
#include <math.h>
#include "config.h"

// ============================================================
// Display (proven config from the Bluetooth_Stream_Deck port)
// ============================================================
// ST7262 RGB panel, 16-bit RGB565 bus, negative HSYNC/VSYNC
// polarity, DE enables on high. Timings from the vendor board
// definition: HPW 1, HBP 42, HFP 20, VPW 10, VBP 12, VFP 4.
//
// PCLK runs at 15 MHz on purpose. The vendor's ESP-IDF stack
// drives this panel at 20 MHz with an esp_lcd DMA bounce buffer
// (WIDTH x 10); Arduino_GFX 1.4.7 has no bounce-buffer support,
// so the scanout DMA reads the PSRAM framebuffer directly and
// 20 MHz outruns sustainable PSRAM bandwidth once WiFi + SD
// traffic contend for the bus. 15 MHz is the proven-stable rate
// for this exact stack (same library, same ST7262 panel, same
// OPI-PSRAM ESP32-S3) in the Stream Deck port.
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    40, 41, 39, 42,      // DE, VSYNC, HSYNC, PCLK
    45, 48, 47, 21, 14,  // R0-R4 (DATA11-15)
    5, 6, 7, 15, 16, 4,  // G0-G5 (DATA5-10)
    8, 3, 46, 9, 1,      // B0-B4 (DATA0-4)
    0, 20, 1, 42,        // hsync polarity, front porch, pulse width, back porch
    0, 4, 10, 12,        // vsync polarity, front porch, pulse width, back porch
    1, 15000000);        // PCLK active on falling edge, 15 MHz (see note above)

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(SCREEN_W, SCREEN_H, rgbpanel, 0, true);

// ============================================================
// Palette (RGB565, same identity as the CrowPanel NAS port)
// ============================================================
static const uint16_t C_BG     = 0x0841;  // near-black navy
static const uint16_t C_PANEL  = 0x10A2;  // deep panel blue
static const uint16_t C_CYAN   = 0x07FF;
static const uint16_t C_MUTED  = 0x9CF3;
static const uint16_t C_GREEN  = 0x07E0;
static const uint16_t C_ORANGE = 0xFDA0;
static const uint16_t C_RED    = 0xF986;
static const uint16_t C_WHITE  = 0xFFFF;

// Default font cell is 6x8 px, scaled by text size.
static inline int textW(int chars, int size) { return chars * 6 * size; }

// ============================================================
// Globals
// ============================================================
Preferences prefs;
AsyncWebServer server(HTTP_PORT);
DNSServer dnsServer;
SemaphoreHandle_t sdMutex = nullptr;

bool sdMounted = false;
bool wifiConnected = false;
bool wifiConnecting = false;
unsigned long wifiConnectStart = 0;
bool apMode = false;
bool otaReady = false;
bool mdnsActive = false;
String currentSSID = "";
String currentIP = "";
float sdTotalGB = 0.0f;
float sdFreeGB = 0.0f;
uint32_t sdActiveFreq = SD_SPI_FREQ_HZ;

unsigned long lastSDPoll = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastSDAnim = 0;
bool uiDirty = true;
int wifiFailCount = 0;
int sdFailStreak = 0;         // failed remounts since last success (drives freq downgrade)

// SD fault diagnostics
unsigned long sdFaultSince = 0;   // millis() when fault first detected (0 = no fault)
int sdRetryCount = 0;             // mount retry attempts since fault

// Upload state. Handlers run on the async_tcp task, pollers on loop();
// every SD touch takes sdMutex. One upload at a time - a second upload
// while one is in flight is rejected (two writers share one file handle).
File uploadFile;
volatile bool uploadFileOpen = false;
volatile unsigned long lastUploadActivity = 0;

// Per-request upload verdict. Lives on the AsyncWebServerRequest via
// _tempObject so concurrent requests report their own result instead of
// clobbering a shared flag. IMPORTANT: _tempObject is released with
// free() by the request destructor - allocate with malloc, never new
// (new/delete here corrupts the heap; see the CrowPanel photo-frame port).
struct UploadState {
    bool success;
    bool rejected;
};

// ============================================================
// Forward declarations
// ============================================================
void initDisplay();
void initSD();
void initWiFi();
void startAPMode();
void startSTAMode(const String& ssid, const String& pass);
void setupWebServer();
void updateUI();
void drawStatusScreen();
void drawSDErrorScreenFull();
void tickSDErrorScreen();
void drawAPScreen();
void pollSD();
void handleWiFiReconnect();
void noteSDFault();
void uploadAbortCleanup(const char* reason);
String formatBytes(uint64_t bytes);
String jsonEscape(const String& input);
String sanitizeFilename(const String& filename);
bool isPathSafe(const String& path);

// ============================================================
// Web UI (single-page file manager, served from PROGMEM)
// ============================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP32 Drive</title>
<style>
*{box-sizing:border-box}body{margin:0;font-family:'Segoe UI',Roboto,Arial,sans-serif;background:#101418;color:#e8eaed}
header{display:flex;align-items:center;gap:10px;padding:10px 18px;background:#1a1f26;box-shadow:0 1px 0 #2a313a;position:sticky;top:0;z-index:5}
.logo{display:flex;align-items:center;gap:8px;font-size:20px;font-weight:600;color:#8ab4f8;white-space:nowrap}
.logo b{background:#8ab4f8;color:#101418;padding:2px 7px;border-radius:6px}
#search{flex:1;min-width:120px;padding:10px 16px;background:#101418;border:1px solid #2a313a;border-radius:10px;color:#e8eaed;font-size:14px;outline:none}
#search:focus{border-color:#8ab4f8}
#mkdirBtn{background:#222a33;color:#8ab4f8;border:1px solid #2a313a;padding:9px 14px;border-radius:10px;font-size:14px;font-weight:600;cursor:pointer;white-space:nowrap}
#mkdirBtn:hover{border-color:#8ab4f8}
#uploadBtn{background:#8ab4f8;color:#101418;border:none;padding:9px 18px;border-radius:10px;font-size:14px;font-weight:600;cursor:pointer;white-space:nowrap}
#uploadBtn:hover{filter:brightness(1.1)}
#layout{display:flex;min-height:calc(100vh - 54px)}
nav{width:250px;background:#161b21;padding:18px 12px}
.navitem{display:block;padding:10px 14px;border-radius:8px;cursor:pointer;color:#e8eaed;font-size:14px;text-decoration:none;margin-bottom:4px}
.navitem:hover{background:#1f262e}.navitem.active{background:#2a3442;color:#8ab4f8}
.card{background:#1a1f26;border:1px solid #2a313a;border-radius:12px;padding:14px;margin-top:16px}
.card h3{margin:0 0 10px;font-size:12px;letter-spacing:.8px;text-transform:uppercase;color:#9aa0a6}
.card p{margin:4px 0;font-size:13px;color:#c5cbd3;word-break:break-all}
.bar{height:7px;background:#2a313a;border-radius:4px;overflow:hidden;margin:8px 0}
.bar i{display:block;height:100%;background:#8ab4f8;border-radius:4px}
main{flex:1;padding:14px 22px;min-width:0}
#crumbs{font-size:15px;color:#9aa0a6;margin:6px 0 14px}
#crumbs a{color:#8ab4f8;text-decoration:none;cursor:pointer}
#crumbs a:hover{text-decoration:underline}
#selbar{display:flex;align-items:center;gap:10px;margin:0 0 12px;padding:9px 14px;background:#243040;border:1px solid #2a313a;border-radius:10px;font-size:13px}
#selbar b{color:#8ab4f8}
#selbar button{background:#8ab4f8;color:#101418;border:none;padding:6px 12px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer}
#selbar button.del{background:#f28b82}
#selbar button.clr{background:none;color:#9aa0a6;font-weight:400;padding:6px 6px}
#selbar button:hover{filter:brightness(1.1)}
table{width:100%;border-collapse:collapse}
th{text-align:left;font-size:12px;color:#9aa0a6;padding:8px;border-bottom:1px solid #2a313a;white-space:nowrap}
tr{border-bottom:1px solid #1f262e}
tr.folder{cursor:pointer}
tbody tr:hover{background:#1a2027}
tbody tr.sel{background:#243040}
td{padding:9px 8px;font-size:14px;vertical-align:middle}
.tname{display:flex;align-items:center;gap:12px;min-width:0}
.nm{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.fname{color:#e8eaed;cursor:pointer}
.icon{width:22px;text-align:center;font-size:17px}
.chk{width:30px}
input[type=checkbox]{accent-color:#8ab4f8;width:15px;height:15px;cursor:pointer;display:block}
.size{text-align:right;color:#9aa0a6;width:110px;white-space:nowrap;font-variant-numeric:tabular-nums}
th.act,td.act{text-align:right;width:170px;white-space:nowrap}
.actbtn{background:#222a33;border:1px solid #2a313a;color:#c5cbd3;border-radius:8px;font-size:12px;padding:5px 10px;margin-left:6px;cursor:pointer}
.actbtn:hover{border-color:#8ab4f8;color:#8ab4f8}
.actbtn.danger:hover{border-color:#f28b82;color:#f28b82}
#empty{padding:60px 20px;text-align:center;color:#5f6368}
.hidden{display:none!important}
#upOv{position:fixed;inset:0;background:rgba(6,10,14,.7);display:flex;align-items:center;justify-content:center;z-index:20}
#upPanel{background:#1a1f26;border:1px solid #2a313a;border-radius:14px;padding:26px 32px;width:min(420px,90vw);text-align:center}
#upBar{height:8px;background:#2a313a;border-radius:5px;margin:16px 0;overflow:hidden}
#upBar i{display:block;height:100%;width:0;background:#8ab4f8;border-radius:5px;transition:width .15s}
#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#2a3442;color:#e8eaed;padding:10px 20px;border-radius:10px;font-size:14px;z-index:30}
#toast.err{border-left:3px solid #f28b82}
input[type=text],input[type=password]{padding:9px 10px;background:#101418;border:1px solid #2a313a;border-radius:8px;color:#e8eaed;width:100%;font-size:13px;margin-top:6px}
.b{background:#8ab4f8;color:#101418;border:none;padding:8px 14px;border-radius:8px;cursor:pointer;font-size:13px;margin-top:10px}
@media(max-width:820px){nav{display:none}th.act,td.act{width:auto}}
</style></head>
<body>
<header><div class='logo'><b>NAS</b> ESP32 Drive</div>
<input id='search' placeholder='Search files' oninput='render()'>
<button id='mkdirBtn' onclick='newFolder()'>+ New folder</button>
<button id='uploadBtn' onclick="document.getElementById('fileInput').click()">&#8593; Upload</button></header>
<div id='layout'>
<nav>
<span class='navitem active'>My Files</span>
<div class='card'><h3>Storage</h3><div class='bar'><i id='stoBar'></i></div><p id='stoTxt'></p></div>
<div class='card'><h3>Transfers</h3><p id='upSp'>&#8593; idle</p><p id='dnSp'>&#8595; idle</p></div>
<div class='card'><h3>Device</h3><p id='netTxt'></p></div>
<div class='card hidden' id='wifiCard'><h3>WiFi Setup</h3>
<input type='text' id='wS' placeholder='SSID'>
<input type='password' id='wP' placeholder='Password'>
<button class='b' onclick='saveWifi()'>Save &amp; Connect</button></div>
</nav>
<main>
<div id='crumbs'></div>
<div id='selbar' class='hidden'>
<b id='selCount'></b>
<button onclick='bulkDownload()'>&#8595; Download</button>
<button class='del' onclick='bulkDelete()'>Delete</button>
<button class='clr' onclick='clearSel()'>Clear</button>
</div>
<table><thead><tr>
<th class='chk'><input type='checkbox' id='selAll' onchange='toggleAll(this.checked)'></th>
<th>Name</th>
<th class='size'>Size</th>
<th class='act'>Actions</th>
</tr></thead>
<tbody id='rows'></tbody></table>
<div id='empty' class='hidden'>This folder is empty</div>
</main></div>
<div id='upOv' class='hidden'><div id='upPanel'><b id='upTxt'>Uploading...</b><div id='upBar'><i></i></div></div></div>
<div id='toast' class='hidden'></div>
<input type='file' id='fileInput' hidden>
<script>
let cur='/',items=[];
const enc=p=>encodeURIComponent(p);
const $=id=>document.getElementById(id);
function toast(msg,err){const t=$('toast');t.textContent=msg;t.className=err?'err':'';t.classList.remove('hidden');setTimeout(()=>t.classList.add('hidden'),3000)}
async function jget(u){const r=await fetch(u);if(!r.ok)throw 0;return r.json()}
function join(p,n){return p==='/'?'/'+n:p+'/'+n}
function parentOf(p){const s=p.slice(0,p.lastIndexOf('/'));return s===''?'/':s}
function fmtSize(b){if(b<1024)return b+' B';if(b<1048576)return(b/1024).toFixed(1)+' KB';return(b/1048576).toFixed(1)+' MB'}
function fmtSp(bps){if(!isFinite(bps)||bps<=0)return'0 KB/s';if(bps<1048576)return(bps/1024).toFixed(1)+' KB/s';return(bps/1048576).toFixed(1)+' MB/s'}
function iconFor(n){const e=(n.split('.').pop()||'').toLowerCase();
if(['png','jpg','jpeg','gif','bmp','webp'].includes(e))return'\u{1F5BC}';
if(['mp4','avi','mkv','mov'].includes(e))return'\u{1F39E}';
if(['mp3','wav','flac'].includes(e))return'\u{1F3B5}';
if(['zip','rar','7z','gz','tar'].includes(e))return'\u{1F5DC}';
if(['txt','md','log','cpp','h','ino','json','csv','xml','ini','py'].includes(e))return'\u{1F4D3}';
if(['pdf'].includes(e))return'\u{1F4C4}';
return'\u{1F4C1}'}
function paint(s){
const used=s.totalGB-s.freeGB,pct=s.totalGB>0?Math.min(100,used*100/s.totalGB):0;
const b=$('stoBar');b.style.width=pct+'%';b.style.background=pct>90?'#f28b82':pct>75?'#fdd663':'#8ab4f8';
$('stoTxt').textContent=s.sdMounted?s.totalGB.toFixed(2)+' GB total \u00B7 '+s.freeGB.toFixed(2)+' GB free':'Not mounted';
$('netTxt').textContent=s.apMode?'AP mode \u00B7 '+s.ip:(s.wifiConnected?'WiFi \u00B7 '+s.ssid:'Connecting...');
$('wifiCard').classList.toggle('hidden',!s.apMode)}
function crumbs(p){const c=$('crumbs');c.textContent='';
if(p!=='/'){const b=document.createElement('a');b.textContent='\u2039 Back';b.onclick=()=>nav(parentOf(p));c.appendChild(b)}
const h=document.createElement('a');h.textContent=' My Files';h.onclick=()=>nav('/');c.appendChild(h);
let acc='';p.split('/').filter(Boolean).forEach(seg=>{acc+='/'+seg;
const a=document.createElement('a');a.textContent=' \u203A '+seg;a.onclick=()=>nav(acc);c.appendChild(a)})}
let sel=new Set();
function clearSel(){sel.clear();updateSel();$('selAll').checked=false;render()}
function updateSel(){const n=sel.size;$('selbar').classList.toggle('hidden',n===0);
$('selCount').textContent=n+' item'+(n>1?'s':'')+' selected'}
function toggleAll(on){items.forEach(i=>{if(!i.d){const p=join(cur,i.n);if(on&&!sel.has(p))sel.add(p);if(!on)sel.delete(p)}});
$('selAll').checked=on;render()}
function syncSelAll(){const files=items.filter(i=>!i.d);
$('selAll').checked=files.length>0&&files.every(i=>sel.has(join(cur,i.n)))}
async function bulkDelete(){if(!sel.size)return;
if(!confirm('Delete '+sel.size+' item(s) permanently?'))return;
const list=[...sel];let ok=0;
for(const p of list){try{const r=await fetch('/delete?file='+enc(p));if(r.ok){ok++;sel.delete(p)}}catch(e){}}
sel.clear();updateSel();
toast(ok===list.length?('Deleted '+ok+' item(s)'):(ok+'/'+list.length+' deleted \u00B7 '+(list.length-ok)+' failed'),ok!==list.length);
nav(cur)}
async function bulkDownload(){
const files=[...sel].filter(p=>{const n=p==='/'?'':p.split('/').pop();
const it=items.find(i=>join(cur,i.n)===p);return it&&!it.d});
if(!files.length){toast('No files selected (folders cannot be downloaded)',true);return}
for(let k=0;k<files.length;k++){
const it=items.find(i=>join(cur,i.n)===files[k]);
await downloadFile(files[k],it.n);sel.delete(files[k]);
if(k<files.length-1)await new Promise(r=>setTimeout(r,500))}
toast('Downloaded '+files.length+' file(s)');updateSel();render()}
function render(){const tb=$('rows');tb.textContent='';
const q=$('search').value.toLowerCase();
const list=items.filter(i=>i.n.toLowerCase().includes(q));
list.sort((a,b)=>(b.d?1:0)-(a.d?1:0)||a.n.localeCompare(b.n));
list.forEach(i=>{const full=join(cur,i.n);const selected=sel.has(full);
const tr=document.createElement('tr');
if(i.d){tr.className='folder';if(selected)tr.classList.add('sel');tr.onclick=()=>nav(full)}
else{if(selected)tr.classList.add('sel')}
const c0=document.createElement('td');c0.className='chk';
const cb=document.createElement('input');cb.type='checkbox';cb.checked=selected;
if(i.d)cb.disabled=true;
cb.onclick=e=>e.stopPropagation();
cb.onchange=()=>{if(cb.checked)sel.add(full);else sel.delete(full);tr.classList.toggle('sel',cb.checked);updateSel();syncSelAll()};
c0.append(cb);
const t1=document.createElement('td');const tn=document.createElement('div');tn.className='tname';
const ic=document.createElement('span');ic.className='icon';ic.textContent=i.d?'\u{1F4C1}':iconFor(i.n);
const nm=document.createElement('span');nm.className='nm'+(i.d?'':' fname');
nm.textContent=i.n;if(!i.d)nm.onclick=()=>downloadFile(full,i.n);
tn.append(ic,nm);t1.append(tn);
const t2=document.createElement('td');t2.className='size';t2.textContent=i.d?'':i.s;
const t3=document.createElement('td');t3.className='act';
if(!i.d){const db=document.createElement('button');db.className='actbtn';db.textContent='Download';
db.onclick=e=>{e.stopPropagation();downloadFile(full,i.n)};
const ab=document.createElement('button');ab.className='actbtn danger';ab.textContent='Delete';
ab.onclick=async e=>{e.stopPropagation();if(!confirm('Delete '+i.n+'?'))return;
try{const r=await fetch('/delete?file='+enc(full));if(r.ok){sel.delete(full);toast('Deleted');nav(cur)}else toast(await r.text(),true)}catch(e){toast('Delete failed',true)}};
t3.append(db,ab)}
tr.append(c0,t1,t2,t3);tb.append(tr)});
$('empty').classList.toggle('hidden',list.length>0);syncSelAll()}
async function nav(p){cur=p;sel.clear();updateSel();$('selAll').checked=false;
try{const d=await jget('/api/list?dir='+enc(p));
items=d.items||[];crumbs(d.path)}catch(e){toast('Not mounted',true);items=[];crumbs(p)}render()}
function newFolder(){const n=prompt('Folder name:');if(!n)return;
fetch('/api/mkdir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'dir='+enc(cur)+'&name='+enc(n)}).then(async r=>{
if(r.ok){toast('Folder created');nav(cur)}else{toast(await r.text()||'Failed',true)}}).catch(()=>toast('Failed',true))}
async function downloadFile(full,name){
const sp=$('dnSp');
if(!window.ReadableStream){location.href='/download?file='+enc(full);return}
const t0=performance.now();let recv=0,shown=t0,shownRecv=0;
try{
const res=await fetch('/download?file='+enc(full));
if(!res.ok){toast('Download failed: '+name,true);return}
const reader=res.body.getReader();const parts=[];
while(true){const r=await reader.read();if(r.done)break;parts.push(r.value);recv+=r.value.length;
const now=performance.now();
if(now-shown>=250){sp.textContent='\u2193 '+fmtSp((recv-shownRecv)*1000/(now-shown));shown=now;shownRecv=recv}}
const avg=recv*1000/(performance.now()-t0);
sp.textContent='\u2193 '+fmtSp(avg)+' \u00B7 done ('+fmtSize(recv)+')';
const blob=new Blob(parts);const url=URL.createObjectURL(blob);
const a=document.createElement('a');a.href=url;a.download=name;document.body.appendChild(a);a.click();a.remove();
setTimeout(()=>URL.revokeObjectURL(url),4000);
}catch(e){toast('Download failed: '+name,true)}}
async function init(){try{const s=await jget('/api/status');paint(s);
const q=new URLSearchParams(location.search).get('dir');
await nav(q||'/')}catch(e){toast('Device not reachable',true)}
setInterval(async()=>{try{paint(await jget('/api/status'))}catch(e){}},15000)}
function saveWifi(){const s=$('wS').value.trim();if(!s)return toast('SSID required',true);
fetch('/setwifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'ssid='+enc(s)+'&pass='+enc($('wP').value)}).then(()=>toast('Saved! Rebooting...'))}
$('fileInput').onchange=e=>{const f=e.target.files[0];if(!f)return;
const fd=new FormData();fd.append('file',f,f.name);
$('upOv').classList.remove('hidden');$('upTxt').textContent='Uploading '+f.name;
const t0=performance.now();
const x=new XMLHttpRequest();x.open('POST','/upload');
x.upload.onprogress=ev=>{if(ev.lengthComputable){
$('upBar').firstElementChild.style.width=Math.round(ev.loaded*100/ev.total)+'%';
const dt=(performance.now()-t0)/1000;if(dt>0.2){const spd=ev.loaded/dt;
$('upTxt').textContent='Uploading '+f.name+' \u00B7 '+fmtSp(spd);
$('upSp').textContent='\u2191 '+fmtSp(spd)}}};
x.onload=()=>{$('upOv').classList.add('hidden');
if(x.status===200){$('upSp').textContent='\u2191 '+fmtSp(f.size*1000/(performance.now()-t0))+' \u00B7 done';
toast('Upload complete');e.target.value='';nav(cur)}else toast('Upload failed',true)};
x.onerror=()=>{$('upOv').classList.add('hidden');toast('Upload failed',true)};
x.send(fd)};
init();
</script></body></html>)rawliteral";

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== VIEWE 5\" NAS - Starting ===");

    sdMutex = xSemaphoreCreateMutex();
    if (!sdMutex) {
        Serial.println("[FATAL] Failed to create SD mutex");
        while (true) delay(1000);
    }

    // Backlight first so the boot splash is visible.
    ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ_HZ, BL_PWM_RES_BITS);
    ledcAttachPin(TFT_BL, BL_PWM_CHANNEL);
    ledcWrite(BL_PWM_CHANNEL, 255);

    initDisplay();
    initSD();
    initWiFi();
    setupWebServer();

    Serial.println("=== Setup complete ===");
}

// ============================================================
// Loop
// ============================================================
void loop() {
    unsigned long now = millis();

    if (now - lastSDPoll >= SD_POLL_INTERVAL) {
        lastSDPoll = now;
        pollSD();
    }

    // SD error screen: localized animation + retry stats every 500ms
    if (!sdMounted && now - lastSDAnim >= SD_RETRY_ANIM_INTERVAL) {
        lastSDAnim = now;
        tickSDErrorScreen();
    }

    // Abandoned-upload watchdog: if the client vanished without a final
    // chunk (browser closed, WiFi dropped), the async handler never runs
    // again. Close the handle and remove the partial file after a stall.
    if (uploadFileOpen && now - lastUploadActivity >= UPLOAD_STALL_TIMEOUT) {
        uploadAbortCleanup("stalled");
    }

    // Non-blocking WiFi connect/reconnect check
    if (!apMode && !wifiConnected) {
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            wifiConnecting = false;
            wifiFailCount = 0;
            currentIP = WiFi.localIP().toString();
            Serial.printf("[WIFI] Connected! IP: %s\n", currentIP.c_str());
            if (!mdnsActive) {
                mdnsActive = MDNS.begin(WIFI_HOSTNAME);
                if (mdnsActive) {
                    Serial.printf("[MDNS] http://%s.local\n", WIFI_HOSTNAME);
                }
            }
            if (!otaReady) {
                ArduinoOTA.setHostname(WIFI_HOSTNAME);
                ArduinoOTA.begin();
                otaReady = true;
                Serial.println("[OTA] Ready - upload via PlatformIO or Arduino IDE OTA");
            }
            uiDirty = true;
        } else if (wifiConnecting && millis() - wifiConnectStart >= WIFI_CONNECT_TIMEOUT) {
            wifiConnecting = false;
            Serial.println("[WIFI] Connection timeout, falling back to AP");
            startAPMode();
        } else if (!wifiConnecting && now - lastWifiRetry >= WIFI_RETRY_INTERVAL) {
            lastWifiRetry = now;
            handleWiFiReconnect();
        }
    }

    if (!apMode && wifiConnected && WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        otaReady = false;
        if (mdnsActive) {
            MDNS.end();
            mdnsActive = false;
        }
        currentIP = "";
        Serial.println("[WIFI] Connection lost, will retry...");
        uiDirty = true;
    }

    if (otaReady) {
        ArduinoOTA.handle();
    }

    if (apMode) {
        dnsServer.processNextRequest();
    }

    // Draw ONLY on state change - no periodic repaint, no flicker
    if (uiDirty) {
        updateUI();
        uiDirty = false;
    }

    delay(10);
}

// ============================================================
// Display Init
// ============================================================
void initDisplay() {
    Serial.println("[DISPLAY] Initializing Arduino_GFX RGB panel...");

    // The RGB framebuffer (800x480x2 = 750 KB) lives in the 8 MB OPI
    // PSRAM. If it is missing, the board variant is wrong - say so and stop.
    if (!psramFound()) {
        Serial.println("[FATAL] PSRAM not detected - wrong board variant "
                       "(needs viewe-uedx80480050e-n16r8: 8 MB OPI PSRAM)");
        while (true) delay(1000);
    }

    if (!gfx->begin()) {
        Serial.println("[FATAL] gfx->begin() FAILED - panel init error");
        while (true) delay(1000);
    }
    gfx->fillScreen(C_BG);
    gfx->setTextColor(C_CYAN, C_BG);
    gfx->setTextSize(2);
    gfx->setCursor(16, 16);
    gfx->print("VIEWE NAS starting...");
    Serial.printf("[DISPLAY] RGB panel ready (%dx%d, PCLK 15 MHz, PSRAM framebuffer)\n",
                  SCREEN_W, SCREEN_H);
}

// ============================================================
// SD Card
// ============================================================
void noteSDFault() {
    if (sdFaultSince == 0) {
        sdFaultSince = millis();
        sdRetryCount = 0;
    }
    uiDirty = true;
}

static bool mountSDAt(uint32_t freq) {
    return SD.begin(SD_CS, SPI, freq);
}

static void readSDStats() {
    uint64_t totalBytes = SD.totalBytes();
    uint64_t usedBytes = SD.usedBytes();
    sdTotalGB = (float)totalBytes / (1024.0f * 1024.0f * 1024.0f);
    sdFreeGB = (usedBytes <= totalBytes)
        ? (float)(totalBytes - usedBytes) / (1024.0f * 1024.0f * 1024.0f)
        : 0.0f;
}

void initSD() {
    Serial.printf("[SD] Initializing SD card on FSPI (CS %d, MOSI %d, MISO %d, SCK %d) @ %u kHz\n",
                  SD_CS, SD_MOSI, SD_MISO, SD_SCK, (unsigned)(sdActiveFreq / 1000));
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    if (mountSDAt(sdActiveFreq)) {
        sdMounted = true;
        sdFaultSince = 0;
        sdRetryCount = 0;
        sdFailStreak = 0;
        readSDStats();
        Serial.printf("[SD] Mounted OK - Total: %.2f GB, Free: %.2f GB\n", sdTotalGB, sdFreeGB);
    } else {
        sdMounted = false;
        sdFailStreak = 1;
        noteSDFault();
        Serial.println("[SD] Mount FAILED - will retry in loop");
    }
    uiDirty = true;
}

void pollSD() {
    bool wasMounted = sdMounted;

    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return;  // SD busy with a transfer; try again next poll
    }

    if (!sdMounted) {
        SD.end();
        SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
        sdRetryCount++;

        // Auto-downgrade the clock after repeated failures - flaky cards
        // often mount at 4 MHz when they reject 20 MHz.
        if (sdFailStreak >= SD_FREQ_DOWNGRADE_AFTER && sdActiveFreq != SD_SPI_FALLBACK_FREQ_HZ) {
            sdActiveFreq = SD_SPI_FALLBACK_FREQ_HZ;
            Serial.printf("[SD] Downgrading SPI clock to %u kHz\n", (unsigned)(sdActiveFreq / 1000));
        }

        if (mountSDAt(sdActiveFreq)) {
            sdMounted = true;
            sdFaultSince = 0;
            sdRetryCount = 0;
            sdFailStreak = 0;
            readSDStats();
            Serial.printf("[SD] Re-mounted OK @ %u kHz - Total: %.2f GB, Free: %.2f GB\n",
                          (unsigned)(sdActiveFreq / 1000), sdTotalGB, sdFreeGB);
            uiDirty = true;
        } else {
            sdFailStreak++;
        }
    } else {
        uint64_t totalBytes = SD.totalBytes();
        if (totalBytes == 0) {
            // An in-flight upload dies with the card; clean up the stale
            // handle and remove the partial file while the FS is mounted.
            if (uploadFileOpen) {
                String partial = uploadFile.path();
                uploadFile.close();
                uploadFileOpen = false;
                SD.remove(partial);
                Serial.println("[UPLOAD] Card removed mid-upload - partial file removed");
            }
            sdMounted = false;
            SD.end();
            sdTotalGB = 0.0f;
            sdFreeGB = 0.0f;
            noteSDFault();
            Serial.println("[SD] Card removed or error");
        } else {
            uint64_t usedBytes = SD.usedBytes();
            float newTotal = (float)totalBytes / (1024.0f * 1024.0f * 1024.0f);
            float newFree = (usedBytes <= totalBytes)
                ? (float)(totalBytes - usedBytes) / (1024.0f * 1024.0f * 1024.0f)
                : 0.0f;
            if (fabsf(newTotal - sdTotalGB) > 0.01f || fabsf(newFree - sdFreeGB) > 0.01f) {
                sdTotalGB = newTotal;
                sdFreeGB = newFree;
                uiDirty = true;
            }
        }
    }

    if (wasMounted != sdMounted) {
        uiDirty = true;
    }

    xSemaphoreGive(sdMutex);
}

// Close an in-flight upload and remove the partial file.
// Called from the stall watchdog (loop task) and the partial-write
// error path (async_tcp task). Must NOT be called while the caller
// already holds sdMutex (single non-recursive mutex).
void uploadAbortCleanup(const char* reason) {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return;
    }
    if (uploadFileOpen) {
        String partial = uploadFile.path();  // capture before close
        uploadFile.close();
        uploadFileOpen = false;
        if (sdMounted) {
            SD.remove(partial);
        }
        Serial.printf("[UPLOAD] Aborted (%s) - partial file removed\n", reason);
    }
    xSemaphoreGive(sdMutex);
    uiDirty = true;
}

// ============================================================
// WiFi
// ============================================================
void initWiFi() {
    prefs.begin(PREF_NAMESPACE, true);
    String savedSSID = prefs.getString(PREF_SSID, "");
    String savedPass = prefs.getString(PREF_PASS, "");
    prefs.end();

    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.setAutoReconnect(false);

    if (savedSSID.length() > 0) {
        Serial.printf("[WIFI] Saved SSID found: %s, connecting...\n", savedSSID.c_str());
        startSTAMode(savedSSID, savedPass);
    } else if (HAS_DEFAULT_CREDENTIALS && strlen(DEFAULT_SSID) > 0) {
        Serial.printf("[WIFI] No saved credentials, trying default SSID: %s\n", DEFAULT_SSID);
        startSTAMode(String(DEFAULT_SSID), String(DEFAULT_PASS));
    } else {
        Serial.println("[WIFI] No saved credentials, starting AP mode");
        startAPMode();
    }
}

void startSTAMode(const String& ssid, const String& pass) {
    apMode = false;
    currentSSID = ssid;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    wifiConnected = false;
    wifiConnecting = true;
    wifiConnectStart = millis();
    wifiFailCount = 0;
    Serial.printf("[WIFI] Connecting to %s...\n", ssid.c_str());
    uiDirty = true;
}

void startAPMode() {
    apMode = true;
    wifiConnected = false;
    currentIP = "";

    if (mdnsActive) {
        MDNS.end();
        mdnsActive = false;
    }

    currentSSID = AP_SSID;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(currentSSID.c_str());
    currentIP = WiFi.softAPIP().toString();

    dnsServer.start(53, "*", WiFi.softAPIP());

    Serial.printf("[WIFI] AP Mode - SSID: %s, IP: %s\n",
                  currentSSID.c_str(), currentIP.c_str());
    uiDirty = true;
}

void handleWiFiReconnect() {
    if (apMode) return;

    if (wifiFailCount == 0) {
        Serial.println("[WIFI] Attempting reconnect...");
        WiFi.disconnect();
        WiFi.reconnect();
    }

    wifiFailCount++;
    Serial.printf("[WIFI] Waiting for reconnect (attempt %d/%d)\n", wifiFailCount, MAX_WIFI_FAIL);

    if (wifiFailCount >= MAX_WIFI_FAIL) {
        wifiFailCount = 0;
        Serial.println("[WIFI] Max retries reached, falling back to AP");
        startAPMode();
    }
    uiDirty = true;
}

// ============================================================
// Helpers
// ============================================================
String jsonEscape(const String& input) {
    String out = input;
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    String clean = "";
    for (size_t i = 0; i < out.length(); i++) {
        char c = out.charAt(i);
        if (c >= 0x20 && c != 0x7F) clean += c;
    }
    return clean;
}

String sanitizeFilename(const String& filename) {
    String clean = filename;
    int lastSlash = clean.lastIndexOf('/');
    if (lastSlash >= 0) clean = clean.substring(lastSlash + 1);
    lastSlash = clean.lastIndexOf('\\');
    if (lastSlash >= 0) clean = clean.substring(lastSlash + 1);
    String result = "";
    for (size_t i = 0; i < clean.length(); i++) {
        char c = clean.charAt(i);
        if (c >= 0x20 && c != 0x7F) result += c;
    }
    if (result.length() > 127) result = result.substring(0, 127);
    return result;
}

bool isPathSafe(const String& path) {
    if (path.indexOf("..") >= 0) return false;
    if (path.length() == 0 || path.charAt(0) != '/') return false;
    return true;
}

// ============================================================
// Web Server (Drive-style SPA + JSON API)
// ============================================================
void setupWebServer() {
    server.onNotFound([](AsyncWebServerRequest* request) {
        if (apMode) {
            request->redirect("http://" + WiFi.softAPIP().toString() + "/");
        } else {
            request->send(404, "text/plain", "Not Found");
        }
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* res = request->beginResponse(200, "text/html", INDEX_HTML);
        res->addHeader("Cache-Control", "no-cache");
        request->send(res);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{\"sdMounted\":";
        json += (sdMounted ? "true" : "false");
        json += ",\"totalGB\":" + String(sdTotalGB, 2);
        json += ",\"freeGB\":" + String(sdFreeGB, 2);
        json += ",\"apMode\":";
        json += (apMode ? "true" : "false");
        json += ",\"wifiConnected\":";
        json += (wifiConnected ? "true" : "false");
        json += ",\"ssid\":\"" + jsonEscape(currentSSID) + "\"";
        json += ",\"ip\":\"" + currentIP + "\"";
        json += ",\"hostname\":\"" WIFI_HOSTNAME "\"";
        json += "}";
        request->send(200, "application/json", json);
    });

    server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!sdMounted) {
            request->send(503, "application/json", "{\"error\":\"sd not mounted\"}");
            return;
        }

        String path = "/";
        if (request->hasParam("dir")) {
            path = request->getParam("dir")->value();
        }
        if (!isPathSafe(path)) {
            request->send(403, "application/json", "{\"error\":\"invalid path\"}");
            return;
        }

        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            request->send(503, "application/json", "{\"error\":\"sd busy\"}");
            return;
        }

        File dir = SD.open(path);
        if (!dir || !dir.isDirectory()) {
            if (dir) dir.close();
            xSemaphoreGive(sdMutex);
            request->send(404, "application/json", "{\"error\":\"not found\"}");
            return;
        }

        String json = "{\"path\":\"" + jsonEscape(path) + "\",\"items\":[";
        bool first = true;
        bool truncated = false;
        File entry = dir.openNextFile();
        while (entry) {
            // Safety valve: a folder with thousands of entries would grow
            // this String until the heap OOMs. Cap it and flag the result;
            // the web UI simply shows the first chunk.
            if (json.length() > 60000) {
                truncated = true;
                entry.close();
                break;
            }
            String name = entry.name();
            if (name != "") {
                if (!first) json += ",";
                first = false;
                json += "{\"d\":";
                json += (entry.isDirectory() ? "true" : "false");
                json += ",\"n\":\"" + jsonEscape(name) + "\"";
                if (!entry.isDirectory()) {
                    json += ",\"s\":\"" + formatBytes(entry.size()) + "\"";
                }
                json += "}";
            }
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();
        xSemaphoreGive(sdMutex);
        if (truncated) json += ",\"truncated\":true";
        json += "]}";
        request->send(200, "application/json", json);
    });

    server.on("/api/mkdir", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!sdMounted) {
            request->send(503, "text/plain", "SD card not mounted");
            return;
        }
        String dir = "/";
        String name = "";
        if (request->hasParam("dir", true)) {
            dir = request->getParam("dir", true)->value();
        }
        if (request->hasParam("name", true)) {
            name = sanitizeFilename(request->getParam("name", true)->value());
        }
        if (name.length() == 0) {
            request->send(400, "text/plain", "Invalid folder name");
            return;
        }
        if (!isPathSafe(dir)) {
            request->send(403, "text/plain", "Invalid path");
            return;
        }
        String fullPath = (dir == "/") ? ("/" + name) : (dir + "/" + name);

        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            request->send(503, "text/plain", "SD busy");
            return;
        }
        if (SD.exists(fullPath)) {
            xSemaphoreGive(sdMutex);
            request->send(409, "text/plain", "Name already exists");
            return;
        }
        bool ok = SD.mkdir(fullPath);
        xSemaphoreGive(sdMutex);
        if (ok) {
            Serial.printf("[MKDIR] %s\n", fullPath.c_str());
            request->send(200, "text/plain", "Folder created");
        } else {
            request->send(500, "text/plain", "Could not create folder");
        }
    });

    server.on("/setwifi", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!apMode) {
            request->send(403, "text/plain", "WiFi config only available in setup mode");
            return;
        }

        String ssid = "";
        String pass = "";
        if (request->hasParam("ssid", true)) {
            ssid = request->getParam("ssid", true)->value();
        }
        if (request->hasParam("pass", true)) {
            pass = request->getParam("pass", true)->value();
        }

        if (ssid.length() > 0 && ssid.length() <= 32) {
            prefs.begin(PREF_NAMESPACE, false);
            prefs.putString(PREF_SSID, ssid);
            prefs.putString(PREF_PASS, pass);
            prefs.end();
            Serial.printf("[WIFI] Credentials saved for SSID: %s\n", ssid.c_str());
            request->send(200, "application/json", "{\"ok\":true}");
            delay(2000);
            ESP.restart();
        } else {
            request->send(400, "text/plain", "Invalid SSID (1-32 chars required)");
        }
    });

    server.on("/download", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!sdMounted || !request->hasParam("file")) {
            request->send(400, "text/plain", "Bad request");
            return;
        }
        String filePath = request->getParam("file")->value();
        if (!isPathSafe(filePath)) {
            request->send(403, "text/plain", "Invalid path");
            return;
        }

        // Validate first, then hand the filesystem to AsyncWebServer - the
        // response owns the File handle and streams it without our mutex.
        // A concurrent unmount (card physically pulled) kills the stream,
        // which is unavoidable and matches the card being gone anyway.
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            request->send(503, "text/plain", "SD busy");
            return;
        }
        File f = SD.open(filePath, FILE_READ);
        bool opened = (bool)f;
        bool isDir = opened && f.isDirectory();
        f.close();
        xSemaphoreGive(sdMutex);

        if (!opened || isDir) {
            request->send(404, "text/plain", "File not found");
            return;
        }
        AsyncWebServerResponse* res = request->beginResponse(SD, filePath, "application/octet-stream");
        res->addHeader("Content-Disposition", "attachment");
        request->send(res);
    });

    server.on("/delete", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!sdMounted || !request->hasParam("file")) {
            request->send(400, "text/plain", "Bad request");
            return;
        }
        String filePath = request->getParam("file")->value();
        if (!isPathSafe(filePath) || filePath == "/") {
            request->send(403, "text/plain", "Invalid path");
            return;
        }

        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
            request->send(503, "text/plain", "SD busy");
            return;
        }
        File f = SD.open(filePath, FILE_READ);
        if (!f) {
            xSemaphoreGive(sdMutex);
            request->send(404, "text/plain", "File not found");
            return;
        }
        bool isDir = f.isDirectory();
        f.close();
        if (isDir) {
            xSemaphoreGive(sdMutex);
            request->send(400, "text/plain", "Refusing to delete folder");
            return;
        }
        bool ok = SD.remove(filePath);
        xSemaphoreGive(sdMutex);
        if (ok) {
            Serial.printf("[DELETE] Removed: %s\n", filePath.c_str());
            request->send(200, "text/plain", "Deleted: " + filePath);
        } else {
            request->send(500, "text/plain", "Failed to delete: " + filePath);
        }
    });

    server.on("/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            UploadState* st = (UploadState*)request->_tempObject;
            bool ok = (st != nullptr) && st->success;
            if (ok) {
                request->send(200, "text/plain", "Upload complete");
            } else {
                request->send(500, "text/plain", "Upload failed - SD not mounted or write error");
            }
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            lastUploadActivity = millis();

            // Per-request verdict state (malloc: _tempObject is freed with
            // free() by the request destructor - see UploadState note above).
            UploadState* st = (UploadState*)request->_tempObject;
            if (st == nullptr) {
                st = (UploadState*)malloc(sizeof(UploadState));
                if (st == nullptr) return;  // OOM: chunks dropped, response is 500
                st->success = false;
                st->rejected = false;
                request->_tempObject = (void*)st;
            }

            // A rejected request drops every remaining chunk of its own
            // transfer; the in-flight upload keeps the file handle.
            if (st->rejected) return;

            if (!sdMounted) {
                if (uploadFileOpen) {
                    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                        uploadFile.close();
                        uploadFileOpen = false;
                        xSemaphoreGive(sdMutex);
                    }
                }
                return;
            }

            // Guard against interleaved uploads: index == 0 starts a new
            // transfer, so a zero index while a file handle is still open
            // means a second, parallel upload sharing the same globals.
            // Reject the newcomer instead of corrupting both transfers.
            if (index == 0 && uploadFileOpen) {
                st->rejected = true;
                Serial.println("[UPLOAD] Rejected: another upload already in progress");
                return;
            }

            if (!index) {
                String safeName = sanitizeFilename(filename);
                if (safeName.length() == 0) {
                    Serial.println("[UPLOAD] Rejected: invalid filename");
                    return;
                }
                String path = "/" + safeName;
                if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
                    Serial.println("[UPLOAD] SD busy, upload rejected");
                    return;
                }
                uploadFile = SD.open(path, FILE_WRITE);
                uploadFileOpen = (bool)uploadFile;
                xSemaphoreGive(sdMutex);
                Serial.printf("[UPLOAD] Start: %s (opened=%d)\n", path.c_str(), uploadFileOpen);
            }

            if (uploadFileOpen && len) {
                if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
                    Serial.println("[UPLOAD] SD busy mid-upload");
                    return;
                }
                size_t written = uploadFile.write(data, len);
                xSemaphoreGive(sdMutex);
                if (written != len) {
                    uploadAbortCleanup("partial write");
                    Serial.println("[UPLOAD] Write error: partial write");
                }
            }

            if (final) {
                if (uploadFileOpen) {
                    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
                        uploadFile.close();
                        uploadFileOpen = false;
                        st->success = true;
                        xSemaphoreGive(sdMutex);
                    }
                }
                Serial.printf("[UPLOAD] Done (success=%d)\n", st->success);
                uiDirty = true;
            }
        }
    );

    server.begin();
    Serial.println("[HTTP] Web server started (ESP32 Drive UI)");
}

// ============================================================
// UI Rendering (800x480, redrawn only on state change)
// ============================================================
void updateUI() {
    if (!sdMounted) {
        drawSDErrorScreenFull();
    } else if (apMode) {
        drawAPScreen();
    } else {
        drawStatusScreen();
    }
}

static void drawHeader(const char* title, uint16_t accent, const char* badge, uint16_t badgeColor) {
    const int HDR_H = 60;
    gfx->fillRect(0, 0, SCREEN_W, HDR_H, C_PANEL);
    gfx->fillRect(0, 0, 6, HDR_H, accent);
    gfx->setTextColor(accent, C_PANEL);
    gfx->setTextSize(3);                       // 18x24
    gfx->setCursor(16, 18);
    gfx->print(title);
    if (badge && badge[0]) {
        gfx->setTextColor(badgeColor, C_PANEL);
        gfx->setTextSize(2);                   // 12x16
        gfx->setCursor(SCREEN_W - 16 - textW(strlen(badge), 2), 22);
        gfx->print(badge);
    }
}

void drawStatusScreen() {
    const bool online = wifiConnected && WiFi.status() == WL_CONNECTED;
    gfx->fillScreen(C_BG);
    drawHeader("NAS // NODE 01", C_CYAN, online ? "ONLINE" : "LINKING",
               online ? C_GREEN : C_ORANGE);

    // --- Network panel ---
    gfx->drawRoundRect(10, 74, 780, 150, 8, C_PANEL);
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(24, 86);
    gfx->print("WIFI NETWORK");
    gfx->setCursor(SCREEN_W - 24 - textW(6 + (int)strlen(WIFI_HOSTNAME), 1), 86);
    gfx->printf("HOST %s", WIFI_HOSTNAME);

    String ssidLine = online ? currentSSID : "Awaiting connection";
    if (ssidLine.length() > 40) ssidLine = ssidLine.substring(0, 40);
    gfx->setTextColor(online ? C_GREEN : C_ORANGE, C_BG);
    gfx->setTextSize(3);                       // 18x24
    gfx->setCursor(24, 106);
    gfx->print(ssidLine);

    gfx->setTextColor(C_WHITE, C_BG);
    gfx->setTextSize(4);                       // 24x32
    gfx->setCursor(24, 168);
    gfx->printf("IP  %s", online ? currentIP.c_str() : "--");

    // --- Storage panel ---
    float usedGB = sdTotalGB - sdFreeGB;
    if (usedGB < 0) usedGB = 0;
    gfx->drawRoundRect(10, 238, 780, 170, 8, C_PANEL);
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(24, 250);
    gfx->print("STORAGE / MICROSD");
    gfx->setTextColor(C_GREEN, C_BG);
    gfx->setCursor(SCREEN_W - 24 - textW(7, 1), 250);
    gfx->print("MOUNTED");

    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setCursor(24, 272);
    gfx->print("TOTAL GB");
    gfx->setCursor(294, 272);
    gfx->print("USED GB");
    gfx->setCursor(564, 272);
    gfx->print("FREE GB");

    gfx->setTextColor(C_WHITE, C_BG);
    gfx->setTextSize(4);
    gfx->setCursor(24, 290);
    gfx->printf("%.2f", sdTotalGB);
    gfx->setCursor(294, 290);
    gfx->printf("%.2f", usedGB);
    gfx->setCursor(564, 290);
    gfx->printf("%.2f", sdFreeGB);

    if (sdTotalGB > 0) {
        float usedPct = (usedGB / sdTotalGB) * 100.0f;
        if (usedPct < 0) usedPct = 0;
        if (usedPct > 100) usedPct = 100;
        uint16_t usageColor = usedPct >= 90 ? C_RED : (usedPct >= 75 ? C_ORANGE : C_CYAN);
        gfx->drawRoundRect(24, 350, 752, 22, 4, C_MUTED);
        gfx->fillRoundRect(26, 352, (int)(748 * usedPct / 100.0f), 18, 3, usageColor);
    }

    // --- Footer ---
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(2);
    gfx->setCursor(16, 440);
    gfx->print(online ? (String("WEB FILES  http://") + currentIP)
                      : String("NETWORK INITIALIZING"));
    if (otaReady) {
        gfx->setTextSize(1);
        gfx->setCursor(SCREEN_W - 16 - textW(9, 1), 444);
        gfx->print("OTA READY");
    }
    Serial.println("[UI] Status screen drawn");
}

void drawSDErrorScreenFull() {
    gfx->fillScreen(C_BG);
    drawHeader("STORAGE // FAULT", C_RED, "NO STORAGE", C_RED);

    // --- Warning panel ---
    gfx->drawRoundRect(10, 74, 780, 210, 8, C_PANEL);
    gfx->drawRoundRect(28, 94, 100, 100, 8, C_RED);
    gfx->setTextColor(C_RED, C_PANEL);
    gfx->setTextSize(6);                       // 36x48
    gfx->setCursor(60, 120);
    gfx->print("!");
    gfx->setTextColor(C_WHITE, C_BG);
    gfx->setTextSize(4);
    gfx->setCursor(160, 106);
    gfx->print("SD CARD FAILED");
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(160, 150);
    gfx->print("Card not detected or mount rejected");
    gfx->setTextColor(C_ORANGE, C_BG);
    gfx->setTextSize(2);
    gfx->setCursor(160, 182);
    gfx->print("1. Remove + reinsert the card");
    gfx->setCursor(160, 210);
    gfx->print("2. Check FAT32 format");
    gfx->setCursor(160, 238);
    gfx->print("3. Try a different card");

    // --- Recovery agent panel ---
    gfx->drawRoundRect(10, 300, 780, 110, 8, C_PANEL);
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(24, 312);
    gfx->print("RECOVERY AGENT");
    gfx->setTextColor(C_CYAN, C_BG);
    gfx->setTextSize(2);
    gfx->setCursor(24, 334);
    gfx->print("Retrying every 5 s");

    // Animated activity dots (ticked locally, no full repaint)
    for (int i = 0; i < 4; ++i) {
        gfx->fillCircle(660 + i * 28, 344, 5, C_PANEL);
    }

    // Network line
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(2);
    gfx->setCursor(24, 432);
    gfx->print("NETWORK  ");
    gfx->setTextColor(wifiConnected ? C_GREEN : C_ORANGE, C_BG);
    gfx->print(wifiConnected ? "ONLINE" : (apMode ? "AP" : "LINKING"));
    Serial.println("[UI] SD fault screen painted");
}

void tickSDErrorScreen() {
    static uint8_t phase = 0;
    phase = (phase + 1) % 4;
    for (int i = 0; i < 4; ++i) {
        gfx->fillCircle(660 + i * 28, 344, 5, i == (int)phase ? C_CYAN : C_PANEL);
    }

    unsigned long secs = 0;
    if (sdFaultSince > 0) secs = (millis() - sdFaultSince) / 1000UL;
    char line[32];
    snprintf(line, sizeof(line), "ATT %-4d ELAPSED %02lu:%02lu",
             sdRetryCount, (unsigned long)(secs / 60), secs % 60);
    gfx->fillRect(20, 362, 400, 20, C_BG);
    gfx->setTextColor(C_WHITE, C_BG);
    gfx->setTextSize(2);
    gfx->setCursor(24, 366);
    gfx->print(line);
}

void drawAPScreen() {
    gfx->fillScreen(C_BG);
    drawHeader("NETWORK // SETUP", C_CYAN, "AP MODE", C_ORANGE);

    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(24, 86);
    gfx->print("CONNECT A DEVICE TO THIS ACCESS POINT");

    // --- AP SSID panel ---
    gfx->drawRoundRect(10, 104, 780, 110, 8, C_PANEL);
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(24, 116);
    gfx->print("SSID");
    String apSsid = currentSSID;
    if (apSsid.length() > 30) apSsid = apSsid.substring(0, 30);
    gfx->setTextColor(C_CYAN, C_BG);
    gfx->setTextSize(4);
    gfx->setCursor(24, 152);
    gfx->print(apSsid);

    // --- Password panel ---
    gfx->drawRoundRect(10, 228, 780, 90, 8, C_PANEL);
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(24, 240);
    gfx->print("PASSWORD");
    gfx->setTextColor(C_WHITE, C_BG);
    gfx->setTextSize(3);
    gfx->setCursor(24, 272);
    gfx->print("OPEN - NO KEY");

    // --- Portal panel ---
    gfx->drawRoundRect(10, 332, 780, 90, 8, C_PANEL);
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(24, 344);
    gfx->print("OPEN CONFIGURATION PORTAL");
    gfx->setTextColor(C_GREEN, C_BG);
    gfx->setTextSize(3);
    gfx->setCursor(24, 376);
    gfx->printf("http://%s", currentIP.c_str());

    // --- Footer ---
    gfx->setTextColor(C_MUTED, C_BG);
    gfx->setTextSize(1);
    gfx->setCursor(16, 446);
    gfx->printf("SD STORAGE  %s", sdMounted ? "READY" : "FAULT / RETRYING");
    Serial.println("[UI] AP setup screen drawn");
}

// ============================================================
// Utilities
// ============================================================
String formatBytes(uint64_t bytes) {
    if (bytes < 1024) return String(bytes) + " B";
    else if (bytes < 1024ULL * 1024) return String(bytes / 1024.0, 1) + " KB";
    else if (bytes < 1024ULL * 1024 * 1024) return String(bytes / (1024.0 * 1024.0), 1) + " MB";
    else return String((double)bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}
