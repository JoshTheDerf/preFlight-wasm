# Minimal OpenSSL shim for Emscripten/WASM builds.
# Marks OpenSSL as FOUND and provides imported interface targets.

# Legacy-style variables:
set(OPENSSL_FOUND TRUE)
set(OPENSSL_INCLUDE_DIR "")
set(OPENSSL_CRYPTO_LIBRARY "")
set(OPENSSL_SSL_LIBRARY "")

# New-style variables:
set(OpenSSL_FOUND TRUE)
set(OpenSSL_INCLUDE_DIR "")
set(OpenSSL_CRYPTO_LIBRARY "")
set(OpenSSL_SSL_LIBRARY "")
# Imported targets (modern CMake)
if(NOT TARGET OpenSSL::Crypto)
  add_library(OpenSSL::Crypto INTERFACE IMPORTED)
  set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
  )
endif()

if(NOT TARGET OpenSSL::SSL)
  add_library(OpenSSL::SSL INTERFACE IMPORTED)
  set_target_properties(OpenSSL::SSL PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
  )
endif()
