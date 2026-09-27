/**
 * @file ui_hal.cpp
 * @brief UI Hardware Abstraction Layer -- Slint implementation (ESP32 port).
 *
 * THIS IS THE ONLY FILE IN THE APPLICATION TREE THAT MAY INCLUDE slint.h OR
 * THE GENERATED printerdemo.h. All Active Objects talk to the display through
 * the ui_hal.hpp interface.
 *
 * Threading: Slint is single-threaded and owns its widgets on the task that
 * calls ui_platform::run(). The Active Objects are separate FreeRTOS tasks, so
 * every mutator here hands a copy of its arguments to
 * slint::invoke_from_event_loop() rather than touching a model directly. That
 * makes the calls asynchronous -- they are queued, not applied before
 * returning -- which is why nothing in this file returns UI state.
 *
 * Copies matter: invoke_from_event_loop runs the lambda later, so anything
 * captured by reference would dangle. Every capture below is by value.
 */

#include "ui_hal.hpp"
#include "ui_platform.hpp"

#include "printerdemo.h" // generated from ui/printerdemo.slint

#include "esp_log.h"
#include "slint-esp.h"

#include <slint-platform.h>
#include <slint.h>

#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

constexpr char TAG[] = "ui_hal";

/* -----------------------------------------------------------------------
 * State owned by the UI event-loop task.
 * ----------------------------------------------------------------------- */
std::optional<slint::ComponentHandle<MainWindow>> s_window;
std::shared_ptr<slint::VectorModel<PrinterQueueItem>> s_queue;
ui_hal::Callbacks s_callbacks;

/** @brief The status strings the .slint side matches on. */
constexpr char const *statusText(app::JobStatus status) noexcept {
    switch (status) {
        case app::JobStatus::Printing: return "printing";
        case app::JobStatus::Paused:   return "paused";
        case app::JobStatus::Waiting:  break;
    }
    return "waiting";
}

/** @brief Convert a domain job into the struct the .slint model holds. */
PrinterQueueItem toItem(app::PrintJob const &job) {
    PrinterQueueItem item;
    item.status = slint::SharedString(statusText(job.status));
    item.progress = job.progress;
    item.title = slint::SharedString(job.title.c_str());
    item.owner = slint::SharedString(job.owner.c_str());
    item.pages = job.pages;
    item.size = slint::SharedString(job.size.c_str());
    item.submission_date = slint::SharedString(job.submission_date.c_str());
    return item;
}

/**
 * @brief Queue @p f onto the UI event loop.
 *
 * Silently drops the work if the platform is not up yet, which can only
 * happen if an AO calls ui_hal before ui_platform::init().
 */
template <typename F>
void onUiThread(F &&f) {
    if (!s_window.has_value()) {
        ESP_LOGW(TAG, "UI call before ui_platform::init(); dropped");
        return;
    }
    slint::invoke_from_event_loop(std::forward<F>(f));
}

} // namespace

/* =======================================================================
 * ui_hal -- called from Active Object tasks
 * ======================================================================= */

namespace ui_hal {

void setCallbacks(Callbacks cb) {
    // Installed before the loop runs (UiMgrAo's initial transition), so this
    // is the one entry point that touches shared state directly.
    s_callbacks = std::move(cb);
}

void insertJob(std::size_t index, app::PrintJob const &job) {
    onUiThread([index, item = toItem(job)] {
        if (!s_queue) {
            return;
        }
        const std::size_t at = std::min(index, s_queue->row_count());
        s_queue->insert(at, item);
    });
}

void updateJob(std::size_t index, app::PrintJob const &job) {
    onUiThread([index, item = toItem(job)] {
        if (s_queue && index < s_queue->row_count()) {
            s_queue->set_row_data(index, item);
        }
    });
}

void removeJob(std::size_t index) {
    onUiThread([index] {
        if (s_queue && index < s_queue->row_count()) {
            s_queue->erase(index);
        }
    });
}

void setInkLevels(std::span<InkLevel const> levels) {
    // Copy out of the caller's span: the lambda outlives this call.
    std::vector<::InkLevel> converted;
    converted.reserve(levels.size());
    for (InkLevel const &level : levels) {
        converted.push_back(::InkLevel{
            .color = slint::Color::from_rgb_uint8(
                static_cast<std::uint8_t>((level.rgb >> 16) & 0xFFU),
                static_cast<std::uint8_t>((level.rgb >> 8) & 0xFFU),
                static_cast<std::uint8_t>(level.rgb & 0xFFU)),
            .level = level.level,
        });
    }

    onUiThread([converted = std::move(converted)] {
        if (s_window.has_value()) {
            (*s_window)->global<PrinterState>().set_ink_levels(
                std::make_shared<slint::VectorModel<::InkLevel>>(converted));
        }
    });
}

void quit() {
    slint::quit_event_loop();
}

} // namespace ui_hal

/* =======================================================================
 * ui_platform -- called from main.cpp only
 * ======================================================================= */

namespace ui_platform {

void init(display::Handles const &handles) {
    const bool double_buffered = handles.framebuffers.size() >= 2U;

    SlintPlatformConfiguration<slint::platform::Rgb565Pixel> config{};
    config.size = slint::PhysicalSize({display::kHRes, display::kVRes});
    config.panel_handle = handles.panel;
    config.touch_handle = handles.touch;
    // A parallel-RGB panel consumes native little-endian RGB565 straight out
    // of the framebuffer, unlike the SPI panels that need the bytes swapped.
    config.byte_swap = false;
    config.panel_type = SlintDisplayPanelType::RgbLcd;

    if (double_buffered) {
        const std::size_t pixels = display::kHRes * display::kVRes;
        config.buffer1 = std::span{
            reinterpret_cast<slint::platform::Rgb565Pixel *>(
                handles.framebuffers[0]),
            pixels};
        config.buffer2 = std::span{
            reinterpret_cast<slint::platform::Rgb565Pixel *>(
                handles.framebuffers[1]),
            pixels};
        ESP_LOGI(TAG, "Slint platform: double-buffered, %ux%u",
                 static_cast<unsigned>(display::kHRes),
                 static_cast<unsigned>(display::kVRes));
    } else {
        // No buffers set: Slint allocates one line in internal RAM and
        // flushes line by line.
        ESP_LOGI(TAG, "Slint platform: line-by-line, %ux%u",
                 static_cast<unsigned>(display::kHRes),
                 static_cast<unsigned>(display::kVRes));
    }

    slint_esp_init(config);

    s_window = MainWindow::create();
    s_queue = std::make_shared<slint::VectorModel<PrinterQueueItem>>();

    // The .slint file seeds printer-queue with sample rows so the design
    // previews well in the viewer. Replace it with our empty model: PrinterAo
    // is the only source of truth for what is queued.
    (*s_window)->global<PrinterQueue>().set_printer_queue(s_queue);

    // Forward user actions to whatever UiMgrAo installed. Guarded because the
    // window exists before the AOs have run their initial transitions.
    (*s_window)->global<PrinterQueue>().on_start_job(
        [](slint::SharedString name) {
            if (s_callbacks.start_job) {
                s_callbacks.start_job(std::string_view{name});
            }
        });

    (*s_window)->global<PrinterQueue>().on_cancel_job([](int index) {
        if (s_callbacks.cancel_job) {
            s_callbacks.cancel_job(index);
        }
    });

    (*s_window)->global<PrinterQueue>().on_pause_job([](int index) {
        if (s_callbacks.pause_job) {
            s_callbacks.pause_job(index);
        }
    });

    (*s_window)->on_quit([] {
        if (s_callbacks.quit) {
            s_callbacks.quit();
        } else {
            slint::quit_event_loop();
        }
    });
}

void run() {
    if (!s_window.has_value()) {
        ESP_LOGE(TAG, "ui_platform::run() before init()");
        return;
    }
    (*s_window)->run();
}

} // namespace ui_platform
