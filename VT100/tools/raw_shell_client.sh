#!/usr/bin/env bash
set -euo pipefail

HOST=""
PORT=2323
ESC_HEX="0x1d" # Ctrl-]

usage() {
  cat <<'EOF'
raw_shell_client.sh - character-mode RAW TCP client for VT100 shell_client testing

Usage:
  ./raw_shell_client.sh <host> [port]

Examples:
  ./raw_shell_client.sh 127.0.0.1
  ./raw_shell_client.sh 192.168.2.10 2323

Behavior:
  - Connects using socat in character mode: raw + echo=0
  - Avoids common line-buffering behavior seen with some nc/telnet setups
  - Exit with Ctrl-] (0x1d)

Notes:
  - This is for trusted LAN RAW TCP sessions (no SSH encryption/auth).
EOF
}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi

HOST="${1:-}"
[[ -n "$HOST" ]] || fail "missing host (use --help)"

if [[ -n ${2:-} ]]; then
  PORT="$2"
fi

[[ "$PORT" =~ ^[0-9]+$ ]] || fail "port must be numeric"
(( PORT >= 1 && PORT <= 65535 )) || fail "port must be in range 1..65535"

SOCAT_BIN="$(command -v socat || true)"
[[ -n "$SOCAT_BIN" ]] || fail "socat not found in PATH"

echo "Connecting RAW TCP"
echo "  host: $HOST"
echo "  port: $PORT"
echo "Exit: Ctrl-]"

exec "$SOCAT_BIN" -,raw,echo=0,escape="$ESC_HEX" "TCP:${HOST}:${PORT}"
