/**
 * @file ui_mgr_ao.cpp
 * @brief UI Manager Active Object implementation.
 *
 * The only AO that calls ui_hal. It owns both halves of the bridge:
 * user actions arriving from the UI event loop are turned into QP events
 * for PrinterAo, and queue changes coming back from PrinterAo are turned
 * into ui_hal calls.
 */

#include "ui_mgr_ao.hpp"

#include "printer_ao.hpp"
#include "ui_hal.hpp"

#include <array>

namespace app {

UiMgrAo ui_mgr_ao;

namespace {

/** @brief CMYK cartridges, matching the demo's ink page. */
constexpr std::array<ui_hal::InkLevel, 4> kInkLevels{{
    {0xFFFF00U, 0.9F},
    {0x00FFFFU, 0.5F},
    {0xFF00FFU, 0.8F},
    {0x000000U, 0.1F},
}};

/* ------------------------------------------------------------------
 * UI -> QP. These run on the Slint event-loop thread, so they do
 * nothing but allocate an event and post it.
 * ------------------------------------------------------------------ */

void onStartJob(std::string_view name) {
    auto *evt = Q_NEW(JobSubmitEvt, JOB_SUBMIT_SIG, name);
    printer_ao.POST(evt, &ui_mgr_ao);
}

void onCancelJob(std::int32_t index) {
    auto *evt = Q_NEW(JobIndexEvt, JOB_CANCEL_SIG, index);
    printer_ao.POST(evt, &ui_mgr_ao);
}

void onPauseJob(std::int32_t index) {
    auto *evt = Q_NEW(JobIndexEvt, JOB_PAUSE_SIG, index);
    printer_ao.POST(evt, &ui_mgr_ao);
}

} // namespace

//............................................................................
UiMgrAo::UiMgrAo()
  : QP::QActive(Q_STATE_CAST(&UiMgrAo::initial))
{}

//............................................................................
Q_STATE_DEF(UiMgrAo, initial) {
    Q_UNUSED_PAR(e);

    ui_hal::setCallbacks(ui_hal::Callbacks{
        .start_job = onStartJob,
        .cancel_job = onCancelJob,
        .pause_job = onPauseJob,
        .quit = [] { ui_hal::quit(); },
    });

    ui_hal::setInkLevels(kInkLevels);

    return tran(&active);
}

//............................................................................
Q_STATE_DEF(UiMgrAo, active) {
    QP::QState status;

    switch (e->sig) {
        case JOB_INSERTED_SIG: {
            auto const *evt = Q_EVT_CAST(JobChangeEvt);
            ui_hal::insertJob(evt->index, evt->job);
            status = Q_HANDLED();
            break;
        }

        case JOB_UPDATED_SIG: {
            auto const *evt = Q_EVT_CAST(JobChangeEvt);
            ui_hal::updateJob(evt->index, evt->job);
            status = Q_HANDLED();
            break;
        }

        case JOB_REMOVED_SIG: {
            auto const *evt = Q_EVT_CAST(JobChangeEvt);
            ui_hal::removeJob(evt->index);
            status = Q_HANDLED();
            break;
        }

        default: {
            status = super(&top);
            break;
        }
    }

    return status;
}

} // namespace app
