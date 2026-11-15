#pragma once

/// @file
/// The run's record: UTF-8 messages in a file, and the same messages on the
/// parent console when there is one.

// Declares std::format, which only LOG's expansion uses.
// ReSharper disable once CppUnusedIncludeDirective
#include <format>
#include <string>
#include <string_view>

/// Opens @p path for appending and attaches to the parent console if there is
/// one. Called once per run. When the file cannot be opened, records go to
/// the console only.
void LogOpen(const std::wstring& path) noexcept;

/// Writes @p message and a line break. Line breaks inside it are kept, as
/// CRLF. There are no timestamps: the run writes the time once when it
/// starts, and a step whose duration matters logs that itself. A message says
/// on its own whether something failed.
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
