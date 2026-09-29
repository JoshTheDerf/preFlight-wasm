#!/usr/bin/env node
// Feature tests for the preFlight bridge: painted facets (support / color),
// variable layer height, height range modifiers, plate custom G-code.
//   node tests/cs-features-test.mjs [buildDir]
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';
const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');
// ------------------------------------------------------------------ meshes
function parseStl(buf) {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const n = dv.getUint32(80, true);
  const isBinary = 84 + n * 50 === buf.byteLength;
  const tris = [];
  if (isBinary) {
    for (let i = 0; i < n; i++) {
      const o = 84 + i * 50 + 12;
      const t = [];
      for (let k = 0; k < 9; k++) t.push(dv.getFloat32(o + k * 4, true));
      tris.push(t);
    }
  } else {
    const txt = new TextDecoder().decode(buf);
    const v = [...txt.matchAll(/vertex\s+(\S+)\s+(\S+)\s+(\S+)/g)].map((m) => [+m[1], +m[2], +m[3]]);
    for (let i = 0; i + 2 < v.length; i += 3) tris.push([...v[i], ...v[i + 1], ...v[i + 2]]);
  }
  // Weld identical vertices so the engine gets a proper indexed mesh.
  const map = new Map();
  const pos = [];
  const idx = new Uint32Array(tris.length * 3);
  tris.forEach((t, ti) => {
    for (let c = 0; c < 3; c++) {
      const key = `${t[c * 3]},${t[c * 3 + 1]},${t[c * 3 + 2]}`;
      let id = map.get(key);
      if (id === undefined) {
        id = pos.length / 3;
        map.set(key, id);
        pos.push(t[c * 3], t[c * 3 + 1], t[c * 3 + 2]);
      }
      idx[ti * 3 + c] = id;
    }
  });
  return { positions: new Float32Array(pos), indices: idx };
}

function cube(size = 20) {
  const s = size;
  const p = [0, 0, 0, s, 0, 0, s, s, 0, 0, s, 0, 0, 0, s, s, 0, s, s, s, s, 0, s, s];
  const f = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7];
  return { positions: new Float32Array(p), indices: new Uint32Array(f) };
}

// Closed torus centred on the origin: 2*nu*nv triangles.
function torus(R, r, nu, nv) {
  const pos = new Float32Array(nu * nv * 3);
  for (let i = 0; i < nu; i++) {
    const u = (i / nu) * 2 * Math.PI;
    for (let j = 0; j < nv; j++) {
      const v = (j / nv) * 2 * Math.PI;
      const k = (i * nv + j) * 3;
      pos[k] = (R + r * Math.cos(v)) * Math.cos(u);
      pos[k + 1] = (R + r * Math.cos(v)) * Math.sin(u);
      pos[k + 2] = r * Math.sin(v);
    }
  }
  const idx = new Uint32Array(nu * nv * 6);
  let t = 0;
  for (let i = 0; i < nu; i++) {
    const i1 = (i + 1) % nu;
    for (let j = 0; j < nv; j++) {
      const j1 = (j + 1) % nv;
      const a = i * nv + j, b = i1 * nv + j, c = i1 * nv + j1, d = i * nv + j1;
      idx.set([a, b, c, a, c, d], t);
      t += 6;
    }
  }
  return { positions: pos, indices: idx };
}

// ------------------------------------------------------------- transforms
const I4 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const translate = (x, y, z) => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1];
function mul(a, b) { // column-major a*b
  const o = new Array(16).fill(0);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) for (let k = 0; k < 4; k++) o[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
  return o;
}
const rotZ = (deg) => { const t = (deg * Math.PI) / 180, c = Math.cos(t), s = Math.sin(t); return [c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; };
const rotX = (deg) => { const t = (deg * Math.PI) / 180, c = Math.cos(t), s = Math.sin(t); return [1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1]; };
const scale = (x, y, z) => [x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1];

// ------------------------------------------------------------------ jobs
const BASE_CONFIG = {
  layer_height: 0.2,
  first_layer_height: 0.2,
  nozzle_diameter: [0.4],
  filament_diameter: [1.75],
  filament_density: [1.24],
  filament_cost: [25],
  perimeters: 2,
  fill_density: '15%',
  bed_shape: ['0x0', '250x0', '250x210', '0x210'],
  max_print_height: 220,
  temperature: [215],
  bed_temperature: [60],
  start_gcode: 'G28 ; home\nG1 Z5 F5000',
  end_gcode: 'M104 S0\nM140 S0\nM84',
  gcode_flavor: 'marlin2',
  skirts: 1,
};

function buildJob(objects, { config = {}, options = {} } = {}) {
  // Pack meshes into one blob: [verts][indices] per object, 4-aligned.
  let size = 0;
  const layout = objects.map((o) => {
    const v = size; size += o.mesh.positions.byteLength;
    const i = size; size += o.mesh.indices.byteLength;
    return { v, i };
  });
  const blob = new Uint8Array(size);
  objects.forEach((o, k) => {
    blob.set(new Uint8Array(o.mesh.positions.buffer, o.mesh.positions.byteOffset, o.mesh.positions.byteLength), layout[k].v);
    blob.set(new Uint8Array(o.mesh.indices.buffer, o.mesh.indices.byteOffset, o.mesh.indices.byteLength), layout[k].i);
  });
  const job = {
    config: { ...BASE_CONFIG, ...config },
    objects: objects.map((o, k) => ({
      name: o.name,
      vertexOffset: layout[k].v,
      vertexCount: o.mesh.positions.length / 3,
      indexOffset: layout[k].i,
      triangleCount: o.mesh.indices.length / 3,
      transform: o.transform ?? I4,
      ...(o.config ? { config: o.config } : {}),
      ...(o.paint ? { paint: o.paint } : {}),
      ...(o.layerHeightProfile ? { layerHeightProfile: o.layerHeightProfile } : {}),
      ...(o.layerRanges ? { layerRanges: o.layerRanges } : {}),
    })),
    options: { validate: true, dropToBed: true, ...options },
  };
  return { job, blob };
}

// ---------------------------------------------------------------- engine
async function loadModule(buildDir) {
  const { default: factory } = await import(pathToFileURL(path.join(buildDir, 'slicer.mjs')).href + `?t=${Math.random()}`);
  const wasmBinary = await readFile(path.join(buildDir, 'slicer.wasm'));
  const stderr = [];
  const m = await factory({ wasmBinary, print: () => {}, printErr: (s) => { stderr.push(s); } });
  m.stderrLog = stderr;
  return m;
}

function withBytes(m, bytes, fn) {
  const p = bytes.length ? m._malloc(bytes.length) >>> 0 : 0;
  if (bytes.length) m.HEAPU8.set(bytes, p);
  try { return fn(p, bytes.length); } finally { if (p) m._free(p); }
}

function readOut(m, pp, lp) {
  const p = m.HEAPU32[pp >>> 2] >>> 0;
  const n = m.HEAPU32[lp >>> 2] >>> 0;
  if (!p) return null;
  const copy = m.HEAPU8.slice(p, p + n);
  m._cs_free(p);
  return copy;
}

// Raw call: jobText (string|Uint8Array) + blob, with explicit lengths allowed to lie.
function sliceRaw(m, jobText, blob, { jobLenOverride, blobLenOverride } = {}) {
  const jobBytes = typeof jobText === 'string' ? new TextEncoder().encode(jobText) : jobText;
  const progress = [];
  m.csProgress = (pct, msg) => progress.push([pct, msg]);
  const outs = m._malloc(16) >>> 0;
  m.HEAPU32.fill(0, outs >>> 2, (outs >>> 2) + 4);
  let rc;
  try {
    rc = withBytes(m, jobBytes, (jp, jl) =>
      withBytes(m, blob, (bp, bl) =>
        m._cs_slice(jp, jobLenOverride ?? jl, bp, blobLenOverride ?? bl, outs, outs + 4, outs + 8, outs + 12)));
    const gcode = readOut(m, outs, outs + 4);
    const reportBytes = readOut(m, outs + 8, outs + 12);
    const report = reportBytes ? JSON.parse(new TextDecoder().decode(reportBytes)) : null;
    return { rc, gcode: gcode ? new TextDecoder().decode(gcode) : null, report, progress };
  } finally {
    m._free(outs);
    delete m.csProgress;
  }
}

const slice = (m, { job, blob }) => sliceRaw(m, JSON.stringify(job), blob);

function evalCond(m, expr, config) {
  const e = new TextEncoder().encode(expr);
  const c = new TextEncoder().encode(typeof config === 'string' ? config : JSON.stringify(config));
  return withBytes(m, e, (ep, el) => withBytes(m, c, (cp, cl) => m._cs_eval_condition(ep, el, cp, cl)));
}

function describe(m) {
  const outs = m._malloc(8) >>> 0;
  try {
    const rc = m._cs_describe_config(outs, outs + 4);
    assert.equal(rc, 0, 'cs_describe_config rc');
    return JSON.parse(new TextDecoder().decode(readOut(m, outs, outs + 4)));
  } finally { m._free(outs); }
}


const T = (o, n) => { const r = o; return r; };
function box(x0, y0, z0, x1, y1, z1) {
  const p = [x0,y0,z0, x1,y0,z0, x1,y1,z0, x0,y1,z0, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1];
  const f = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7];
  return { positions: new Float32Array(p), indices: new Uint32Array(f) };
}
function merge(a, b) {
  const nv = a.positions.length / 3;
  const pos = new Float32Array(a.positions.length + b.positions.length); pos.set(a.positions); pos.set(b.positions, a.positions.length);
  const idx = new Uint32Array(a.indices.length + b.indices.length); idx.set(a.indices); idx.set(b.indices.map((i) => i + nv), a.indices.length);
  return { positions: pos, indices: idx };
}
const dir = path.resolve(process.argv[2] ?? path.join(ROOT, 'build-release'));
const m = await loadModule(dir);
let failed = 0;
const ok = (c, msg) => { console.log(`${c ? 'PASS' : 'FAIL'} ${msg}`); if (!c) failed++; };
const count = (g, re) => (g.match(re) || []).length;
const run = (objects, cfg = {}, options = {}) => { const r = slice(m, buildJob(objects, { config: cfg, options })); if (r.rc !== 0) console.log('  report:', JSON.stringify(r.report?.error ?? r.report).slice(0, 400)); return r; };

// Support painting: a mushroom (stem + overhanging cap); auto supports off.
const mush = merge(box(0, 0, 0, 10, 10, 20), box(-15, -15, 20, 25, 25, 26));
const at = [110, 100, 0];
const tr = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, ...at, 1];
const supCfg = { support_material: 1, support_material_auto: 0 };
const plain = run([{ name: 'mush', mesh: mush, transform: tr }], supCfg);
const capTris = Array.from({ length: 12 }, (_, i) => [12 + i, '4']);
const painted = run([{ name: 'mush', mesh: mush, transform: tr, paint: { support: capTris } }], supCfg);
const sp = (r) => count(r.gcode ?? '', /;TYPE:Support material/g);
ok(plain.rc === 0 && painted.rc === 0, 'support paint slices');
ok(sp(plain) === 0 && sp(painted) > 0, `support enforcer paint → support (plain ${sp(plain)}, painted ${sp(painted)})`);

// Color painting: top face of a cube with extruder 2.
const c = box(0, 0, 0, 20, 20, 20);
const two = { nozzle_diameter: [0.4, 0.4], filament_diameter: [1.75, 1.75], temperature: [215, 215], filament_density: [1.24, 1.24], filament_cost: [25, 25], extruder_colour: ['#FF0000', '#00FF00'], wipe_tower: 1, use_relative_e_distances: 1, layer_gcode: 'G92 E0' };
const topTris = [[2, '8'], [3, '8'], [4, '8'], [5, '8'], [6, '8'], [7, '8'], [8, '8'], [9, '8'], [10, '8'], [11, '8']];
const col = run([{ name: 'c', mesh: c, transform: tr, paint: { color: topTris } }], two);
const tchanges = count(col.gcode ?? '', /^T1\b/gm);
ok(col.rc === 0 && tchanges > 0, `color paint → tool changes to T1 (${tchanges})`);

// Variable layer height: 0.2 below 10 mm, 0.1 above.
const uni = run([{ name: 'c', mesh: c, transform: tr }]);
const layers = (r) => count(r.gcode ?? '', /^;LAYER_CHANGE/gm);
const prof = run([{ name: 'c', mesh: c, transform: tr, layerHeightProfile: [0, 0.2, 10, 0.2, 10.1, 0.1, 20, 0.1] }]);
ok(prof.rc === 0 && layers(prof) > layers(uni) + 30, `layer height profile (${layers(uni)} → ${layers(prof)} layers)`);

// Height range modifier with its own layer height, and one without (defaults to the print's).
const rng = run([{ name: 'c', mesh: c, transform: tr, layerRanges: [{ min: 10, max: 20, config: { layer_height: 0.1 } }, { min: 2, max: 4, config: { fill_density: '50%' } }] }]);
ok(rng.rc === 0 && layers(rng) > layers(uni) + 30, `height range layer_height (${layers(rng)} layers)`);

// Plate custom G-code: colour change + pause.
const cg = run([{ name: 'c', mesh: c, transform: tr }], { color_change_gcode: 'M600', pause_print_gcode: 'M601' },
  { customGcodes: [{ z: 10, type: 'color_change', extruder: 1, color: '#00FF00' }, { z: 15, type: 'pause', extra: 'check' }] });
ok(cg.rc === 0 && count(cg.gcode ?? '', /^M600/gm) === 1 && count(cg.gcode ?? '', /^M601/gm) === 1, `custom G-code: M600 ${count(cg.gcode ?? '', /^M600/gm)}, M601 ${count(cg.gcode ?? '', /^M601/gm)}`);

// Bad paint input is ignored, not a crash.
const bad = run([{ name: 'c', mesh: c, transform: tr, paint: { support: [[-1, '4'], [9999, '4'], ['x', 1], [0, '']], color: 'nope' } }]);
ok(bad.rc === 0, 'malformed paint is ignored');
process.exit(failed ? 1 : 0);
