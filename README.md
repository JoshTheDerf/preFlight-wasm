# preFlight WebAssembly engine

A WebAssembly build of [preFlight](https://github.com/oozebot/preFlight)
**v1.3.0** (a PrusaSlicer derivative), packaged as a slicing engine for
[Cubby Slicer](../../cubby-slicer). It implements the engine-neutral C ABI in
`cubby-slicer/docs/ENGINE-CONTRACT.md` (`cs_version`, `cs_describe_config`,
`cs_slice`, `cs_eval_condition`, `cs_free`).

See [PORTING.md](PORTING.md) for what was changed in preFlight, what is
disabled, and which workarounds remain.

## Layout

```
patches/preflight-wasm.patch   applied to ../preflight (tag v1.3.0); regenerate with
                               `git -C ../preflight diff > patches/preflight-wasm.patch`
wasm/CMakeLists.txt            superbuild: flags, dependency lookup, preFlight options
wasm/cmake/                    Find modules: TBB (-> shim), ZLIB/PNG/JPEG (-> Emscripten ports), cereal
wasm/tbb_shim/                 sequential oneTBB replacement (the only "shim" in the build)
bridge/cs_bridge.cpp           engine ABI implementation (uses ../wasm-bridge/cs_common.hpp)
bridge/CMakeLists.txt          the `slicer` target -> slicer.mjs + slicer.wasm, link flags
scripts/build-wasm.sh          patch + configure + build + schema generation
scripts/gen-schema.mjs         writes schema.json / version.json from the built module
tests/cs-slice-test.mjs        end-to-end + robustness tests (release and debug builds)
```

## Building

Prerequisites (shared with the Orca port): `../wasm-deps` with the toolchain
and dependency prefix, and a preFlight checkout at `../preflight`:

```bash
bash ../wasm-deps/build-deps.sh all               # Eigen, CGAL, cereal, GMP/MPFR, NLopt, Boost, ...
bash ../wasm-deps/build-deps.sh preflight-extras  # Qhull, expat, heatshrink, nlohmann_json, nanosvg
git -C ../preflight checkout v1.3.0

bash scripts/build-wasm.sh                        # release -> build-release/
BUILD_VARIANT=debug bash scripts/build-wasm.sh    # debug   -> build-debug/  (-O1 -g2 SAFE_HEAP ASSERTIONS=2)
node tests/cs-slice-test.mjs                      # runs against every build dir that exists
```

`NPROC` (default 3) caps parallel compile jobs; ccache is used when on PATH
(`wasm-deps/env.sh` sets it up). The script applies the patch idempotently and
refuses to build if the checkout has diverged from it.

Artifacts (`build-<variant>/`):

| file | purpose |
|---|---|
| `slicer.mjs` | ES module; default export is the factory `PreflightModule(moduleArgs)` |
| `slicer.wasm` | engine |
| `schema.json` | `cs_describe_config` output |
| `version.json` | `cs_version` output |

Stage them into the web app with `cubby-slicer/scripts/fetch-preflight-wasm.sh`.

## Runtime notes (for the web worker)

* Factory: `import PreflightModule from './slicer.mjs'`; pass `wasmBinary`
  or `instantiateWasm`/`locateFile` as usual. No `.data` file, no preloaded
  files: the engine reads nothing from the virtual FS.
* Progress: set `module.csProgress = (percent, message) => {}` before calling
  `_cs_slice`; it is called synchronously from inside the slice (0-100,
  monotone, ends at 100).
* Exports: `_cs_version _cs_describe_config _cs_slice _cs_eval_condition
  _cs_free _malloc _free`, runtime `HEAPU8 HEAP32 HEAPU32 UTF8ToString
  stringToUTF8 lengthBytesUTF8 FS`. Re-read `HEAPU8` after every call (memory
  grows) and treat pointers as unsigned (`>>> 0`; heap can exceed 2 GB).
* Engine log output (Boost.Log errors only) goes to `printErr`.
* Single-threaded; run it in a Worker.

## License

AGPL-3.0-or-later, inherited from preFlight / PrusaSlicer.
