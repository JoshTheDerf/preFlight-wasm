# preFlight WebAssembly Edition (work in progress)

A WebAssembly port of [preFlight slicer](https://github.com/oozebot/preFlight), adapted from [orcaslicer-wasm](https://github.com/allanwrench28/orcaslicer-wasm). The goal is to run preFlight's slicing engine entirely in a browser tab.

## Status

**This is an in-progress port.** The scaffolding, patch, and bridge are in place but the dependency cross-build (Boost, CGAL, GMP, MPFR, PNG, ZLIB, JPEG, Qhull, EXPAT) has not been fully exercised. Expect to iterate on configure-time errors.

## Layout

```
preflight-wasm/
├── README.md                 # This file
├── patches/
│   └── preflight-wasm.patch  # WASM/Emscripten guards for preflight/
├── bridge/
│   ├── CMakeLists.txt        # Builds preflight_wasm_bridge static lib
│   ├── preflight_wrap.cpp    # C entry points: orc_init / orc_slice / orc_describe_config
│   └── preflight_wrap.h
├── wasm/
│   ├── CMakeLists.txt        # Top-level WASM build, drives ../preflight + ../bridge
│   ├── toolchain/emsdk.env   # Sourced by build scripts to activate emsdk
│   ├── cmake/                # Custom Find*.cmake modules for WASM-built deps
│   └── wasm_shims/           # Header-only stubs (TBB, OpenVDB, OpenCV, etc.)
├── deps/
│   ├── boost-wasm/           # Boost cross-build (build_boost.sh)
│   ├── toolchain-wasm/       # GMP/MPFR/CGAL cross-build (build_math.sh)
│   └── emsdk/                # Emscripten SDK (auto-installed)
└── scripts/
    ├── setup.sh
    ├── build-wasm.sh
    └── sanity.sh
```

`../preflight/` and `../orcaslicer-wasm/` are sibling directories (the upstream sources and the reference implementation).

## What the patch does

`patches/preflight-wasm.patch` introduces Emscripten guards into the preflight source tree without changing native builds:

- Root `CMakeLists.txt` — make TBB optional (use the shim under `wasm_shims/tbb/` when TBB isn't found).
- `bundled_deps/hidapi/CMakeLists.txt` — short-circuit to an INTERFACE library on Emscripten.
- `src/CMakeLists.txt` — gate `slic3r-arrange`, `slic3r-arrange-wrapper`, `libseqarrange`, `occt_wrapper` behind `NOT EMSCRIPTEN`; `return()` early from the executable definition under Emscripten so the CLI binary is not built.
- `src/libslic3r/CMakeLists.txt` — add `SLIC3R_WITH_OCCT` and `SLIC3R_WITH_OPENCV` options; drop `LibBGCode::bgcode_convert` and `libseqarrange` from the link line under Emscripten; define `SLIC3R_NO_BGCODE` / `SLIC3R_NO_SEQARRANGE`.
- Targeted source fixes for `AABBTreeLines.hpp`, `Feature/FuzzySkin/FuzzySkin.cpp`, `Format/STEP.cpp`, `GCode.hpp`, `Geometry/VoronoiUtilsCgal.cpp`, `Platform.cpp`, `Thread.cpp` — `#ifdef __EMSCRIPTEN__` blocks and C++ strictness workarounds.

Native builds are unaffected: every change is guarded behind `EMSCRIPTEN` or `__EMSCRIPTEN__`.

## What is NOT yet ported

- **Cross-built dependencies.** `deps/boost-wasm/build_boost.sh` and `deps/toolchain-wasm/build_math.sh` are inherited from orcaslicer-wasm. preFlight also requires WASM-built PNG, ZLIB, JPEG, EXPAT, Qhull. Each must be either added to a similar wasm-deps build pipeline or shimmed.
- **LibBGCode (preflight-only)** is dropped from the WASM link line via the patch; downstream call sites need `SLIC3R_NO_BGCODE` guards. This will surface as link errors once configuration succeeds.
- **libseqarrange** — same; the preflight CLI calls into arrange features, but the WASM bridge bypasses that surface entirely.
- **PythonRuntime / `SLIC3R_PYTHON_PREPROCESSOR`** — Python 3.14 embedding is preflight-specific and is left off (no opt-in for WASM). The pybind11 / pyembed link is already gated by `if (SLIC3R_PYTHON_PREPROCESSOR)` in `src/libslic3r/CMakeLists.txt`; just do not set that flag for the WASM configure.
- **3D preview / G-code viewer** — the orca web UI under `../orcaslicer-wasm/web/` can be retargeted at the preflight bridge symbols (the C entry points `orc_init` / `orc_slice` / `orc_describe_config` are kept identical for that reason). No web frontend is included in this repo yet.

## C entry points

The bridge exposes the same C API as orcaslicer-wasm so the existing web frontend can be reused:

```c
int orc_describe_config(uint8_t** json_out, int* json_len);
int orc_init(const uint8_t* cfg, int len);
int orc_slice(const uint8_t* model, int len,
              uint8_t** gcode_out, int* gcode_len);
void orc_free(void* p);
const char* orc_decode_exception(void* exception_ptr);
```

## Building (rough outline)

```bash
# 1. Activate emsdk (auto-installed by setup.sh)
./scripts/setup.sh

# 2. Cross-build Boost (Linux/macOS host)
bash deps/boost-wasm/build_boost.sh

# 3. Cross-build GMP/MPFR/CGAL
bash deps/toolchain-wasm/build_math.sh

# 4. Configure & build the slicer module
./scripts/build-wasm.sh
```

Artifacts land in `build-wasm/slicer.{js,wasm}`.

## Differences from orcaslicer-wasm

- **Class rename** — preflight uses `GCodeGenerator` where Orca uses `GCode`. The bridge adapts.
- **No multi-plate** — preflight has no `Print::get_plate_origin()` / `GCode::set_gcode_offset()`. The bridge sets a zero origin.
- **`load_stl` signature** — preflight's takes 3 args (`path, model, object_name`), not Orca's 5.
- **No `set_temporary_dir`** — `/tmp` is used directly via MEMFS.
- **No `DynamicPrintConfig::set_num_filaments`** — extruder count is the only knob.
- **Different module rename** — the JS module is `PreflightModule` (vs `OrcaModule`).
