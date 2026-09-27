/**
 * @file printer_ao.hpp
 * @brief Printer Active Object -- owns the print queue.
 *
 * Pure domain logic: this AO holds the only copy of the queue and never
 * touches the UI. Every change it makes is announced to UiMgrAo as a
 * JOB_INSERTED / JOB_UPDATED / JOB_REMOVED event.
 */

#ifndef PRINTER_AO_HPP
#define PRINTER_AO_HPP

#include "app_events.hpp"
#include "print_job.hpp"
#include "qpcpp.hpp"

#include <array>
#include <cstddef>

namespace app {

/**
 * @brief The printer's job queue and its state machine.
 *
 * @verbatim
 *  operational
 *  +-- idle      no jobs queued
 *  +-- printing  head job advancing on a 1 s tick
 *  +-- paused    head job held, tick disarmed
 * @endverbatim
 *
 * JOB_SUBMIT and JOB_CANCEL are handled in `operational` so they work in
 * every substate.
 */
class PrinterAo : public QP::QActive {
public:
    /** @brief Hard cap on queued jobs; submissions past this are dropped. */
    static constexpr std::size_t kMaxJobs = 16U;

    PrinterAo();

private:
    std::array<PrintJob, kMaxJobs> queue_;
    std::size_t count_{0U};
    QP::QTimeEvt tick_evt_;

    /* queue helpers -- each one announces its change to UiMgrAo */
    bool append(std::string_view title);
    void erase(std::size_t index);
    void publishUpdate(std::size_t index);

    [[nodiscard]] bool empty() const noexcept { return count_ == 0U; }

    /* state handlers */
    Q_STATE_DECL(initial);
    Q_STATE_DECL(operational);
    Q_STATE_DECL(idle);
    Q_STATE_DECL(printing);
    Q_STATE_DECL(paused);
};

/** @brief The single PrinterAo instance. */
extern PrinterAo printer_ao;

} // namespace app

#endif /* PRINTER_AO_HPP */
