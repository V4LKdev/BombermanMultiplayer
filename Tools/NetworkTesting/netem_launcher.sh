#!/usr/bin/env bash
set -euo pipefail

# Linux-only helper for repeatable client latency testing.

DEFAULT_IFACE="lo"
DEFAULT_DELAY_MS=0
DEFAULT_JITTER_MS=0
DEFAULT_LOSS_PCT=0
DEFAULT_CLIENT_CMD=(./cmake-build-debug/Bomberman)
ACTIVE_IFACE=""
ACTIVE_CHILD_PID=0

print_header() {
  printf '\n=== Bomberman Network Impairment Launcher ===\n'
}

# Checks if ip, tc, sudo, and awk are available.
require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'Missing required command: %s\n' "$1" >&2
    exit 1
  fi
}

prompt_with_default() {
  local prompt="$1"
  local default="$2"
  local input
  read -r -p "$prompt [$default]: " input
  if [[ -z "$input" ]]; then
    printf '%s' "$default"
  else
    printf '%s' "$input"
  fi
}

prompt_uint() {
  local label="$1"
  local default="$2"
  local value
  while true; do
    value="$(prompt_with_default "$label" "$default")"
    if [[ "$value" =~ ^[0-9]+$ ]]; then
      printf '%s' "$value"
      return 0
    fi
    printf 'Please enter a non-negative integer.\n' >&2
  done
}

prompt_loss_pct() {
  local value
  while true; do
    value="$(prompt_with_default "Packet loss percent" "$DEFAULT_LOSS_PCT")"
    if [[ "$value" =~ ^([0-9]+)(\.[0-9]+)?$ ]] && awk -v v="$value" 'BEGIN { exit !(v >= 0 && v <= 100) }'; then
      printf '%s' "$value"
      return 0
    fi
    printf 'Please enter a percentage in the range [0, 100].\n' >&2
  done
}


show_interfaces() {
  printf '\nDetected network interfaces:\n'
  ip -o link show | awk -F': ' '{print "  - " $2}'
  printf '\n'
}

# qdisc contains the current network impairment settings for an interface.
show_qdisc_state() {
  local iface="$1"
  printf '\nCurrent qdisc for %s:\n' "$iface"
  sudo tc qdisc show dev "$iface"
  printf '\n'
}


clear_impairment() {
  local iface="$1"
  sudo tc qdisc del dev "$iface" root 2>/dev/null || true
}

# Builds and applies the tc netem command based on the provided parameters.
apply_impairment() {
  local iface="$1"
  local delay_ms="$2"
  local jitter_ms="$3"
  local loss_pct="$4"

  local args=(netem)

  if [[ "$delay_ms" != "0" || "$jitter_ms" != "0" ]]; then
    args+=(delay "${delay_ms}ms")
    if [[ "$jitter_ms" != "0" ]]; then
      args+=("${jitter_ms}ms")
    fi
  fi

  if [[ "$loss_pct" != "0" && "$loss_pct" != "0.0" ]]; then
    args+=(loss "${loss_pct}%")
  fi

  if [[ ${#args[@]} -eq 1 ]]; then
    printf 'No impairment requested. Leaving %s clean.\n' "$iface"
    clear_impairment "$iface"
    return 0
  fi

  printf 'Applying impairment on %s: tc qdisc replace dev %s root %s\n' \
    "$iface" "$iface" "${args[*]}"
  # Replace existing qdisc or add if none exists
  sudo tc qdisc replace dev "$iface" root "${args[@]}"
}

run_client() {
  local iface="$1"
  shift

  printf '\nLaunching Bomberman Client:\n  '
  printf '%q ' "$@"
  printf '\n\n'

  ACTIVE_IFACE="$iface"
  ACTIVE_CHILD_PID=0

  cleanup() {
    if [[ -n "$ACTIVE_IFACE" ]]; then
      printf '\nCleaning up impairment on %s...\n' "$ACTIVE_IFACE"
      clear_impairment "$ACTIVE_IFACE"
      ACTIVE_IFACE=""
    fi
  }

  # Forwards signals to the child process and ensures clean shutdown.
  forward_signal_and_cleanup() {
    local signal="$1"
    if [[ "$ACTIVE_CHILD_PID" -ne 0 ]]; then
      kill "-$signal" "$ACTIVE_CHILD_PID" 2>/dev/null || true
    fi
    cleanup
  }

  trap cleanup EXIT
  trap 'forward_signal_and_cleanup INT' INT
  trap 'forward_signal_and_cleanup TERM' TERM

  "$@" &
  ACTIVE_CHILD_PID=$!
  wait "$ACTIVE_CHILD_PID"
  ACTIVE_CHILD_PID=0
}


main() {
  require_cmd ip
  require_cmd tc
  require_cmd sudo
  require_cmd awk

  print_header
  show_interfaces

  local iface delay_ms jitter_ms loss_pct
  iface="$(prompt_with_default "Network interface" "$DEFAULT_IFACE")"
  delay_ms="$(prompt_uint "Base one-way delay (ms)" "$DEFAULT_DELAY_MS")"
  jitter_ms="$(prompt_uint "Jitter (ms)" "$DEFAULT_JITTER_MS")"
  loss_pct="$(prompt_loss_pct)"

  clear_impairment "$iface"
  apply_impairment "$iface" "$delay_ms" "$jitter_ms" "$loss_pct"
  show_qdisc_state "$iface"

  # Build the client command, allowing additional arguments to be passed through.
  local client_cmd=("${DEFAULT_CLIENT_CMD[@]}")
  if [[ $# -gt 0 ]]; then
    client_cmd+=("$@")
  fi

  run_client "$iface" "${client_cmd[@]}"
}

main "$@"
