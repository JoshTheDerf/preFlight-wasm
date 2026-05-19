# preFlight → WASM porting log

## Status: ✅ Builds and loads in Node.js

A clean build produces three artifacts in `build-wasm/`:

| File | Size | Purpose |
|---|---|---|
| `slicer.js`   | ~265 KB | Emscripten loader (ES module, exports `PreflightModule`) |
| `slicer.wasm` | ~7.3 MB | preFlight's `libslic3r` + bridge compiled to WebAssembly |
| `slicer.data` | ~71 MB  | Preloaded `resources/` bundle (profiles, shapes, icons) |

Sanity check (Node 22):

```bash
cd build-wasm
node -e "require('./slicer.js')().then(m => console.log(Object.keys(m).filter(k=>k.startsWith('_orc'))))"
# → [ '_orc_describe_config', '_orc_init', '_orc_slice', '_orc_free', '_orc_decode_exception' ]
```

The C entry points (`orc_init`, `orc_slice`, `orc_describe_config`, `orc_free`, `orc_decode_exception`) are exported and the 256 MB heap is allocated.

Not yet exercised end-to-end:
- An actual `orc_slice(stl_bytes, ...)` invocation with a small mesh — slicer hasn't been tested against a real model, only the module-load path.
- The bundled `web/` frontend from orcaslicer-wasm hasn't been wired to load `PreflightModule`.

## Building

```bash
cd /server/thederf/slicers/preflight-wasm

# 1. Activate emsdk (auto-installed by setup.sh)
source deps/emsdk/emsdk_env.sh

# 2. Use the bundled cmake
export PATH="$PWD/deps/cmake-3.30.5-linux-x86_64/bin:$PATH"

# 3. (one-time, ~30 min) Build Boost (threading=single, BOOST_LOG_WITHOUT_SYSLOG)
bash deps/boost-wasm/build_boost.sh

# 4. (one-time, ~30 min) Build GMP / MPFR / CGAL 6.1
bash deps/toolchain-wasm/build_math.sh

# 5. Build the WASM slicer (auto-applies patches/preflight-wasm.patch)
#    Cap NPROC to ≤ 4 (each emcc job takes ~500 MB).
NPROC=4 bash scripts/build-wasm.sh -clean
```

ccache is wired into the build (`deps/ccache/`) — subsequent builds that re-run identical compilations hit the cache.

## What the port does

### Bridge (`bridge/preflight_wrap.cpp`)
Adapted from orcaslicer-wasm's wasm_wrap.cpp with these translations:
- includes retargeted to `../../preflight/src/libslic3r/`
- class rename: `GCode` → `GCodeGenerator`
- removed orca-only calls: `set_temporary_dir`, `Print::get_plate_origin`, `GCode::set_gcode_offset`, `DynamicPrintConfig::set_num_filaments`
- `load_stl` signature: 3 args (preflight) instead of 5 (orca)
- `comDevelop` → `comExpert` (preflight enum value)
- `def.enum_keys_map` → `def.enum_def->value_to_index()` (preflight encapsulated enum metadata into `ConfigOptionEnumDef`)
- `build_config_schema()` simplified to skip metadata that preflight doesn't expose
- C entry points kept as `orc_init` / `orc_slice` / `orc_describe_config` for orcaslicer-wasm web frontend compatibility

### Patch (`patches/preflight-wasm.patch`, ~950 lines)
Idempotent, applies cleanly to current preflight `main`. Categories:

**Build system gating (under `EMSCRIPTEN` / `NOT EMSCRIPTEN`):**
- Root `CMakeLists.txt`: make TBB optional; gate CURL / OpenGL / NLopt REQUIRED; gate `find_package(EXPAT)` / `find_package(PNG)` / `find_package(OpenVDB)`.
- `bundled_deps/hidapi/CMakeLists.txt`: Emscripten short-circuit.
- `src/CMakeLists.txt`: gate `slic3r-arrange`, `slic3r-arrange-wrapper`, `libseqarrange`, `occt_wrapper`, Qhull; early `return()` to skip native CLI/GUI.
- `src/libslic3r/CMakeLists.txt`: add `SLIC3R_WITH_OCCT` / `SLIC3R_WITH_OPENCV` options; gate `libjpeg-turbo`; gate `libseqarrange` from link line; `list(REMOVE_ITEM SLIC3R_SOURCES ArrangeHelper.cpp ArrangeHelper.hpp)` under EMSCRIPTEN; define `SLIC3R_NO_SEQARRANGE`.

**Source-level guards:**
- `PNGReadWrite.cpp`: whole file under `#ifdef __EMSCRIPTEN__` with no-op stubs (libpng not built for WASM).
- `TriangleMesh.cpp`: `its_convex_hull` early-returns under EMSCRIPTEN (Qhull unavailable); qhull includes gated.
- `Print.cpp`: `ArrangeHelper.hpp` include + `check_seq_conflict()` call gated.
- `GCode.cpp`: two `tbb::filter<I, O>` parallel_pipeline blocks rewritten as sequential loops (TBB shim lacks templated filter type erasure).
- `GCode/PostProcessor.cpp`: stubbed `run_script()` under EMSCRIPTEN (Boost.Process v2 not in Boost 1.83).
- `PrintObject.cpp`: extend Apple `unique_ptr` `= {}` workaround to Emscripten (same libc++ ambiguity).
- `Utils/DirectoriesUtils.cpp`: add EMSCRIPTEN branch returning `/data` MEMFS path.
- `Geometry/VoronoiUtilsCgal.cpp`: explicit template instantiations re-added inside EMSCRIPTEN branch so `is_voronoi_diagram_planar_angle` linker symbols resolve.
- Several `<unordered_set>` / `<vector>` transitive-include additions for files that previously got them via PCH on native builds: `LayerRegion.cpp`, `ObjectID.cpp`, `SurfaceCollection.cpp`, `Feature/Interlocking/InterlockingGenerator.*`, `ProgressConfig.hpp`, `ShortestPath.hpp`.
- `Model.cpp`: add `<tbb/parallel_for.h>` + `<tbb/blocked_range.h>` (used by `parallel_for` block).
- `SupportSpotsGenerator.cpp`: hoist Eigen `cast<double>()` expressions to named variables (Eigen 3.4 returns `CwiseUnaryOp` from `cast<>()` which doesn't implicitly convert to the `Vec<...>` matrix type the function template expects).
- `utils.cpp` is left alone; the conflicting `boost/log/trivial.hpp` shim was deleted from `wasm_shims/boost/log/` and `wasm_shims/boost_runtime/boost/log/` instead.

### Dependencies

| Dep | Source | How |
|---|---|---|
| Boost 1.83 | `deps/boost-wasm/build_boost.sh` | cross-built static, `threading=single`, `BOOST_LOG_WITHOUT_{SYSLOG,EVENT_LOG,DEBUG_OUTPUT,IPC}` |
| Eigen 3.4 | `deps/eigen/` | header-only |
| nlohmann_json 3.11 | `deps/nlohmann_json/` | single-header, `Findnlohmann_json.cmake` + `nlohmann_jsonConfig.cmake` |
| nanosvg | `deps/nanosvg/` | single-header |
| EXPAT 2.6.3 | `deps/expat-2.6.3/` | tarball + custom minimal `CMakeLists.txt` (xmlparse + xmlrole + xmltok, `XML_POOR_ENTROPY`) |
| heatshrink 0.4.1 | `deps/heatshrink-src/` | tarball + custom `CMakeLists.txt` exposing `heatshrink::heatshrink_dynalloc` |
| GMP / MPFR | `deps/toolchain-wasm/build_math.sh` | cross-built |
| CGAL 6.1 | `deps/toolchain-wasm/build_math.sh` | cross-built (was 5.4 originally; 6.1 needed because preflight uses `AABB_traits_3` + std::optional `property_map<>`) |
| TBB | `wasm/wasm_shims/tbb/` + `oneapi/tbb/` | header-only shim, `namespace oneapi::tbb` with `tbb = oneapi::tbb` alias |
| zlib | emscripten port (`-sUSE_ZLIB=1`) | built-in |
| LibBGCode | preflight's `bundled_deps/libbgcode/` (re-enabled in patch) | source build |
| OCCT, OpenVDB, OpenCV, OpenGL, CURL, NLopt, Qhull, libpng, libjpeg-turbo | gated out / stubbed | INTERFACE IMPORTED targets only |

## Notable workarounds

- **`-O0` link.** wasm-opt asserts internally on aggressive optimization (`binaryen ArenaVector` OOB) with our libslic3r.wasm; -O3 and -O2 both trigger it. Per-TU `-O3` compile flags are preserved, so the .o files are optimized — only the final wasm-opt pass is skipped. Worth re-trying after an emsdk upgrade.
- **TBB shim namespace pattern.** All shim headers use `namespace oneapi::tbb { ... }` with `namespace tbb = oneapi::tbb;` aliased at the bottom; any shim that uses `namespace tbb { ... }` directly will conflict with this alias.
- **Boost.Log namespace mismatch.** Boost.Log's `boost::log::v2*` namespace varies by `BOOST_LOG_NO_THREADS` × `BOOST_LOG_STATIC_LINK`. Static + single-threaded matches at consumer (no `BOOST_HAS_THREADS`) ↔ lib (built with `threading=single`).
- **Bridge → slicer linkage.** The `slicer` executable does **not** re-compile `preflight_wrap.cpp`; it links the already-built `libpreflight_wasm_bridge.a` (with `-Wl,--whole-archive` so the C entry points survive static linking).

## Open issues to verify

1. Run `orc_slice` against a small STL and check the produced G-code is non-empty / parsable.
2. Wire the orcaslicer-wasm web UI (or a minimal HTML page) to load `PreflightModule` and exercise the schema endpoint.
3. The `resources/` preload is 71 MB. Trim it to just what `set_resources_dir` / `set_var_dir` / `set_sys_shapes_dir` actually need.
4. Re-test under a newer emsdk to see if wasm-opt's binaryen assertion is fixed (then we can restore `-O3` at link).
5. Several files were rewritten or significantly modified rather than minimally guarded. If preflight upstream evolves quickly, expect the patch to need re-translation for those files (esp. `GCode.cpp`, `VoronoiUtilsCgal.cpp`).

## What's NOT working / not implemented

- Arrange features (auto-arrange, sequential collision detection).
- Convex hull (Qhull) — `its_convex_hull` returns empty.
- STEP / CAD imports (OCCT off).
- JPEG / PNG thumbnail encoding.
- Binary G-code conversion that actually compresses (LibBGCode binarize is compiled but the heatshrink runtime path is exercised only if you call into it).
- Python pre-processor (`SLIC3R_PYTHON_PREPROCESSOR=OFF`).
- wxWidgets GUI (`SLIC3R_GUI=OFF`).
- Multi-threading (Emscripten built single-threaded — no SharedArrayBuffer / pthread).
