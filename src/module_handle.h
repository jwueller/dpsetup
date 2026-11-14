#pragma once

#include "symbol.h"

#include <Windows.h>

#include <algorithm>
#include <bit>
#include <string>
#include <type_traits>

/**
 * @brief An export typed as @p Pfn. @ref FARPROC is `INT_PTR (WINAPI*)()` only
 *  so that every export fits one return type; a direct function-to-function
 *  cast from it is what `cast-function-type-strict` flags, and the calling
 *  convention it appears to assert is not one a cast could change; a detour
 *  through `void*` trades that for `bugprone-casting-through-void`.
 *  @ref std::bit_cast is neither: two pointers of one width, bits
 *  reinterpreted, which is all the loader ever promised. Prefer
 *  @ref PROC_ADDRESS_OF, which takes the type from the declaration.
 */
template <typename Pfn>
    requires std::is_pointer_v<Pfn> && std::is_function_v<std::remove_pointer_t<Pfn>>
[[nodiscard]] Pfn ProcAddressAs(
    _In_ const HMODULE hModule,
    _In_z_ const char* const lpszProcName
) noexcept
{
    return std::bit_cast<Pfn>(GetProcAddress(hModule, lpszProcName));
}

/**
 * `PROC_ADDRESS_OF(hModule, DllRegisterServer)`: the export's type and its
 * name both come from the one identifier, so neither can drift from the other.
 * `decltype` never odr-uses the declaration, so a function this module declares
 * but does not define is fine.
 */
#define PROC_ADDRESS_OF(hModule, symbol) \
    ::ProcAddressAs<decltype(&(symbol))>((hModule), (SYMBOL(symbol)))

/// Full path of @p hModule (null: the executable), however long: the buffer
///  grows until the answer fits, up to the 32768 characters a path can have.
///  Empty on failure. Raw Win32 only, so it is safe under loader lock.
[[nodiscard]] inline std::wstring ModulePathFrom(const HMODULE hModule)
{
    constexpr DWORD cchMax = 32768;

    std::wstring path(MAX_PATH, L'\0');
    while (true)
    {
        const auto cchBuffer = static_cast<DWORD>(path.size());
        const DWORD cchPath = GetModuleFileNameW(hModule, path.data(), cchBuffer);
        if (cchPath == 0)
            return {};

        if (cchPath < cchBuffer)
        {
            path.resize(cchPath);
            return path;
        }

        if (cchBuffer >= cchMax)
            return {};

        path.resize(std::min(cchBuffer * 2, cchMax));
    }
}
