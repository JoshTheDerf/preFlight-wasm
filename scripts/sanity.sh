#!/usr/bin/env bash
set -euo pipefail
echo "node: $(node -v || true)"
echo "npm:  $(npm -v || true)"
echo "cmake: $(cmake --version 2>/dev/null | head -n1 || true)"
if command -v emcc >/dev/null 2>&1; then
  echo "emcc: $(emcc --version | head -n1)"
else
  echo "emcc: NOT FOUND"
  echo "hint: source /opt/emsdk/emsdk_env.sh"
fi
