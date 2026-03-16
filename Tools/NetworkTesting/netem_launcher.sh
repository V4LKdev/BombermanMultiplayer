#!/usr/bin/env bash
set -euo pipefail

# Linux-only helper for repeatable client latency testing.

DEFAULT_IFACE="lo"
DEFAULT_DELAY_MS=0
DEFAULT_JITTER_MS=0
DEFAULT_LOSS_PCT=0
DEFAULT_LOSS_CORRELATION_PCT=0
DEFAULT_CLIENT_CMD=(./cmake-build-debug/Bomberman)
ACTIVE_IFACE=""
ACTIVE_CHILD_PID=0
SUDO_READY=0

print_header() {
  printf '\n=== Bomberman Network Impairment Launcher ===\n'
}

cleanup() {
  if [[ -n "$ACTIVE_IFACE" ]]; then
    printf '\nCleaning up impairment on %s...\n' "$ACTIVE_IFACE"
    clear_impairment "$ACTIVE_IFACE"
    ACTIVE_IFACE=""
  fi
}

abort_script() {
  local signal_name="${1:-INT}"
  if [[ "$ACTIVE_CHILD_PID" -ne 0 ]]; then
    kill "-$signal_name" "$ACTIVE_CHILD_PID" 2>/dev/null || true
  fi
  cleanup
  printf '\nLauncher interrupted. Exiting.\n' >&2
  exit 130
}

# Checks if ip, tc, sudo, and awk are available.
require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    printf 'Missing required command: %s\n' "$1" >&2
    exit 1
  fi
}

prompt_with_default() {
  local __out_var="$1"
  local prompt="$2"
  local default="$3"
  local input=""

  if ! IFS= read -r -p "$prompt [$default]: " input < /dev/tty; then
    return 130
  fi

  if [[ -z "$input" ]]; then
    input="$default"
  fi

  printf -v "$__out_var" '%s' "$input"
}

prompt_uint() {
  local __out_var="$1"
  local label="$2"
  local default="$3"
  local value=""
  while true; do
    if ! prompt_with_default value "$label" "$default"; then
      return 130
    fi
    if [[ "$value" =~ ^[0-9]+$ ]]; then
      printf -v "$__out_var" '%s' "$value"
      return 0
    fi
    printf 'Please enter a non-negative integer.\n' >&2
  done
}

prompt_pct_in_range() {
  local __out_var="$1"
  local label="$2"
  local default="$3"
  local value
  while true; do
    if ! prompt_with_default value "$label" "$default"; then
      return 130
    fi
    if [[ "$value" =~ ^([0-9]+)(\.[0-9]+)?$ ]] && awk -v v="$value" 'BEGIN { exit !(v >= 0 && v <= 100) }'; then
      printf -v "$__out_var" '%s' "$value"
      return 0
    fi
    printf 'Please enter a percentage in the range [0, 100].\n' >&2
  done
}

prompt_loss_pct() {
  prompt_pct_in_range "$1" "Packet loss percent" "$DEFAULT_LOSS_PCT"
}

prompt_loss_correlation_pct() {
  prompt_pct_in_range "$1" "Loss correlation percent" "$DEFAULT_LOSS_CORRELATION_PCT"
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
  sudo -n tc qdisc show dev "$iface"
  printf '\n'
}


clear_impairment() {
  local iface="$1"
  if [[ "$SUDO_READY" -eq 1 ]]; then
    sudo -n tc qdisc del dev "$iface" root 2>/dev/null || true
  fi
}

ensure_sudo_session() {
  if [[ "$SUDO_READY" -eq 1 ]]; then
    return 0
  fi

  printf '\nAuthenticating sudo for tc netem changes...\n'
  if ! sudo -v < /dev/tty; then
    local rc=$?
    if [[ "$rc" -eq 130 ]]; then
      return 130
    fi
    printf 'sudo authentication failed. No impairment was applied.\n' >&2
    return "$rc"
  fi
  SUDO_READY=1
}

# Builds and applies the tc netem command based on the provided parameters.
apply_impairment() {
  local iface="$1"
  local delay_ms="$2"
  local jitter_ms="$3"
  local loss_pct="$4"
  local loss_correlation_pct="$5"

  local args=(netem)

  if [[ "$delay_ms" != "0" || "$jitter_ms" != "0" ]]; then
    args+=(delay "${delay_ms}ms")
    if [[ "$jitter_ms" != "0" ]]; then
      args+=("${jitter_ms}ms")
    fi
  fi

  if [[ "$loss_pct" != "0" && "$loss_pct" != "0.0" ]]; then
    args+=(loss "${loss_pct}%")
    if [[ "$loss_correlation_pct" != "0" && "$loss_correlation_pct" != "0.0" ]]; then
      args+=("${loss_correlation_pct}%")
    fi
  fi

  if [[ ${#args[@]} -eq 1 ]]; then
    printf 'No impairment requested. Leaving %s clean.\n' "$iface"
    clear_impairment "$iface"
    return 0
  fi

  printf 'Applying impairment on %s: tc qdisc replace dev %s root %s\n' \
    "$iface" "$iface" "${args[*]}"
  # Replace existing qdisc or add if none exists
  sudo -n tc qdisc replace dev "$iface" root "${args[@]}"
}

run_client() {
  local iface="$1"
  shift

  printf '\nLaunching Bomberman Client:\n  '
  printf '%q ' "$@"
  printf '\n\n'

  ACTIVE_IFACE="$iface"
  ACTIVE_CHILD_PID=0

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

  local iface delay_ms jitter_ms loss_pct loss_correlation_pct
  prompt_with_default iface "Network interface" "$DEFAULT_IFACE"
  prompt_uint delay_ms "Base one-way delay (ms)" "$DEFAULT_DELAY_MS"
  prompt_uint jitter_ms "Jitter (ms)" "$DEFAULT_JITTER_MS"
  prompt_loss_pct loss_pct
  if [[ "$loss_pct" != "0" && "$loss_pct" != "0.0" ]]; then
    prompt_loss_correlation_pct loss_correlation_pct
  else
    loss_correlation_pct="$DEFAULT_LOSS_CORRELATION_PCT"
  fi

  ensure_sudo_session
  clear_impairment "$iface"
  apply_impairment "$iface" "$delay_ms" "$jitter_ms" "$loss_pct" "$loss_correlation_pct"
  show_qdisc_state "$iface"

  # Build the client command, allowing additional arguments to be passed through.
  local client_cmd=("${DEFAULT_CLIENT_CMD[@]}")
  if [[ $# -gt 0 ]]; then
    client_cmd+=("$@")
  fi

  run_client "$iface" "${client_cmd[@]}"
}

trap cleanup EXIT
trap 'abort_script INT' INT
trap 'abort_script TERM' TERM

main "$@"
