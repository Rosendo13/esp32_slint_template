/**
 * @file main.cpp
 * @brief Entry point: bring up the display, start the Active Objects, then
 *        hand this task to the Slint event loop.
 *
 * Layering:
 *   display.cpp      panel + touch, no UI toolkit
 *   ui_hal.cpp       the only file that knows Slint exists
 *   qp_app/          PrinterAo (domain) and UiMgrAo (bridge), no Slint
 */

#include "display.hpp"
#include "printer_ao.hpp"
#include "qpcpp.hpp"
#include "ui_mgr_ao.hpp"
#include "ui_platform.hpp"

#include "esp_log.h"

#include <array>

namespace {

constexpr char TAG[] = "main";

/* -----------------------------------------------------------------------
 * QF storage. All static: the FreeRTOS port creates tasks and queues from
 * caller-supplied memory (configSUPPORT_STATIC_ALLOCATION).
 * ----------------------------------------------------------------------- */

/// Pool block big enough for the largest event in app_events.hpp.
union AppEvtPoolEl {
    app::JobSubmitEvt submit;
    app::JobIndexEvt index;
    app::JobChangeEvt change;
};
QF_MPOOL_EL(AppEvtPoolEl) appPoolSto[CONFIG_QP_SMALL_POOL_SIZE];

/// Queues sized for bursts: each queue change fans out one event to UiMgrAo,
/// and QACTIVE_POST uses QF_NO_MARGIN, so a full queue is a hard assert
/// (Q_onError -> esp_restart), i.e. a silent reboot.
std::array<QP::QEvtPtr, 32> printerQueueSto;
std::array<QP::QEvtPtr, 32> uiMgrQueueSto;

/// UiMgrAo marshals into Slint (SharedString allocation, model updates);
/// PrinterAo only shuffles PODs but formats timestamps with strftime.
std::array<StackType_t, 4096> printerStack;
std::array<StackType_t, 4096> uiMgrStack;

} // namespace

extern "C" void app_main() {
    const display::Handles handles = display::init();

    // Must precede QF startup: UiMgrAo's initial transition calls ui_hal,
    // which needs the event loop's queue to exist.
    ui_platform::init(handles);

    QP::QF::init();
    QP::QF::poolInit(appPoolSto, sizeof(appPoolSto), sizeof(appPoolSto[0]));

    // ORDER MATTERS: this port runs an AO's top-most initial transition inside
    // QActive::start(), on the *caller's* stack, before that call returns.
    // UiMgrAo's initial transition does not post to PrinterAo, but PrinterAo
    // posts to UiMgrAo as soon as a job arrives, so UiMgrAo must already have
    // its queue created. Start the consumer first regardless of priority.
    app::ui_mgr_ao.setAttr(QP::TASK_NAME_ATTR, "UiMgr");
    app::ui_mgr_ao.start(
        Q_PRIO(1U, 4U),
        uiMgrQueueSto.data(), uiMgrQueueSto.size(),
        uiMgrStack.data(), sizeof(uiMgrStack));

    app::printer_ao.setAttr(QP::TASK_NAME_ATTR, "Printer");
    app::printer_ao.start(
        Q_PRIO(2U, 5U),
        printerQueueSto.data(), printerQueueSto.size(),
        printerStack.data(), sizeof(printerStack));

    // NOTE: deliberately NOT QF::run(). The QP/C++ FreeRTOS port's run() is
    // QF::onStartup() followed by vTaskStartScheduler(), which is wrong on
    // ESP-IDF: the scheduler is already running by the time app_main() is
    // called (app_main *is* a task). A second vTaskStartScheduler() re-runs
    // vPortSetupTimer(), whose esp_intr_alloc() then fails with "No free
    // interrupt inputs"; logging that error takes a recursive newlib lock from
    // a non-yieldable context and abort()s, giving an endless reboot loop.
    // Calling the startup hook directly installs the FreeRTOS tick hook that
    // drives QP time events, which is all run() usefully did here.
    QP::QF::onStartup();

    ESP_LOGI(TAG, "Active Objects started; entering Slint event loop");

    // Blocks until ui_hal::quit(). Runs on the app_main task, so
    // CONFIG_MAIN_TASK_STACK_SIZE has to cover Slint's renderer.
    ui_platform::run();

    ESP_LOGI(TAG, "Event loop exited");
    QP::QF::onCleanup();
}
