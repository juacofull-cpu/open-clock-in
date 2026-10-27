// Página del panel web del reloj (se sirve tal cual desde la placa). Solo lo del aparato:
// estado, conexión con el servidor y actualizar el programa. Las marcas y los
// reportes están en el servidor.
#pragma once
#include <pgmspace.h>

static const char PANEL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Reloj control</title>
<style>
:root{--neg:#111;--bla:#fff;--gris:#6b6b6b;--linea:#e3e3e3;--ok:#14a44d;--err:#d92d20;--fondo:#f4f4f2}
*{box-sizing:border-box}
body{margin:0;font-family:Barlow,system-ui,sans-serif;font-weight:500;background:var(--fondo);color:var(--neg)}
header{background:var(--neg);color:var(--bla);padding:18px 20px}
h1{font-family:Anton,Impact,sans-serif;font-weight:400;font-size:32px;margin:0;letter-spacing:.5px}
main{max-width:720px;margin:0 auto;padding:16px 16px 60px}
h2{font-family:Anton,Impact,sans-serif;font-weight:400;font-size:22px;margin:26px 0 10px}
table{width:100%;border-collapse:collapse;background:var(--bla)}
th,td{text-align:left;padding:10px 12px;border-bottom:1px solid var(--linea)}
th{font-weight:600;color:var(--gris);width:40%}
.ok{color:var(--ok);font-weight:600}.mal{color:var(--err);font-weight:600}
label{display:flex;flex-direction:column;font-size:14px;color:var(--gris);gap:4px;margin-bottom:12px}
input,select{font:500 17px system-ui,sans-serif;padding:10px;border:1px solid #ccc;background:var(--bla);width:100%}
.btn{font:600 17px system-ui,sans-serif;padding:11px 18px;border:0;background:var(--neg);color:var(--bla);cursor:pointer}
.nota{color:var(--gris);font-size:15px}
#msg{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:var(--neg);color:var(--bla);padding:12px 18px;display:none}
</style></head><body>
<header><h1>RELOJ CONTROL</h1></header>
<main>
<h2>Estado</h2>
<table id="tEst"></table>
<h2>Conexión con el servidor</h2>
<p class="nota">La llave es la variable RELOJ_TOKEN del panel. Queda guardada solo en el reloj: no se vuelve a mostrar.</p>
<label>Dirección del panel<input id="url" placeholder="https://..."></label>
<label>Llave del reloj<input id="llave" type="password" autocomplete="off"></label>
<label>Oficina de este reloj<select id="oficina"><option value="A">Oficina A</option><option value="B">Oficina B</option></select></label>
<p class="nota">Cada marca lleva la oficina: así el calendario del panel sabe quién está en cuál.</p>
<button class="btn" id="guardar">Guardar</button>
<h2>Actualizar programa</h2>
<p class="nota">Sube el archivo firmware.bin que te entreguen. El reloj se reinicia solo al terminar.</p>
<p><input type="file" id="bin" accept=".bin"></p>
<button class="btn" id="subir">Actualizar</button>
<p id="prog" class="nota"></p>
</main>
<div id="msg"></div>
<script>
const $=s=>document.querySelector(s);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function aviso(t){const m=$('#msg');m.textContent=t;m.style.display='block';clearTimeout(m._t);m._t=setTimeout(()=>m.style.display='none',3500)}
let primera=true;
async function estado(){
 try{
  const e=await (await fetch('/api/estado')).json(),f=(a,b)=>`<tr><th>${a}</th><td>${b}</td></tr>`;
  $('#tEst').innerHTML=f('Panel',`<span class="${e.conectado?'ok':'mal'}">${e.conectado?'Conectado':esc(e.panel)}</span>`)+
   f('Oficina',esc(e.oficina))+f('Marcas por enviar',e.cola)+f('WiFi',`${esc(e.wifi)}${e.senal?` (señal ${e.senal} dBm)`:''}`)+
   f('Lector de huella',e.lector?'conectado':'<span class="mal">no responde</span>')+
   f('Hora del reloj',e.hora||'<span class="mal">sin hora</span>')+f('Versión',e.version);
  if(primera){$('#url').value=e.panelUrl;$('#oficina').value=e.oficina;$('#llave').placeholder=e.llave?'(guardada; déjala vacía para no cambiarla)':'Pega aquí la llave';primera=false}
 }catch(x){$('#tEst').innerHTML='<tr><td class="mal">No se pudo conectar con el reloj</td></tr>'}
}
$('#guardar').onclick=async()=>{
 const r=await fetch('/api/panel',{method:'POST',body:new URLSearchParams({url:$('#url').value,llave:$('#llave').value,oficina:$('#oficina').value})});
 if(!r.ok)return aviso(await r.text());
 $('#llave').value='';$('#llave').placeholder='(guardada; déjala vacía para no cambiarla)';aviso('Guardado. Probando la conexión...');
 setTimeout(estado,1500);setTimeout(estado,5000)};
$('#subir').onclick=()=>{const f=$('#bin').files[0];if(!f)return aviso('Elige el archivo .bin');
 const fd=new FormData();fd.append('f',f);const x=new XMLHttpRequest();x.open('POST','/actualizar');
 x.upload.onprogress=e=>$('#prog').textContent='Subiendo '+Math.round(e.loaded*100/e.total)+' %';
 x.onload=()=>$('#prog').textContent=x.responseText=='ok'?'Listo. El reloj se está reiniciando; recarga en 20 segundos.':'Falló la actualización: el archivo no sirve.';
 x.send(fd)};
estado();setInterval(estado,5000);
</script></body></html>)HTML";
