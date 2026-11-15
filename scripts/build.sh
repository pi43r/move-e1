#!/usr/bin/env bash
# Build E-1 for Move (ARM64) and package it under dist/:
#   dist/e1-module.tar.gz       (folder e1/)
# Uses aarch64-linux-gnu-gcc if it is on PATH, otherwise Docker.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "${1:-}" != "--inside" ]] && ! command -v aarch64-linux-gnu-gcc >/dev/null; then
  docker build -t e1-builder scripts
  exec docker run --rm -v "$PWD":/build -w /build e1-builder bash scripts/build.sh --inside
fi

CC=aarch64-linux-gnu-gcc
CFLAGS="-std=c11 -O2 -fPIC -shared -fvisibility=hidden -Wall -Isrc/include"
ENGINE=src/engine/e1_engine.c

rm -rf dist && mkdir -p dist/e1

$CC $CFLAGS src/e1/dsp/e1_plugin.c $ENGINE -o dist/e1/dsp.so -lm
cp src/e1/module.json src/e1/ui.js src/e1/e1_view.mjs src/e1/e1_library.mjs src/e1/help.json dist/e1/

aarch64-linux-gnu-nm -D dist/e1/dsp.so | grep -q " T move_plugin_init_v2" || { echo "e1: missing move_plugin_init_v2" >&2; exit 1; }
file dist/e1/dsp.so

(cd dist && tar -czf e1-module.tar.gz e1)
ls -l dist/*.tar.gz
