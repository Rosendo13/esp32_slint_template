# esp32_slint_template

An ESP-IDF template for an 800x480 touch display driven by **QP/C++** active
objects and a **Slint** user interface, using Slint's printer demo as the
starting UI.

The point of the template is the seam between the two: application logic lives
in hierarchical state machines that know nothing about the UI toolkit, and the
UI toolkit is reached through one narrow interface that can be reimplemented
without touching application code.

## Layering

```
main/main.cpp          entry point: display -> UI platform -> AOs -> event loop
main/display.cpp       RGB panel + GT911 bring-up. No UI toolkit.
main/ui_hal.cpp        THE ONLY FILE THAT INCLUDES SLINT.
main/ui/               the .slint sources, fonts and images
components/qp_app/     PrinterAo (domain) + UiMgrAo (bridge). No Slint.
components/qpcpp/      ESP-IDF port of QP/C++ around a pristine submodule
components/qp_fw_glue/ QF::onStartup/onCleanup and Q_onError
```

`components/qp_app/include/ui_hal.hpp` is the seam. Active objects call only
those functions; porting to a different toolkit means rewriting
`main/ui_hal.cpp` and nothing else.

### Who owns what

`PrinterAo` owns the print queue and is pure domain logic — it never calls the
UI. Every change it makes leaves as one `JOB_INSERTED` / `JOB_UPDATED` /
`JOB_REMOVED` event carrying the affected record. `UiMgrAo` receives those and
turns them into `ui_hal` calls, and in the other direction turns Slint's
callbacks into QP events. Neither AO shares mutable state with the other.

```
 Slint callbacks ──> UiMgrAo ──JOB_SUBMIT/CANCEL/PAUSE──> PrinterAo
                        ^                                    │
                        └────JOB_INSERTED/UPDATED/REMOVED─────┘
```

`PrinterAo`'s state machine:

```
operational            handles JOB_SUBMIT and JOB_CANCEL in every substate
├── idle               queue empty
├── printing           head job advancing on a 1 s QTimeEvt
└── paused             head job held, tick disarmed
```

## Hardware

- ESP32-S3 with octal PSRAM and **8 MB flash**
- 800x480 parallel-RGB panel (16-bit, pins in `main/display.cpp`)
- GT911 capacitive touch on I2C0 (SDA 8, SCL 9)

Different panel or pinout: edit the constants at the top of
`main/display.cpp`. Nothing else in the tree knows the resolution.

## Prerequisites

- ESP-IDF **v6.0** (tested against `6.0.0`)
- The submodules:
  ```sh
  git submodule update --init --recursive
  ```

**Rust is not required.** Slint's runtime is written in Rust, but the
`slint/slint` component downloads a prebuilt `libslint_cpp.a` for
`xtensa-esp32s3-none-elf` and a host `slint-compiler` from the Slint GitHub
release. No cargo invocation happens during the build. See
`rust-toolchain.toml` for the one fallback case where a toolchain would be
needed.

## Build and flash

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXX flash monitor
```

Current image size is ~1.65 MB against the 4 MB `factory` partition in
`partitions.csv`.

## Changing the UI

Edit the files under `main/ui/`. `slint_target_sources()` in
`main/CMakeLists.txt` compiles `ui/printerdemo.slint` into `printerdemo.h`;
imported `.slint` files, fonts and images are tracked as dependencies, so
touching any of them re-runs the Slint compiler.

If you add a callback or a global property that application code must reach,
add a corresponding function to `ui_hal.hpp` and implement it in
`main/ui_hal.cpp` — do not include `printerdemo.h` anywhere else.

## Threading

Slint is single-threaded and owns its widgets on the task that calls
`ui_platform::run()` (which is `app_main`). Active objects are separate
FreeRTOS tasks, so every mutator in `main/ui_hal.cpp` hands a **copy** of its
arguments to `slint::invoke_from_event_loop()`. Those calls are asynchronous —
queued, not applied before returning — which is why no `ui_hal` function
returns UI state. Work queued before the loop starts is fine; it drains once
`run()` is reached.

## Things worth knowing

- **`Q_NEW` does not run a constructor.** QF carves the event out of a memory
  pool and casts. Event payloads must therefore be trivially copyable and fixed
  size — hence `FixedString<N>` in `print_job.hpp` rather than `std::string`.
  The project builds with `QEVT_PAR_INIT`, so `Q_NEW` calls `evt->init(...)`
  immediately after allocating.
- **`QF::run()` is deliberately not called.** On ESP-IDF the scheduler is
  already running when `app_main` starts, and the port's `run()` would call
  `vTaskStartScheduler()` a second time, ending in a reboot loop. `main.cpp`
  calls `QF::onStartup()` directly; see the comment there.
- **AO start order matters.** This port runs an AO's initial transition inside
  `QActive::start()`, on the caller's stack. Start `UiMgrAo` before
  `PrinterAo` so its queue exists before anything posts to it.
- **`CONFIG_ESP_MAIN_TASK_STACK_SIZE` is 150000**, matching the upstream Slint
  ESP-IDF demo, because `app_main` becomes the event-loop task and carries the
  software renderer. This is the one value not validated on hardware here;
  measure with `uxTaskGetStackHighWaterMark(nullptr)` before trimming it, and
  note that it comes out of internal DRAM.
- **Posting with `QF_NO_MARGIN` asserts on a full queue**, which routes to
  `Q_onError` and `esp_restart()` — i.e. a silent reboot. Queue depths are in
  `main.cpp`.

## Troubleshooting the display

### Image offset horizontally (e.g. the left edge appears mid-screen)

Almost always a PSRAM/EDMA bandwidth or alignment problem, not a rendering bug.
The RGB peripheral streams the framebuffer out of PSRAM continuously; if a
burst arrives late or misaligned, the line shifts and stays shifted.

Check in this order:

1. **`CONFIG_ESP32S3_DATA_CACHE_LINE_32B` must stay 32B.** A 64B line reliably
   produces a ~half-screen offset on this board. See the comment in
   `sdkconfig.defaults`.
2. **`dma_burst_size = 64`** in `main/display.cpp` must stay consistent with
   the cache settings.
3. **Reduce PSRAM read pressure.** Double-buffering has the panel DMA and the
   renderer hitting PSRAM at once. Fall back to line-by-line rendering with a
   bounce buffer, which is Espressif's recommended setup for an RGB panel with
   its framebuffer in PSRAM:
   ```
   idf.py menuconfig
     Project Configuration -> [ ] Use double Frame Buffer
                              [*] Use bounce buffer
   ```
   Slint then renders into an internal-RAM line buffer instead of the PSRAM
   framebuffers. Slower, but much easier on the memory bus.
4. **Lower `kPixelClockHz`** in `main/display.cpp` (18 MHz default). Dropping to
   12–14 MHz trades refresh rate for DMA headroom and is a quick way to confirm
   bandwidth as the cause.

### Touch does nothing, or lands in the wrong place

First rule out the display being offset — if the image is shifted, taps are
correct but the *picture* isn't, which looks identical to broken touch.

At boot the log reports which address answered:

```
display: GT911 probe 0x5D: ACK
display: GT911 probe 0x14: ESP_ERR_NOT_FOUND
```

- **Neither address ACKs** — wiring, pull-ups or power, not software. This
  board ties the GT911 INT/RST pins away from the MCU (`rst_gpio_num` and
  `int_gpio_num` are both `-1`), so the address is fixed by the board's own
  strapping and cannot be reassigned in firmware.
- **0x14 ACKs instead of 0x5D** — change `io_config.dev_addr` in
  `initTouch()` to `0x14`.
- **An address ACKs but presses land wrong** — it's an axis mapping issue.
  Adjust `swap_xy` / `mirror_x` / `mirror_y` in the `esp_lcd_touch_config_t`
  in `main/display.cpp`. Note these were carried over from the LVGL build;
  Slint maps coordinates independently, so the LVGL-correct values are not
  automatically right here.

## Upgrading dependencies

- **QP/C++**: bump the `extern/qpcpp` tag, then follow
  `components/qpcpp/README.md` — the ESP-IDF port is three vendored files with
  a documented diff against upstream's `ports/freertos/`.
- **Slint**: bump `slint/slint` in `main/idf_component.yml`. The UI assets in
  `main/ui/` came from the `v1.18.0` tag of the Slint repo
  (`demos/printerdemo_mcu/ui`, with `fonts`/`images` resolved from
  `demos/printerdemo/ui`).
