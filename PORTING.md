# preFlight v1.3.0 → WebAssembly: porting notes

This replaces the v1.0.0 port (branch `main`). The old port "worked" but ran
with `-sEMULATE_FUNCTION_POINTER_CASTS`, JS-emulated exceptions
(`-fexceptions`/`DISABLE_EXCEPTION_CATCHING=0`), a `-O0` link, header shims
that shadowed real libraries (Boost.Thread/Log/Format/Optional, cereal, CGAL,
expat, libpng, libjpeg, NLopt, OpenSSL...) and skipped `append_full_config()`
because of a "null function" vtable trap. Those were symptoms of mixed
ABI/exception/threading models; the web app saw "memory access out of bounds"
traps. This port removes all of them.

## Build model (see cubby-slicer/docs/ENGINE-CONTRACT.md)

* Every object — preFlight, bundled deps, `../wasm-deps` archives, bridge — is
  compiled with `-fwasm-exceptions -sSUPPORT_LONGJMP=wasm`, single-threaded
  (no `-pthread`, Boost `threading=single`).
* No `-sEMULATE_FUNCTION_POINTER_CASTS`, no global `operator new/delete`
  replacement, no stub header for any library that is linked.
* Real libraries: Boost 1.84 (filesystem, log, log_setup, regex, chrono,
  date_time, iostreams, nowide), Eigen 5.0.1, CGAL 6.1 (header-only, with
  preFlight's `CGAL_DISABLE_GMP` → Boost.Multiprecision), cereal, NLopt,
  Qhull 8.1-alpha3, expat 2.6.4, heatshrink 0.4.1, nlohmann_json 3.12,
  nanosvg (fltk fork) from `../wasm-deps`; zlib / libpng / libjpeg from the
  Emscripten ports (`-sUSE_ZLIB=1 -sUSE_LIBPNG=1 -sUSE_LIBJPEG=1` on every
  compile and on the link). The extra deps were added to
  `wasm-deps/build-deps.sh` (`preflight-extras` target).
* Not linked: `libboost_atomic.a` (Boost's Jamfile forces `threading=multi`
  → `-pthread` objects) and Boost.Locale (not built: no iconv/ICU; preFlight
  only `#include`s it). Boost.Thread is not built either (see below).
* TBB: `wasm/tbb_shim` — a sequential oneTBB replacement, the only shim.
  Semantics kept: `simple_partitioner` splits to the grain size; pipelines are
  type-erased `filter<I,O>` and `flow_control::stop()` never pushes the
  input's dummy value downstream; `task_group` defers exceptions to `wait()`;
  `enumerable_thread_specific` is lazy and `clear()` empties it;
  `concurrent_vector` is `std::deque`-backed (stable element addresses);
  mutexes terminate on self-deadlock instead of spinning forever.
  `GCode.cpp`'s pipeline therefore runs unmodified (the old port rewrote it).
* Release link: `-O3` (binaryen) — the old `-O0` workaround for a binaryen
  assertion is gone with emsdk 6.0.10. Debug: `-O1 -g2 -sASSERTIONS=2
  -sSAFE_HEAP=1 -sSTACK_OVERFLOW_CHECK=2` (compile `-O1 -g2`, NDEBUG kept so
  the same engine paths run as in release).
* `-sMALLOC=dlmalloc -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=128MB
  -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=64MB -sABORTING_MALLOC=0`; no preloaded
  files (`--preload-file` removed: nothing reads the FS at slice time).
  `INITIAL_MEMORY` is 128 MB, not 64 MB: wasm-ld rejects 64 MB because the
  64 MB stack plus static data does not fit.
* `-sSTACK_OVERFLOW_CHECK=2` in release too (the stack cannot be placed first,
  so level 1 would let an overflow silently overwrite static data).
* `-sINCOMING_MODULE_JS_API=[locateFile,print,printErr,instantiateWasm,
  wasmBinary,getPreloadedPackage,onAbort,monitorRunDependencies,setStatus,
  noInitialRun,preRun,postRun]` — Emscripten ≥ 4 silently ignores other
  factory arguments in release builds. (`csProgress` is not listed; the
  bridge reads `Module["csProgress"]` at call time.)

## The patch (`patches/preflight-wasm.patch`)

Rebuilt from scratch against v1.3.0; every hunk of the v1.0.0 patch was
re-audited. Kept / new hunks, all guarded by `EMSCRIPTEN` / `__EMSCRIPTEN__`:

| file | change | why |
|---|---|---|
| `CMakeLists.txt` | Boost components without thread/process/locale/atomic | not built / not linkable single-threaded |
| `CMakeLists.txt` | skip `find_package(CURL)`, `OpenGL`, `OpenVDB` | networking / GUI / voxel libs, not linked |
| `CMakeLists.txt` | no `-flto` | objects stay plain wasm; wasm-opt runs at link |
| `src/CMakeLists.txt` | skip `slic3r-arrange*`, `libseqarrange`; `return()` before CLI/GUI executables | libseqarrange needs Z3; only libslic3r is consumed |
| `src/libslic3r/CMakeLists.txt` | drop `ArrangeHelper.cpp`, `pchheader.cpp`; don't link `libseqarrange`, `OCCTWrapper` | Z3 / OCCT not built; pchheader pulls Boost.Thread |
| `Print.cpp` | `m_sequential_collision_detected = nullopt` | libseqarrange (Z3) collision check unavailable; `validate()`'s own clearance checks still run |
| `Format/STEP.cpp` | `load_step` throws | OCCT not available |
| `GCode/PostProcessor.cpp` | `run_script` returns an error | no processes in a browser (bridge never calls it) |
| `Thread.hpp/.cpp` | no Boost.Thread; `std::thread::id` for the main-thread id; thread naming is a no-op | Boost.Thread headers require `BOOST_HAS_THREADS` (-pthread); defining it would change `shared_ptr`'s refcount layout vs the compiled Boost libs |
| `Brim.cpp`, `TriangleMeshSlicer.cpp` | `boost::lock_guard<std::mutex>` → `std::lock_guard` | same reason; identical semantics |
| `GCode/Thumbnails.hpp` | don't include `boost/beast/core.hpp` (only `beast/.../base64.hpp` is used) | beast/core pulls in Asio, whose config requires POSIX threads/signals |
| `GCode/Thumbnails.cpp` | JPEG thumbnails: RGBA→RGB + `JCS_RGB` when `JCS_EXTENSIONS` is absent; include `jmorecfg.h` only with libjpeg-turbo | the Emscripten port is IJG libjpeg (no `JCS_EXT_RGBA`, no include guard) |
| `GCode.hpp` | nop `LayerResult` id = `numeric_limits<size_t>::max()` | `coord_t` max (int64) narrows to 32-bit `size_t` (hard error); only `nop_layer_result` is ever tested |
| `Platform.cpp` | Emscripten → generic Linux | otherwise `static_assert(false, "Unknown platform")` |
| `Utils/DirectoriesUtils.cpp` | `GetDataDir()` → `/home/web_user/.config` | only Win32/Linux branches exist; unused by the bridge |
| `PrintObject.cpp` | use `.reset()` like `__APPLE__` | libc++ finds `= {}` ambiguous for the octree `unique_ptr` pair |
| `Arachne/` + `Athena/SkeletalTrapezoidation.cpp` | `coord_t(v.size()) - 1` in `interpolate()` | **real wasm32 OOB bug**: with 32-bit `size_t`, `size() - 1` of an empty vector is 4294967295, still positive in int64, so the `>= 0` loop read out of bounds (found by the Orca port; the rest of libslic3r was grepped for the pattern) |
| `ShortestPath.hpp`, `ProgressConfig.hpp`, `Athena/.../SplitPromotionBeadingStrategy.cpp` | missing `#include`s | natively supplied by the precompiled header, which is off here |

Dropped from the v1.0.0 patch (obsolete or replaced by real libraries):
TBB-optional CMake, EXPAT/PNG/NLopt/Qhull/JPEG gating (real libs now),
`hidapi` (GUI-only in v1.3.0), `PNGReadWrite.cpp` stubs (real libpng),
`its_convex_hull` stub (real Qhull), `VoronoiUtilsCgal.cpp` "always planar"
stub (real CGAL), `GCode.cpp` sequential pipeline rewrite and the
`append_full_config()` skip (the full `; preflight_config` block is emitted
again), `FuzzySkin` seed tweak, `Model.cpp`/`LayerRegion`/`ObjectID`/
`SurfaceCollection`/`InterlockingGenerator` include hunks and the
`AABBTreeLines`/`SupportSpotsGenerator` Eigen cast hoists (not needed with
v1.3.0 + Eigen 5).

## Bridge (`bridge/cs_bridge.cpp`)

* Config: `DynamicPrintConfig::full_print_config()` → `set_deserialize` per key
  with `ForwardCompatibilitySubstitutionRule::Enable` (preFlight's
  `EnableSilent` substitutes without recording; `Enable` records, and the
  records become `report.substitutions`; values that fail to parse and cannot
  be substituted are skipped and reported too) → `handle_legacy_composite()` → `normalize_fdm()`. JSON arrays are
  joined the way each option parses them: `escape_strings_cstyle` (`;`,
  C-style quoting) for string vectors, `,` for numeric/bool/enum vectors, `XxY`
  for points (`[x,y]` accepted). Unknown keys become one `unknown_options`
  warning (preFlight's `handle_legacy()` would otherwise drop unknown and
  obsolete keys silently). `binary_gcode` is forced off (warning) — the ABI
  returns text.
* Model: one `ModelObject`/`ModelVolume` per job object; the 4×4 transform is
  baked into the mesh in double precision (winding flipped for mirroring
  transforms, singular transforms rejected), degenerate faces removed, one
  identity instance; per-object config → `ModelObject::config`; `dropToBed` →
  `ensure_on_bed()`. As in the preFlight CLI, instances not fully inside the
  build volume are skipped (warning `outside_bed`); nothing left → error.
* `Print::apply` → `validate(&warnings)` (errors → rc 3) → `process()` →
  `export_gcode("", &result)` (memory mode: text from
  `result.gcode_object->text_buffer()`). Stats: normal-mode time, per-extruder
  volume → mm / cm³ / g / cost from filament diameter/density/cost, distinct
  layer `print_z` count (objects + supports), max `print_z`. Warnings: apply,
  validate and all print/object step warnings (deduplicated).
* Every exported function catches `std::exception` and `...`; rc: 0 ok,
  2 invalid job, 3 engine error, 4 out of memory. Each call owns its
  Model/Print/result, so repeated slices on one instance are independent.
* `cs_eval_condition`: same config construction, then
  `PlaceholderParser::evaluate_boolean_expression`. Empty → 1, parse/eval
  error → -1, bad config JSON → -2.
* `cs_describe_config`: all FFF keys of `print_config_def` present in
  `FullPrintConfig` or in `Preset::{print,filament,printer}_options()`;
  `scope` from those preset lists (`object` for object/region-only keys).

## Disabled / not supported

* Sequential-print (`complete_objects`) collision detection via libseqarrange
  (Z3). preFlight's `Print::validate` extruder-clearance check still applies.
* Arrange (the app positions objects), STEP import, OpenVDB-based features,
  G-code post-processing scripts, Python pre-processor, binary G-code output,
  thumbnails (no thumbnail callback is passed).
* Multi-threading.

## Remaining workarounds

* The TBB shim (sequential).
* Boost.Thread-free `Thread.hpp`/`Thread.cpp` under `__EMSCRIPTEN__`.
* Local `Findcereal.cmake`: cereal's installed version file rejects 32-bit
  consumers (generated on a 64-bit host); the headers are used directly.
