#!/usr/bin/env node
// End-to-end tests for the preFlight engine module against the Cubby Slicer
// engine ABI (cubby-slicer/docs/ENGINE-CONTRACT.md).
//
//   node tests/cs-slice-test.mjs [buildDir ...]     (default: build-release build-debug, whichever exist)
//   QUICK=1  skips the 200k-triangle mesh on debug builds
//
// Exit code 0 = all passed.
import { readFile, access } from 'node:fs/promises';
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

// ------------------------------------------------------------- G-code checks
function analyzeGcode(g) {
  const lines = g.split('\n');
  let layerChanges = 0, extrusions = 0, maxLayerZ = 0, z = 0, maxMoveZ = 0;
  for (const line of lines) {
    if (line.startsWith(';LAYER_CHANGE')) layerChanges++;
    const zm = /^;Z:([\d.]+)/.exec(line);
    if (zm) maxLayerZ = Math.max(maxLayerZ, +zm[1]);
    if (/^G[01]\s/.test(line)) {
      const zz = /\sZ(-?[\d.]+)/.exec(line);
      if (zz) { z = +zz[1]; maxMoveZ = Math.max(maxMoveZ, z); }
      if (/\sE[\d.]/.test(line) && /\s[XY]-?[\d.]/.test(line)) extrusions++;
    }
  }
  return { lines: lines.length, layerChanges, extrusions, maxLayerZ, maxMoveZ };
}

function checkSlice(res, { expectHeight, label }) {
  assert.equal(res.rc, 0, `${label}: rc=${res.rc} error=${res.report?.error}`);
  assert.ok(res.report && res.report.ok === true, `${label}: report.ok`);
  assert.equal(res.report.error, null, `${label}: report.error`);
  const s = res.report.stats;
  assert.ok(s, `${label}: stats`);
  assert.ok(s.printTimeSec > 1, `${label}: printTimeSec ${s.printTimeSec}`);
  assert.ok(Array.isArray(s.filamentMm) && s.filamentMm[0] > 10, `${label}: filamentMm ${s.filamentMm}`);
  assert.ok(s.filamentCm3[0] > 0 && s.filamentG[0] > 0 && s.filamentCost[0] > 0, `${label}: filament stats`);
  assert.ok(s.layers > 2, `${label}: layers ${s.layers}`);
  assert.ok(Math.abs(s.maxZ - expectHeight) <= 0.25, `${label}: maxZ ${s.maxZ} vs ${expectHeight}`);
  assert.ok(res.report.timings.processMs >= 0 && res.report.timings.exportMs >= 0, `${label}: timings`);
  assert.ok(res.gcode && res.gcode.length > 1000, `${label}: gcode length`);
  const a = analyzeGcode(res.gcode);
  assert.ok(a.layerChanges >= s.layers * 0.9, `${label}: layer changes ${a.layerChanges} vs ${s.layers}`);
  assert.ok(a.extrusions > s.layers * 4, `${label}: extrusion moves ${a.extrusions}`);
  assert.ok(Math.abs(a.maxLayerZ - s.maxZ) < 0.01, `${label}: ;Z max ${a.maxLayerZ} vs stats ${s.maxZ}`);
  assert.ok(a.maxMoveZ <= s.maxZ + 15, `${label}: max move Z ${a.maxMoveZ}`);
  // progress hook: monotone, ends at 100
  const pcts = res.progress.map((p) => p[0]);
  assert.ok(pcts.length > 3, `${label}: progress callbacks`);
  for (let i = 1; i < pcts.length; i++) assert.ok(pcts[i] >= pcts[i - 1], `${label}: progress monotone`);
  assert.equal(pcts[pcts.length - 1], 100, `${label}: progress ends at 100`);
  return a;
}

const stripVolatile = (g) => g.split('\n').filter((l) => !/generated by|^; (estimated|model printing time)/i.test(l)).join('\n');

// ------------------------------------------------------------------ tests
async function runSuite(buildDir) {
  const variant = path.basename(buildDir);
  const isDebug = /debug/.test(variant);
  const results = [];
  const t = async (name, fn) => {
    const t0 = performance.now();
    try {
      await fn();
      results.push({ name, ok: true, ms: performance.now() - t0 });
      console.log(`  ok   ${name} (${((performance.now() - t0) / 1000).toFixed(1)}s)`);
    } catch (e) {
      results.push({ name, ok: false, err: e });
      console.log(`  FAIL ${name}: ${e.stack || e}`);
    }
  };

  console.log(`\n== ${buildDir}`);
  const m = await loadModule(buildDir);

  const cylinder = parseStl(await readFile(path.join(ROOT, 'tests/fixtures/cylinder.stl')));
  const cyl = (() => { // bbox for expected height
    let lo = Infinity, hi = -Infinity;
    for (let i = 2; i < cylinder.positions.length; i += 3) { lo = Math.min(lo, cylinder.positions[i]); hi = Math.max(hi, cylinder.positions[i]); }
    return { h: hi - lo };
  })();

  await t('cs_version', () => {
    const v = JSON.parse(m.UTF8ToString(m._cs_version()));
    assert.equal(v.engine, 'preflight');
    assert.equal(v.version, '1.3.0');
    assert.equal(v.bridge, 1);
    assert.equal(v.build, isDebug ? 'debug' : 'release');
  });

  await t('schema', () => {
    const s = describe(m);
    assert.equal(s.engine, 'preflight');
    const o = s.options;
    const n = Object.keys(o).length;
    assert.ok(n > 300, `only ${n} options`);
    const expect = {
      layer_height: ['float', 'print'], perimeters: ['int', 'print'], fill_density: ['percent', 'print'],
      fill_pattern: ['enum', 'print'], nozzle_diameter: ['floats', 'machine'], bed_shape: ['points', 'machine'],
      filament_diameter: ['floats', 'filament'], temperature: ['ints', 'filament'], start_gcode: ['string', 'machine'],
      first_layer_height: ['floatOrPercent', 'print'], wipe: ['bools', 'machine'], compatible_printers_condition: ['string', 'print'],
    };
    for (const [k, [type, scope]] of Object.entries(expect)) {
      assert.ok(o[k], `schema missing ${k}`);
      assert.equal(o[k].type, type, `${k}.type`);
      assert.equal(o[k].scope, scope, `${k}.scope`);
    }
    assert.ok(o.fill_pattern.enumValues.includes('gyroid') && o.fill_pattern.enumLabels.length === o.fill_pattern.enumValues.length);
    assert.equal(o.start_gcode.multiline, true);
    assert.ok(['simple', 'advanced', 'expert'].includes(o.layer_height.mode));
    const valid = new Set(['bool', 'int', 'float', 'percent', 'floatOrPercent', 'string', 'enum', 'point', 'point3',
      'bools', 'ints', 'floats', 'percents', 'floatsOrPercents', 'strings', 'points', 'enums']);
    for (const [k, d] of Object.entries(o)) {
      assert.ok(valid.has(d.type), `${k}: bad type ${d.type}`);
      assert.ok(['print', 'filament', 'machine', 'object', 'other'].includes(d.scope), `${k}: scope`);
      assert.equal(typeof d.default, 'string', `${k}: default`);
      assert.ok(d.min === null || Number.isFinite(d.min));
      assert.ok(d.max === null || Number.isFinite(d.max));
    }
    const scopes = {};
    for (const d of Object.values(o)) scopes[d.scope] = (scopes[d.scope] || 0) + 1;
    console.log(`       ${n} options, scopes ${JSON.stringify(scopes)}`);
  });

  await t('cs_eval_condition', () => {
    const cfg = { printer_notes: 'PRINTER_VENDOR_PRUSA3D\nPRINTER_MODEL_MK4', nozzle_diameter: ['0.4'], printer_model: 'MK4' };
    assert.equal(evalCond(m, 'printer_notes=~/.*PRINTER_VENDOR_PRUSA3D.*/', cfg), 1);
    assert.equal(evalCond(m, 'printer_notes=~/.*PRINTER_MODEL_MINI.*/', cfg), 0);
    assert.equal(evalCond(m, 'nozzle_diameter[0]==0.4', cfg), 1);
    assert.equal(evalCond(m, 'nozzle_diameter[0]==0.6 and printer_model=="MK4"', cfg), 0);
    assert.equal(evalCond(m, 'printer_model=="MK4" or nozzle_diameter[0]>1', cfg), 1);
    assert.equal(evalCond(m, '', cfg), 1);
    assert.ok(evalCond(m, '((( nozzle_diameter', cfg) < 0);
    assert.ok(evalCond(m, 'no_such_variable == 3', cfg) < 0);
    assert.ok(evalCond(m, 'nozzle_diameter[0]==0.4', '{not json') < 0);
  });

  await t('cube 20mm', () => {
    const r = slice(m, buildJob([{ name: 'cube', mesh: cube(20), transform: translate(115, 95, 0) }]));
    const a = checkSlice(r, { expectHeight: 20, label: 'cube' });
    console.log(`       ${r.report.stats.layers} layers, ${a.extrusions} extrusions, ${(r.gcode.length / 1024).toFixed(0)} KiB, ${r.report.stats.printTimeSec.toFixed(0)} s, warnings ${r.report.warnings.length}`);
  });

  await t('cylinder.stl fixture', () => {
    const r = slice(m, buildJob([{ name: 'cylinder', mesh: cylinder, transform: translate(125, 105, 0) }]));
    checkSlice(r, { expectHeight: cyl.h, label: 'cylinder' });
  });

  await t('multi-object, non-identity transforms, per-object config', () => {
    // cube: rotated 30deg about Z, scaled, floating 7mm above the bed (dropToBed pulls it down)
    const tA = mul(translate(70, 80, 7), mul(rotZ(30), scale(1.2, 0.8, 1.5)));
    // cylinder: tipped 90deg about X (lies on its side), mirrored in X, moved to the other side
    const tB = mul(translate(170, 110, 30), mul(scale(-1, 1, 1), rotX(90)));
    const job = buildJob([
      { name: 'A', mesh: cube(20), transform: tA, config: { perimeters: 4, fill_density: '30%' } },
      { name: 'B', mesh: cylinder, transform: tB },
    ]);
    const r = slice(m, job);
    // tallest: cube 20*1.5=30mm; lying cylinder height = its diameter
    let lo = Infinity, hi = -Infinity;
    const p = cylinder.positions;
    for (let i = 0; i < p.length; i += 3) { lo = Math.min(lo, p[i + 1]); hi = Math.max(hi, p[i + 1]); }
    checkSlice(r, { expectHeight: Math.max(30, hi - lo), label: 'multi' });
    assert.ok(!r.report.substitutions.length, `unexpected substitutions ${r.report.substitutions}`);
  });

  if (!(isDebug && process.env.QUICK)) {
    await t('large mesh (torus, 200k triangles)', () => {
      const mesh = torus(40, 12, 500, 200);
      assert.equal(mesh.indices.length / 3, 200000);
      const r = slice(m, buildJob([{ name: 'torus', mesh, transform: translate(125, 105, 12) }], { config: { layer_height: 0.3, first_layer_height: 0.3 } }));
      checkSlice(r, { expectHeight: 24, label: 'torus' });
      console.log(`       torus: ${r.report.stats.layers} layers, process ${r.report.timings.processMs.toFixed(0)} ms, export ${r.report.timings.exportMs.toFixed(0)} ms, heap ${(m.HEAPU8.length / 2 ** 20).toFixed(0)} MiB`);
    });
  }

  await t('substitutions + unknown keys are reported, slice still succeeds', () => {
    const r = slice(m, buildJob([{ name: 'cube', mesh: cube(10), transform: translate(100, 100, 0) }],
      { config: { fill_pattern: 'no_such_pattern', some_orca_only_key: '1', wall_loops: 3 } }));
    checkSlice(r, { expectHeight: 10, label: 'subst' });
    assert.ok(r.report.substitutions.some((s) => s.startsWith('fill_pattern')), JSON.stringify(r.report.substitutions));
    const w = r.report.warnings.find((x) => x.code === 'unknown_options');
    assert.ok(w && /some_orca_only_key/.test(w.message) && /wall_loops/.test(w.message), JSON.stringify(r.report.warnings));
  });

  await t('validation error -> rc!=0 with message', () => {
    const r = slice(m, buildJob([{ name: 'cube', mesh: cube(10), transform: translate(100, 100, 0) }],
      { config: { layer_height: 0.9, nozzle_diameter: [0.4] } }));
    assert.notEqual(r.rc, 0);
    assert.equal(r.report.ok, false);
    assert.ok(typeof r.report.error === 'string' && r.report.error.length > 5, r.report.error);
    assert.equal(r.gcode, null);
    console.log(`       error: ${r.report.error.split('\n')[0]}`);
  });

  await t('5 back-to-back slices on one instance (deterministic)', () => {
    const job = buildJob([{ name: 'cube', mesh: cube(15), transform: translate(100, 100, 0) }]);
    let first = null;
    for (let i = 0; i < 5; i++) {
      const r = slice(m, job);
      checkSlice(r, { expectHeight: 15, label: `repeat ${i}` });
      const g = stripVolatile(r.gcode);
      if (first === null) first = g;
      else assert.equal(g.length, first.length, `repeat ${i}: gcode differs from first run`);
    }
  });

  await t('malformed jobs -> clean error reports, no trap', () => {
    const good = buildJob([{ name: 'cube', mesh: cube(10), transform: translate(100, 100, 0) }]);
    const mutate = (fn) => { const j = structuredClone(good.job); fn(j); return JSON.stringify(j); };
    const cases = [
      ['invalid JSON', '{"config": {', good.blob],
      ['not an object', '[1,2,3]', good.blob],
      ['no objects', mutate((j) => { j.objects = []; }), good.blob],
      ['vertex range out of bounds', mutate((j) => { j.objects[0].vertexOffset = good.blob.length; }), good.blob],
      ['index range out of bounds', mutate((j) => { j.objects[0].indexOffset = good.blob.length - 8; }), good.blob],
      ['unaligned offset', mutate((j) => { j.objects[0].vertexOffset = 2; }), good.blob],
      ['negative offset', mutate((j) => { j.objects[0].vertexOffset = -12; }), good.blob],
      ['huge vertexCount', mutate((j) => { j.objects[0].vertexCount = 2 ** 31; }), good.blob],
      ['huge triangleCount (overflow)', mutate((j) => { j.objects[0].triangleCount = 2 ** 53; }), good.blob],
      ['float count', mutate((j) => { j.objects[0].vertexCount = 8.5; }), good.blob],
      ['index out of range', null, (() => { const b = good.blob.slice(); new Uint32Array(b.buffer, good.job.objects[0].indexOffset, 36)[5] = 8; return b; })()],
      ['NaN vertex', null, (() => { const b = good.blob.slice(); new Float32Array(b.buffer, 0, 24)[4] = NaN; return b; })()],
      ['Inf vertex', null, (() => { const b = good.blob.slice(); new Float32Array(b.buffer, 0, 24)[7] = Infinity; return b; })()],
      ['transform wrong length', mutate((j) => { j.objects[0].transform = [1, 0, 0]; }), good.blob],
      ['transform null entry', mutate((j) => { j.objects[0].transform[3] = null; }), good.blob],
      ['singular transform', mutate((j) => { j.objects[0].transform = new Array(16).fill(0); }), good.blob],
      ['config not an object', mutate((j) => { j.config = 'layer_height=0.2'; }), good.blob],
      ['all-degenerate mesh', null, (() => { const b = good.blob.slice(); new Uint32Array(b.buffer, good.job.objects[0].indexOffset, 36).fill(0); return b; })()],
      ['object far outside bed', mutate((j) => { j.objects[0].transform = translate(5000, 5000, 0); }), good.blob],
      ['empty blob', JSON.stringify(good.job), new Uint8Array(0)],
    ];
    for (const [name, jobText, blob] of cases) {
      const r = sliceRaw(m, jobText ?? JSON.stringify(good.job), blob);
      assert.notEqual(r.rc, 0, `${name}: expected failure, got rc=0`);
      assert.ok(r.report && r.report.ok === false && typeof r.report.error === 'string' && r.report.error.length, `${name}: report ${JSON.stringify(r.report)}`);
      assert.equal(r.gcode, null, `${name}: no gcode on failure`);
    }
    // lying lengths: blob_len larger than the job's references is fine, smaller must be rejected
    const r1 = sliceRaw(m, JSON.stringify(good.job), good.blob, { blobLenOverride: 16 });
    assert.notEqual(r1.rc, 0);
    const r2 = sliceRaw(m, JSON.stringify(good.job), good.blob, { jobLenOverride: 5 });
    assert.notEqual(r2.rc, 0);
    const r3 = sliceRaw(m, JSON.stringify(good.job), good.blob, { jobLenOverride: 0 });
    assert.notEqual(r3.rc, 0);
    // null output pointers must not crash
    const jb = new TextEncoder().encode(JSON.stringify(good.job));
    const rc = withBytes(m, jb, (jp, jl) => withBytes(m, good.blob, (bp, bl) => m._cs_slice(jp, jl, bp, bl, 0, 0, 0, 0)));
    assert.equal(rc, 0, 'slice with null outputs');
    // instance still healthy afterwards
    checkSlice(slice(m, good), { expectHeight: 10, label: 'after malformed' });
  });

  await t('fresh instances (3x) each slice', async () => {
    const job = buildJob([{ name: 'cyl', mesh: cylinder, transform: translate(100, 100, 0) }]);
    for (let i = 0; i < 3; i++) {
      const mi = await loadModule(buildDir);
      checkSlice(slice(mi, job), { expectHeight: cyl.h, label: `fresh ${i}` });
    }
  });

  const failed = results.filter((r) => !r.ok);
  console.log(`== ${variant}: ${results.length - failed.length}/${results.length} passed`);
  return failed.length === 0;
}

const dirs = process.argv.slice(2);
if (!dirs.length) {
  for (const d of ['build-release', 'build-debug']) {
    try { await access(path.join(ROOT, d, 'slicer.wasm')); dirs.push(path.join(ROOT, d)); } catch {}
  }
}
if (!dirs.length) { console.error('no build dirs found'); process.exit(2); }
let ok = true;
for (const d of dirs) ok = (await runSuite(path.resolve(d))) && ok;
process.exit(ok ? 0 : 1);
