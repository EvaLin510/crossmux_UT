#!/usr/bin/env python3
"""Build a single-file browser flasher (esptool-js + Web Serial) for the UT37 e-reader.

Usage:
  python3 scripts/make_flasher_html.py --bootloader B.bin --partitions P.bin \
      --boot-app0 A.bin --app F.bin --title "UT37 1.6.0" --out UT37-Flasher.html
Flash map (ESP32-S3, 16MB): 0x0 bootloader, 0x8000 partitions, 0xe000 boot_app0, 0x10000 app.
"""
import argparse, base64, hashlib, pathlib

TEMPLATE = r'''<!doctype html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>__TITLE__ 刷機工具</title>
<style>
body{font-family:system-ui,"Microsoft JhengHei",sans-serif;max-width:640px;margin:2rem auto;padding:0 1rem;color:#222}
button{font-size:1rem;padding:.7rem 1.2rem;margin:.3rem .3rem .3rem 0;cursor:pointer}
#bar{width:100%;height:1.2rem}#log{background:#111;color:#9f9;padding:.7rem;height:14rem;overflow:auto;font:12px/1.4 monospace;white-space:pre-wrap}
.note{background:#fff7e0;border:1px solid #e6c860;padding:.6rem .8rem;border-radius:6px;font-size:.9rem}
</style></head><body>
<h2>__TITLE__ 刷機工具</h2>
<p class="note">需使用 Chrome 或 Edge（支援 Web Serial），且需連網載入刷機元件。刷機前請用 USB 線連接裝置。若找不到連接埠，先按住電源鍵關機，再重新插上 USB。</p>
<label><input type="checkbox" id="erase"> 清除全部資料（第一次從官方韌體換過來建議勾選；會刪除設定與閱讀進度）</label><br>
<button id="go">連接並刷機</button>
<button id="mon">監看序列埠記錄（診斷用）</button>
<button id="cp">複製記錄</button>
<progress id="bar" value="0" max="100"></progress>
<div id="status">尚未開始</div>
<pre id="log"></pre>
<p style="font-size:.8rem;color:#666">韌體校驗 SHA-256（app）：__SHA__</p>
<script type="module">
import {ESPLoader, Transport} from "https://cdn.jsdelivr.net/npm/esptool-js@0.5.4/bundle.js";
const SEGS = __SEGS__;
const $ = id => document.getElementById(id);
const log = s => { $("log").textContent += s + "\n"; $("log").scrollTop = 1e9; };
const term = {clean(){ $("log").textContent=""; }, writeLine:log, write:s=>{ $("log").textContent+=s; }};
const toBin = b64 => atob(b64);
$("go").onclick = async () => {
  if(!("serial" in navigator)){ alert("此瀏覽器不支援 Web Serial，請改用 Chrome 或 Edge。"); return; }
  $("go").disabled = true;
  let transport;
  try{
    const port = await navigator.serial.requestPort();
    transport = new Transport(port, true);
    const loader = new ESPLoader({transport, baudrate:921600, romBaudrate:115200, terminal:term});
    $("status").textContent = "連接中…";
    const chip = await loader.main();
    log("晶片：" + chip);
    if(!/ESP32-S3/i.test(chip)){ throw new Error("偵測到的不是 ESP32-S3，已停止"); }
    const fileArray = SEGS.map(s => ({data: toBin(s.data), address: s.addr}));
    $("status").textContent = "寫入中，請勿拔線…";
    await loader.writeFlash({
      fileArray, flashSize:"keep", flashMode:"keep", flashFreq:"keep",
      eraseAll: $("erase").checked, compress:true,
      reportProgress:(i,w,t)=>{ $("bar").value = ((i + w/t)/fileArray.length)*100; }
    });
    $("bar").value = 100;
    $("status").textContent = "完成，裝置重新啟動…";
    await loader.after("hard_reset");
    log("完成。若螢幕沒有反應，請拔掉 USB 後長按電源鍵開機。");
  }catch(e){
    $("status").textContent = "失敗：" + (e.message||e);
    log("錯誤：" + (e.stack||e));
  }finally{
    try{ if(transport) await transport.disconnect(); }catch(_){}
    $("go").disabled = false;
  }
};

let monPort = null, monReader = null, monStop = false;
const sleep = ms => new Promise(r => setTimeout(r, ms));
$("cp").onclick = async () => { try{ await navigator.clipboard.writeText($("log").textContent); $("status").textContent="已複製記錄"; }catch(e){ $("status").textContent="複製失敗，請手動選取"; } };
async function readLoop(port){
  const dec = new TextDecoder();
  monReader = port.readable.getReader();
  try{
    while(true){
      const {value, done} = await monReader.read();
      if(done) break;
      if(value){ $("log").textContent += dec.decode(value, {stream:true}); $("log").scrollTop = 1e9; }
    }
  } finally { try{ monReader.releaseLock(); }catch(_){} monReader = null; }
}
$("mon").onclick = async () => {
  if(!("serial" in navigator)){ alert("請使用 Chrome 或 Edge。"); return; }
  if(monPort){ monStop = true; try{ if(monReader) await monReader.cancel(); }catch(_){} return; }
  monStop = false;
  try{
    monPort = await navigator.serial.requestPort();
    $("mon").textContent = "停止監看";
    log("--- 序列埠監看開始（會先重啟裝置）---");
    let first = true;
    while(!monStop){
      try{
        await monPort.open({baudRate:115200});
        if(first){
          first = false;
          try{ await monPort.setSignals({dataTerminalReady:false, requestToSend:true}); await sleep(120);
               await monPort.setSignals({dataTerminalReady:false, requestToSend:false}); }catch(_){}
        }
        $("status").textContent = "監看中…";
        await readLoop(monPort);
      }catch(e){ log("[連線中斷：" + (e.message||e) + "]"); }
      try{ await monPort.close(); }catch(_){}
      if(monStop) break;
      // 裝置重啟後會重新列舉：最多等 12 秒找回連接埠
      let found = null;
      for(let k=0;k<40 && !found && !monStop;k++){ await sleep(300); const ps = await navigator.serial.getPorts(); if(ps.length) found = ps[0]; }
      if(!found){ log("[找不到連接埠，已停止]"); break; }
      monPort = found;
    }
  }catch(e){ log("監看結束：" + (e.message||e)); }
  finally{
    try{ if(monPort) await monPort.close(); }catch(_){}
    monPort = null; $("mon").textContent = "監看序列埠記錄（診斷用）";
  }
};
</script></body></html>'''

def seg(path, addr):
    data = pathlib.Path(path).read_bytes()
    return {"addr": addr, "data": base64.b64encode(data).decode(), "size": len(data)}, data

ap = argparse.ArgumentParser()
for k in ("bootloader", "partitions", "boot-app0", "app", "out"):
    ap.add_argument("--" + k, required=True)
ap.add_argument("--title", default="UT37")
a = ap.parse_args()
parts = [seg(a.bootloader, 0x0), seg(a.partitions, 0x8000),
         seg(a.boot_app0, 0xE000), seg(a.app, 0x10000)]
sha = hashlib.sha256(parts[3][1]).hexdigest()
import json
segs = json.dumps([{"addr": p[0]["addr"], "data": p[0]["data"]} for p in parts])
html = TEMPLATE.replace("__TITLE__", a.title).replace("__SHA__", sha).replace("__SEGS__", segs)
pathlib.Path(a.out).write_text(html, encoding="utf-8")
print(f"wrote {a.out} ({len(html)//1024} KiB), app sha256 {sha}")
