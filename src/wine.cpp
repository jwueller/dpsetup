/// @file
/// What makes an extracted DirectPlay win in a Wine/Proton prefix: marking
/// the extracted binaries native-first (DLL overrides + in-process COM
/// registration) so the prefix picks them up over its own builtins, and
/// removing a system-directory entry at the Unix level where Win32 cannot.
/// `main.cpp`'s @ref RunInstallMode calls these after a verified extraction.

#include <Windows.h>
#include <olectl.h>

#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "duration.h"
#include "encoding.h"
#include "log.h"
#include "module_handle.h"
#include "unique_resource.h"

#include "steps.h"

namespace Retrofit::Setup
{
namespace
{
/**
 * @brief Wine DLL-override value names for the extracted binaries, plus
 *  `dplay`: Wine has a builtin `dplay.dll` too, so a game that carries its
 *  own one needs the override as well.
 *
 * @warning The extracted names are the verbatim `winetricks directplay`
 *  convention: two entries keep their .exe suffix, the rest don't. Don't
 *  "normalize" this: it has to match what Wine's DLL loader actually looks
 *  up.
 */
constexpr LPCWSTR kDllOverrideNames[] = {
    L"dplaysvr.exe",
    L"dplay",
    L"dplayx",
    L"dpmodemx",
    L"dpnet",
    L"dpnhpast",
    L"dpnhupnp",
    L"dpnsvr.exe",
    L"dpwsockx",
};

/**
 * @brief The subset of @ref kWantedFiles exposing COM servers that
 *  @ref RegisterWineComServers needs to register.
 */
constexpr LPCWSTR kComRegisterDlls[] = {
    L"dplayx.dll",
    L"dpnet.dll",
    L"dpnhpast.dll",
    L"dpnhupnp.dll",
};
} // namespace

namespace
{
using GetUnixFileNameFn = char* (__cdecl*)(LPCWSTR);

/// Wine's `wine_get_unix_file_name`, or null on a real Windows.
[[nodiscard]] GetUnixFileNameFn ResolveGetUnixFileName() noexcept
{
    const HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    return hK32
               ? ProcAddressAs<GetUnixFileNameFn>(hK32, "wine_get_unix_file_name")
               : nullptr;
}
} // namespace

bool IsWine() noexcept
{
    return ResolveGetUnixFileName() != nullptr;
}

bool UnixRemoveEntry(const std::wstring& dir, const std::wstring& fileName)
{
    const auto pGetUnixName = ResolveGetUnixFileName();
    if (!pGetUnixName)
        return false; // not Wine: nothing to unlink

    char* unixDir = pGetUnixName(dir.c_str());
    if (!unixDir)
        return false;
    const std::string unixPath = std::string(unixDir) + "/" + Utf8FromWide(std::wstring_view{fileName});
    HeapFree(GetProcessHeap(), 0, unixDir);

    WCHAR sysDir[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return false;

    // Wine runs a native Unix program via start.exe /unix.
    const std::wstring startExe = std::wstring(sysDir, n) + L"\\start.exe";
    const std::wstring cmd =
        L"\"" + startExe + L"\" /unix /bin/rm -f \"" + WideFromUtf8(std::string_view{unixPath}) + L"\"";
    std::vector cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0'); // CreateProcessW's lpCommandLine must be writable

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
        startExe.c_str(),
        cmdBuf.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    ))
    {
        const DWORD error = GetLastError();
        LOG(
            "CreateProcessW('{}') -> error {}",
            Utf8FromWide(startExe),
            error
        );
        return false;
    }
    {
        const UniqueProcessInformation process(pi);
        WaitForSingleObject(process.m_hProcess.Get(), MillisecondsFrom(5s));
    }

    // start.exe /unix returns before its Unix child finishes, and we can't
    // wait on that child through it, so confirm the entry has actually
    // disappeared before reporting success (bounded, ~2s).
    const std::wstring dosPath = dir + L"\\" + fileName;
    for (int i = 0; i < 100; ++i)
    {
        if (GetFileAttributesW(dosPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            LOG(
                "UnixRemoveEntry('{}') -> ok",
                unixPath
            );
            return true;
        }
        Sleep(MillisecondsFrom(20ms));
    }
    LOG(
        "UnixRemoveEntry('{}') -> still present after unlink",
        unixPath
    );
    return false;
}

/**
 * @brief Marks the extracted binaries (@ref kDllOverrideNames) as
 *  native-first in the Wine registry. Install mode only.
 *
 * @warning Overrides must precede registration
 *  (@ref RegisterWineComServers) so the DLLs we call
 *  @ref DllRegisterServer on get loaded as the native binaries we just
 *  extracted, rather than Wine's builtins.
 */
void ApplyWineDllOverrides()
{
    UniqueKey key;
    constexpr auto lpSubKey = L"Software\\Wine\\DllOverrides";
    const LSTATUS lStatus = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        lpSubKey,
        0,
        nullptr,
        0,
        KEY_SET_VALUE,
        nullptr,
        key.Put(),
        nullptr
    );
    if (lStatus != ERROR_SUCCESS)
    {
        LOG(
            "RegCreateKeyExW('HKCU\\{}') -> hr 0x{:08X}",
            Utf8FromWide(lpSubKey),
            static_cast<unsigned>(HRESULT_FROM_WIN32(lStatus))
        );
        return;
    }

    constexpr WCHAR kOverrideValue[] = L"native,builtin";
    int written = 0;
    for (const auto name : kDllOverrideNames)
    {
        if (RegSetValueExW(
            key.Get(),
            name,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(kOverrideValue),
            sizeof(kOverrideValue)
        ) == ERROR_SUCCESS)
            ++written;
    }

    // dpsetup exits immediately after install; Wine otherwise only flushes
    // user.reg on a timer / wineserver shutdown, so force it now to make the
    // overrides durable before the game's first launch reads them cold.
    RegFlushKey(key.Get());
    key.Reset();

    const auto cOverrides = std::size(kDllOverrideNames);
    LOG(
        "ApplyWineDllOverrides -> {} of {} set",
        written,
        cOverrides
    );
}

/**
 * @brief Registers the extracted COM servers (@ref kComRegisterDlls)
 *  in-process. Install mode only.
 *
 * @note In-process @ref DllRegisterServer works here because everything
 *  involved (this exe, the extracted DLLs) is 32-bit, like us. Loading
 *  by full system-directory path with @ref LOAD_WITH_ALTERED_SEARCH_PATH
 *  is what guarantees we pick up the native DLL (and its extracted
 *  siblings) we just placed there, not a `dplayx.dll` of the game's own
 *  that an ordinary @ref LoadLibrary search order might find first.
 */
void RegisterWineComServers(const std::wstring& destDir)
{
    for (const auto dll : kComRegisterDlls)
    {
        const std::wstring fullPath = destDir + L"\\" + dll;
        const UniqueModule library(
            LoadLibraryExW(
                fullPath.c_str(),
                nullptr,
                LOAD_WITH_ALTERED_SEARCH_PATH
            )
        );
        if (!library)
        {
            // Continues past individual failures: a single missing COM server
            // here isn't fatal.
            LOG(
                "LoadLibraryExW('{}') -> error {}",
                Utf8FromWide(fullPath),
                GetLastError()
            );
            continue;
        }

        if (const auto pDllRegisterServer = PROC_ADDRESS_OF(
            library.Get(),
            DllRegisterServer
        ))
        {
            const HRESULT hr = pDllRegisterServer();
            LOG(
                "DllRegisterServer('{}') -> {} HRESULT{{0x{:08X}}}",
                Utf8FromWide(dll),
                SUCCEEDED(hr) ? "ok" : "failed",
                static_cast<unsigned>(hr)
            );
        }
        else
        {
            LOG(
                "GetProcAddress('{}') -> exports no DllRegisterServer",
                Utf8FromWide(dll)
            );
        }
    }
}
} // namespace Retrofit::Setup
