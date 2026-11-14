#include <Windows.h>
#include <olectl.h>

#include <chrono>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "casts.h"
#include "encoding.h"
#include "log.h"
#include "proc_address.h"
#include "unique_resource.h"

#include "steps.h"

using namespace std::literals::chrono_literals;

namespace
{
/// The whole milliseconds a Win32 timeout parameter (a `DWORD`) takes. A
/// coarser unit converts implicitly; a finer one needs an explicit
/// `duration_cast` at the call, where the truncation is visible.
[[nodiscard]] constexpr DWORD MillisecondsFrom(const std::chrono::milliseconds duration) noexcept
{
    return saturate_cast<DWORD>(duration.count());
}

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

constexpr LPCWSTR kComRegisterDlls[] = {
    L"dplayx.dll",
    L"dpnet.dll",
    L"dpnhpast.dll",
    L"dpnhupnp.dll",
};

using GetUnixFileNameFn = char* (__cdecl*)(LPCWSTR);

/// Null off Wine.
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
        return false;

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
        const UniqueHandle process(pi.hProcess);
        const UniqueHandle thread(pi.hThread);
        WaitForSingleObject(process.Get(), MillisecondsFrom(5s));
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

namespace
{
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
            "RegCreateKeyExW('HKCU\\{}') -> HRESULT{{0x{:08X}}}",
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

/// In-process @ref DllRegisterServer works because this exe and the
/// extracted DLLs are all 32-bit. Loading by full system-directory path with
/// @ref LOAD_WITH_ALTERED_SEARCH_PATH picks up the native DLL (and its
/// extracted siblings) just placed there, not a `dplayx.dll` of the game's
/// own that an ordinary search order might find first.
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
} // namespace

void PreferExtractedDirectPlay(const std::wstring& sysDir)
{
    // Overrides first, so the DLLs whose DllRegisterServer runs load as the
    // native binaries just extracted rather than Wine's builtins.
    ApplyWineDllOverrides();
    RegisterWineComServers(sysDir);
}
