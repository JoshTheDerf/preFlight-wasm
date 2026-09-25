#!/usr/bin/env node
// Writes <buildDir>/schema.json (cs_describe_config) and <buildDir>/version.json
// (cs_version) by instantiating the freshly built module in node.
//   node scripts/gen-schema.mjs build-release
import { readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const buildDir = path.resolve(process.argv[2] ?? 'build-release');
const { default: factory } = await import(pathToFileURL(path.join(buildDir, 'slicer.mjs')).href);
const wasmBinary = await readFile(path.join(buildDir, 'slicer.wasm'));
const m = await factory({ wasmBinary, print: () => {}, printErr: (s) => process.stderr.write(s + '\n') });

const version = JSON.parse(m.UTF8ToString(m._cs_version()));

const outPtr = m._malloc(4);
const lenPtr = m._malloc(4);
const rc = m._cs_describe_config(outPtr, lenPtr);
if (rc !== 0) throw new Error(`cs_describe_config rc=${rc}`);
const p = m.HEAPU32[outPtr >>> 2] >>> 0;
const n = m.HEAPU32[lenPtr >>> 2] >>> 0;
const text = new TextDecoder().decode(m.HEAPU8.subarray(p, p + n));
m._cs_free(p);
m._free(outPtr);
m._free(lenPtr);

const schema = JSON.parse(text);
const count = Object.keys(schema.options).length;
if (count < 100) throw new Error(`schema has only ${count} options`);
await writeFile(path.join(buildDir, 'schema.json'), JSON.stringify(schema));
await writeFile(path.join(buildDir, 'version.json'), JSON.stringify(version, null, 2) + '\n');
console.log(`schema.json: ${count} options; version.json: ${JSON.stringify(version)}`);
