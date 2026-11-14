#pragma once

/// @file
/// The run's record: one UTF-8 line per message in a file next to the exe,
/// and the same line on the parent console when there is one.

#include <format>
#include <string>
#include <string_view>

/// Opens @p path for appending, replacing a log already open, and attaches to
/// the parent console if there is one. Returns false when the file cannot be
/// opened; records then go to the console only.
bool LogOpen(const std::wstring& path) noexcept;

/// Writes one record: UTC timestamp, @p message. A line break inside
/// @p message becomes a space so a record stays one line. A record says on its
/// own whether something failed.
void LogWrite(std::string_view message) noexcept;

/// `LOG("fmt {}", args...)`: @ref LogWrite of `std::format`. The arguments are
/// evaluated inside the `try`, so nothing escapes to the caller.
#define LOG(...)                                                             \
    do                                                                       \
    {                                                                        \
        try                                                                  \
        {                                                                    \
            ::LogWrite(std::format(__VA_ARGS__));                            \
        }                                                                    \
        catch (...)                                                          \
        {                                                                    \
        }                                                                    \
    } while (false)
