# vendor-cmake/

Hand-written CMake drivers for vendored sources whose upstreams either
ship no usable CMake config (expat ships autotools; the bundled
expat-config.cmake assumes a built/installed tree we don't have) or whose
config doesn't expose the targets we need (heatshrink ships a Makefile
but no library target).

These files are dropped into `deps/<vendor>/CMakeLists.txt` after the
upstream tarball is unpacked or cloned. `scripts/setup.sh` and
`.github/workflows/build.yml` both perform this copy. Edit them in place
here — never in `deps/`, which is gitignored.

| Source                                | Destination                            |
| ------------------------------------- | -------------------------------------- |
| `expat-2.6.3-CMakeLists.txt`          | `deps/expat-2.6.3/CMakeLists.txt`      |
| `heatshrink-CMakeLists.txt`           | `deps/heatshrink-src/CMakeLists.txt`   |
