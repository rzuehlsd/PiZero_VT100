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
