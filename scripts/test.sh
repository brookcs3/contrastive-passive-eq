#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Cameron Brooks
# SPDX-License-Identifier: GPL-3.0-only
#
# Run the tests on a built bundle: the C++ against Python cross-check (when the unit tests have written build/tests/cpp_reference.json),
# then the Pedalboard tests: load, impulse responses against the analog network, control coverage, and the README examples.
# Needs python3 with the packages in tests/requirements.txt (a venv is fine; PYTHON=<interpreter> picks one).
# usage: scripts/test.sh [bundle]   (default build/bin/ContrastivePassive.vst3); results in build/test-results/<arch>/
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUNDLE="${1:-$ROOT/build/bin/ContrastivePassive.vst3}"
PY="${PYTHON:-python3}"
OUT="$ROOT/build/test-results/$(uname -m)"
mkdir -p "$OUT"
rc=0
run() { echo "== $*"; "$@" || { echo "!! failed: $*"; rc=1; }; }
if [ -f "$ROOT/build/tests/cpp_reference.json" ]; then
  run "$PY" "$ROOT/tests/crosscheck.py" "$ROOT/build/tests/cpp_reference.json"
fi
run "$PY" "$ROOT/tests/pb_load.py" "$BUNDLE" "$OUT/parameters.json"
run "$PY" "$ROOT/tests/pb_ir_test.py" "$BUNDLE" "$OUT/pb_ir_test.json"
run "$PY" "$ROOT/tests/pb_coverage.py" "$BUNDLE" "$OUT/pb_coverage.json"
run "$PY" "$ROOT/tests/readme_examples.py" "$BUNDLE"
if [ "$rc" -eq 0 ]; then echo "== ALL PASSED"; else echo "== FAILED"; fi
exit "$rc"
