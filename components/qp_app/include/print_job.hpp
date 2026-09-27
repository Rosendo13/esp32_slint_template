/**
 * @file print_job.hpp
 * @brief The print-job record shared by the domain layer and the UI HAL.
 *
 * Instances of PrintJob travel inside QP events, which are carved out of a
 * QF memory pool: QF hands back raw storage and casts, it never runs a
 * constructor (see QP::QF::q_new()). Every member therefore has to be
 * trivially copyable and fixed size -- no std::string, no heap.
 */

#ifndef PRINT_JOB_HPP
#define PRINT_JOB_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace app {

/**
 * @brief A null-terminated string with inline storage for N-1 characters.
 *
 * Trivially copyable and trivially default-constructible so it is legal to
 * place in pool memory. Longer input is truncated rather than rejected --
 * these are display strings, and a QP event is the wrong place to fail.
 */
template <std::size_t N>
class FixedString {
    static_assert(N > 1U, "FixedString needs room for at least one char");

public:
    FixedString() = default;

    void assign(std::string_view s) noexcept {
        const std::size_t n = std::min(s.size(), N - 1U);
        std::copy_n(s.data(), n, buf_.data());
        buf_[n] = '\0';
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view{buf_.data()};
    }

    [[nodiscard]] const char *c_str() const noexcept { return buf_.data(); }

private:
    std::array<char, N> buf_;
};

/** @brief Where a job is in its lifecycle. */
enum class JobStatus : std::uint8_t {
    Waiting,   ///< queued behind the head of the queue
    Printing,  ///< currently at the head and advancing
    Paused,    ///< at the head but not advancing
};

/** @brief One entry in the printer queue. */
struct PrintJob {
    FixedString<48> title;
    FixedString<32> owner;
    FixedString<16> size;
    FixedString<24> submission_date;
    std::uint16_t pages;
    std::uint8_t progress;  ///< percent complete, 0-100
    JobStatus status;
};

} // namespace app

#endif /* PRINT_JOB_HPP */
