#!/usr/bin/env python3

import signal
import sys

PREFIX = b"SIMHOST: "
SHUTDOWN_SEQUENCE = b"exit\r"

IGNORE_PREFIXES = (
    b"wlan-log:",
    b"Welcome to the Circle WLAN logging console",
    b"WLAN mode is active.",
    b"Log output is mirrored here",
    b"Type 'help' for a list of commands.",
    b"Host bridge mode auto-enabled by config.",
    b"Press Ctrl-C or type +++ to return to command mode.",
)


def _write_remote(payload: bytes) -> None:
    if not payload:
        return
    sys.stdout.buffer.write(payload)
    sys.stdout.buffer.flush()


def _is_ignored_remote_line(line: bytes) -> bool:
    for prefix in IGNORE_PREFIXES:
        if line.startswith(prefix):
            return True
    return False


def _is_echo_candidate(line: bytes) -> bool:
    if not line:
        return False
    for ch in line:
        if ch in (9,):
            continue
        if ch < 32 or ch > 126:
            return False
    return True


def _handle_sigint(_signum: int, _frame) -> None:
    _write_remote(SHUTDOWN_SEQUENCE)
    raise SystemExit(0)


def _handle_sigterm(_signum: int, _frame) -> None:
    _write_remote(SHUTDOWN_SEQUENCE)
    raise SystemExit(0)


def _pop_next_line(buffer: bytes):
    for index, ch in enumerate(buffer):
        if ch != 10 and ch != 13:
            continue

        next_index = index + 1
        if next_index < len(buffer):
            nxt = buffer[next_index]
            if (ch == 13 and nxt == 10) or (ch == 10 and nxt == 13):
                next_index += 1

        line = buffer[:index]
        remainder = buffer[next_index:]
        return line, remainder

    return None, buffer


def main() -> int:
    signal.signal(signal.SIGINT, _handle_sigint)
    signal.signal(signal.SIGTERM, _handle_sigterm)

    pending = b""

    while True:
        chunk = sys.stdin.buffer.read1(4096)
        if not chunk:
            return 0

        pending += chunk

        while True:
            line, pending = _pop_next_line(pending)
            if line is None:
                break
            line = line.rstrip(b"\r")

            if not line:
                continue
            if line.startswith(PREFIX):
                continue
            if _is_ignored_remote_line(line):
                continue
            if not _is_echo_candidate(line):
                continue

            _write_remote(PREFIX + line + b"\r\n")


if __name__ == "__main__":
    raise SystemExit(main())
