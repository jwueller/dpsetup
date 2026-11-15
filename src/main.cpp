#include <Windows.h>
#include <delayimp.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "encoding.h"
#include "log.h"
#include "scoped_resource.h"
#include "meta.h"

#include "steps.h"

namespace
{
enum class Command
{
    Install,
    Extract,
    Help,
};

struct CommandLineOptions
{
    Command command = Command::Install;
    /// For @ref Command::Extract.
    std::wstring targetDir;
    /// Absolute: the elevated relaunch passes it on, and an elevated process
    /// starts in the system directory.
    std::wstring logPath;
    /// Collected rather than logged: the log is opened at @ref logPath, so
    /// it is not open yet while parsing.
    std::vector<std::string> errors;
};

[[nodiscard]] bool IsSwitch(
    const std::wstring_view name,
    const std::wstring_view expected
) noexcept
{
    return CompareStringOrdinal(
        name.data(),
        static_cast<int>(name.size()),
        expected.data(),
        static_cast<int>(expected.size()),
        TRUE
    ) == CSTR_EQUAL;
}

/// @p path unchanged when it cannot be resolved.
[[nodiscard]] std::wstring GetFullPath(const std::wstring& path)
{
    const DWORD cchRequired =
        GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (cchRequired == 0)
        return path;

    std::wstring fullPath(cchRequired, L'\0');
    const DWORD cchFullPath = GetFullPathNameW(
        path.c_str(),
        cchRequired,
        fullPath.data(),
        nullptr
    );
    if (cchFullPath == 0 || cchFullPath >= cchRequired)
        return path;
    fullPath.resize(cchFullPath);
    return fullPath;
}

[[nodiscard]] CommandLineOptions ParseCommandLine(
    const std::span<const LPWSTR> args,
    const std::wstring& defaultLogPath
)
{
    CommandLineOptions result;
    result.logPath = defaultLogPath;
    bool helpRequested = false;
    bool logRequested = false;
    bool installRequested = false;
    bool extractRequested = false;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::wstring_view arg = args[i];
        if (!arg.starts_with(L'/'))
        {
            if (result.command == Command::Extract)
            {
                result.errors.push_back(
                    std::format("more than one folder: {:?}", Utf8FromWide(arg))
                );
                continue;
            }
            result.command = Command::Extract;
            result.targetDir = arg;
            continue;
        }

        const std::wstring_view name = arg.substr(1);
        if (IsSwitch(name, L"?") || IsSwitch(name, L"help"))
        {
            helpRequested = true;
        }
        else if (IsSwitch(name, L"install") || IsSwitch(name, L"repair"))
        {
            installRequested = true;
        }
        else if (IsSwitch(name, L"extract"))
        {
            extractRequested = true;
        }
        else if (IsSwitch(name, L"log"))
        {
            if (i + 1 == args.size())
                result.errors.emplace_back("/log needs a file");
            else if (logRequested)
                result.errors.emplace_back("more than one /log");
            else
            {
                logRequested = true;
                result.logPath = GetFullPath(args[++i]);
            }
        }
        else if (
            IsSwitch(name, L"quiet") || IsSwitch(name, L"passive") ||
            IsSwitch(name, L"norestart")
        )
        {
            // Without a UI and without restarts, these already describe
            // every run.
        }
        else
        {
            result.errors.push_back(
                std::format("unsupported switch {:?}", Utf8FromWide(arg))
            );
        }
    }

    if (installRequested && result.command == Command::Extract)
        result.errors.emplace_back("/install and /repair take no folder");
    if (extractRequested && result.command != Command::Extract)
        result.errors.emplace_back("/extract needs a folder");

    // Agent's choice: help wins over everything else, errors included, so
    // that `/?` added to any command line explains it; rejecting such a
    // command line as invalid was passed over.
    if (helpRequested)
        result.command = Command::Help;

    return result;
}

/// Quoted so that `CommandLineToArgvW` gives back @p arg exactly.
void AppendQuotedArgument(std::wstring& out, const std::wstring_view arg)
{
    if (!out.empty())
        out += L' ';
    out += L'"';
    size_t backslashes = 0;
    for (const wchar_t ch : arg)
    {
        if (ch == L'\\')
        {
            ++backslashes;
            continue;
        }
        // Backslashes double only where a quote follows them.
        out.append(ch == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        out += ch;
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
}

/// The elevated child's arguments: these, with the log as an absolute path,
/// since an elevated process starts in the system directory and would
/// resolve a relative one there.
[[nodiscard]] std::wstring BuildRelaunchArguments(
    const std::span<const LPWSTR> args,
    const std::wstring_view logPath
)
{
    std::wstring arguments;
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::wstring_view arg = args[i];
        if (arg.starts_with(L'/') && IsSwitch(arg.substr(1), L"log"))
        {
            ++i;
            continue;
        }
        AppendQuotedArgument(arguments, arg);
    }
    AppendQuotedArgument(arguments, L"/log");
    AppendQuotedArgument(arguments, logPath);
    return arguments;
}

/// A message box: the GUI subsystem leaves a console's prompt running
/// beside this process, so console output would land in the middle of it.
void ShowUsage() noexcept
{
    constexpr auto usage =
        L"dpsetup.exe [/install | /repair] [/quiet | /passive] [/norestart]\n"
        L"    Installs, registers and enables DirectPlay on this machine.\n"
        L"    Requires administrator permissions.\n"
        L"\n"
        L"dpsetup.exe [/extract] [/quiet | /passive] <folder>\n"
        L"    Extracts the DirectPlay runtime binaries into <folder>.\n"
        L"\n"
        L"dpsetup.exe /? | /help\n"
        L"    Shows this message.\n"
        L"\n"
        L"/log <file>\n"
        L"    Logs to <file> instead of dpsetup.log beside dpsetup.exe.\n"
        L"\n"
        L"There is no user interface and Windows is never restarted, so "
        L"/quiet, /passive and /norestart change nothing.";
    MessageBoxW(
        nullptr,
        usage,
        L"" PROJECT_MODULE L" v" PROJECT_VERSION,
        MB_OK | MB_ICONINFORMATION
    );
}

/// Empty on failure.
[[nodiscard]] std::wstring GetOwnPath()
{
    std::wstring path(MAX_PATH, L'\0');
    while (true)
    {
        constexpr DWORD cchMax = 32768;
        const auto cchBuffer = static_cast<DWORD>(path.size());
        const DWORD cchPath = GetModuleFileNameW(
            nullptr,
            path.data(),
            cchBuffer
        );
        if (cchPath == 0)
            return {};

        if (cchPath < cchBuffer)
        {
            path.resize(cchPath);
            // The failure paths return a fresh empty string, so this one
            // moves instead of eliding; once per run.
            // NOLINTNEXTLINE(clang-diagnostic-nrvo)
            return path;
        }

        if (cchBuffer >= cchMax)
            return {};

        path.resize(std::min(cchBuffer * 2, cchMax));
    }
}

/// Without a trailing backslash.
[[nodiscard]] std::wstring GetOwnDirectory()
{
    const std::wstring path = GetOwnPath();
    const auto slash = path.find_last_of(L'\\');
    return slash != std::wstring::npos ? path.substr(0, slash) : L".";
}

/// False when the query fails.
[[nodiscard]] bool IsElevated() noexcept
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;
    const ScopedHandle token(hToken);

    TOKEN_ELEVATION elevation{};
    DWORD cbReturned = 0;
    const BOOL queried = GetTokenInformation(
        token.Get(),
        TokenElevation,
        &elevation,
        sizeof(elevation),
        &cbReturned
    );
    return queried && elevation.TokenIsElevated != 0;
}

/**
 * @brief Install mode needs administrator rights (system directory, DISM),
 *  and the manifest does not ask for them so that extraction mode can run
 *  unelevated: relaunches this executable elevated and waits for it.
 *
 * @param arguments Passed on to the elevated child unchanged.
 * @param exitCode Receives the exit code of the elevated child, or, when
 *  nothing was installed, `ERROR_INSTALL_USEREXIT` for a declined prompt
 *  and `ERROR_INSTALL_FAILURE` for a relaunch that failed.
 * @returns Whether the relaunch replaces this run (the caller exits with
 *  @p exitCode); false when this process may proceed itself: already
 *  elevated, or Wine, which has no UAC and gives this process every right
 *  it will get.
 */
[[nodiscard]] bool RelaunchElevated(
    const std::wstring& arguments,
    int& exitCode
)
{
    if (IsWine())
        return false;

    if (IsElevated())
    {
        LOG("running elevated");
        return false;
    }

    const std::wstring exePath = GetOwnPath();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exePath.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info))
    {
        const DWORD error = GetLastError();
        LOG("ShellExecuteExW({:?}) -> error {}", Utf8FromWide(exePath), error);
        const bool declined = error == ERROR_CANCELLED;
        exitCode = declined ? ERROR_INSTALL_USEREXIT : ERROR_INSTALL_FAILURE;
        LOG(
            "install mode -> failed, exit code {} (requires administrator permissions, but elevation {})",
            exitCode,
            declined ? "was declined" : "failed"
        );
        return true;
    }

    const ScopedHandle child(info.hProcess);
    LOG("not elevated: relaunched elevated, waiting");
    WaitForSingleObject(child.Get(), INFINITE);

    DWORD childExitCode = ERROR_INSTALL_FAILURE;
    (void)GetExitCodeProcess(child.Get(), &childExitCode);
    LOG("elevated child exited with {}", childExitCode);
    exitCode = static_cast<int>(childExitCode);
    return true;
}

/// Routes on @ref LoadDismApi alone, never falling back to extraction, and
/// returns `ERROR_SUCCESS` only when DirectPlay is available afterwards, a
/// pending restart included. CONTRIBUTING's Design and Traps hold the
/// reasons.
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
        LOG("GetSystemDirectoryW -> {}", n);
        return ERROR_INSTALL_FAILURE;
    }
    const std::wstring_view systemDirectory{sysDir, n};
    LOG("GetSystemDirectoryW -> {:?}", Utf8FromWide(systemDirectory));

    if (const ScopedModule dismApi = LoadDismApi())
    {
        const bool ok = EnableDirectPlayFeature(dismApi.Get());
        LOG(
            "DISM {}",
            ok ? "DirectPlay enabled" : "couldn't enable DirectPlay"
        );
        return ok ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
    }

    const ExtractResult extractResult = ExtractDirectPlayFiles(exeDir, sysDir);
    if (extractResult == ExtractResult::Fatal)
    {
        LOG("FATAL: dxnt.cab failed its integrity check");
        return ERROR_INSTALL_PACKAGE_INVALID;
    }

    if (extractResult != ExtractResult::Ok || !VerifyExtractedFiles(sysDir))
    {
        LOG("extraction did not take effect");
        return ERROR_INSTALL_FAILURE;
    }

    PreferExtractedDirectPlay(sysDir);
    LOG(
        "native DirectPlay extracted into {:?}",
        Utf8FromWide(systemDirectory)
    );
    return ERROR_SUCCESS;
}

/// No DLL overrides, COM registration or DISM here: those only make sense
/// against the real system directory. The exit code of a failure is the
/// Win32 error that stopped the extraction where there is one
/// (`ERROR_ACCESS_DENIED` for a folder that cannot be written), so a caller
/// can tell the cause without the log.
[[nodiscard]] DWORD RunExtractionMode(
    const std::wstring& exeDir,
    const std::wstring& targetDir
)
{
    const auto extractResult = ExtractDirectPlayFiles(exeDir, targetDir);
    const bool ok =
        extractResult == ExtractResult::Ok && VerifyExtractedFiles(targetDir);

    DWORD exitCode = ERROR_SUCCESS;
    if (extractResult == ExtractResult::Fatal)
        exitCode = ERROR_INSTALL_PACKAGE_INVALID;
    else if (!ok && FirstExtractionError() != ERROR_SUCCESS)
        exitCode = FirstExtractionError();
    else if (!ok)
        exitCode = ERROR_INSTALL_FAILURE;

    LOG(
        "extraction into {:?} -> {}, exit code {}",
        Utf8FromWide(targetDir),
        ok ? "ok" : "failed",
        exitCode
    );
    return exitCode;
}

/// The delay-load helper's hook: every delay-loaded DLL (`cabinet.dll`,
/// `bcrypt.dll`, the two imports that are no KnownDLLs) comes from the
/// system directory. A plain load searches the exe's own folder first, and
/// this exe sits in a game folder anyone may write to and relaunches itself
/// elevated, so a DLL planted beside it would run with that token. A null
/// return lets the helper load it its own way, so the system directory's
/// full path is the second try for a Windows that predates the flag.
FARPROC WINAPI LoadDelayedFromSystemDirectory(
    const unsigned dliNotify,
    const PDelayLoadInfo pdli
) noexcept
{
    if (dliNotify != dliNotePreLoadLibrary)
        return nullptr;

    HMODULE hModule = LoadLibraryExA(
        pdli->szDll,
        nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32
    );
    if (hModule == nullptr)
    {
        std::array<char, MAX_PATH> path{};
        if (const UINT cchDirectory =
                GetSystemDirectoryA(path.data(), path.size());
            cchDirectory != 0 && cchDirectory < path.size())
        {
            const std::string fullPath =
                std::string(path.data()) + "\\" + pdli->szDll;
            hModule = LoadLibraryExA(
                fullPath.c_str(),
                nullptr,
                LOAD_WITH_ALTERED_SEARCH_PATH
            );
        }
    }
    return reinterpret_cast<FARPROC>(hModule);
}
} // namespace

extern "C" const PfnDliHook __pfnDliNotifyHook2 =
    LoadDelayedFromSystemDirectory;

int WINAPI wWinMain(
    _In_ const HINSTANCE hInstance,
    _In_opt_ const HINSTANCE hPrevInstance,
    _In_ const LPWSTR lpCmdLine,
    _In_ const int nShowCmd
)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    const std::wstring exeDir = GetOwnDirectory();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const ScopedLocal argvOwner(argv);
    // A missing/unparsable command line falls back to install mode.
    const auto args = argv && argc > 1
                          ? std::span<const LPWSTR>(argv + 1, argc - 1)
                          : std::span<const LPWSTR>();

    const auto options = ParseCommandLine(args, exeDir + L"\\dpsetup.log");
    LogOpen(options.logPath);

    LOG("{} v{}", PROJECT_MODULE, PROJECT_VERSION);
    LOG("> {}", Utf8FromWide(GetCommandLineW()));

    if (options.command == Command::Help)
    {
        ShowUsage();
        return ERROR_SUCCESS;
    }

    if (!options.errors.empty())
    {
        for (const std::string& error : options.errors)
            LOG("{}", error);
        LOG("invalid command line, exit code {}", ERROR_INVALID_PARAMETER);
        return ERROR_INVALID_PARAMETER;
    }

    switch (options.command)
    {
    case Command::Help:
        std::unreachable();

    case Command::Install:
    {
        LOG("install mode");
        int relaunchExitCode = 1;
        if (
            RelaunchElevated(
                BuildRelaunchArguments(args, options.logPath),
                relaunchExitCode
            )
        )
        {
            return relaunchExitCode;
        }

        const DWORD exitCode = RunInstallMode(exeDir);
        LOG(
            "install mode -> {}, exit code {}",
            exitCode == ERROR_SUCCESS ? "ok" : "failed",
            exitCode
        );
        return static_cast<int>(exitCode);
    }

    case Command::Extract:
        LOG("extraction mode");
        return static_cast<int>(RunExtractionMode(exeDir, options.targetDir));
    }

    std::unreachable();
}
