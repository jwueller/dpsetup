#pragma once

#include <algorithm>
#include <string_view>
#include <type_traits>

template <typename T>
concept CharLike =
    std::is_same_v<const T, const char> ||
    std::is_same_v<const T, const wchar_t>;

/**
 * @brief ASCII-only lowercase of one character: the same answer on every
 *  machine and locale. Anything outside `A` to `Z` passes through, so the
 *  bytes of a UTF-8 multibyte sequence (all above 0x7F) are untouched.
 */
template <CharLike CharT>
[[nodiscard]] constexpr CharT AsciiLowerFrom(const CharT ch) noexcept
{
    return ch >= CharT{'A'} && ch <= CharT{'Z'} ? static_cast<CharT>(ch - CharT{'A'} + CharT{'a'}) : ch;
}

/**
 * @brief Case-insensitive equality by ASCII folding only, independent of the
 *  locale. For names the code owns, which are ASCII by construction.
 */
[[nodiscard]] constexpr bool EqualsAsciiNoCase(
    const std::string_view lhs,
    const std::string_view rhs
) noexcept
{
    return std::ranges::equal(
        lhs,
        rhs,
        [](const char a, const char b) noexcept
        {
            return AsciiLowerFrom(a) == AsciiLowerFrom(b);
        }
    );
}
