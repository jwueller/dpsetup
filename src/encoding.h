#pragma once

#include "casts.h"

#include <Windows.h>

// Declares std::size_t, which only reaches this file transitively.
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <string>
#include <string_view>

// Lenient conversions between UTF-16 and UTF-8, the log's encoding: an
// unconvertible sequence is substituted rather than failing the call. They
// throw only what allocating the result throws.
//
// NOLINTBEGIN(bugprone-suspicious-stringview-data-usage): false positive, the
// size travels alongside data()

[[nodiscard]] inline std::string Utf8FromWide(const std::wstring_view in)
{
    std::string out;
    // UTF-8 needs at most three bytes per UTF-16 unit.
    out.resize_and_overwrite(
        3 * in.size(),
        [&](char* const lpOut, const std::size_t cbOut) noexcept
        {
            const int cbConverted =
                in.empty()
                    ? 0
                    : WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        in.data(),
                        assert_in_range<int>(in.size()),
                        lpOut,
                        saturate_cast<int>(cbOut),
                        nullptr,
                        nullptr
                    );
            return static_cast<std::size_t>(cbConverted);
        }
    );
    return out;
}

[[nodiscard]] inline std::wstring WideFromUtf8(const std::string_view in)
{
    std::wstring out;
    // UTF-8 never produces more UTF-16 units than input bytes.
    out.resize_and_overwrite(
        in.size(),
        [&](wchar_t* const lpOut, const std::size_t cchOut) noexcept
        {
            const int cchConverted =
                in.empty()
                    ? 0
                    : MultiByteToWideChar(
                        CP_UTF8,
                        0,
                        in.data(),
                        assert_in_range<int>(in.size()),
                        lpOut,
                        saturate_cast<int>(cchOut)
                    );
            return static_cast<std::size_t>(cchConverted);
        }
    );
    return out;
}

// NOLINTEND(bugprone-suspicious-stringview-data-usage)
