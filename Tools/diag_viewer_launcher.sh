#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VIEWER_PATH="${SCRIPT_DIR}/diag_viewer.html"

if [[ ! -f "${VIEWER_PATH}" ]]; then
  printf 'Diagnostics viewer not found at %s\n' "${VIEWER_PATH}" >&2
  exit 1
fi

if command -v xdg-open >/dev/null 2>&1; then
  xdg-open "${VIEWER_PATH}" >/dev/null 2>&1 &
  disown || true
  exit 0
fi

if command -v open >/dev/null 2>&1; then
  open "${VIEWER_PATH}"
  exit 0
fi

printf 'Open this file in a browser:\n  %s\n' "${VIEWER_PATH}"
