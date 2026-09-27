/**
 * @file display.cpp
 * @brief RGB panel + GT911 bring-up (ESP32-S3).
 *
 * Ported from the LVGL example this template started as, with two changes:
 * the framebuffers are handed to the caller instead of to LVGL, and touch
 * goes through the current I2C master driver rather than the legacy one.
 */

#include "display.hpp"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>

namespace display {
namespace {

constexpr char TAG[] = "display";

/* ---- I2C (touch) ---- */
constexpr gpio_num_t kI2cScl = GPIO_NUM_9;
constexpr gpio_num_t kI2cSda = GPIO_NUM_8;
constexpr i2c_port_num_t kI2cPort = I2C_NUM_0;
constexpr std::uint32_t kI2cFreqHz = 400'000U;

/* ---- RGB panel ---- */
constexpr std::uint32_t kPixelClockHz = 18'000'000U;
constexpr int kPinBacklight = -1;
constexpr int kPinDispEn = -1;
constexpr int kPinHsync = 46;
constexpr int kPinVsync = 3;
constexpr int kPinDe = 5;
constexpr int kPinPclk = 7;

/// Data pins, low to high: B3..B7, G2..G7, R3..R7.
constexpr std::array<int, 16> kDataPins{
    14, 38, 18, 17, 10,      // B3 B4 B5 B6 B7
    39, 0,  45, 48, 47, 21,  // G2 G3 G4 G5 G6 G7
    1,  2,  42, 41, 40,      // R3 R4 R5 R6 R7
};

#if CONFIG_EXAMPLE_DOUBLE_FB
constexpr std::size_t kNumFramebuffers = 2U;
#else
constexpr std::size_t kNumFramebuffers = 1U;
#endif

/// Storage behind Handles::framebuffers; static so the span stays valid.
std::array<std::uint16_t *, 2> s_framebuffers{};

//............................................................................
i2c_master_bus_handle_t initI2c() {
    const i2c_master_bus_config_t bus_cfg{
        .i2c_port = kI2cPort,
        .sda_io_num = kI2cSda,
        .scl_io_num = kI2cScl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags{
            .enable_internal_pullup = true,
            .allow_pd = false,
        },
    };

    i2c_master_bus_handle_t bus = nullptr;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    ESP_LOGI(TAG, "I2C master bus ready");
    return bus;
}

//............................................................................
esp_lcd_panel_handle_t initPanel() {
#if CONFIG_EXAMPLE_DOUBLE_FB
    ESP_LOGI(TAG, "Install RGB LCD panel (2 framebuffers in PSRAM)");
#else
    ESP_LOGI(TAG, "Install RGB LCD panel (1 framebuffer in PSRAM)");
#endif

    esp_lcd_rgb_panel_config_t panel_config{};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.data_width = 16;
    // dma_burst_size shares a union with the deprecated psram_trans_align, so
    // this is the same field the LVGL-era code set to 64 -- named for what it
    // actually controls. Must stay consistent with the PSRAM/cache settings in
    // sdkconfig.defaults.
    panel_config.dma_burst_size = 64;
    panel_config.num_fbs = kNumFramebuffers;
#if CONFIG_EXAMPLE_USE_BOUNCE_BUFFER
    panel_config.bounce_buffer_size_px = 10U * kHRes;
#endif
    panel_config.disp_gpio_num = kPinDispEn;
    panel_config.pclk_gpio_num = kPinPclk;
    panel_config.vsync_gpio_num = kPinVsync;
    panel_config.hsync_gpio_num = kPinHsync;
    panel_config.de_gpio_num = kPinDe;
    std::copy(kDataPins.begin(), kDataPins.end(),
              std::begin(panel_config.data_gpio_nums));
    panel_config.timings = {
        .pclk_hz = kPixelClockHz,
        .h_res = kHRes,
        .v_res = kVRes,
        .hsync_pulse_width = 4,
        .hsync_back_porch = 8,
        .hsync_front_porch = 8,
        .vsync_pulse_width = 4,
        .vsync_back_porch = 16,
        .vsync_front_porch = 16,
        .flags{.pclk_active_neg = true},
    };
    panel_config.flags.fb_in_psram = true;

    esp_lcd_panel_handle_t panel = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    if constexpr (kPinBacklight >= 0) {
        const gpio_config_t bk_cfg{
            .pin_bit_mask = 1ULL << kPinBacklight,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&bk_cfg));
        ESP_ERROR_CHECK(gpio_set_level(static_cast<gpio_num_t>(kPinBacklight), 1));
    }

    return panel;
}

//............................................................................
/**
 * @brief Log which GT911 address actually answers on the bus.
 *
 * The GT911 latches its I2C address from the INT/RST strapping at power-up:
 * 0x5D or 0x14. This board wires neither pin to the MCU (both are -1), so the
 * address cannot be forced and depends on the board's own pull resistors.
 * Probing turns "touch does nothing" into a one-line answer.
 */
void probeTouchAddresses(i2c_master_bus_handle_t bus) {
    constexpr std::array<std::uint16_t, 2> kCandidates{0x5D, 0x14};
    for (std::uint16_t addr : kCandidates) {
        const esp_err_t err = i2c_master_probe(bus, addr, 100);
        ESP_LOGI(TAG, "GT911 probe 0x%02X: %s", addr,
                 (err == ESP_OK) ? "ACK" : esp_err_to_name(err));
    }
}

esp_lcd_touch_handle_t initTouch(i2c_master_bus_handle_t bus) {
    probeTouchAddresses(bus);

    // Deliberately NOT ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG(): that macro lists
    // .scl_speed_hz (the last struct member) before .control_phase_bytes, and
    // C++ requires designated initializers in declaration order. Assigning
    // field by field keeps the same values without depending on that order.
    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 16;
    io_config.flags.disable_control_phase = 1;
    io_config.scl_speed_hz = kI2cFreqHz;

    esp_lcd_panel_io_handle_t io_handle = nullptr;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bus, &io_config, &io_handle));

    const esp_lcd_touch_config_t tp_cfg{
        .x_max = kHRes,
        .y_max = kVRes,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels{
            .reset = 0,
            .interrupt = 0,
        },
        .flags{
            .swap_xy = 0,
            // The GT911 already reports Y in the same direction the panel
            // scans and the renderer lays out, so it must NOT be mirrored.
            // With mirror_y = 1 a press lands at (y_max - y), i.e. the same
            // distance from the opposite edge, so widgets respond mirrored
            // about the horizontal centre line while the image itself is
            // drawn correctly.
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    esp_lcd_touch_handle_t touch = nullptr;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(io_handle, &tp_cfg, &touch));
    ESP_LOGI(TAG, "GT911 touch controller ready");

#if CONFIG_TOUCH_BOOT_DIAGNOSTIC
    // Sample the controller directly, before Slint owns it, so a dead bus can
    // be told apart from coordinates that arrive but land in the wrong place.
    // Runs to completion here, so there is no concurrent reader.
    ESP_LOGI(TAG, "Touch diagnostic: press the screen now (%d s)...",
             CONFIG_TOUCH_BOOT_DIAGNOSTIC_SECONDS);

    const int iterations = CONFIG_TOUCH_BOOT_DIAGNOSTIC_SECONDS * 20;
    int reported = 0;
    for (int i = 0; i < iterations; ++i) {
        const esp_err_t err = esp_lcd_touch_read_data(touch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Touch diagnostic: read failed: %s",
                     esp_err_to_name(err));
            break;
        }

        std::array<std::uint16_t, 1> x{};
        std::array<std::uint16_t, 1> y{};
        std::uint8_t count = 0;
        if (esp_lcd_touch_get_coordinates(touch, x.data(), y.data(), nullptr,
                                          &count, 1)
            && count > 0U) {
            ESP_LOGI(TAG, "Touch diagnostic: point at x=%u y=%u", x[0], y[0]);
            ++reported;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (reported == 0) {
        ESP_LOGW(TAG, "Touch diagnostic: no points seen. The bus reads OK but "
                      "the controller reported no contact.");
    } else {
        ESP_LOGI(TAG, "Touch diagnostic: %d point(s); expected range is "
                      "x 0-%u, y 0-%u",
                 reported, static_cast<unsigned>(kHRes),
                 static_cast<unsigned>(kVRes));
    }
#endif

    return touch;
}

} // namespace

//............................................................................
Handles init() {
    i2c_master_bus_handle_t bus = initI2c();
    esp_lcd_panel_handle_t panel = initPanel();
    esp_lcd_touch_handle_t touch = initTouch(bus);

    std::span<std::uint16_t *> fbs{};
#if CONFIG_EXAMPLE_DOUBLE_FB
    // Hand the renderer the driver's own buffers so flushing is a page flip
    // rather than a copy.
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(
        panel, 2,
        reinterpret_cast<void **>(&s_framebuffers[0]),
        reinterpret_cast<void **>(&s_framebuffers[1])));
    fbs = std::span{s_framebuffers};
#endif

    return Handles{
        .panel = panel,
        .touch = touch,
        .framebuffers = fbs,
    };
}

} // namespace display
