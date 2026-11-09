#!/usr/bin/env bash
# Cross-compile the 32-bit Windows DLL from Linux using zig (pip: ziglang).
# One-time setup:  pip install --target .tools ziglang     (or reuse ../d2-mod-qol/.tools, picked up automatically)
set -euo pipefail
cd "$(dirname "$0")"
TOOLS="$PWD/.tools"
[ -d "$TOOLS" ] || TOOLS="$PWD/../d2-mod-qol/.tools"
export PYTHONPATH="$TOOLS"
mkdir -p build .out
ZIG="python3 -m ziglang cc -target x86-windows-gnu -O2 -Wall"
$ZIG -shared -o .out/quartermaster.dll src/quartermaster.c
cp .out/quartermaster.dll build/quartermaster.dll   # build/ holds only the deliverables; intermediates live in .out/
cp src/quartermaster.ini build/quartermaster.ini
echo "built: build/quartermaster.dll build/quartermaster.ini"
