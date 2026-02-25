#!/usr/bin/env python3

import fcntl
import os
import sys
import termios


def main() -> int:
    if len(sys.argv) < 3:
        print("Usage: VT100_CTTY_RUN.py <pty-path> <command> [args...]", file=sys.stderr)
        return 2

    pty_path = sys.argv[1]
    command = sys.argv[2:]

    try:
        os.setsid()
        fd = os.open(pty_path, os.O_RDWR | os.O_NOCTTY)
        fcntl.ioctl(fd, termios.TIOCSCTTY, 0)

        os.dup2(fd, 0)
        os.dup2(fd, 1)
        os.dup2(fd, 2)

        if fd > 2:
            os.close(fd)

        os.execvp(command[0], command)
    except Exception as exc:
        print(f"VT100_CTTY_RUN failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
