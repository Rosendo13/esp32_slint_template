/**
 * @file ui_platform.hpp
 * @brief Lifecycle of the UI toolkit, kept out of the app-facing ui_hal.hpp.
 *
 * ui_hal.hpp is what Active Objects include, so it stays free of both Slint
 * and esp_lcd types. Only main.cpp includes this header.
 */

#ifndef UI_PLATFORM_HPP
#define UI_PLATFORM_HPP

#include "display.hpp"

namespace ui_platform {

/**
 * @brief Initialize the UI toolkit and build the window.
 *
 * Must run before any ui_hal call and before the Active Objects start,
 * because ui_hal queues work onto the event loop this sets up.
 */
void init(display::Handles const &handles);

/**
 * @brief Run the UI event loop on the calling task. Returns on ui_hal::quit().
 *
 * The Slint event loop is single-threaded and owns every widget, so this
 * task -- and only this task -- may touch Slint APIs directly.
 */
void run();

} // namespace ui_platform

#endif /* UI_PLATFORM_HPP */
