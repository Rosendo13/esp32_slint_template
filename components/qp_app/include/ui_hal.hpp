/**
 * @file ui_hal.hpp
 * @brief UI Hardware Abstraction Layer.
 *
 * Sits between the application layer (Active Objects) and the UI toolkit
 * (Slint). Active Objects call only these functions -- they never include
 * slint.h, the generated printerdemo.h, or touch any slint:: API directly.
 *
 * To port to a different UI toolkit, rewrite main/ui_hal.cpp only. No
 * application code changes are required.
 *
 * THREADING: every function here is safe to call from any FreeRTOS task.
 * Slint itself is not thread-safe and owns a single event loop, so the
 * implementation marshals each call onto that loop. Calls are therefore
 * asynchronous: they are queued, not applied before returning.
 */

#ifndef UI_HAL_HPP
#define UI_HAL_HPP

#include "print_job.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace ui_hal {

/** @brief One ink cartridge, as drawn on the ink page. */
struct InkLevel {
    std::uint32_t rgb;  ///< 0xRRGGBB
    float level;        ///< remaining fraction, 0.0-1.0
};

/**
 * @brief Hooks the UI calls when the user does something.
 *
 * Invoked on the UI event-loop thread, NOT on an AO thread. An
 * implementation must do nothing beyond posting a QP event.
 */
struct Callbacks {
    std::function<void(std::string_view name)> start_job;
    std::function<void(std::int32_t index)> cancel_job;
    std::function<void(std::int32_t index)> pause_job;
    std::function<void()> quit;
};

/** @brief Install the user-action hooks. Call before the event loop runs. */
void setCallbacks(Callbacks cb);

/* -----------------------------------------------------------------------
 * Printer queue. Mirrors the queue owned by PrinterAo; indices are
 * positions in that queue.
 * ----------------------------------------------------------------------- */

/** @brief Insert @p job at @p index, shifting later rows down. */
void insertJob(std::size_t index, app::PrintJob const &job);

/** @brief Replace the row at @p index in place. */
void updateJob(std::size_t index, app::PrintJob const &job);

/** @brief Remove the row at @p index. */
void removeJob(std::size_t index);

/* -----------------------------------------------------------------------
 * Ink levels
 * ----------------------------------------------------------------------- */

/** @brief Replace the ink-level model shown on the ink page. */
void setInkLevels(std::span<InkLevel const> levels);

/** @brief Ask the UI event loop to terminate. */
void quit();

} // namespace ui_hal

#endif /* UI_HAL_HPP */
