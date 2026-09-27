/**
 * @file app_events.hpp
 * @brief Signals and event classes exchanged between Active Objects.
 *
 * The project builds with QEVT_PAR_INIT (see qp_config.hpp), so Q_NEW()
 * calls evt->init(args...) right after carving the event out of the pool.
 * Every event class below therefore provides init() overloads instead of
 * constructors -- QF never runs a constructor on a pool event.
 */

#ifndef APP_EVENTS_HPP
#define APP_EVENTS_HPP

#include "print_job.hpp"
#include "qpcpp.hpp"

#include <cstdint>
#include <string_view>

namespace app {

/** @brief Application signals. */
enum AppSignal : QP::QSignal {
    /* UI -> PrinterAo: user actions coming off the Slint event loop */
    JOB_SUBMIT_SIG = QP::Q_USER_SIG,  ///< submit a new job by name
    JOB_CANCEL_SIG,                   ///< cancel the job at an index
    JOB_PAUSE_SIG,                    ///< pause/resume the job at an index

    /* PrinterAo internal */
    PRINT_TICK_SIG,  ///< periodic progress tick for the head job

    /* PrinterAo -> UiMgrAo: one incremental change to the queue */
    JOB_INSERTED_SIG,
    JOB_UPDATED_SIG,
    JOB_REMOVED_SIG,

    MAX_APP_SIG
};

/** @brief Submit a job with the given title (UI -> PrinterAo). */
class JobSubmitEvt : public QP::QEvt {
public:
    FixedString<48> title;

    void init(std::string_view name) noexcept { title.assign(name); }
    void init(QP::QEvt::DynEvt) noexcept { title.assign({}); }
};

/** @brief Address a job by its position in the queue (UI -> PrinterAo). */
class JobIndexEvt : public QP::QEvt {
public:
    std::int32_t index;

    void init(std::int32_t idx) noexcept { index = idx; }
    void init(QP::QEvt::DynEvt) noexcept { index = -1; }
};

/**
 * @brief One incremental queue change (PrinterAo -> UiMgrAo).
 *
 * Carrying the whole record keeps the two AOs from sharing mutable state:
 * PrinterAo owns the queue, UiMgrAo owns the UI, and this event is the only
 * thing that crosses between them. It maps one-to-one onto the insert /
 * update / erase operations of the Slint model behind the UI HAL.
 */
class JobChangeEvt : public QP::QEvt {
public:
    PrintJob job;
    std::uint16_t index;

    void init(std::uint16_t idx, PrintJob const &j) noexcept {
        index = idx;
        job = j;
    }
    void init(std::uint16_t idx) noexcept { index = idx; }
    void init(QP::QEvt::DynEvt) noexcept { index = 0U; }
};

} // namespace app

#endif /* APP_EVENTS_HPP */
