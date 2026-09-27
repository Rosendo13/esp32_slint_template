# `qpcpp` component — QP/C++ for ESP-IDF, without forking QP/C++

`extern/qpcpp` is the upstream [QuantumLeaps/qpcpp](https://github.com/QuantumLeaps/qpcpp)
submodule, pinned at `v8.1.5` and **kept byte-identical to upstream**.
`git -C extern/qpcpp status` must always come back clean. This component is the only
place ESP32-specific QP/C++ code lives.

## Why a custom port is needed

QP/C++ v8.1.5 ships no `ports/esp-idf`. The closest match, `ports/freertos`, targets
vanilla FreeRTOS, where `taskENTER_CRITICAL()` takes no arguments. ESP-IDF's
FreeRTOS is the SMP fork and requires a `portMUX_TYPE *` spinlock. That mismatch is
expanded *inside* QP/C++'s own translation units (`qf_act.cpp`, `qf_ps.cpp`,
`qep_hsm.cpp`, …), so it cannot be papered over from the application side the way
`main/ui_hal.cpp` wraps Slint — Slint is a leaf you call into, QP/C++ is a framework
that calls you.

QP/C++ is designed for exactly this: a port is just a directory supplying
`qp_port.hpp` and `qf_port.cpp`. So instead of editing the submodule, `port/` holds
our own.

## How the shadowing works

QP/C++'s platform-independent sources use `#include "qp_port.hpp"`. They live in
`src/qf/`, which has no sibling by that name, so the include path decides —
and `CMakeLists.txt` puts `port/` first.

`qf_port.cpp` is the exception: quoted includes resolve next to the *including file*
first, so upstream's `ports/freertos/qf_port.cpp` would always find upstream's
`qp_port.hpp` no matter what `-I` order says. That is why `qf_port.cpp` is copied into
`port/` rather than shadowed. `qs_port.hpp` is copied too, purely so `port/` is
self-contained and upstream's `ports/freertos/` never needs to be on the include
path at all.

## Vendored files and their deltas

| File | Delta from upstream `ports/freertos/` |
|------|----------------------------------------|
| `port/qp_port.hpp` | `#ifdef ESP_PLATFORM` critical-section macros taking `&QF_esp_mux`; `freertos/`-prefixed kernel includes; `extern` declaration of the mux |
| `port/qf_port.cpp` | `#ifdef ESP_PLATFORM` definition of `portMUX_TYPE QF_esp_mux` |
| `port/qs_port.hpp` | none |

Each carries a provenance header naming its origin and upstream revision.

## Application-owned configuration

`qp_port.hpp` includes `"qp_config.hpp"`, which this component does **not** provide.
It lives in `components/qp_app/include/qp_config.hpp` so the application controls
`QF_MAX_ACTIVE`, pool counts and event sizing. That directory is on this component's
include path (see `CMakeLists.txt`).

## Upgrading QP/C++

```sh
git -C extern/qpcpp fetch --tags
git -C extern/qpcpp checkout <new-tag>
git -C extern/qpcpp diff <old-tag>..<new-tag> -- ports/freertos/
```

Apply anything relevant from that diff to `port/`, refresh the provenance headers,
and check whether the source list in `CMakeLists.txt` still matches
`extern/qpcpp/src/qf/`. That three-file diff is the entire maintenance cost — the
same cost a fork would carry, but confined to files this repo owns.
