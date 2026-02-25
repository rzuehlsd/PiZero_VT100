# Profiling helper (not integrated by default)

This directory contains a lightweight, scope-based profiler adapted for `VT100`:

- `profiler.h`
- `profiler.cpp`

It is currently **standalone only** and is **not** linked into `VT100`.

## What it does

- `PROFILE_SCOPE("Label")` measures elapsed microseconds for one C++ scope.
- Timings are aggregated in up to 32 slots (`count`, `avg`, `max`, `total`).
- `PROFILE_DUMP(intervalUs)` periodically logs and resets collected values.

## How to use after integration

1. Add the source file to `OBJS` in `VT100/Makefile`:

```make
$(BUILDDIR)/profiler.o
```

2. Add this include path (or move files to `include/src`):

```make
CPPFLAGS += -I$(APPHOME)/tools
CFLAGS   += -I$(APPHOME)/tools
```

3. Include in a module you want to measure:

```cpp
#include "profiler.h"
```

4. Add profiling scopes in hot paths:

```cpp
void ExampleFunction()
{
    PROFILE_SCOPE("ExampleFunction");
    // work...
}
```

5. Trigger periodic output from a safe, low-frequency loop:

```cpp
PROFILE_DUMP(10000000ULL); // every 10 seconds
```

## Integration cautions

- Keep dump intervals coarse (5-10 s or more) to avoid logging overhead.
- Do not enable profiling in release/latency-critical runs unless needed.
- This implementation does not add explicit locking for concurrent slot updates.

---

# RAW shell helper (host side)

This directory also contains a host-side helper for `shell_client` raw TCP sessions:

- `start_raw_shell_server.sh`
- `raw_shell_client.sh`

## Purpose

Starts a robust `socat` listener and launches an interactive shell behind `script(1)` so the session has a proper controlling TTY.

This avoids common issues where shell startup appears to work (`.zshrc` runs) but interactive commands behave incorrectly.

## Usage

From `VT100/tools/`:

```bash
./start_raw_shell_server.sh
```

For local/host-side interactive testing (character mode), use:

```bash
./raw_shell_client.sh 127.0.0.1 2323
```

Options:

- `--port <n>`: listen port (default `2323`)
- `--shell <path>`: shell executable (default `/bin/zsh`)
- `--kill-existing`: stop existing listeners on that port before start
- `--once`: accept one client only (no `fork`)

Examples:

```bash
./start_raw_shell_server.sh --kill-existing
./start_raw_shell_server.sh --port 4242 --shell /bin/bash
```

## Notes

- Designed for trusted local-network use with RAW TCP (no SSH transport).
- Uses `script(1)` in BSD/Linux-compatible mode detection.
- Applies `stty sane isig icanon echo -ixon -ixoff` at shell-session start.
- Avoids forcing `rawer` mode in `socat`, because this can disable signals/canonical behavior in interactive sessions.
- Uses a quote-safe temporary launcher script for `socat EXEC`, avoiding nested-shell quoting failures.
- Stop with `Ctrl-C` in the terminal running the helper.

## Client behavior caveat

Some `telnet` clients start in line mode. In that mode, single-key interactions and control keys (for example in `nano` or `vttest`) may not work as expected.

Symptoms like `stty -a` showing `-isig -icanon -echo` indicate a broken interactive terminal state for full-screen tools.

Recommended character-mode clients for host-side troubleshooting:

Preferred (bundled):

```bash
./raw_shell_client.sh <host-ip> <port>
```

```bash
nc <host-ip> <port>
```

or

```bash
socat -,raw,echo=0 TCP:<host-ip>:<port>
```

If `telnet` must be used, switch it to character mode (`Ctrl-]`, then `mode character`).
