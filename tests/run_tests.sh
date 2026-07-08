#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOVIS="$ROOT/novis"

"$NOVIS" check "$ROOT/tests/banking_ai.novis" >/dev/null
"$NOVIS" run "$ROOT/tests/banking_ai.novis" >/tmp/novis_banking_ai.out
expected_banking=$'MXN 1012.34\n0.802183888559\ntrue'
actual_banking="$(cat /tmp/novis_banking_ai.out)"
if [[ "$actual_banking" != "$expected_banking" ]]; then
  echo "banking_ai output mismatch" >&2
  echo "$actual_banking" >&2
  exit 1
fi

"$NOVIS" check "$ROOT/tests/framework_contracts.novis" >/dev/null
"$NOVIS" run "$ROOT/tests/framework_contracts.novis" >/tmp/novis_framework.out
if [[ "$(cat /tmp/novis_framework.out)" != "USD 25.00" ]]; then
  echo "framework_contracts output mismatch" >&2
  cat /tmp/novis_framework.out >&2
  exit 1
fi

"$NOVIS" check "$ROOT/tests/stdlib_fs_string.novis" >/dev/null
"$NOVIS" run "$ROOT/tests/stdlib_fs_string.novis" >/tmp/novis_stdlib.out
if [[ "$(cat /tmp/novis_stdlib.out)" != "5" ]]; then
  echo "stdlib output mismatch" >&2
  cat /tmp/novis_stdlib.out >&2
  exit 1
fi

"$NOVIS" env init >/tmp/novis_env_init.out
if ! grep -q "Novis env ready" /tmp/novis_env_init.out; then
  echo "env init output mismatch" >&2
  cat /tmp/novis_env_init.out >&2
  exit 1
fi
"$NOVIS" env info >/tmp/novis_env_info.out
if ! grep -q "Python runtime: not required" /tmp/novis_env_info.out; then
  echo "env info should report no Python runtime" >&2
  cat /tmp/novis_env_info.out >&2
  exit 1
fi

"$NOVIS" importpy pandas numpy >/tmp/novis_importpy_install.out
if ! grep -q "Python runtime required: no" /tmp/novis_importpy_install.out; then
  echo "importpy should install native providers without Python runtime" >&2
  cat /tmp/novis_importpy_install.out >&2
  exit 1
fi
"$NOVIS" check "$ROOT/tests/importpy_native.novis" >/dev/null
"$NOVIS" run "$ROOT/tests/importpy_native.novis" >/tmp/novis_importpy.out
if [[ "$(cat /tmp/novis_importpy.out)" != "2.5 4 3" ]]; then
  echo "importpy native output mismatch" >&2
  cat /tmp/novis_importpy.out >&2
  exit 1
fi

if "$NOVIS" importpy unknown_pkg >/tmp/novis_importpy_unknown.out 2>&1; then
  echo "unknown importpy provider should fail" >&2
  exit 1
fi
if ! grep -q "no native importpy provider" /tmp/novis_importpy_unknown.out; then
  echo "unknown importpy diagnostic missing" >&2
  cat /tmp/novis_importpy_unknown.out >&2
  exit 1
fi
"$NOVIS" env list >/tmp/novis_env_list.out
if ! grep -q "pandas -> pandas" /tmp/novis_env_list.out; then
  echo "env list should show installed pandas provider" >&2
  cat /tmp/novis_env_list.out >&2
  exit 1
fi

if "$NOVIS" check "$ROOT/tests/diagnostic_bad.novis" >/tmp/novis_bad.out 2>&1; then
  echo "diagnostic_bad should fail" >&2
  exit 1
fi
if ! grep -q "money values need an explicit currency" /tmp/novis_bad.out; then
  echo "diagnostic help missing" >&2
  cat /tmp/novis_bad.out >&2
  exit 1
fi

"$NOVIS" emit-ir "$ROOT/examples/bank_risk.novis" >/tmp/novis_ir.out
if ! grep -q "NovisIR v1.0" /tmp/novis_ir.out; then
  echo "IR version missing" >&2
  exit 1
fi

bash -n "$ROOT/install.sh" >/dev/null
if [ ! -x "$ROOT/install.sh" ]; then
  chmod +x "$ROOT/install.sh"
fi

installed_version="$("$NOVIS" --help | head -n 1 | awk '{print $2}')"
if [ -z "$installed_version" ]; then
  echo "installed version is empty" >&2
  exit 1
fi

echo "novis tests: ok"
