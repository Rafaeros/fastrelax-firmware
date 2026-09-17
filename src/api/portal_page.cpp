#include "api/portal_page.h"

#include "config.h"
#include "util/device_identity.h"

namespace portal {

String pageHtml() {
  String html = R"=====(
<!DOCTYPE html><html lang="pt-br"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FastRelax - Cadeira</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  :root{--gold:#c9a15c;--gold-soft:#d8b978;--gold-deep:#a67c3d;--bg:#0a0806;--line:rgba(201,161,92,.18)}
  body{font-family:'Segoe UI',-apple-system,sans-serif;min-height:100vh;display:flex;align-items:center;
       justify-content:center;padding:24px;background:radial-gradient(120% 80% at 50% -10%,#1c1712,var(--bg) 55%),var(--bg);color:#f3ece0}
  .card{width:min(92%,420px);background:linear-gradient(160deg,rgba(28,23,18,.92),rgba(12,10,8,.92));
        border:1px solid var(--line);padding:40px 32px;border-radius:28px;text-align:center;
        box-shadow:0 40px 80px rgba(0,0,0,.6)}
  .brand{display:flex;align-items:center;justify-content:center;gap:12px;margin-bottom:28px}
  .mark{width:40px;height:40px;border-radius:50%;
        background:conic-gradient(from 210deg,var(--gold-soft),var(--gold-deep),#2a2018,var(--gold-soft))}
  .name{font-size:26px;font-weight:300;letter-spacing:1px;color:var(--gold-soft)}
  h1{font-weight:300;font-size:20px;margin-bottom:6px}
  .sub{color:rgba(243,236,224,.5);font-size:13px;margin-bottom:28px}
  .dial{width:190px;height:190px;margin:0 auto 20px;border-radius:50%;display:flex;align-items:center;
        justify-content:center;border:1px solid var(--line);background:radial-gradient(circle at 50% 40%,#1a150f,#0c0a08)}
  .dial.on{border-color:rgba(216,185,120,.6);box-shadow:0 0 40px rgba(201,161,92,.4)}
  .timer{font-size:42px;font-weight:200;font-variant-numeric:tabular-nums;color:rgba(243,236,224,.55)}
  .dial.on .timer{color:var(--gold-soft)}
  .status{font-size:13px;color:rgba(243,236,224,.7);min-height:20px;margin-bottom:22px}
  .meta{font-size:11px;color:rgba(243,236,224,.35);line-height:1.7;margin-bottom:22px;font-variant-numeric:tabular-nums}
  button{width:100%;padding:16px;border-radius:30px;font-size:13px;font-weight:600;letter-spacing:2px;
         text-transform:uppercase;cursor:pointer;border:1px solid var(--line);background:transparent;color:var(--gold-soft)}
  button:disabled{opacity:.35;cursor:default}
</style></head><body>
<div class="card">
  <div class="brand"><div class="mark"></div><div class="name">Lanx</div></div>
  <h1>Cadeira FastRelax</h1>
  <div class="sub">Controlada pelo aplicativo</div>
  <div class="dial" id="dial"><div class="timer" id="timer">--</div></div>
  <div class="status" id="status">Carregando...</div>
  <div class="meta" id="meta"></div>
  <button id="btn" onclick="stopSession()" disabled>Parada de emergencia</button>
</div>
<script>
const TOKEN = ')=====";

  html += device_identity::token();

  html += R"=====(';
async function stopSession(){
  if(!confirm('Interromper a sessao em andamento?'))return;
  await fetch('/stop',{method:'POST',headers:{'X-Device-Token':TOKEN}});
}
function tick(){
  fetch('/status').then(r=>r.json()).then(d=>{
    const dial=document.getElementById('dial'),timer=document.getElementById('timer');
    const status=document.getElementById('status'),btn=document.getElementById('btn');
    document.getElementById('meta').innerHTML =
      'MAC '+d.mac+'<br>IP '+d.ip+' &middot; '+d.link+
      (d.link==='wi-fi'?' &middot; sinal '+d.rssi+' dBm':'')+'<br>ligado ha '+
      Math.floor(d.uptimeSeconds/60)+' min';

    if(d.phase==='idle'){
      dial.classList.remove('on');timer.innerHTML='--';
      status.innerHTML='Aguardando comando do aplicativo';
      btn.disabled=true;return;
    }
    if(d.phase==='cooldown'){
      dial.classList.remove('on');timer.innerHTML=d.remainingSeconds;
      status.innerHTML='Estabilizando &middot; livre em '+d.remainingSeconds+'s';
      btn.disabled=true;return;
    }
    btn.disabled=false;
    if(d.phase==='starting'){
      dial.classList.remove('on');timer.innerHTML=d.remainingSeconds;
      status.innerHTML='Preparando... acomode-se';
    }else{
      dial.classList.add('on');
      const m=Math.floor(d.remainingSeconds/60),s=d.remainingSeconds%60;
      timer.innerHTML=String(m).padStart(2,'0')+':'+String(s).padStart(2,'0');
      status.innerHTML='Sessao em andamento &middot; #'+d.sessionId;
    }
  }).catch(()=>{document.getElementById('status').innerHTML='Sem resposta do dispositivo';});
}
setInterval(tick,1000);tick();
</script></body></html>
)=====";

  return html;
}

}  // namespace portal
