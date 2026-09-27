/**
 * @file printer_ao.cpp
 * @brief Printer Active Object implementation.
 *
 * Owns the print queue. Contains no UI code at all -- every change to the
 * queue leaves this AO as a JOB_INSERTED / JOB_UPDATED / JOB_REMOVED event
 * addressed to UiMgrAo.
 */

#include "printer_ao.hpp"

#include "ui_mgr_ao.hpp"

#include <cstdio>
#include <ctime>

namespace app {

PrinterAo printer_ao;

namespace {

/** @brief Ticks between progress steps. 1 s at the configured tick rate. */
constexpr std::uint32_t kTickPeriod = configTICK_RATE_HZ;

/** @brief Progress added to the head job on every tick, in percent. */
constexpr std::uint8_t kProgressStep = 1U;

/** @brief Format "HH:MM:SS DD/MM/YYYY" for a job's submission stamp. */
void formatNow(FixedString<24> &out) noexcept {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    char buf[24] = {0};
    if (localtime_r(&now, &tm_buf) != nullptr) {
        std::strftime(buf, sizeof(buf), "%H:%M:%S %d/%m/%y", &tm_buf);
    }
    out.assign(buf);
}

} // namespace

//............................................................................
PrinterAo::PrinterAo()
  : QP::QActive(Q_STATE_CAST(&PrinterAo::initial)),
    tick_evt_(this, PRINT_TICK_SIG, 0U)
{}

//............................................................................
// Queue mutators. Each one tells UiMgrAo what changed, so the UI model and
// this queue stay index-for-index identical without sharing memory.
//............................................................................

bool PrinterAo::append(std::string_view title) {
    if (count_ >= kMaxJobs) {
        return false; // queue full -- drop rather than overwrite
    }

    const std::size_t index = count_;
    PrintJob &job = queue_[index];

    job.title.assign(title);
    job.owner.assign("joe@example.com");
    job.size.assign("100kB");
    job.pages = 1U;
    job.progress = 0U;
    job.status = JobStatus::Waiting;
    formatNow(job.submission_date);

    ++count_;

    auto *evt = Q_NEW(JobChangeEvt, JOB_INSERTED_SIG,
                      static_cast<std::uint16_t>(index), job);
    ui_mgr_ao.POST(evt, this);
    return true;
}

void PrinterAo::erase(std::size_t index) {
    if (index >= count_) {
        return;
    }

    for (std::size_t i = index + 1U; i < count_; ++i) {
        queue_[i - 1U] = queue_[i];
    }
    --count_;

    auto *evt = Q_NEW(JobChangeEvt, JOB_REMOVED_SIG,
                      static_cast<std::uint16_t>(index));
    ui_mgr_ao.POST(evt, this);
}

void PrinterAo::publishUpdate(std::size_t index) {
    if (index >= count_) {
        return;
    }

    auto *evt = Q_NEW(JobChangeEvt, JOB_UPDATED_SIG,
                      static_cast<std::uint16_t>(index), queue_[index]);
    ui_mgr_ao.POST(evt, this);
}

//............................................................................
// State machine
//............................................................................

Q_STATE_DEF(PrinterAo, initial) {
    Q_UNUSED_PAR(e);
    return tran(&operational);
}

//............................................................................
// Handles the events that make sense in every substate, so `idle`,
// `printing` and `paused` only have to describe what is different.
Q_STATE_DEF(PrinterAo, operational) {
    QP::QState status;

    switch (e->sig) {
        case Q_INIT_SIG: {
            status = tran(&idle);
            break;
        }

        case JOB_SUBMIT_SIG: {
            auto const *evt = Q_EVT_CAST(JobSubmitEvt);
            const bool was_empty = empty();
            if (append(evt->title.view()) && was_empty) {
                // first job in an empty queue: start printing it
                status = tran(&printing);
            } else {
                status = Q_HANDLED();
            }
            break;
        }

        case JOB_CANCEL_SIG: {
            auto const *evt = Q_EVT_CAST(JobIndexEvt);
            if (evt->index < 0) {
                status = Q_HANDLED();
                break;
            }

            const auto index = static_cast<std::size_t>(evt->index);
            const bool was_head = (index == 0U);
            erase(index);

            if (empty()) {
                status = tran(&idle);
            } else if (was_head) {
                // a new job is at the head: re-enter `printing` so it is
                // marked Printing and the tick restarts from a full period
                status = tran(&printing);
            } else {
                // a queued job went away; the head is unaffected
                status = Q_HANDLED();
            }
            break;
        }

        default: {
            status = super(&top);
            break;
        }
    }

    return status;
}

//............................................................................
Q_STATE_DEF(PrinterAo, idle) {
    QP::QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG:
        case Q_EXIT_SIG: {
            status = Q_HANDLED();
            break;
        }

        default: {
            status = super(&operational);
            break;
        }
    }

    return status;
}

//............................................................................
Q_STATE_DEF(PrinterAo, printing) {
    QP::QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG: {
            if (!empty()) {
                queue_[0].status = JobStatus::Printing;
                publishUpdate(0U);
            }
            tick_evt_.armX(kTickPeriod, kTickPeriod);
            status = Q_HANDLED();
            break;
        }

        case Q_EXIT_SIG: {
            tick_evt_.disarm();
            status = Q_HANDLED();
            break;
        }

        case PRINT_TICK_SIG: {
            if (empty()) {
                status = tran(&idle);
                break;
            }

            PrintJob &head = queue_[0];
            if (head.progress >= 100U) {
                erase(0U); // job finished
                status = empty() ? tran(&idle) : tran(&printing);
            } else {
                head.progress =
                    static_cast<std::uint8_t>(head.progress + kProgressStep);
                publishUpdate(0U);
                status = Q_HANDLED();
            }
            break;
        }

        case JOB_PAUSE_SIG: {
            status = tran(&paused);
            break;
        }

        default: {
            status = super(&operational);
            break;
        }
    }

    return status;
}

//............................................................................
Q_STATE_DEF(PrinterAo, paused) {
    QP::QState status;

    switch (e->sig) {
        case Q_ENTRY_SIG: {
            if (!empty()) {
                queue_[0].status = JobStatus::Paused;
                publishUpdate(0U);
            }
            status = Q_HANDLED();
            break;
        }

        case JOB_PAUSE_SIG: {
            // the pause control is a toggle
            status = empty() ? tran(&idle) : tran(&printing);
            break;
        }

        default: {
            status = super(&operational);
            break;
        }
    }

    return status;
}

} // namespace app
