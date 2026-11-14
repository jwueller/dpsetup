#pragma once

#include <cassert>
#include <concepts>
#include <limits>
#include <utility>

template <std::integral OutT, std::integral InT>
constexpr OutT saturate_cast(const InT input) noexcept
{
#ifdef __cpp_lib_saturation_arithmetic
    return std::saturate_cast<OutT>(input);
#else
    if (std::in_range<OutT>(input))
        return static_cast<OutT>(input);

    const auto min = std::numeric_limits<OutT>::lowest();
    const auto max = (std::numeric_limits<OutT>::max)();
    return std::cmp_less(input, min) ? min : max;
#endif
}

template <std::integral OutT, std::integral InT>
constexpr OutT assert_in_range(const InT input) noexcept
{
    assert(
        std::in_range<OutT>(input) &&
        "assert_in_range: value out of target range"
    );

    return saturate_cast<OutT>(input);
}
