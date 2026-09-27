/**
 * @file ui_mgr_ao.hpp
 * @brief UI Manager Active Object -- the only AO that talks to the UI HAL.
 *
 * Runs the bridge in both directions:
 *   UI  -> QP : Slint callbacks (start/cancel/pause/quit) become QP events
 *               posted to PrinterAo.
 *   QP  -> UI : JOB_INSERTED / JOB_UPDATED / JOB_REMOVED from PrinterAo
 *               become ui_hal calls.
 */

#ifndef UI_MGR_AO_HPP
#define UI_MGR_AO_HPP

#include "app_events.hpp"
#include "qpcpp.hpp"

namespace app {

class UiMgrAo : public QP::QActive {
public:
    UiMgrAo();

private:
    Q_STATE_DECL(initial);
    Q_STATE_DECL(active);
};

/** @brief The single UiMgrAo instance. */
extern UiMgrAo ui_mgr_ao;

} // namespace app

#endif /* UI_MGR_AO_HPP */
