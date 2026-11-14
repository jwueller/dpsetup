#pragma once

/// @file
/// The run's record: one UTF-8 line per message in a file next to the exe,
/// and the same line on the parent console when there is one.

// Declares std::format, which only LOG's expansion uses.
// ReSharper disable once CppUnusedIncludeDirective
#include <format>
#include <string>
#include <string_view>

/// Opens @p path for appending and attaches to the parent console if there is
/// one. Called once per run. When the file cannot be opened, records go to
/// the console only.
void LogOpen(const std::wstring& path) noexcept;

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
