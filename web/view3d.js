// 3D view of the web status page (ticket #37, SPEC 9.1). Source of the asset /a/v3d-<hash>.js:
// tools/make_web_assets.py gzips this file into src/web_assets.h, the handwheel sends it from flash.
// Loaded only when the user switches the map to "3D" (src/web_pages.h). Read-only: draws what
// /status.json reports, sends nothing.
//
// default(el, C, B, G) builds the scene into el and returns the update function for the list V of the
// status page: called with the latest /status.json object, or null while the handwheel does not answer.
// Throws (before touching el) when WebGL is missing; the page then stays on the 2D map.
// C = calibration from include/ui_config.h (micrometres), B = board geometry from
// include/spoilboard_geometry.h (0.1 mm, board-local), G = rotary module from include/a_axis_geometry.h
// (0.1 mm, from Makera's CAD: x board-local, y/z relative to the axis). Everything here is in machine
// millimetres, Z up.
import * as T from './three-0.160.0.js';
import { OrbitControls } from './oc.js';  // tools/make_web_assets.py puts in /a/oc-<hash>.js

const BOARD_T = 6;      // board thickness, mm (spoilboard_geometry.h)
const CB_DEPTH = 2;     // counterbore depth, mm (DECISIONS.md, ticket #36)
const TRAIL_MAX = 240;  // trail points (one per status update with movement)
const GLIDE_MS = 900;   // same glide as the 2D map
// Rotary module (A axis): stock radius (not in the CAD model, the jaws open to it); material per box of
// G.b (1 = upright, 0 = rail, foot, housing).
const STOCK_R = 15, BOX_MAT = '0101001';

export default function view3d(el, C, B, G) {
  const k = 1e-3;
  const R = new T.WebGLRenderer({ antialias: true, alpha: true });  // throws without WebGL
  R.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));

  const ox = C.ox * k, oy = C.oy * k, bz = C.bz * k, bw = B.w / 10, bh = B.h / 10;
  const x0 = C.x0 * k, x1 = C.x1 * k, y0 = C.y0 * k, y1 = C.y1 * k, z0 = C.z0 * k, z1 = C.z1 * k;
  const S = new T.Scene();
  const cam = new T.PerspectiveCamera(32, 1, 1, 5000);
  cam.up.set(0, 0, 1);

  // Colours come from the page tokens (light/dark), re-read when the scheme changes.
  const css = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim() || '#888';
  const mats = [];  // [material, token, property]
  const M = (m, tok, prop = 'color') => (mats.push([m, tok, prop]), m);
  const std = (tok, o = {}) => M(new T.MeshStandardMaterial({ roughness: .85, metalness: 0, ...o }), tok);
  const line = (tok, o = {}) => M(new T.LineBasicMaterial({ transparent: true, ...o }), tok);

  const hemi = new T.HemisphereLight(0xffffff, 0x888888, 1.6);
  hemi.position.set(0, 0, 1);
  const sun = new T.DirectionalLight(0xffffff, 1.8);
  sun.position.set(-120, -260, 400);
  S.add(hemi, sun);

  // Spoilboard: lower layer with the through holes, upper CB_DEPTH layer with the counterbores.
  const P = (x, y) => new T.Vector2(ox + x / 10, oy + y / 10);
  const plate = (top, depth, z) => {
    const s = new T.Shape(B.o.map(p => P(p[0], p[1])));
    for (let i = 0; i < B.p.length; i += 3) {
      const [r, q] = B.k[B.p[i + 2]], c = P(B.p[i], B.p[i + 1]), h = new T.Path();
      h.absarc(c.x, c.y, (top ? r : q || r) / 10, 0, 2 * Math.PI, true);
      s.holes.push(h);
    }
    const g = new T.ExtrudeGeometry(s, { depth, bevelEnabled: false, curveSegments: 18 });
    g.translate(0, 0, z);
    return g;
  };
  const boardMat = std('--md');
  const edgeMat = line('--me', { opacity: .9 });
  for (const g of [plate(false, BOARD_T - CB_DEPTH, bz - BOARD_T), plate(true, CB_DEPTH, bz - CB_DEPTH)]) {
    S.add(new T.Mesh(g, boardMat), new T.LineSegments(new T.EdgesGeometry(g, 40), edgeMat));
  }

  // Machine travel as a dashed box.
  const box = new T.BoxGeometry(x1 - x0, y1 - y0, z1 - z0);
  box.translate((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  const travel = new T.LineSegments(new T.EdgesGeometry(box),
    M(new T.LineDashedMaterial({ dashSize: 3, gapSize: 3, transparent: true, opacity: .5 }), '--ink'));
  travel.computeLineDistances();
  S.add(travel);

  // Tool: cutter tip exactly at MPos, shank and a spindle nose above it.
  const tool = new T.Group();
  const cyl = (r, h, z, seg = 28) => { const g = new T.CylinderGeometry(r, r, h, seg); g.rotateX(Math.PI / 2); g.translate(0, 0, z); return g; };
  const tip = new T.ConeGeometry(2.6, 8, 28);
  tip.rotateX(-Math.PI / 2);
  tip.translate(0, 0, 4);
  const toolMat = std('--ac', { roughness: .45, metalness: .25 });
  tool.add(new T.Mesh(tip, toolMat), new T.Mesh(cyl(2.6, 22, 19), toolMat),
    new T.Mesh(cyl(8, 6, 33), std('--mu', { roughness: .5, metalness: .4 })),
    new T.Mesh(cyl(13, 30, 51), std('--ln', { roughness: .6, metalness: .2 })));
  // Plumb line and ring on the board top below the tool: shows where it would cut.
  const plumbGeo = new T.BufferGeometry().setAttribute('position', new T.Float32BufferAttribute([0, 0, 0, 0, 0, -1], 3));
  const plumb = new T.Line(plumbGeo, line('--ac', { opacity: .7 }));
  const ring = new T.Mesh(new T.RingGeometry(3.4, 5, 40), M(new T.MeshBasicMaterial({ side: T.DoubleSide }), '--ac'));
  S.add(tool, plumb, ring);

  // Work zero (MPos − WPos): quartered disc as on the 2D map, plus short X/Y/Z axes.
  const wz = new T.Group();
  const discMat = M(new T.MeshBasicMaterial({ side: T.DoubleSide }), '--ink');
  const panelMat = M(new T.MeshBasicMaterial({ side: T.DoubleSide }), '--pn');
  wz.add(new T.Mesh(new T.CircleGeometry(4.2, 32), discMat));
  const face = new T.Mesh(new T.CircleGeometry(3.2, 32), panelMat);
  face.position.z = .02;
  const q1 = new T.Mesh(new T.CircleGeometry(3.2, 12, 0, Math.PI / 2), discMat);
  const q3 = new T.Mesh(new T.CircleGeometry(3.2, 12, Math.PI, Math.PI / 2), discMat);
  q1.position.z = q3.position.z = .04;
  wz.add(face, q1, q3);
  for (const [d, tok] of [[[1, 0, 0], '--bad'], [[0, 1, 0], '--ok'], [[0, 0, 1], '--ac']]) {
    const g = new T.BufferGeometry().setAttribute('position', new T.Float32BufferAttribute([0, 0, 0, d[0] * 22, d[1] * 22, d[2] * 22], 3));
    wz.add(new T.Line(g, line(tok, { opacity: 1 })));
  }
  S.add(wz);

  // Trail of recent positions, fading towards the panel colour.
  const trailPos = new Float32Array(TRAIL_MAX * 3), trailCol = new Float32Array(TRAIL_MAX * 3);
  const trailGeo = new T.BufferGeometry();
  trailGeo.setAttribute('position', new T.BufferAttribute(trailPos, 3));
  trailGeo.setAttribute('color', new T.BufferAttribute(trailCol, 3));
  trailGeo.setDrawRange(0, 0);
  const trail = new T.Line(trailGeo, new T.LineBasicMaterial({ vertexColors: true }));
  trail.frustumCulled = false;
  S.add(trail);
  const pts = [];
  const paintTrail = () => {
    const a = new T.Color(css('--pn')), b = new T.Color(css('--ac')), c = new T.Color();
    pts.forEach((p, i) => {
      trailPos.set(p, i * 3);
      c.copy(a).lerp(b, .15 + .85 * ((i + 1) / pts.length) ** 1.5).toArray(trailCol, i * 3);
    });
    trailGeo.setDrawRange(0, pts.length);
    trailGeo.attributes.position.needsUpdate = trailGeo.attributes.color.needsUpdate = true;
  };

  // Rotary module (A axis), only with axis_a_enabled && has_a (ticket #39: the machine always reports
  // five values, has_a alone says nothing about the module) and while the toggle "A-Modul" is on.
  // Simplified Makera module from the CAD (ticket #48, include/a_axis_geometry.h): axis parallel to X,
  // G.y behind the board corner and G.z above the board top. Rail, headstock, motor housing, tailstock
  // with quill and live centre stand still; chuck, jaws and stock turn with A.
  const g = v => v / 10, gx = v => ox + v / 10;
  const ay = oy + g(G.y), az = bz + g(G.z);
  const rot = new T.Group(), spin = new T.Group();
  const alu = std('--mu', { roughness: .55, metalness: .35 }), alu2 = std('--ln', { roughness: .6, metalness: .2 });
  const xcyl = (r, x0, x1, seg = 40) => { const c = new T.CylinderGeometry(r, r, x1 - x0, seg); c.rotateZ(-Math.PI / 2); c.translate((x0 + x1) / 2, 0, 0); return c; };
  const cub = (x0, x1, y0, y1, z0, z1) => { const b = new T.BoxGeometry(x1 - x0, y1 - y0, z1 - z0); b.translate((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2); return b; };
  G.b.forEach((b, i) => rot.add(new T.Mesh(cub(gx(b[0]), gx(b[1]), g(b[2]), g(b[3]), g(b[4]), g(b[5])), BOX_MAT[i] == 1 ? alu : alu2)));
  const [u0, u1, ur, ut, utr] = G.q, tipX = gx(ut);
  rot.add(new T.Mesh(xcyl(g(ur), gx(u0), gx(u1)), alu));                                                  // quill
  const centre = new T.ConeGeometry(g(utr), gx(u0) - tipX, 24);                                           // live centre
  centre.rotateZ(Math.PI / 2);
  centre.translate((tipX + gx(u0)) / 2, 0, 0);
  rot.add(new T.Mesh(centre, alu));
  G.s.forEach((c, i) => spin.add(new T.Mesh(xcyl(g(c[2]), gx(c[0]), gx(c[1])), i ? alu : alu2)));          // chuck
  const [j0, j1, jr0, jr1, jw, jn, ja] = G.j, jx0 = gx(j0), jr = Math.max(g(jr0), STOCK_R);
  for (let i = 0; i < jn; i++) {                                                                          // jaws, open to the stock
    const j = new T.Mesh(cub(jx0, gx(j1), -g(jw) / 2, g(jw) / 2, jr, jr + g(jr1 - jr0)), alu);
    j.rotation.x = T.MathUtils.degToRad(ja - 90 + i * 360 / jn);
    spin.add(j);
  }
  const stockMat = std('--cb', { roughness: .8 });
  spin.add(new T.Mesh(xcyl(STOCK_R, jx0, tipX), stockMat));                                               // stock, chuck to centre
  spin.add(new T.Mesh(cub(gx(j1), tipX, -2, 2, STOCK_R, STOCK_R + 1.2), toolMat));                        // angle mark
  // Geometry is built around y = 0, z = 0 so spin rotates about the A axis itself.
  rot.position.set(0, ay, az);
  rot.add(spin);
  rot.visible = false;
  S.add(rot);
  const chip = document.createElement('div');
  chip.className = 'a3';
  chip.hidden = true;
  // Toggle "A-Modul anzeigen" (ticket #39): only with the rotary axis, remembered per browser, default on.
  let showA = true;
  try { showA = localStorage.a3 !== '0'; } catch (e) { /* storage blocked: default */ }
  const tog = document.createElement('button');
  tog.type = 'button';
  tog.className = 'r3 t3';
  tog.textContent = 'A-Modul';
  tog.title = tog.ariaLabel = 'A-Modul anzeigen';
  tog.hidden = true;
  tog.setAttribute('aria-pressed', showA);

  // Frame, reset button, DOM. Nothing went into el before this point.
  R.domElement.setAttribute('aria-label', '3D-Ansicht: Werkzeug über der Opferplatte');
  R.domElement.setAttribute('role', 'img');
  const reset = document.createElement('button');
  reset.type = 'button';
  reset.className = 'r3';
  reset.title = reset.ariaLabel = 'Ansicht zurücksetzen';
  reset.innerHTML = "<svg viewBox='0 0 24 24' width=18 height=18 fill=none stroke=currentColor stroke-width=2.2 stroke-linecap=round><path d='M4.5 12a7.5 7.5 0 1 0 2.2-5.3'/><path d='M4 4v4.5h4.5'/></svg>";
  const st = document.createElement('style');
  st.textContent = '#v3 canvas{display:block;width:100%;height:100%;outline:0}' +
    '.r3,.a3{position:absolute;background:var(--pn);border:1px solid var(--ln);border-radius:9px;color:var(--ink)}' +
    '.r3{top:0;right:0;width:2.25rem;height:2.25rem;display:grid;place-items:center;padding:0;cursor:pointer}' +
    '.t3{right:2.75rem;width:auto;padding:0 .7rem;font:inherit;font-size:.8rem;font-weight:650;color:var(--mu)}' +
    '.t3[aria-pressed=true]{background:var(--ac);border-color:var(--ac);color:var(--on)}' +
    '.a3{left:0;bottom:0;padding:.2rem .6rem;font-size:.85rem;font-variant-numeric:tabular-nums}.a3 span{color:var(--mu)}';
  el.textContent = '';
  el.append(st, R.domElement, reset, tog, chip);

  const ctl = new OrbitControls(cam, R.domElement);
  ctl.target.set((Math.min(x0, ox) + Math.max(x1, ox + bw)) / 2, (Math.min(y0, oy) + Math.max(y1, oy + bh)) / 2,
    bz + (z1 - z0) * .3);
  {
    const r = Math.hypot(x1 - x0, y1 - y0, z1 - z0) / 2;
    const d = r / Math.sin(T.MathUtils.degToRad(cam.fov / 2)) * .82;
    cam.position.copy(ctl.target).add(new T.Vector3(.32, -.78, .54).normalize().multiplyScalar(d));
  }
  ctl.minDistance = 40;
  ctl.maxDistance = 1600;
  ctl.maxPolarAngle = Math.PI * .58;
  ctl.update();
  ctl.saveState();

  let raf = 0, w = 0, h = 0;
  const draw = () => { if (!raf) raf = requestAnimationFrame(frame); };
  ctl.addEventListener('change', draw);
  reset.onclick = () => { ctl.reset(); draw(); };
  let aOn = false;  // rotary axis enabled and reported
  const showRot = () => { rot.visible = aOn && showA; };
  tog.onclick = () => {
    showA = !showA;
    try { localStorage.a3 = showA ? '' : '0'; } catch (e) { /* not remembered */ }
    tog.setAttribute('aria-pressed', showA);
    showRot();
    ch.hidden = C.ac || !showA;
    draw();
  };
  new ResizeObserver(() => {
    w = el.clientWidth; h = el.clientHeight;
    if (!w || !h) return;  // hidden (2D selected): nothing to draw
    R.setSize(w, h, false);
    cam.aspect = w / h;
    cam.updateProjectionMatrix();
    draw();
  }).observe(el);

  const recolor = () => {
    for (const [m, tok, prop] of mats) m[prop].set(css(tok));
    paintTrail();
    draw();
  };
  recolor();
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', recolor);
  R.domElement.addEventListener('webglcontextlost', e => e.preventDefault());
  R.domElement.addEventListener('webglcontextrestored', draw);

  // Glide like the 2D map: from the shown state to the new one in GLIDE_MS, ease-out.
  const still = matchMedia('(prefers-reduced-motion: reduce)');
  let from = null, to = null, cur = null, t0 = 0;
  const lerp = (a, b, f) => a.map((v, i) => v + (b[i] - v) * f);
  function place(s) {
    const [x, y, z, a, wx, wy, wzz] = s;
    tool.position.set(x, y, z);
    plumb.position.set(x, y, z);
    plumb.scale.z = Math.max(z - bz, .001);
    ring.position.set(x, y, bz + .05);
    wz.position.set(wx, wy, wzz);
    spin.rotation.x = T.MathUtils.degToRad(a);
  }
  function frame(now) {
    raf = 0;
    if (!w || !h) return;
    if (to) {
      const f = still.matches ? 1 : Math.min((now - t0) / GLIDE_MS, 1);
      cur = lerp(from, to, 1 - (1 - f) ** 3);
      place(cur);
      if (f < 1) draw();
    }
    R.render(S, cam);
  }

  // Chip like the position readout: work angle (WPos) first, machine angle (MPos) small. The model
  // turns with MPos. The hint stays while the axis position is uncalibrated and the module is shown.
  const fmt = v => (v < 0 ? '−' : '') + Math.abs(v).toFixed(3).replace('.', ',');
  const cw = document.createElement('b'), cm = document.createElement('span'), ch = document.createElement('span');
  ch.textContent = ', Lage der Drehachse aus CAD';
  ch.hidden = C.ac || !showA;
  chip.append(cw, cm, ch);
  return d => {
    const on = !!(d && d.have_status);
    tool.visible = plumb.visible = ring.visible = wz.visible = on;
    aOn = on && !!(d.axis_a_enabled && d.has_a);
    showRot();
    chip.hidden = tog.hidden = !aOn;
    if (on) {
      const m = d.mpos, p = d.wpos, a = aOn ? m[3] : 0;
      const s = [m[0], m[1], m[2], a, m[0] - p[0], m[1] - p[1], m[2] - p[2]];
      if (aOn) {
        cw.textContent = 'A ' + fmt(p[3]) + '°';
        cm.textContent = ' · Maschine ' + fmt(m[3]) + '°';
      }
      from = cur || s;
      to = s;
      t0 = performance.now();
      if (!cur) cur = s;
      const last = pts[pts.length - 1];
      if (!last || Math.hypot(last[0] - m[0], last[1] - m[1], last[2] - m[2]) > .05) {
        pts.push([m[0], m[1], m[2]]);
        if (pts.length > TRAIL_MAX) pts.shift();
        paintTrail();
      }
    }
    draw();
  };
}
