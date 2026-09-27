/**
 * @file bsp.cpp
 * @brief QP/C++ framework callbacks for ESP-IDF.
 *
 * Supplies the hooks QP/C++ leaves to the target: the periodic tick that
 * drives QTimeEvt, and the error handler behind Q_ASSERT/Q_REQUIRE.
 */

#include "qpcpp.hpp"

#include "esp_freertos_hooks.h"
#include "esp_log.h"
#include "esp_system.h"

namespace {

constexpr char TAG[] = "bsp";

/** @brief Nonzero once QF::onStartup() has run; gates the tick hook. */
volatile int qf_run_active = 0;

IRAM_ATTR void freertos_tick_hook() {
    if (qf_run_active != 0) {
        BaseType_t higher_prio_task_woken = pdFALSE;

        // drive QP time events at rate 0
        QP::QTimeEvt::TICK_FROM_ISR(&higher_prio_task_woken, nullptr);

        if (higher_prio_task_woken != pdFALSE) {
            portYIELD_FROM_ISR();
        }
    }
}

} // namespace

namespace QP::QF {

//............................................................................
void onStartup() {
    esp_register_freertos_tick_hook_for_cpu(freertos_tick_hook, APP_CPU_NUM);
    qf_run_active = 1;
    ESP_LOGI(TAG, "QF started.");
}

//............................................................................
void onCleanup() {
    qf_run_active = 0;
    esp_deregister_freertos_tick_hook_for_cpu(freertos_tick_hook, APP_CPU_NUM);
}

} // namespace QP::QF

//............................................................................
// Declared extern "C" and [[noreturn]] by qsafe.h.
extern "C" Q_NORETURN Q_onError(char const *const module, int_t const id) {
    ESP_LOGE(TAG, "ERROR in %s:%d", module, id);
    esp_restart();
}
