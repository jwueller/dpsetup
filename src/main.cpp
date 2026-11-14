/**
 * @file
 *
 * @brief DirectPlay prerequisite bootstrap: Makes the official Microsoft
 *  DirectPlay component available. Triggered once per machine (Windows) or per
 *  prefix (Wine/Proton) by a Steam install script such as
 *  `examples/installscript.vdf`.
 *
 * @note Standalone by design: a windowless GUI-subsystem exe that statically
 *  links the CRT (see CMakeLists.txt) so it works during Steam's prerequisite
 *  installation step, before any vcredist is guaranteed to be present.
 *
 * @note Being GUI-subsystem, shells do not wait for it by default (Steam
 *  waits on the process handle and reads the exit code regardless). Callers
 *  that need the result must wait themselves:
 *  `start /wait dpsetup.exe <args>` (cmd) or
 *  `(Start-Process dpsetup.exe -ArgumentList <args> -Wait -PassThru).ExitCode`
 *  (PowerShell). When launched from a terminal, the log records are also
 *  written to that console (see `log.h`).
 *
 * @note Orchestration only: CAB/FDI
 *  extraction and its verification live in `cab.cpp`, the Wine
 *  overrides and COM registration in `wine.cpp`, and the Windows
 *  leg (DISM) in `dism.cpp`: `steps.h` declares what crosses those
 *  file boundaries.
 */

#include <Windows.h>
#include <delayimp.h>
#include <shellapi.h>

#include <array>
#include <string>
#include <string_view>

#include "encoding.h"
#include "log.h"
#include "module_handle.h"
#include "unique_resource.h"
#include "meta.h"

#include "steps.h"

namespace Retrofit::Setup
{
namespace
{
/// Directory containing this executable, without a trailing backslash.
[[nodiscard]] std::wstring GetOwnDirectory()
{
    const std::wstring path = ModulePathFrom(nullptr);
    const size_t slash = path.find_last_of(L'\\');
    return slash != std::wstring::npos
               ? path.substr(0, slash)
               : L".";
}

/// Whether this process token is elevated (always false when the query fails).
[[nodiscard]] bool IsElevated() noexcept
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;
    const UniqueHandle token(hToken);

    TOKEN_ELEVATION elevation{};
    DWORD cbReturned = 0;
    return GetTokenInformation(
        token.Get(),
        TokenElevation,
        &elevation,
        sizeof(elevation),
        &cbReturned
    ) && elevation.TokenIsElevated != 0;
}

/**
 * @brief Install mode needs administrator rights (system directory, DISM),
 *  and the manifest does not ask for them so that extraction mode can run
 *  unelevated: relaunches this executable elevated and waits for it.
 *
 * @param exitCode Receives the exit code of the elevated child, or, when
 *  nothing was installed, `ERROR_INSTALL_USEREXIT` for a declined prompt
 *  and `ERROR_INSTALL_FAILURE` for a relaunch that failed.
 * @returns Whether the relaunch replaces this run (the caller exits with
 *  @p exitCode); false when this process may proceed itself: already
 *  elevated, or Wine, which has no UAC and gives this process every right
 *  it will get.
 */
[[nodiscard]] bool RelaunchElevated(int& exitCode)
{
    if (IsWine())
        return false;

    if (IsElevated())
    {
        LOG(
            "running elevated"
        );
        return false;
    }

    const std::wstring exePath = ModulePathFrom(nullptr);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exePath.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info))
    {
        const DWORD error = GetLastError();
        LOG(
            "ShellExecuteExW('{}') failed: error {}",
            Utf8FromWide(exePath),
            error
        );
        exitCode = error == ERROR_CANCELLED ? ERROR_INSTALL_USEREXIT : ERROR_INSTALL_FAILURE;
        LOG(
            "install mode: failed, exit code {} (requires administrator permissions, but elevation {})",
            exitCode,
            error == ERROR_CANCELLED ? "was declined" : "failed"
        );
        return true;
    }

    const UniqueHandle child(info.hProcess);
    LOG(
        "not elevated: relaunched elevated, waiting"
    );
    WaitForSingleObject(child.Get(), INFINITE);

    DWORD childExitCode = ERROR_INSTALL_FAILURE;
    (void)GetExitCodeProcess(child.Get(), &childExitCode);
    LOG(
        "elevated child exited with {}",
        childExitCode
    );
    exitCode = static_cast<int>(childExitCode);
    return true;
}

/**
 * @brief No-argument mode, run by Steam's install-script runner.
 *
 * @note Routes by which servicing stack the machine has
 *  (@ref LoadDismApi). Windows gets DISM, and a failure there
 *  ends the run: with the feature off, Windows' system directory holds
 *  stand-ins an administrator may overwrite, so extracting would "succeed"
 *  there and leave unserviced DirectX 9 era binaries in place of Windows'
 *  own, plus Wine registry keys. Anything else (a Wine/Proton prefix, which has no DISM
 *  API) gets the extraction tier.
 *
 * @returns The process exit code, a Win32 error code as Windows installers
 *  use them. `ERROR_SUCCESS` only when Microsoft's DirectPlay is available
 *  afterwards: the feature reads back as enabled, or the extracted files
 *  are in the prefix's system directory at their exact sizes. Steam's
 *  install-script runner takes 0 as "done, never run again" and anything
 *  else as "run again on the next launch". A restart Windows asks for is
 *  still `ERROR_SUCCESS`, never `ERROR_SUCCESS_REBOOT_REQUIRED`: Steam
 *  would run the setup again, and the feature works without the restart.
 */
[[nodiscard]] DWORD RunInstallMode(const std::wstring& exeDir)
{
    // The DirectPlay binaries are 32-bit only; GetSystemDirectoryW(),
    // called from this 32-bit process, resolves to wherever 32-bit
    // system binaries belong (SysWOW64 alongside a 64-bit OS/prefix,
    // system32 for a 32-bit-only one): exactly where they need to go.
    WCHAR sysDir[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
    {
        LOG(
            "GetSystemDirectoryW failed: returned {}",
            n
        );
        return ERROR_INSTALL_FAILURE;
    }
    const std::wstring_view systemDirectory{sysDir, n};
    LOG(
        "GetSystemDirectoryW: '{}'",
        Utf8FromWide(systemDirectory)
    );

    if (const UniqueModule dismApi = LoadDismApi())
    {
        const bool ok = EnableDirectPlayFeature(dismApi.Get());
        LOG(
            "DISM {}",
            ok ? "enabled the DirectPlay feature" : "did not leave the DirectPlay feature enabled"
        );
        return ok ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
    }

    // Place Microsoft's native DirectPlay in the prefix's system directory.
    const ExtractResult extractResult = RunExtractionPipeline(exeDir, sysDir);
    if (extractResult == ExtractResult::Fatal)
    {
        LOG("FATAL: dxnt.cab failed its integrity check");
        return ERROR_INSTALL_PACKAGE_INVALID;
    }

    if (extractResult != ExtractResult::Ok || !VerifyExtractedFiles(sysDir))
    {
        LOG(
            "extraction did not take effect"
        );
        return ERROR_INSTALL_FAILURE;
    }

    ApplyWineDllOverrides();
    RegisterWineComServers(sysDir);
    LOG(
        "native DirectPlay extracted into '{}'",
        Utf8FromWide(systemDirectory)
    );
    return ERROR_SUCCESS;
}

/// The delay-load helper's hook: every delay-loaded DLL (`cabinet.dll`,
/// `bcrypt.dll`, the two imports that are no KnownDLLs) comes from the
/// system directory. A plain load searches the exe's own folder first, and
/// this exe sits in a game folder anyone may write to and relaunches itself
/// elevated, so a DLL planted beside it would run with that token. A null
/// return lets the helper load it its own way, so the system directory's
/// full path is the second try for a Windows that predates the flag.
FARPROC WINAPI LoadDelayedFromSystemDirectory(const unsigned dliNotify, const PDelayLoadInfo pdli) noexcept
{
    if (dliNotify != dliNotePreLoadLibrary)
        return nullptr;

    HMODULE hModule = LoadLibraryExA(pdli->szDll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hModule == nullptr)
    {
        std::array<char, MAX_PATH> path{};
        const UINT cchDirectory = GetSystemDirectoryA(path.data(), static_cast<UINT>(path.size()));
        if (cchDirectory != 0 && cchDirectory < path.size())
        {
            const std::string fullPath = std::string(path.data(), cchDirectory) + "\\" + pdli->szDll;
            hModule = LoadLibraryExA(fullPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
    }
    return reinterpret_cast<FARPROC>(hModule);
}
} // namespace
} // namespace Retrofit::Setup

extern "C" const PfnDliHook __pfnDliNotifyHook2 = Retrofit::Setup::LoadDelayedFromSystemDirectory;

using namespace Retrofit;
using namespace Retrofit::Setup;

/**
 * @brief Dispatches between install mode (no arguments, @ref RunInstallMode)
 *  and extraction mode (one argument: the directory to extract into).
 */
int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    const std::wstring exeDir = GetOwnDirectory();

    // Open the breadcrumb log for BOTH modes: if setup ever fails in the
    // field, this file is the only record of why. A failure to open it is
    // non-fatal: the LOG calls then reach the console only, if there is one.
    LogOpen(exeDir + L"\\dpsetup.log");

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const UniqueLocal argvOwner(argv);
    // A missing/unparsable command line falls back to install mode (argc <= 1).
    const bool extractionMode = argv && argc > 1;
    const std::wstring targetDir = extractionMode ? std::wstring(argv[1]) : std::wstring();

    LOG(
        "{} v{} {} mode",
        PROJECT_MODULE,
        PROJECT_VERSION,
        extractionMode ? "extraction" : "install"
    );

    if (!extractionMode)
    {
        int relaunchExitCode = 1;
        if (RelaunchElevated(relaunchExitCode))
            return relaunchExitCode;

        const DWORD exitCode = RunInstallMode(exeDir);
        LOG(
            "install mode: {}, exit code {}",
            exitCode == ERROR_SUCCESS ? "ok" : "failed",
            exitCode
        );
        return static_cast<int>(exitCode);
    }

    // Extraction mode: `dpsetup.exe <dir>` extracts kWantedFiles from
    // dxnt.cab into <dir> and nothing else: no DLL overrides, no COM
    // registration, no DISM, since those only make sense against the real
    // system directory. The exit code means "the 8 binaries are in <dir>
    // with their exact expected sizes", same rigor as install mode's Tier 1.
    const ExtractResult extractResult = RunExtractionPipeline(
        exeDir,
        targetDir
    );
    const bool ok = extractResult == ExtractResult::Ok && VerifyExtractedFiles(targetDir);

    // The exit code of a failure is the Win32 error that stopped the
    // extraction where there is one (ERROR_ACCESS_DENIED for a folder that
    // cannot be written), so a caller can tell the cause without the log.
    DWORD exitCode = ERROR_SUCCESS;
    if (extractResult == ExtractResult::Fatal)
        exitCode = ERROR_INSTALL_PACKAGE_INVALID;
    else if (!ok)
        exitCode = FirstExtractionError() != ERROR_SUCCESS ? FirstExtractionError() : ERROR_INSTALL_FAILURE;

    LOG(
        "extraction into '{}': {}, exit code {}",
        Utf8FromWide(targetDir),
        ok ? "ok" : "failed",
        exitCode
    );
    return static_cast<int>(exitCode);
}
