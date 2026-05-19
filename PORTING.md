# preFlight → WASM porting log

## Status: ✅ Slicing works end-to-end

A clean build produces three artifacts in `build-wasm/`:

| File | Size | Purpose |
|---|---|---|
| `slicer.js`   | ~265 KB | Emscripten loader (ES module, exports `PreflightModule`) |
| `slicer.wasm` | ~7.3 MB | preFlight's `libslic3r` + bridge compiled to WebAssembly |
| `slicer.data` | ~71 MB  | Preloaded `resources/` bundle (profiles, shapes, icons) |

Reference test (cylinder.stl from preFlight's shipped resources):

```
$ node tests/slice-test.cjs
STL bytes: 72084
Module loaded, attempting slice...
[wasm] [pflt_slice] start len=72084
[wasm] ... (slice phase 0% → 33%, 13s) ...
[wasm] [pflt_slice] export complete wall_time_ms=10298.82
[wasm] [pflt_slice] retrieved gcode from in-memory object, size=1332851
orc_slice rc= 0
OK gcode len= 1332851
```

The output is valid preFlight G-code: header, layer-change markers,
standard moves, filament stats footer. Confirmed in both Node 22 and in
a browser tab via the cad-project consumer.

## Building

```bash
cd preflight-wasm

# 1. one-time bootstrap (emsdk, portable cmake, ccache, vendored single-headers)
./scripts/setup.sh

# 2. cross-build Boost 1.83 (threading=single, BOOST_LOG_WITHOUT_{SYSLOG,EVENT_LOG,DEBUG_OUTPUT,IPC})
bash deps/boost-wasm/build_boost.sh

# 3. cross-build GMP + MPFR + CGAL 6.1
bash deps/toolchain-wasm/build_math.sh

# 4. clone preFlight sibling
(cd .. && git clone https://github.com/oozebot/preFlight.git preflight)

# 5. build the WASM slicer (auto-applies patches/preflight-wasm.patch)
#    cap NPROC ≤ 4 (each emcc job uses ~500 MB)
NPROC=4 bash scripts/build-wasm.sh
```

ccache is wired in (`deps/ccache/`). Re-runs of identical compilations
hit the cache.

## What the port does

### Bridge (`bridge/preflight_wrap.cpp`)

Originally translated from orcaslicer-wasm's `wasm_wrap.cpp`. preFlight
diverged from OrcaSlicer enough that several translations were needed:

- includes retargeted to `../../preflight/src/libslic3r/`
- class rename: `GCode` → `GCodeGenerator`
- removed orca-only calls: `set_temporary_dir`, `Print::get_plate_origin`,
  `GCode::set_gcode_offset`, `DynamicPrintConfig::set_num_filaments`
- `load_stl` signature: 3 args (preflight) instead of 5 (orca)
- `comDevelop` → `comExpert` (preflight enum value)
- `def.enum_keys_map` → `def.enum_def->value_to_index()` (preflight
  encapsulated enum metadata into `ConfigOptionEnumDef`)
- `build_config_schema()` simplified to skip metadata preflight doesn't expose
- C entry points kept as `orc_init` / `orc_slice` / `orc_describe_config`
  for orcaslicer-wasm web frontend compatibility

**Two important bridge-side fixes for working slicing:**

1. **In-memory G-code retrieval.** preFlight's `do_export()` uses
   memory-based processing: it constructs the G-code in a `GCodeObject`
   buffer and only writes to disk if you pass a `GCodeProcessorResult*`.
   The bridge passes one, then reads `result.gcode_object->text_buffer()`
   for the JS side. The original bridge passed only a path and got back
   an empty file.

2. **Disabled global `operator new`/`operator delete` overrides.** The
   bridge used to install instrumented overrides for failed-alloc debug
   logging. Combined with `-sEMULATE_FUNCTION_POINTER_CASTS=1` and
   `-fexceptions`, these overrides interfered with libcxxabi's exception
   machinery: `__cxa_throw` would resolve a thunk-table entry to function
   index 0 (the trap thunk), surfacing as a "null function" trap during
   G-code finalization. Wrapping the overrides in `#if 0` (so libcxx's
   canonical operator new/delete are used) cleared the trap.

### Patch (`patches/preflight-wasm.patch`, ~940 lines, 27 files)

Idempotent; applies cleanly to a fresh preflight checkout.

**Build system gating (under `EMSCRIPTEN` / `NOT EMSCRIPTEN`):**

- Root `CMakeLists.txt`: make TBB optional; gate `find_package`
  CURL / OpenGL / NLopt / EXPAT / PNG / OpenVDB.
- `bundled_deps/hidapi/CMakeLists.txt`: Emscripten short-circuit.
- `src/CMakeLists.txt`: gate `slic3r-arrange`, `slic3r-arrange-wrapper`,
  `libseqarrange`, `occt_wrapper`, Qhull; early `return()` to skip native
  CLI/GUI.
- `src/libslic3r/CMakeLists.txt`: add `SLIC3R_WITH_OCCT` /
  `SLIC3R_WITH_OPENCV` options; gate `libjpeg-turbo`; gate `libseqarrange`
  from the link line; `list(REMOVE_ITEM SLIC3R_SOURCES ArrangeHelper.cpp
  ArrangeHelper.hpp)` under EMSCRIPTEN; define `SLIC3R_NO_SEQARRANGE`.

**Pipeline rewrites:**

- `GCode.cpp` — both `process_layers()` variants use `tbb::filter<I, O>`
  type erasure that the wasm_shims TBB stub does not provide. Both are
  driven by a hand-rolled sequential loop under `__EMSCRIPTEN__`:

  ```cpp
  tbb::flow_control fc;
  while (!fc.is_stopped()) {
      auto p = smooth_path_interpolator(fc);
      if (fc.is_stopped()) break;
      auto lr = generator(std::move(p));
      if (m_spiral_vase)        lr = spiral_vase(std::move(lr));
      if (m_pressure_equalizer) lr = pressure_equalizer(std::move(lr));
      lr = arc_handler(std::move(lr));
      auto str = cooling(std::move(lr));
      if (m_find_replace) str = find_replace(std::move(str));
      output(std::move(str));
  }
  ```

**Post-export workaround for `append_full_config()`:**

- `GCode.cpp` — `append_full_config()` is skipped under `__EMSCRIPTEN__`.
  Its call to `cfg.keys()` (a virtual on `DynamicConfig`) on
  `Print::m_full_print_config` traps with "null function" *before*
  entering the function body — the virtual dispatch resolves to function
  table index 0. Three other prior `DynamicConfig::keys()` calls in the
  same slice succeed, so this is specific to that one object's vtable.
  Strong suspect: a thunk-table issue caused by
  `-sEMULATE_FUNCTION_POINTER_CASTS=1` + `-fexceptions`. The block this
  would emit is purely informational (`; preflight_config = …` G-code
  comments), so skipping is benign. Documented as an open issue in
  README.md.

**Other source-level guards:**

- `PNGReadWrite.cpp` — whole file gated; nop stubs (libpng not built for WASM).
- `TriangleMesh.cpp` — `its_convex_hull` early-returns under EMSCRIPTEN
  (Qhull unavailable); qhull includes gated.
- `Print.cpp` — `ArrangeHelper.hpp` include + `check_seq_conflict()` gated.
- `GCode/PostProcessor.cpp` — stubbed `run_script()` (Boost.Process v2 not
  in Boost 1.83).
- `PrintObject.cpp` — extend Apple `unique_ptr` `= {}` workaround to
  Emscripten (same libc++ ambiguity).
- `Utils/DirectoriesUtils.cpp` — add EMSCRIPTEN branch returning `/data`
  MEMFS path.
- `Geometry/VoronoiUtilsCgal.cpp` — explicit template instantiations
  re-added inside the EMSCRIPTEN branch so
  `is_voronoi_diagram_planar_angle` linker symbols resolve.
- Transitive `<unordered_set>` / `<vector>` additions for files that
  previously got them via PCH on native builds: `LayerRegion.cpp`,
  `ObjectID.cpp`, `SurfaceCollection.cpp`,
  `Feature/Interlocking/InterlockingGenerator.{cpp,hpp}`,
  `ProgressConfig.hpp`, `ShortestPath.hpp`.
- `Model.cpp` — add `<tbb/parallel_for.h>` + `<tbb/blocked_range.h>`.
- `SupportSpotsGenerator.cpp` — hoist Eigen `cast<double>()` to named
  variables (Eigen 3.4 returns `CwiseUnaryOp` which doesn't implicitly
  convert to `Vec<...>`).

The conflicting `boost/log/trivial.hpp` shims under
`wasm_shims/boost/log/` and `wasm_shims/boost_runtime/boost/log/` were
removed entirely; static Boost.Log links against its own real header.

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
| CGAL 6.1 | `deps/toolchain-wasm/build_math.sh` | cross-built (5.4 was originally tried; 6.1 needed because preflight uses `AABB_traits_3` + std::optional `property_map<>`) |
| TBB | `wasm/wasm_shims/tbb/` + `oneapi/tbb/` | header-only shim, `namespace oneapi::tbb` with `tbb = oneapi::tbb` alias |
| zlib | emscripten port (`-sUSE_ZLIB=1`) | built-in |
| LibBGCode | preflight's `bundled_deps/libbgcode/` (re-enabled in patch) | source build |
| OCCT, OpenVDB, OpenCV, OpenGL, CURL, NLopt, Qhull, libpng, libjpeg-turbo | gated out / stubbed | INTERFACE IMPORTED targets only |

## Notable workarounds

- **`-O0` link.** wasm-opt asserts internally on aggressive optimization
  (`binaryen ArenaVector` OOB) with our libslic3r.wasm; both `-O3` and `-O2`
  link triggered it. Per-TU `-O3` compile flags are preserved, so the `.o`
  files are optimized — only the final wasm-opt pass is skipped. Worth
  re-trying after an emsdk upgrade.
- **`-sEMULATE_FUNCTION_POINTER_CASTS=1`.** Required for the build to load
  at all. Removing it (we tried) caused module-instantiation `LinkError`
  on a real signature mismatch somewhere in libcxxabi exception unwinding.
  This flag is implicated in the `append_full_config()` vtable trap and is
  worth investigating further.
- **TBB shim namespace pattern.** All shim headers use
  `namespace oneapi::tbb { ... }` with `namespace tbb = oneapi::tbb;`
  aliased at the bottom; any shim that uses `namespace tbb { ... }`
  directly will conflict with this alias.
- **Boost.Log namespace mismatch.** Boost.Log's `boost::log::v2*`
  namespace varies by `BOOST_LOG_NO_THREADS` × `BOOST_LOG_STATIC_LINK`.
  Static + single-threaded matches at consumer (no `BOOST_HAS_THREADS`)
  ↔ lib (built with `threading=single`).
- **Bridge → slicer linkage.** The `slicer` executable does **not**
  re-compile `preflight_wrap.cpp`; it links the already-built
  `libpreflight_wasm_bridge.a` with `-Wl,--whole-archive` so the C entry
  points survive static linking.

## Open issues to investigate

1. **Root-cause the `append_full_config()` vtable trap.** Most likely an
   `-sEMULATE_FUNCTION_POINTER_CASTS=1` thunk-table issue. If we can get
   the binary to load without that flag (currently blocked on a real
   signature mismatch), the workaround in the patch can be removed.
2. **`-O3` link.** Re-test under a newer emsdk; the binaryen
   `ArenaVector` assertion may be fixed upstream, which would let us
   shrink the wasm and recover the optimizer's wasm-opt pass.
3. **Trim `slicer.data`.** The 71 MB preload covers the full `resources/`
   tree (profiles, shapes, icons, hint files, language packs). The bridge
   probably needs only a small subset — `set_resources_dir`,
   `set_var_dir`, `set_sys_shapes_dir` etc. give us hooks to point at
   trimmed dirs.
4. **Wipe-tower & sequential-print modes.** The patch covers both
   `process_layers` variants but only the parallel-mode variant has been
   exercised by tests. A multi-extruder model with `complete_objects=true`
   would hit the sequential variant.
5. **G-code post-processing.** preFlight's `run_script` is stubbed
   under WASM. Plugin / postprocess hooks aren't supported in the
   browser yet.

## What's NOT implemented

- Arrange features (auto-arrange, sequential collision detection).
- Convex hull (Qhull) — `its_convex_hull` returns empty.
- STEP / CAD imports (OCCT off).
- JPEG / PNG thumbnail encoding.
- Binary G-code conversion that actually compresses (LibBGCode binarize
  is compiled but the heatshrink runtime path is exercised only if a
  consumer calls into it).
- Python pre-processor (`SLIC3R_PYTHON_PREPROCESSOR=OFF`).
- wxWidgets GUI (`SLIC3R_GUI=OFF`).
- Multi-threading (Emscripten built single-threaded — no
  SharedArrayBuffer / pthread).
