#pragma once
// Look of all web pages (SPEC 9.1, ticket #36): shared style for the captive portal, the settings form
// and the status page, plus the constant part of the status page. Everything inline, no external
// resources, system fonts only. Plain string literals, so the status page can be sent straight from
// flash without a heap copy (webui.cpp). Written compact on purpose (page target < 14 KiB = 14336 bytes;
// 13618 bytes with the 2D/3D switch of ticket #37, 13784 with ticket #39, 13787 with ticket #41, 14072 with
// the CAD rotary module of ticket #48, measured with tools/dump_web_pages.cpp).
// No Arduino headers: tools/dump_web_pages.cpp renders the pages on the host for the browser check.
#include <cstddef>
#include <cstdio>

#include "a_axis_geometry.h"
#include "spoilboard_geometry.h"
#include "web_asset_paths.h"
#include "ui_config.h"

// Light/dark via prefers-color-scheme. Tokens: bg aluminium, pn panel, ink text, mu muted, ln lines,
// ac accent, on text on accent, ok/run/wa/bad state colours, md/me spoilboard face/edge, cb counterbore,
// th through hole.
#define WEB_CSS R"~(:root{color-scheme:light dark;--bg:#e6e9ec;--pn:#fbfcfd;--ink:#16202b;--mu:#5c6876;--ln:#d0d6dd;--ac:#1d5bc9;--on:#fff;--ok:#0f7d5b;--wa:#a66b05;--bad:#c22f3b;--md:#cfae82;--me:#a7845a;--cb:#b8956a;--th:#5a3f24}
@media(prefers-color-scheme:dark){:root{--bg:#0e141b;--pn:#161f29;--ink:#e1e6ec;--mu:#8f9ba8;--ln:#27323e;--ac:#78a7ff;--on:#0e141b;--ok:#3cc28f;--wa:#e7a83e;--bad:#f3646e;--md:#7d6143;--me:#9a7b57;--cb:#685036;--th:#1d140b}}
*{box-sizing:border-box}[hidden]{display:none!important}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,sans-serif}
.top,main{max-width:74rem;margin:0 auto;padding:.9rem 1rem}.top{display:flex;align-items:center;gap:.55rem;font-weight:650;font-size:1.1rem}.top svg{width:28px;color:var(--ac)}.top a{margin-left:auto;font-weight:500;font-size:.95rem}
a{color:var(--ac);text-underline-offset:3px}:focus-visible{outline:2px solid var(--ac);outline-offset:2px}main{padding-top:0}
.card{background:var(--pn);border:1px solid var(--ln);border-radius:16px;padding:1rem 1.1rem}
.hint{color:var(--mu);font-size:.86rem;margin:.35rem 0 0}.foot{color:var(--mu);font-size:.86rem;text-align:center;margin:1.4rem 0 0}
)~"

// Portal and settings form only.
#define WEB_FORM_CSS R"~(.n{max-width:32rem}.f{margin:0 max(1rem,50% - 15rem) 2rem;padding:1.1rem}label{display:block;margin:1rem 0 .3rem;font-weight:600;font-size:.92rem}input{width:100%;font:inherit;padding:.65rem .75rem;border:1px solid var(--ln);border-radius:10px;background:var(--bg);color:inherit}
.chk{display:flex;gap:.65rem;align-items:center;font-weight:500;font-size:1rem}.chk input{width:1.3rem;height:1.3rem;margin:0;accent-color:var(--ac)}
button{display:block;width:100%;margin-top:1.2rem;padding:.8rem;font:inherit;font-weight:600;border:0;border-radius:10px;background:var(--ac);color:var(--on)}.sec{margin-top:.6rem;background:none;color:var(--ac);border:1px solid var(--ln)}
.err{color:var(--bad);background:color-mix(in srgb,var(--bad) 11%,#0000);border-radius:10px;padding:.7rem .85rem;margin:0 0 .5rem}
)~"

// Handwheel mark (generic, not a vendor logo).
#define WEB_ICON "<svg viewBox='0 0 24 24' fill=currentColor aria-hidden=true><circle cx=12 cy=12 r=9.5 fill=none stroke=currentColor stroke-width=2.2 /><circle cx=12 cy=12 r=2.6 /><circle cx=16.6 cy=7.4 r=1.7 /></svg>"

// favicon href "data:," keeps the browser from asking the handwheel for /favicon.ico.
#define WEB_META "<meta name=viewport content='width=device-width,initial-scale=1'><link rel=icon href='data:,'><title>Handrad</title>"

// Start of the portal and settings pages; the page ends with "</main></body></html>".
#define WEB_PAGE_HEAD "<!doctype html><html lang=de><head><meta charset=utf-8>" WEB_META "<style>" WEB_CSS WEB_FORM_CSS \
  "</style></head><body><header class='top n'>" WEB_ICON "Handrad</header><main class='f card'>"

// Status page only. st = machine state (--s set by the script picks the colour), gt = handwheel gate, rd = readout,
// mp = 2D map, zb = Z bar, ad = A dial.
#define WEB_STATUS_CSS R"~(.g{display:grid;gap:.8rem}h3{margin:0 0 .5rem;font-size:.95rem}
#st{border-left:6px solid var(--s)}
#sw{font-size:2.1rem;font-weight:750;line-height:1.1;color:var(--s)}#sr,#age,.m,.lg,.sd,#gt span{color:var(--mu);font-size:.85rem}
#gt{display:flex;align-items:center;gap:.5rem;margin-top:.8rem;padding:.55rem .75rem;border-radius:10px;--c:var(--ok);color:var(--c);background:color-mix(in srgb,var(--c) 13%,#0000)}
#gt.wa{--c:var(--wa)}#gt.bad{--c:var(--bad)}#gt b{font-size:1.15rem}#gt svg{width:18px}#gt.ok svg{display:none}#al{margin-top:.6rem;color:var(--bad)}
.rh{display:flex;justify-content:space-between;align-items:baseline}.old{color:var(--wa)}
.row{display:grid;grid-template-columns:2.2rem 1fr;align-items:center;column-gap:.7rem;padding:.35rem .5rem;margin:0 -.5rem;border-radius:10px}
.l{grid-row:span 2;display:grid;place-items:center;height:2.2rem;border-radius:9px;border:1.5px solid var(--ln);font-weight:700;color:var(--mu)}
.on{background:color-mix(in srgb,var(--ac) 10%,#0000)}.on .l{background:var(--ac);border-color:var(--ac);color:var(--on)}
.w{font-size:2.15rem;font-weight:650;line-height:1.1}.w,.m{text-align:right;font-variant-numeric:tabular-nums}.u{font-size:.5em;color:var(--mu)}
.mw{display:flex;gap:.6rem}#map{flex:1;min-width:0}.tr{fill:none;stroke:var(--ink);stroke-dasharray:3 4;opacity:.5}.bd{fill:var(--md);stroke:var(--me)}#cb{fill:var(--cb)}#th{fill:var(--th)}
.tr,.bd,.k,.wz *{vector-effect:non-scaling-stroke}.k{fill:none;stroke:var(--ac);stroke-width:2.2}.wz circle{fill:var(--pn);stroke:var(--ink);stroke-width:1.5}.wz path{fill:var(--ink)}
.an #tl,.an #wz,.an .zm,.an #an{transition:transform .9s cubic-bezier(.25,.7,.3,1)}
.sd{width:3rem;display:flex;flex-direction:column;align-items:center;gap:.4rem}.zb{position:relative;flex:1;width:14px;border-radius:7px;background:var(--bg);border:1px solid var(--ln)}
.zm{position:absolute;left:-7px;right:-7px;top:0}.zm i{position:absolute;inset:-2px 0 auto;height:4px;border-radius:2px;background:var(--ac)}#z0 i{background:var(--ink);height:2px;inset:-1px 3px auto}
#ad{width:40px;height:40px;border:3px solid var(--ln);border-radius:50%;position:relative}#an{position:absolute;left:15px;top:2px;width:4px;height:15px;background:var(--ac);transform-origin:50% 15px}
.lg{display:flex;flex-wrap:wrap;gap:.3rem 1rem;margin-top:.6rem}.lg i{display:inline-block;width:12px;height:12px;border-radius:50%;border:2px solid var(--ac);margin-right:.35rem}
.lg .z{border:1.5px solid var(--ink);background:conic-gradient(var(--ink) 25%,#0000 0 50%,var(--ink) 0 75%,#0000 0)}.lg .v{border:1px dashed var(--mu);border-radius:0}
dl{display:grid;grid-template-columns:auto 1fr;gap:.35rem 1rem;margin:0;font-size:.93rem}dt{color:var(--mu)}dd{margin:0;text-align:right;overflow-wrap:anywhere}
.bt{height:8px;border-radius:4px;background:var(--bg);border:1px solid var(--ln);overflow:hidden;margin:.3rem 0 .7rem}.bt i{display:block;height:100%;background:var(--ok)}.bt.wa i{background:var(--wa)}
.bp{font-size:1.6rem;font-weight:650}summary{cursor:pointer;font-weight:600;font-size:.95rem}[open] summary{margin-bottom:.6rem}
#off{position:sticky;top:0;z-index:2;background:var(--bad);color:#fff;padding:.7rem 1rem;text-align:center;font-weight:600}.lost main{opacity:.45;filter:grayscale(1)}
.vh{display:flex;justify-content:space-between;align-items:center;margin:-.25rem 0 .5rem}.vh h3{margin:0}
.sg{display:flex;padding:2px;border-radius:9px;background:var(--bg);border:1px solid var(--ln)}.sg button{font:inherit;font-size:.8rem;font-weight:650;padding:.15rem .75rem;border:0;border-radius:7px;background:none;color:var(--mu);cursor:pointer}.sg [aria-pressed=true]{background:var(--ac);color:var(--on)}
#v3{position:relative;aspect-ratio:1;max-height:75vh;width:100%}#v3:empty:after{content:'3D-Ansicht wird geladen …';position:absolute;inset:0;display:grid;place-items:center;color:var(--mu)}.d3 .mw,#view:not(.d3) #v3{display:none}
@media(min-width:900px){.top,main{max-width:92rem}.g{grid-template-columns:3fr 2fr;grid-template-rows:repeat(5,auto) 1fr;align-items:start}.g>*{grid-column:2}.mp{grid-column:1;grid-row:1/span 6}.foot{grid-column:1/-1}#v3{aspect-ratio:auto;height:70vh;min-height:24rem}#map{max-height:70vh}}
@media(prefers-reduced-motion:reduce){.an *{transition:none!important}}
)~"

// Lock icon for a blocked handwheel.
#define WEB_LOCK "<svg viewBox='0 0 24 24' fill=currentColor><rect x=5 y=10.5 width=14 height=10 rx=2 /><path d='M8 10.5V8a4 4 0 0 1 8 0v2.5' fill=none stroke=currentColor stroke-width=2.2 /></svg>"

// Work zero symbol (quartered circle), used on the map and in the legend.
#define WEB_WZ "<circle r=4 /><path d='M0 0H4A4 4 0 0 0 0-4ZM0 0H-4A4 4 0 0 0 0 4Z'/>"

// Status page after the dynamic start written by webui.cpp ("<!doctype html>...<script>var C={...}
// </script>", calibration from ui_config.h). Every view is an update function in V, called with the
// latest /status.json object d, or with null while the handwheel does not answer. The 3D view
// (ticket #37, web/view3d.js) is imported only when "3D" is chosen and then registers itself the same
// way (m3; L = last answer, U = its update function); 2D stays the fallback. The first block
// draws the 2D top view in machine millimetres (y flipped by the outer SVG group); the second fills
// the text cards.
#define WEB_STATUS_TAIL WEB_META "<style>" WEB_CSS WEB_STATUS_CSS "</style></head><body>" \
  "<div id=off hidden></div><header class=top>" WEB_ICON "Handrad<a href=/settings>Einstellungen</a></header>" \
  R"~(<main class=g>
<section class=card id=st><div id=sw>Verbinde …</div><div id=sr>Warte auf das Handrad</div><div id=gt hidden>)~" WEB_LOCK R"~(<span>Handrad</span><b id=gb></b></div><div id=al hidden></div></section>
<section class='card rd'><div class=rh><h3>Position in mm</h3><span id=age></span></div><div id=ax></div></section>
<section class='card mp' id=view><div class=vh><h3>Lage auf der Platte</h3><div class=sg role=group aria-label=Ansicht><button id=b2 aria-pressed=true>2D</button><button id=b3 aria-pressed=false>3D</button></div></div><div id=v3></div><div class=mw><svg id=map role=img aria-label=Draufsicht><g transform='scale(1,-1)'>
<path class=bd id=bd /><path id=cb /><path id=th /><rect class=tr id=tr /><g class=wz id=wz>)~" WEB_WZ R"~(</g>
<g id=tl><circle class=k r=5.5 /><path class=k d='M-11 0H-3M3 0H11M0-11V-3M0 3V11'/></g></g></svg>
<div class=sd>Z<div class=zb><div class=zm id=z0><i></i></div><div class=zm id=zm><i></i></div></div><span id=zv></span><div id=ac hidden><div id=ad><i id=an></i></div>A</div></div></div>
<div class=lg><span><i></i>Werkzeug</span><span><i class=z></i>Werkstück-Null</span><span><i class=v></i>Verfahrweg</span></div>
<p class=hint id=cal>Plattenlage geschätzt, noch nicht eingemessen.</p><p class=hint id=e3 hidden></p></section>
<section class=card><h3>Verbindung</h3><dl id=cn></dl></section>
<section class=card><h3>Handrad</h3><div id=bt></div><dl id=hr></dl></section>
<details class=card><summary>Link-Statistik (5 s)</summary><dl id=ls></dl></details>
<p class=foot id=fw></p></main>
<script>
const B=)~" SPOILBOARD_JS R"~(,G=)~" A_AXIS_JS R"~(,$=i=>document.getElementById(i),V=[],A='XYZA',k=1e-3,
S={Idle:['Bereit','ok'],Run:['Läuft','ac'],Hold:['Angehalten','wa'],Pause:['Pause','wa'],Alarm:['Alarm','bad'],Home:['Referenzfahrt','ac'],Tool:['Werkzeugwechsel','wa'],Sleep:['Ruhezustand'],Wait:['Wartet','wa'],Unknown:['Unbekannt']},
h=s=>String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c])),
dl=(i,L)=>$(i).innerHTML=L.map(x=>'<dt>'+x[0]+'<dd>'+x[1]).join(''),
n3=v=>(v<0?'−':'')+Math.abs(v).toFixed(3),
at=(e,a,v)=>e.setAttribute(a,v),tx=(i,v)=>$(i).textContent=v,ha=d=>d.axis_a_enabled&&d.has_a;
$('ax').innerHTML=[...A].map(c=>`<div class=row id=r${c}><span class=l>${c}</span><span class=w id=w${c}>–</span><span class=m id=m${c}></span></div>`).join('');
{const ox=C.ox*k,oy=C.oy*k,x0=Math.min(C.x0*k,ox)-12,x1=Math.max(C.x1*k,ox+B.w/10)+12,y0=Math.min(C.y0*k,oy)-12,y1=Math.max(C.y1*k,oy+B.h/10)+12,
t=$('tr'),P=p=>(ox+p[0]/10)+','+(oy+p[1]/10),O=(x,y,q)=>{x=ox+x/10-q/10;y=oy+y/10;return`M${x},${y}a${q/10},${q/10} 0 1 0 ${q/5},0a${q/10},${q/10} 0 1 0 ${-q/5},0`},cb=[],th=[],
cl=(v,a,b)=>Math.min(Math.max(v,a+4),b-4),T=(e,x,y)=>e.style.transform=`translate(${cl(x,x0,x1)}px,${cl(y,y0,y1)}px)`,
Z=(e,z)=>e.style.transform=`translateY(${Math.min(Math.max((C.z1-z/k)/(C.z1-C.z0),0),1)*$('zm').parentNode.clientHeight}px)`;
at($('map'),'viewBox',[x0,-y1,x1-x0,y1-y0]);at(t,'x',C.x0*k);at(t,'y',C.y0*k);at(t,'width',(C.x1-C.x0)*k);at(t,'height',(C.y1-C.y0)*k);
at($('bd'),'d','M'+B.o.map(P).join('L')+'Z');for(let i=0;i<B.p.length;i+=3){const[r,q]=B.k[B.p[i+2]],x=B.p[i],y=B.p[i+1];if(q)cb.push(O(x,y,r));th.push(O(x,y,q||r))}at($('cb'),'d',cb.join(''));at($('th'),'d',th.join(''));
$('cal').hidden=C.cal;
V.push(d=>{const on=d&&d.have_status;for(const i of['tl','wz','zm','z0','zv'])$(i).style.opacity=+on;$('ac').hidden=!(on&&ha(d));if(!on)return;
const m=d.mpos,w=d.wpos;T($('tl'),m[0],m[1]);T($('wz'),m[0]-w[0],m[1]-w[1]);Z($('zm'),m[2]);Z($('z0'),m[2]-w[2]);
tx('zv',n3(m[2]));if(ha(d))$('an').style.transform=`rotate(${m[3]}deg)`;
requestAnimationFrame(()=>$('view').classList.add('an'))})}
V.push(d=>{if(!d)return;const st=d.have_status,s=st?S[d.state]||[d.state]:['Kein Status'],job=st&&d.playing,
free=d.gate=='Bereit'||d.gate=='Fährt',g=$('gt'),a=$('age'),q=d.rssi,t=d.stats,u=d.uptime_s;
$('st').style.setProperty('--s','var(--'+(job?'ac':s[1]||'mu')+')');tx('sw',job?'Job läuft':s[0]);
tx('sr',st?'Maschine meldet '+d.state+(d.halted?', angehalten':'')+(d.inch_mode?', Zoll-Modus':''):'Noch kein Statusbericht');
g.hidden=0;g.className=free?'ok':/^Al/.test(d.gate)?'bad':'wa';tx('gb',d.gate);
$('al').hidden=!d.alarm;tx('al','Letzte Meldung: '+d.alarm);
a.textContent=st?'Status vor '+(d.status_age_ms/1000).toFixed(1).replace('.',',')+' s':'';a.className=st&&d.status_age_ms>1500?'old':'';
[...A].forEach((c,i)=>{$('r'+c).hidden=i>2&&!(st&&ha(d));$('r'+c).className='row'+(d.axis.includes(c)?' on':'');
$('w'+c).innerHTML=st?n3(d.wpos[i])+(i>2?'<span class=u>°</span>':''):'–';tx('m'+c,st?'Maschine '+n3(d.mpos[i]):'')});
dl('cn',[['Maschine',`<b style=color:var(--${d.link=='Online'?'ok':'wa'})>● </b>`+h(d.link)],['WLAN',q==null?'getrennt':q+' dBm'],['IP',h(d.ip||'–')],['Letzter Fehler',h(d.last_error||'–')]]);
$('bt').innerHTML=d.bat_absent?'<div class=bp>USB<span class=u> kein Akku</span></div>':`<div class=bp>${d.bat_percent} %<span class=u> Akku</span></div><div class='bt${d.bat_warn?' wa':''}'><i style=width:${d.bat_percent}%></i></div>`;
dl('hr',[...d.bat_absent?[]:[['Spannung',d.bat_mv+' mV'+(d.bat_warn?', schwach':'')]],['Achse',h(d.axis)],['Sitzung',d.session_active?'aktiv':'–'],['Drehachse A',d.axis_a_enabled?'aktiviert':'aus'],['Einstellungen',d.settings_allowed?'speicherbar':'gesperrt']]);
dl('ls',t?[['Status ok/kaputt',t.status_ok+'/'+t.status_bad],['Text/andere',t.text_frames+'/'+t.other_frames],['Max. Lücke',t.max_gap_ms+' ms'],['Über 250 ms',t.gaps_over_limit],['Verworfen',t.dropped],['Loop',t.loop_max_ms+' ms max., Ø '+t.loop_mean_us+' µs'],['Web',t.web_max_us+' µs max.']]:[['Keine Daten','–']]);
tx('fw','Firmware '+d.fw+', läuft seit '+(u<3600?'':(u/3600|0)+' h ')+(u%3600/60|0)+' min')});
let lt=0,L=null,U=0;
const m3=o=>{$('view').classList.toggle('d3',o);at($('b2'),'aria-pressed',!o);at($('b3'),'aria-pressed',o);try{localStorage.h3=o?1:''}catch(e){}
if(o&&!U){U=1;import(')~" WEB_V3D_PATH R"~(').then(m=>{U=m.default($('v3'),C,B,G);V.push(U);U(L);$('e3').hidden=1}).catch(e=>{U=0;m3(!1);$('e3').hidden=0;tx('e3','3D-Ansicht nicht verfügbar ('+(e&&e.message||e)+'), Karte bleibt 2D.')})}};
$('b2').onclick=()=>m3(!1);$('b3').onclick=()=>m3(!0);try{localStorage.h3&&m3(!0)}catch(e){}
const f=()=>{const c=new AbortController(),t=setTimeout(()=>c.abort(),3000),o=$('off'),x=b=>{document.body.classList.toggle('lost',b);o.hidden=!b;
o.textContent='Keine Verbindung zum Handrad. '+(lt?'Letzte Antwort vor '+Math.round((Date.now()-lt)/1000)+' s.':'Noch keine Antwort.')};
fetch('/status.json',{signal:c.signal}).then(r=>{if(!r.ok)throw 0;return r.json()}).then(d=>{lt=Date.now();L=d;x(0);V.forEach(v=>v(d))})
.catch(()=>{L=null;x(1);V.forEach(v=>v(null))}).finally(()=>{clearTimeout(t);setTimeout(f,1000)})};
f();
</script></body></html>)~"

// Dynamic start of the status page: doctype and the calibration object C (micrometres, ui_config.h).
// Returns the length written (snprintf semantics).
inline int web_status_start(char* out, size_t cap) {
  return snprintf(out, cap,
                  "<!doctype html><html lang=de><head><meta charset=utf-8><script>var C={cal:%d,ox:%ld,oy:%ld,"
                  "x0:%ld,x1:%ld,y0:%ld,y1:%ld,z0:%ld,z1:%ld,bz:%ld,ac:%d}</script>",
                  UI_POS_CALIBRATED ? 1 : 0, static_cast<long>(UI_BOARD_ORIGIN_MPOS_X_UM),
                  static_cast<long>(UI_BOARD_ORIGIN_MPOS_Y_UM), static_cast<long>(UI_TRAVEL_X_MIN_UM),
                  static_cast<long>(UI_TRAVEL_X_MAX_UM), static_cast<long>(UI_TRAVEL_Y_MIN_UM),
                  static_cast<long>(UI_TRAVEL_Y_MAX_UM), static_cast<long>(UI_TRAVEL_Z_MIN_UM),
                  static_cast<long>(UI_TRAVEL_Z_MAX_UM), static_cast<long>(UI_BOARD_TOP_MPOS_Z_UM),
                  UI_A_POS_CALIBRATED ? 1 : 0);
}
