#!/usr/bin/env bash
set -euo pipefail

PORT=2323
SHELL_BIN="/bin/zsh"
KILL_EXISTING=0
ONCE=0
SOCAT_LOG_OPTS="-d -d"

usage() {
  cat <<'EOF'
start_raw_shell_server.sh - robust RAW TCP shell endpoint for VT100 shell_client mode

Usage:
  ./start_raw_shell_server.sh [options]

Options:
  -p, --port <port>       Listen port (default: 2323)
  -s, --shell <path>      Shell executable (default: /bin/zsh)
      --kill-existing     Kill existing socat listener on selected port before start
      --once              Serve exactly one client (no fork)
  -h, --help              Show this help

Behavior:
  - Starts a TCP listener with socat and spawns an interactive login shell per session.
  - Applies a strict stty baseline (sane, isig, icanon, echo) for better control-key behavior.
  - Uses PTY options without forcing global raw/no-signal mode.

Client note:
  - Prefer VT100 shell_client directly, or use nc/socat clients for character mode.
  - telnet clients may default to line mode (control keys and single-char input delayed).

Examples:
  ./start_raw_shell_server.sh
  ./start_raw_shell_server.sh --port 4242 --kill-existing
  ./start_raw_shell_server.sh --shell /bin/bash --once

Stop:
  Ctrl-C in this terminal, or kill the socat process.
EOF
}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--port)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      PORT="$2"
      shift 2
      ;;
    -s|--shell)
      [[ $# -ge 2 ]] || fail "missing value for $1"
      SHELL_BIN="$2"
      shift 2
      ;;
    --kill-existing)
      KILL_EXISTING=1
      shift
      ;;
    --once)
      ONCE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      fail "unknown argument: $1 (use --help)"
      ;;
  esac
done

[[ "$PORT" =~ ^[0-9]+$ ]] || fail "port must be numeric"
(( PORT >= 1 && PORT <= 65535 )) || fail "port must be in range 1..65535"
[[ -x "$SHELL_BIN" ]] || fail "shell not executable: $SHELL_BIN"

SOCAT_BIN="$(command -v socat || true)"
[[ -n "$SOCAT_BIN" ]] || fail "socat not found in PATH"

LAUNCHER_SCRIPT="$(mktemp -t vt100-raw-shell.XXXXXX)"
cleanup() {
  rm -f "$LAUNCHER_SCRIPT"
}
trap cleanup EXIT INT TERM

cat >"$LAUNCHER_SCRIPT" <<EOF
#!/usr/bin/env bash
stty sane isig icanon echo -ixon -ixoff 2>/dev/null || true
exec "${SHELL_BIN}" -il
EOF
chmod +x "$LAUNCHER_SCRIPT"

if (( KILL_EXISTING == 1 )); then
  if command -v lsof >/dev/null 2>&1; then
    existing_pids=()
    while IFS= read -r pid; do
      [[ -n "$pid" ]] && existing_pids+=("$pid")
    done < <(lsof -tiTCP:"$PORT" -sTCP:LISTEN 2>/dev/null || true)
    if (( ${#existing_pids[@]} > 0 )); then
      echo "Stopping existing listener(s) on port $PORT: ${existing_pids[*]}"
      kill "${existing_pids[@]}" || true
      sleep 0.2
    fi
  else
    echo "Note: lsof not found; --kill-existing could not probe listeners." >&2
  fi
fi

EXEC_CMD="$LAUNCHER_SCRIPT"

LISTEN_SPEC="TCP-LISTEN:${PORT},reuseaddr"
if (( ONCE == 0 )); then
  LISTEN_SPEC+=" ,fork"
  LISTEN_SPEC="${LISTEN_SPEC/ ,/,}"
fi

echo "Starting RAW shell server"
echo "  port : $PORT"
echo "  shell: $SHELL_BIN"
echo "  mode : $([[ $ONCE -eq 1 ]] && echo one-client || echo multi-client)"
echo "Connect from VT100 shell_client to host:${PORT}"
echo "Waiting for incoming shell client connections..."
echo "RAW shell server ready (listening on ${PORT})"
exec "$SOCAT_BIN" $SOCAT_LOG_OPTS "$LISTEN_SPEC" "EXEC:${EXEC_CMD},pty,setsid,ctty,stderr"
