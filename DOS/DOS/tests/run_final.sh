#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOS="$ROOT/build-stage5/dos"
for test in stage1 stage1_edge stage2 stage3 stage4 stage5; do
  echo "[RUN] $test"
  "$DOS" run "$ROOT/tests/$test.dos" >/dev/null
done
"$DOS" build "$ROOT/tests/stage5.dos" -o "$ROOT/tests/stage5pkg"
chmod +x "$ROOT/tests/stage5pkg"
"$ROOT/tests/stage5pkg" >/dev/null
printf '%s\n' 'FINAL_TESTS=PASS'
