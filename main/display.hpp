/**
 * @file display.hpp
 * @brief Board bring-up for the 800x480 parallel-RGB panel and GT911 touch.
 *
 * Knows about GPIOs and panel timings and nothing else -- no UI toolkit, no
 * QP. main.cpp hands the resulting handles to the UI platform layer.
 */

#ifndef DISPLAY_HPP
#define DISPLAY_HPP

#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace display {

inline constexpr std::uint32_t kHRes = 800U;
inline constexpr std::uint32_t kVRes = 480U;

/** @brief Everything the UI layer needs to drive the screen. */
struct Handles {
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;

    /**
     * @brief The driver-allocated framebuffers, in PSRAM.
     *
     * Two entries when CONFIG_EXAMPLE_DOUBLE_FB is set, so the renderer can
     * double-buffer; empty otherwise, which asks the renderer to allocate a
     * one-line buffer in internal RAM and flush line by line.
     */
    std::span<std::uint16_t *> framebuffers;
};

/**
 * @brief Bring up I2C, the RGB panel and the touch controller.
 * @return Handles valid for the lifetime of the program.
 *
 * Aborts via ESP_ERROR_CHECK on any failure -- there is no usable
 * degraded mode for a display-only device.
 */
Handles init();

} // namespace display

#endif /* DISPLAY_HPP */
