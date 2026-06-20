#include "network.h"

#include "history.h"
#include "network_detail.h"
#include "time_sync.h"

#if FERM_ENABLE_NETWORK
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_random.h>
#endif

#if FERM_ENABLE_NETWORK && FERM_ENABLE_OTA
#include <Update.h>
#endif

#if FERM_ENABLE_NETWORK
#include "web_assets.h"
#endif

namespace ferm {

using namespace network_detail;

String NetworkManager::assetVersionQuery() const {
  // ?v= is for cache-busting. Due to no-cache/must-revalidate headers on js/css/manifest,
  // the query only meaningfully affects the long-lived icon assets.
  return String("?v=") + String(FIRMWARE_GIT_SHA);
}

String NetworkManager::webAppHead(const char *title, bool appCss,
                                    const char *extraHead) const {
  const String q = assetVersionQuery();
  String html = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#071015">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="FermentDial">
<meta name="mobile-web-app-capable" content="yes">
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="manifest" href="/manifest.webmanifest)HTML";
  html += q;
  html += R"HTML(">
<link rel="apple-touch-icon" href="/icon-192.png)HTML";
  html += q;
  html += R"HTML(">
)HTML";
  if (appCss) {
    html += R"HTML(<link rel="stylesheet" href="/app.css)HTML";
    html += q;
    html += R"HTML(">
)HTML";
  }
  if (extraHead != nullptr) {
    html += extraHead;
  }
  html += R"HTML(<title>)HTML";
  html += title;
  html += R"HTML(</title>
</head>)HTML";
  return html;
}

String NetworkManager::pageHtml() const {
  String html = webAppHead("FermentDial", /*appCss=*/true);
  html += R"HTML(
<body data-page="dashboard"><div id="app"></div>
<script src="/app.js)HTML";
  html += assetVersionQuery();
  html += R"HTML("></script>
</body></html>)HTML";
  return html;
}

String NetworkManager::settingsHtml() const {
  String html = webAppHead("FermentDial Settings", /*appCss=*/true);
  html += R"HTML(
<body data-page="settings"><div id="app"></div>
<script src="/app.js)HTML";
  html += assetVersionQuery();
  html += R"HTML("></script>
</body></html>)HTML";
  return html;
}

String NetworkManager::setupHtml() const {
  String html = webAppHead("FermentDial Wi-Fi", /*appCss=*/false, SETUP_STYLE);
  html += R"HTML(<body><main><div class="card">
<h1>FermentDial Wi-Fi</h1>
<p class="hint">Join your fermentation controller to your home Wi-Fi.</p>
<p><a href="/dashboard">Open live dashboard</a></p>
<p><a href="/settings">Device settings</a></p>
<form method="post" action="/wifi">
<label>Wi-Fi name</label><input name="ssid" autocomplete="off" required>
<button type="button" onclick="scanWifi()">Scan for networks</button>
<p class="hint scanStatus" id="wifiScanStatus">Select a scanned network or enter the name manually.</p>
<div class="networkList" id="wifiNetworks"></div>
<label>Password</label><input name="pass" type="password">
<button type="submit">Save and reboot</button>
</form>
<script>
function pickWifi(ssid){document.querySelector('input[name=ssid]').value=ssid}
async function scanWifi(){
 const status=document.getElementById('wifiScanStatus'),list=document.getElementById('wifiNetworks');
 status.textContent='Scanning...'; list.innerHTML='';
 try{
  const r=await fetch('/api/wifi/scan'); if(!r.ok)throw new Error('scan failed');
  const data=await r.json(),nets=(data.networks||[]).filter(n=>n.ssid);
  if(!nets.length){status.textContent='No networks found. Enter the name manually.';return}
  status.textContent='Select a network or enter the name manually.';
  for(const n of nets){
   const b=document.createElement('button'),name=document.createElement('span'),meta=document.createElement('span');
   b.type='button'; name.textContent=n.ssid; meta.className='networkMeta'; meta.textContent=(n.secure?'secured':'open')+' '+n.rssi+' dBm';
   b.appendChild(name); b.appendChild(meta); b.onclick=function(){pickWifi(n.ssid)}; list.appendChild(b);
  }
 }catch(e){status.textContent='Scan failed. Enter the name manually.'}
}
</script>
<p class="hint">Setup AP: )HTML";
  html += htmlEscape(setupApSsid());
  html += R"HTML( (open network)</p>
</div></main></body></html>)HTML";
  return html;
}

String NetworkManager::loginHtml(bool showError) const {
  String html = webAppHead("FermentDial Login", /*appCss=*/false, LOGIN_STYLE);
  html += R"HTML(<body><main><div class="card">
<h1>FermentDial</h1>
<p class="hint">Enter the admin password to reach device settings.</p>
)HTML";
  if (showError) {
    html += R"HTML(<p class="err">Incorrect password. Try again.</p>)HTML";
  }
  html += R"HTML(<form method="post" action="/login">
<label>Password</label><input name="password" type="password" autofocus required>
<button type="submit">Log in</button>
</form>
<p><a href="/dashboard">Back to dashboard</a></p>
</div></main></body></html>)HTML";
  return html;
}

String NetworkManager::firmwareHtml() const {
  constexpr const char *FIRMWARE_STYLE = R"HTML(<style>
html{background:#071015}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0;min-height:100vh}
main{max-width:520px;margin:auto;padding:24px}.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
h1{margin-top:0;color:#b0d8f8}input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:8px;border:1px solid #1e3840}
input{background:#102126;color:#d0e8f0}button{background:#356f89;color:white;font-weight:900}.hint{color:#a9bac8}.warn{color:#ffd178}
a{color:#79d4ff}
</style>)HTML";
  String html = webAppHead("FermentDial Firmware", /*appCss=*/false, FIRMWARE_STYLE);
  html += R"HTML(<body><main><div class="card">
<h1>Firmware Update</h1>
<p class="warn">Outputs turn off before the update starts. The controller reboots after a successful upload.</p>
<form method="post" action="/firmware" enctype="multipart/form-data">
<label>Firmware .bin</label><input type="file" name="firmware" accept=".bin" required>
<button type="submit">Upload and reboot</button>
</form>
<p class="hint">Build with <code>uv run platformio run -e m5stack_dial_demo</code>, then upload <code>.pio/build/m5stack_dial_demo/firmware.bin</code>.</p>
<p><a href="/wifi">Wi-Fi settings</a> - <a href="/dashboard">Dashboard</a></p>
</div></main></body></html>)HTML";
  return html;
}

#if FERM_ENABLE_SCREEN_MIRROR
String NetworkManager::framebufferViewerHtml() const {
  constexpr const char *SCREEN_MIRROR_STYLE = R"HTML(<style>
html{background:#071015}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0;min-height:100vh}
main{max-width:520px;margin:auto;padding:24px;text-align:center}
.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
h1{margin-top:0;color:#b0d8f8;font-size:1.4rem}
.controls{display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:center;margin:16px 0}
label{font-size:.9rem;color:#a9bac8}
input[type=range]{width:160px;vertical-align:middle}
button{font:inherit;padding:10px 18px;border-radius:8px;border:1px solid #1e3840;background:#356f89;color:white;font-weight:700;cursor:pointer}
button.stop{background:#1e3840}
.status{font-size:.85rem;color:#a9bac8;min-height:1.2em}
.screenWrap{display:inline-block;border-radius:50%;overflow:hidden;border:2px solid #1e3840;line-height:0;vertical-align:top}
.screenScaler{transform-origin:0 0;image-rendering:pixelated;image-rendering:crisp-edges}
#screen{display:block;image-rendering:pixelated;image-rendering:crisp-edges;touch-action:none;cursor:pointer}
.zoomBtns{display:flex;gap:8px;justify-content:center;margin:8px 0}
.zoomBtns button{width:auto;min-width:48px}
.zoomBtns button.active{background:#4a8fa8}
.hint{font-size:.85rem;color:#a9bac8}
a{color:#79d4ff}
</style>)HTML";
  String html = webAppHead("Screen Mirror", /*appCss=*/false, SCREEN_MIRROR_STYLE);
  html += R"HTML(<body><main><div class="card">
<h1>Screen Mirror</h1>
<p class="status" id="status">Loading...</p>
<div class="screenWrap" id="screenWrap"><div class="screenScaler" id="screenScaler"><canvas id="screen" width="240" height="240"></canvas></div></div>
<div class="zoomBtns">
<button type="button" class="active" data-zoom="1">1x</button>
<button type="button" data-zoom="2">2x</button>
</div>
<p class="hint">Drag left/right to change pages, tap for quick menu, hold for settings, scroll to turn the dial. 1x matches the physical dial pixel-for-pixel.</p>
<div class="controls">
<label>Interval <span id="intervalLabel">1.0</span>s</label>
<input type="range" id="interval" min="250" max="5000" step="250" value="1000">
<button type="button" id="toggle">Stop</button>
</div>
<p><a href="/dashboard">Dashboard</a></p>
</div></main>
<script>
let W=240,H=240,stride=240,zoom=1;
const canvas=document.getElementById('screen');
const ctx=canvas.getContext('2d');
ctx.imageSmoothingEnabled=false;
const statusEl=document.getElementById('status');
const wrapEl=document.getElementById('screenWrap');
const scalerEl=document.getElementById('screenScaler');
const intervalInput=document.getElementById('interval');
const intervalLabel=document.getElementById('intervalLabel');
const toggleBtn=document.getElementById('toggle');
let running=true,busy=false,intervalMs=1000;
const TAP_MAX_MOVE=12,SWIPE_MIN_MOVE=36,HOLD_MS=800;
let ptrDown=false,startX=0,startY=0,lastX=0,lastY=0,holdTimer=null,holdFired=false;
function scale5(v){return(v<<3)|(v>>2)}
function scale6(v){return(v<<2)|(v>>4)}
function unpack565(v,d,p){
 d[p]=scale5((v>>11)&31);d[p+1]=scale6((v>>5)&63);d[p+2]=scale5(v&31);d[p+3]=255
}
function setZoom(next){
 zoom=next;
 for(const b of document.querySelectorAll('.zoomBtns button')){
  b.classList.toggle('active',Number(b.dataset.zoom)===zoom);
 }
 const px=W*zoom;
 wrapEl.style.width=px+'px';
 wrapEl.style.height=px+'px';
 scalerEl.style.width=W+'px';
 scalerEl.style.height=H+'px';
 scalerEl.style.transform='scale('+zoom+')';
}
function decodeFrame(buf,imgW,imgH,rowStride){
 const img=ctx.createImageData(imgW,imgH),d=img.data;
 for(let y=0;y<imgH;y++){
  const row=y*rowStride*2;
  for(let x=0;x<imgW;x++){
   const i=row+x*2;
   const p=(y*imgW+x)*4;
   const v=(buf[i]<<8)|buf[i+1];
   unpack565(v,d,p);
  }
 }
 return img;
}
async function refresh(){
 if(!running){schedule();return}
 if(document.hidden||busy){schedule();return}
 busy=true;
 try{
  const r=await fetch('/api/screen');
  if(!r.ok)throw new Error('HTTP '+r.status);
  W=Number(r.headers.get('X-Frame-Width')||W);
  H=Number(r.headers.get('X-Frame-Height')||H);
  stride=Number(r.headers.get('X-Frame-Stride')||W);
  if(canvas.width!==W||canvas.height!==H){
   canvas.width=W;canvas.height=H;setZoom(zoom);
  }
  const buf=new Uint8Array(await r.arrayBuffer());
  ctx.putImageData(decodeFrame(buf,W,H,stride),0,0);
  statusEl.textContent='Updated';
 }catch(e){statusEl.textContent='Fetch failed'}
 busy=false;schedule();
}
function deviceCoords(clientX,clientY){
 const rect=canvas.getBoundingClientRect();
 const x=Math.floor((clientX-rect.left)*(W/rect.width));
 const y=Math.floor((clientY-rect.top)*(H/rect.height));
 return{x:Math.max(0,Math.min(W-1,x)),y:Math.max(0,Math.min(H-1,y))};
}
function clearHoldTimer(){
 if(holdTimer){clearTimeout(holdTimer);holdTimer=null}
}
async function sendInput(payload){
 const body=new URLSearchParams(payload);
 try{
  const r=await fetch('/api/screen/input',{method:'POST',body});
  if(!r.ok)return;
  const retry=function(){if(busy)setTimeout(retry,50);else refresh()};
  retry();
 }catch(_){}
}
function finishPointer(){
 if(!ptrDown)return;
 ptrDown=false;
 clearHoldTimer();
 if(holdFired)return;
 const dx=lastX-startX,dy=lastY-startY,absX=Math.abs(dx),absY=Math.abs(dy);
 if(absX>=SWIPE_MIN_MOVE||absY>=SWIPE_MIN_MOVE){
  sendInput({type:'swipe',dx:String(dx),dy:String(dy)});
  return;
 }
 sendInput({type:'tap',x:String(lastX),y:String(lastY)});
}
function schedule(){setTimeout(refresh,intervalMs)}
intervalInput.oninput=function(){
 intervalMs=Number(intervalInput.value);
 intervalLabel.textContent=(intervalMs/1000).toFixed(1);
};
toggleBtn.onclick=function(){
 running=!running;
 toggleBtn.textContent=running?'Stop':'Start';
 toggleBtn.classList.toggle('stop',!running);
 if(running)refresh();
};
for(const b of document.querySelectorAll('.zoomBtns button')){
 b.onclick=function(){setZoom(Number(b.dataset.zoom))};
}
canvas.addEventListener('pointerdown',function(e){
 if(e.button!==0)return;
 canvas.setPointerCapture(e.pointerId);
 ptrDown=true;holdFired=false;
 const p=deviceCoords(e.clientX,e.clientY);
 startX=lastX=p.x;startY=lastY=p.y;
 clearHoldTimer();
 holdTimer=setTimeout(function(){
  holdFired=true;
  sendInput({type:'hold'});
 },HOLD_MS);
});
canvas.addEventListener('pointermove',function(e){
 if(!ptrDown)return;
 const p=deviceCoords(e.clientX,e.clientY);
 lastX=p.x;lastY=p.y;
 if(Math.abs(lastX-startX)>TAP_MAX_MOVE||Math.abs(lastY-startY)>TAP_MAX_MOVE)clearHoldTimer();
});
canvas.addEventListener('pointerup',finishPointer);
canvas.addEventListener('pointercancel',finishPointer);
canvas.addEventListener('wheel',function(e){
 e.preventDefault();
 sendInput({type:'scroll',delta:String(e.deltaY>0?1:-1)});
},{passive:false});
setZoom(1);
refresh();
</script>
</body></html>)HTML";
  return html;
}
#endif

} // namespace ferm
