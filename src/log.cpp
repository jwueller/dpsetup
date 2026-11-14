#include "log.h"

#include <Windows.h>

#include <string>

#include "encoding.h"

namespace
{
auto g_hLog = INVALID_HANDLE_VALUE;

// Set only when a parent console exists: a double-click and the Steam
// install-script launch stay windowless (never AllocConsole). Redirection is
// deliberately not served: scripts get the exit code and dpsetup.log, the
// console is a nicety for a human at a terminal.
auto g_hConsole = INVALID_HANDLE_VALUE;
} // namespace

void LogOpen(const std::wstring& path) noexcept
{
    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        g_hConsole = CreateFileW(
            L"CONOUT$",
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
    }

    // Install mode relaunches itself elevated and the child opens the same
    // file while the parent still holds it: the sharing flags let both in, and
    // FILE_APPEND_DATA puts each write at the current end of file. The
    // child's CREATE_ALWAYS truncates, so the log keeps the child's records
    // and not what the parent wrote before relaunching.
    g_hLog = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

void LogWrite(const std::string_view message) noexcept
{
    if (g_hLog == INVALID_HANDLE_VALUE && g_hConsole == INVALID_HANDLE_VALUE)
        return;

    try
    {
        SYSTEMTIME now;
        GetSystemTime(&now);

        std::string line = std::format(
            "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z ",
            now.wYear,
            now.wMonth,
            now.wDay,
            now.wHour,
            now.wMinute,
            now.wSecond,
            now.wMilliseconds
        );
        for (const char ch : message)
            line.push_back(ch == '\r' || ch == '\n' ? ' ' : ch);

        if (g_hConsole != INVALID_HANDLE_VALUE)
        {
            const std::wstring wideLine = WideFromUtf8(line) + L"\r\n";
            DWORD cchWritten = 0;
            (void)WriteConsoleW(
                g_hConsole,
                wideLine.c_str(),
                wideLine.size(),
                &cchWritten,
                nullptr
            );
        }

        if (g_hLog != INVALID_HANDLE_VALUE)
        {
            line += "\r\n";
            DWORD cbWritten = 0;
            (void)WriteFile(
                g_hLog,
                line.data(),
                line.size(),
                &cbWritten,
                nullptr
            );
        }
    }
    // A record that cannot be built is dropped; logging never affects the
    // caller.
    // NOLINTNEXTLINE(bugprone-empty-catch)
    catch (...)
    {
    }
}
