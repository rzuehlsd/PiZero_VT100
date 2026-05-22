#!/usr/bin/env bash

set -euo pipefail

# check overscan and invisuable first line
printf $'\e[1;1HTOP'

# Sends a simple CUP + text sequence for VT100 shell-client testing.
# Expected: cursor moves to row 10, col 30 and prints "HELLO".

printf $'\e[10;30HHELLO'

# Alternative (python):
# python3 -c 'import sys; sys.stdout.buffer.write(b"\x1b[10;10HHELLO")'

# additional tests

for i in $(seq 1 12); do printf "\e[%d;1HROW%02d" "$i" "$i"; done

# move test to bottom right and query cursor position (should report 24,80 or similar)
printf '\e[999;999H\e[6n'  # move to bottom right and query cursor position (should report 24,80 or similar)]
