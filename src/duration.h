#pragma once

#include "casts.h"

#include <chrono>

using namespace std::literals::chrono_literals;

/**
 * @brief The whole milliseconds a Win32 timeout parameter (a `DWORD`) takes.
 *
 * The one place a duration becomes the integer such an API wants. It takes
 * `std::chrono::milliseconds`, so a coarser unit converts implicitly and a
 * finer one needs an explicit `duration_cast` at the call, where the
 * truncation is visible.
 */
[[nodiscard]] constexpr unsigned long MillisecondsFrom(const std::chrono::milliseconds duration) noexcept
{
    return saturate_cast<unsigned long>(duration.count());
}
