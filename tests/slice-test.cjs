#!/usr/bin/env node
// Baseline slicing test for preflight-wasm.
//
// Loads ../build-wasm/slicer.js (or $PREFLIGHT_WASM_DIR/slicer.js), runs
// orc_slice on tests/fixtures/cylinder.stl, writes the produced G-code
// to a file, and exits 0 on success / non-zero on any failure.
//
// Usage:
//   node tests/slice-test.cjs                       # writes /tmp/preflight-wasm-test.gcode
//   PREFLIGHT_WASM_OUT=/tmp/out.gcode node tests/slice-test.cjs
//   PREFLIGHT_WASM_DIR=../some/build node tests/slice-test.cjs
//
// Designed to be invoked from CI. Emits structured pass/fail lines on stdout
// so a downstream verifier (verify-gcode.cjs) can be chained.

const fs = require('fs');
const path = require('path');

const repoRoot   = path.resolve(__dirname, '..');
const wasmDir    = process.env.PREFLIGHT_WASM_DIR || path.join(repoRoot, 'build-wasm');
const stlPath    = path.join(__dirname, 'fixtures', 'cylinder.stl');
const outPath    = process.env.PREFLIGHT_WASM_OUT || '/tmp/preflight-wasm-test.gcode';
const slicerJs   = path.join(wasmDir, 'slicer.js');

if (!fs.existsSync(slicerJs)) {
  console.error(`FAIL: slicer.js not found at ${slicerJs}`);
  console.error('Build it first: NPROC=4 bash scripts/build-wasm.sh');
  process.exit(2);
}

const stl = fs.readFileSync(stlPath);
console.log(`stl_bytes ${stl.length}`);

// Load the slicer module via Node's CJS loader. slicer.js is UMD-style;
// the footer assigns to module.exports when require is in scope, which it
// is once we mirror the file to a .cjs extension. We mirror to a tempfile
// instead of reading from build-wasm/ directly because the build output
// uses the .js extension (the package.json may or may not declare "type":
// "commonjs" in a checked-in build).
const os = require('os');
const tmpCjs = path.join(os.tmpdir(), `preflight-slicer-${process.pid}.cjs`);
fs.copyFileSync(slicerJs, tmpCjs);
process.on('exit', () => { try { fs.unlinkSync(tmpCjs); } catch {} });

const factory = require(tmpCjs);
if (typeof factory !== 'function') {
  console.error('FAIL: PreflightModule factory not exported by slicer.js');
  process.exit(2);
}

const start = Date.now();

factory({
  locateFile: f => path.join(wasmDir, f),
  print:    () => {},
  printErr: m => process.stderr.write(`[wasm] ${m}\n`),
}).then(m => {
  const inPtr     = m._malloc(stl.length);
  const outPtrPtr = m._malloc(4);
  const outLenPtr = m._malloc(4);
  if (!inPtr || !outPtrPtr || !outLenPtr) {
    console.error('FAIL: malloc returned 0');
    process.exit(3);
  }
  m.HEAPU8.set(stl, inPtr);
  m.setValue(outPtrPtr, 0, 'i32');
  m.setValue(outLenPtr, 0, 'i32');

  // empty init payload — bridge falls back to bundled defaults
  m._orc_init(0, 0);

  let rc;
  try {
    rc = m._orc_slice(inPtr, stl.length, outPtrPtr, outLenPtr);
  } catch (e) {
    console.error(`FAIL: orc_slice threw: ${e && e.message || e}`);
    if (e && e.stack) console.error(e.stack);
    process.exit(4);
  }

  if (rc !== 0) {
    console.error(`FAIL: orc_slice returned non-zero rc=${rc}`);
    process.exit(5);
  }

  const gp = m.getValue(outPtrPtr, 'i32') >>> 0;
  const gl = m.getValue(outLenPtr, 'i32') >>> 0;
  if (!gp || !gl) {
    console.error(`FAIL: orc_slice returned empty gcode (ptr=${gp} len=${gl})`);
    process.exit(6);
  }

  const gcode = Buffer.from(m.HEAPU8.subarray(gp, gp + gl));
  fs.writeFileSync(outPath, gcode);

  m._orc_free(gp);
  m._free(inPtr);
  m._free(outPtrPtr);
  m._free(outLenPtr);

  const elapsedMs = Date.now() - start;
  console.log(`gcode_bytes ${gl}`);
  console.log(`gcode_path ${outPath}`);
  console.log(`elapsed_ms ${elapsedMs}`);
  console.log('PASS slice');
}).catch(e => {
  console.error(`FAIL: module load/run rejected: ${e && e.message || e}`);
  if (e && e.stack) console.error(e.stack);
  process.exit(7);
});
