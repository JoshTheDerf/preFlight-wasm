# preFlight WebAssembly Edition

A WebAssembly port of [preFlight slicer](https://github.com/oozebot/preFlight),
adapted from [orcaslicer-wasm](https://github.com/allanwrench28/orcaslicer-wasm).
preFlight is itself a [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer)
derivative.

## Status

**Working end-to-end.** A reference STL (`cylinder.stl` from preFlight's
shipped resources) slices to a 1.33 MB valid preFlight G-code blob in ~25
seconds, both in Node and in a browser tab.

```
[pflt_slice] export complete wall_time_ms=10298.82
[pflt_slice] retrieved gcode from in-memory object, size=1332851
orc_slice rc=0
```

The corresponding G-code begins with the preFlight header, layer-change
markers, and standard G-code (`G1`, `M104`, `M106`, etc.); ends with
filament-usage and time-estimate stats. See [PORTING.md](PORTING.md) for
the full investigation log.

## Known limitations

- **Single-threaded.** No SharedArrayBuffer / pthread support; slicing
  runs on the main JS thread (use a Web Worker upstream).
- **~285 MB peak heap** during slice; the build sets a 4 GB
  `MAXIMUM_MEMORY` and grows from `INITIAL_MEMORY=256 MB`.
- **Disabled features:** arc fitting, OCCT/STEP import, Qhull convex
  hull (`its_convex_hull` returns empty), OpenVDB, libjpeg-turbo thumbnails,
  libpng thumbnails. Each is gated under `#ifdef __EMSCRIPTEN__` with a
  stub body.
- **Full-config dump skipped.** `append_full_config()` is bypassed under
  Emscripten — see PORTING.md for the underlying vtable-thunk-table issue
  with `-sEMULATE_FUNCTION_POINTER_CASTS=1` + `-fexceptions`. The
  informational `; preflight_config = …` block at the tail of the G-code is
  empty; nothing downstream consumes it.
- **Wipe tower / sequential-print modes** not exercised yet.

## Layout

```
preflight-wasm/
├── README.md, PORTING.md
├── patches/
│   └── preflight-wasm.patch        # applies to a clean preFlight checkout
├── bridge/
│   ├── CMakeLists.txt              # builds libpreflight_wasm_bridge.a
│   ├── preflight_wrap.cpp          # C entry points + in-memory G-code retrieval
│   └── preflight_wrap.h
├── wasm/
│   ├── CMakeLists.txt              # top-level emcmake build
│   ├── toolchain/emsdk.env
│   ├── cmake/                      # Find*.cmake for cross-built deps
│   └── wasm_shims/                 # header-only stubs (TBB single-thread,
│                                   # NLopt, oneAPI TBB namespace alias, etc.)
├── scripts/
│   ├── setup.sh                    # bootstraps emsdk, cmake, ccache, deps
│   ├── build-wasm.sh               # applies patch + runs emcmake build
│   └── sanity.sh
└── tests/
    ├── slice-test.cjs              # runs orc_slice on cylinder.stl
    └── verify-gcode.cjs            # validates output shape (markers, size)
```

`../preflight/` is a sibling checkout of oozeBot/preFlight that this tree
patches and links against.

## Building

```bash
# 1. one-time toolchain bootstrap
#    (downloads emsdk, portable cmake, ccache; builds Boost 1.83 + GMP +
#     MPFR + CGAL 6.1 for wasm32; ~30 min on a 4-core machine)
./scripts/setup.sh
bash deps/boost-wasm/build_boost.sh
bash deps/toolchain-wasm/build_math.sh

# 2. clone the upstream preflight sibling
cd .. && git clone https://github.com/oozebot/preFlight.git preflight && cd preflight-wasm

# 3. build (NPROC=4 caps parallelism; each emcc job needs ~500 MB)
NPROC=4 bash scripts/build-wasm.sh
```

Artifacts land in `build-wasm/`:

```
slicer.js     ~265 KB  (Emscripten loader, ES module, exports PreflightModule)
slicer.wasm   ~7 MB    (libslic3r + bridge)
slicer.data   ~71 MB   (preloaded resources/ bundle — profiles, shapes, icons)
```

## Testing

```bash
node tests/slice-test.cjs            # slice cylinder.stl
node tests/verify-gcode.cjs out.gcode # validate output shape
```

CI runs both on every push to `main`.

## Consuming from a web app

The C entry points kept from orcaslicer-wasm:

```c
int  orc_init        (const uint8_t* cfg, int len);
int  orc_slice       (const uint8_t* model, int len,
                      uint8_t** gcode_out, int* gcode_len);
void orc_free        (void* p);
const char* orc_decode_exception(void* exception_ptr);
int  orc_describe_config(uint8_t** json_out, int* json_len);
```

The config payload passed to `orc_init` uses **PrusaSlicer** option names
(`layer_height`, `fill_density`, `fill_pattern`, `perimeter_speed`,
`temperature`, `bed_temperature`, `perimeters`, `top_solid_layers`, …). The
bridge also accepts a handful of legacy aliases via
`apply_config_overrides` — see `bridge/preflight_wrap.cpp` for the alias
table.

```javascript
import PreflightModule from './slicer.js';

const mod = await PreflightModule({ locateFile: f => `/wasm/${f}` });
const stl = new Uint8Array(await fetch('cylinder.stl').then(r => r.arrayBuffer()));

const inPtr     = mod._malloc(stl.length);
const outPtrPtr = mod._malloc(4);
const outLenPtr = mod._malloc(4);
mod.HEAPU8.set(stl, inPtr);
mod.setValue(outPtrPtr, 0, 'i32');
mod.setValue(outLenPtr, 0, 'i32');

const cfg = new TextEncoder().encode(JSON.stringify({
  layer_height: 0.2, fill_density: 20, fill_pattern: 'grid',
  perimeter_speed: 60, temperature: 210, bed_temperature: 60,
  perimeters: 2, top_solid_layers: 3, bottom_solid_layers: 3,
}));
const cfgPtr = mod._malloc(cfg.length);
mod.HEAPU8.set(cfg, cfgPtr);
mod._orc_init(cfgPtr, cfg.length);
mod._free(cfgPtr);

const rc = mod._orc_slice(inPtr, stl.length, outPtrPtr, outLenPtr);
if (rc !== 0) throw new Error(`slice failed rc=${rc}`);

const gp  = mod.getValue(outPtrPtr, 'i32') >>> 0;
const gl  = mod.getValue(outLenPtr, 'i32') >>> 0;
const gcode = new TextDecoder().decode(mod.HEAPU8.subarray(gp, gp + gl));

mod._orc_free(gp);
mod._free(inPtr);
mod._free(outPtrPtr);
mod._free(outLenPtr);
```

## C-API differences from orcaslicer-wasm

The C entry-point names match `orc_*` so the orcaslicer-wasm web frontend
can be retargeted, but the wrapped C++ surface differs because preFlight has
diverged from OrcaSlicer:

- preFlight uses `GCodeGenerator` where Orca uses `GCode`. The bridge adapts.
- preFlight has no multi-plate concept: no `Print::get_plate_origin()`, no
  `GCode::set_gcode_offset()`. Bridge sets a zero origin implicitly.
- preFlight's `load_stl` is 3-arg (`path, model, object_name`) vs Orca's 5.
- No `DynamicPrintConfig::set_num_filaments` — extruder count is the only knob.
- preFlight's `do_export` uses **memory-based** G-code processing (no disk
  write) and only emits a file if you pass a `GCodeProcessorResult*`. The
  bridge does this and reads `result.gcode_object->text_buffer()`.
- JS module exports `PreflightModule` (vs `OrcaModule`).

## License

AGPL-3.0+, inherited from preFlight (a PrusaSlicer derivative).

The patches and bridge code in this repository are authored by
[Joshua Bemenderfer](https://github.com/JoshTheDerf) and contributors.
