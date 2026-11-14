#pragma once

#include "casts.h"

#include <Windows.h>

#include <cassert>
// Declares std::size_t, which only reaches this file transitively.
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

/// What a string's bytes mean. A @ref std::string does not say which of
/// the two narrow encodings it holds, so a conversion names it.
///
/// Where the process code page is UTF-8, @ref Ansi and @ref Utf8 convert
/// identically and no test tells them apart: the name is the guard.
enum class Encoding
{
    /**
     * The narrow process code page.
     *
     * @see CP_ACP
     */
    Ansi,

    /**
     * UTF-8, the standard narrow encoding: the log and @ref std::format.
     *
     * @see CP_UTF8
     */
    Utf8,

    /**
     * UTF-16LE in @ref wchar_t, what Windows calls "Unicode".
     */
    Wide,
};

/// The character type a string of encoding @p E is made of.
template <Encoding E>
using CharOf = std::conditional_t<E == Encoding::Wide, wchar_t, char>;

/// How a conversion treats text that does not convert losslessly.
///
/// - @ref ConversionMode::Lenient: flags 0; an unmappable character is
///   substituted and content never fails the conversion.
/// - @ref ConversionMode::Strict: fails unless converting the result back
///   would yield the input. An ill-formed sequence or a character the target
///   code page cannot represent fails with a Win32 error HRESULT.
///
/// In strict mode, narrow to Wide uses `MB_ERR_INVALID_CHARS`. Wide to Utf8
/// uses `WC_ERR_INVALID_CHARS`. Wide to Ansi uses it only where the process
/// code page is UTF-8; otherwise `WC_NO_BEST_FIT_CHARS` plus the
/// used-default-character out-parameter detects substitution.
/// A same-encoding conversion is a copy and is lossless by definition in
/// either mode.
enum class ConversionMode
{
    Lenient,
    Strict,
};

namespace EncodingDetail
{
[[nodiscard]] constexpr UINT CodePageOf(const Encoding encoding) noexcept
{
    return encoding == Encoding::Utf8 ? CP_UTF8 : CP_ACP;
}

[[nodiscard]] inline HRESULT HresultFromLastError() noexcept
{
    const DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

/// Converts @p in into @p out and reports the code units in @p cchConverted,
/// without a terminator. An empty @p out probes the whole conversion's size.
/// A too-small buffer fails with `ERROR_INSUFFICIENT_BUFFER`, reports zero
/// code units and leaves the buffer contents unspecified.
[[nodiscard]] inline HRESULT WideIntoNarrow(
    const std::span<char> out,
    const std::wstring_view in,
    const UINT codePage,
    const ConversionMode mode,
    std::size_t& cchConverted
) noexcept
{
    // NOLINTBEGIN(bugprone-suspicious-stringview-data-usage): false positive

    HRESULT result = E_UNEXPECTED;

    const UINT effectiveCodePage = codePage == CP_ACP ? GetACP() : codePage;
    const bool takesInvalidCharsFlag = effectiveCodePage == CP_UTF8;
    const DWORD flags =
        mode == ConversionMode::Lenient
            ? 0
            : takesInvalidCharsFlag
            ? WC_ERR_INVALID_CHARS
            : WC_NO_BEST_FIT_CHARS;
    BOOL usedDefaultChar = FALSE;
    const LPBOOL lpUsedDefaultChar =
        mode == ConversionMode::Strict && !takesInvalidCharsFlag
            ? &usedDefaultChar
            : nullptr;

    const int cchWideChar = assert_in_range<int>(in.size());
    const int cbOut = saturate_cast<int>(out.size());
    int cbMultiByte = 0;

    cchConverted = 0;

    if (in.empty())
    {
        result = S_OK;
        goto out;
    }

    cbMultiByte = WideCharToMultiByte(
        effectiveCodePage,
        flags,
        in.data(),
        cchWideChar,
        out.data(),
        cbOut,
        nullptr,
        lpUsedDefaultChar
    );
    if (!cbMultiByte)
    {
        result = HresultFromLastError();
        goto out;
    }

    if (usedDefaultChar)
    {
        result = HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
        goto out;
    }

    cchConverted = static_cast<std::size_t>(cbMultiByte);
    result = S_OK;

out:
    return result;

    // NOLINTEND(bugprone-suspicious-stringview-data-usage)
}

/// @ref WideIntoNarrow in the other direction.
[[nodiscard]] inline HRESULT NarrowIntoWide(
    const std::span<wchar_t> out,
    const std::string_view in,
    const UINT codePage,
    const ConversionMode mode,
    std::size_t& cchConverted
) noexcept
{
    // NOLINTBEGIN(bugprone-suspicious-stringview-data-usage): false positive

    HRESULT result = E_UNEXPECTED;

    const DWORD flags = mode == ConversionMode::Strict ? MB_ERR_INVALID_CHARS : 0;
    const int cbMultiByte = assert_in_range<int>(in.size());
    const int cchOut = saturate_cast<int>(out.size());
    int cchWideChar = 0;

    cchConverted = 0;

    if (in.empty())
    {
        result = S_OK;
        goto out;
    }

    cchWideChar = MultiByteToWideChar(
        codePage,
        flags,
        in.data(),
        cbMultiByte,
        out.data(),
        cchOut
    );
    if (!cchWideChar)
    {
        result = HresultFromLastError();
        goto out;
    }

    cchConverted = static_cast<std::size_t>(cchWideChar);
    result = S_OK;

out:
    return result;

    // NOLINTEND(bugprone-suspicious-stringview-data-usage)
}

[[nodiscard]] inline HRESULT WideIntoNarrow(
    std::string& out,
    const std::wstring_view in,
    const UINT codePage,
    const ConversionMode mode
) noexcept
{
    HRESULT result = E_UNEXPECTED;

    if (in.size() > out.max_size() / 3)
    {
        result = E_OUTOFMEMORY;
        goto out;
    }

    try
    {
        // UTF-8 needs at most three bytes per UTF-16 unit, legacy ANSI at most two.
        out.resize_and_overwrite(
            3 * in.size(),
            [&](char* const lpOut, const std::size_t cchOut) noexcept
            {
                std::size_t cchConverted = 0;
                result = WideIntoNarrow(
                    std::span(lpOut, cchOut),
                    in,
                    codePage,
                    mode,
                    cchConverted
                );
                return SUCCEEDED(result) ? cchConverted : 0;
            }
        );
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    catch (const std::length_error&)
    {
        result = E_OUTOFMEMORY;
    }

out:
    return result;
}

[[nodiscard]] inline HRESULT NarrowIntoWide(
    std::wstring& out,
    const std::string_view in,
    const UINT codePage,
    const ConversionMode mode
) noexcept
{
    HRESULT result = E_UNEXPECTED;

    try
    {
        // No process code page produces more UTF-16 units than input bytes.
        out.resize_and_overwrite(
            in.size(),
            [&](wchar_t* const lpOut, const std::size_t cchOut) noexcept
            {
                std::size_t cchConverted = 0;
                result = NarrowIntoWide(
                    std::span(lpOut, cchOut),
                    in,
                    codePage,
                    mode,
                    cchConverted
                );
                return SUCCEEDED(result) ? cchConverted : 0;
            }
        );
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY;
    }
    catch (const std::length_error&)
    {
        result = E_OUTOFMEMORY;
    }

    return result;
}
} // namespace EncodingDetail

/// Converts @p in from encoding @p From to encoding @p To, replacing the
/// contents of @p out and reusing its capacity.
///
/// The same encoding on both sides is a lossless copy in either mode;
/// Ansi to Utf8 and back goes through UTF-16. On failure @p out is empty
/// and the result is the Win32 error as an HRESULT (`E_FAIL` if the API
/// set none), or `E_OUTOFMEMORY`. An empty @p in succeeds with an empty
/// @p out.
///
/// This form never throws. @ref EncodedFrom and the named wrappers
/// @ref Utf8FromWide, @ref WideFromUtf8, @ref AnsiFromWide,
/// @ref WideFromAnsi, @ref Utf8FromAnsi, @ref AnsiFromUtf8 are lenient
/// value forms and throw only @ref std::bad_alloc on allocation failure.
template <Encoding To, Encoding From>
[[nodiscard]] HRESULT EncodeInto(
    std::basic_string<CharOf<To>>& out,
    const std::basic_string_view<CharOf<From>> in,
    const ConversionMode mode = ConversionMode::Lenient
) noexcept
{
    HRESULT result = E_UNEXPECTED;

    if constexpr (To == From)
    {
        (void)mode;
        try
        {
            out.assign(in);
            result = S_OK;
        }
        catch (const std::bad_alloc&)
        {
            result = E_OUTOFMEMORY;
        }
        catch (const std::length_error&)
        {
            result = E_OUTOFMEMORY;
        }
    }
    else if constexpr (To == Encoding::Wide)
    {
        result = EncodingDetail::NarrowIntoWide(
            out,
            in,
            EncodingDetail::CodePageOf(From),
            mode
        );
    }
    else if constexpr (From == Encoding::Wide)
    {
        result = EncodingDetail::WideIntoNarrow(
            out,
            in,
            EncodingDetail::CodePageOf(To),
            mode
        );
    }
    else
    {
        std::wstring wide;
        result = EncodingDetail::NarrowIntoWide(
            wide,
            in,
            EncodingDetail::CodePageOf(From),
            mode
        );
        if (SUCCEEDED(result))
        {
            result = EncodingDetail::WideIntoNarrow(
                out,
                wide,
                EncodingDetail::CodePageOf(To),
                mode
            );
        }
    }

    if (FAILED(result))
        out.clear();

    return result;
}

/// The lenient value form of @ref EncodeInto: throws only @ref std::bad_alloc.
template <Encoding To, Encoding From>
[[nodiscard]] std::basic_string<CharOf<To>> EncodedFrom(
    const std::basic_string_view<CharOf<From>> in
)
{
    std::basic_string<CharOf<To>> out;
    const HRESULT hr = EncodeInto<To, From>(out, in);
    if (FAILED(hr))
    {
        assert(hr == E_OUTOFMEMORY);
        throw std::bad_alloc();
    }

    return out;
}

// @formatter:off
[[nodiscard]] inline std::wstring WideFromUtf8(const std::string_view in) { return EncodedFrom<Encoding::Wide, Encoding::Utf8>(in); }
[[nodiscard]] inline std::string Utf8FromWide(const std::wstring_view in) { return EncodedFrom<Encoding::Utf8, Encoding::Wide>(in); }
// @formatter:on
