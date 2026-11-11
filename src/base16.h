#pragma once

#include <cstdint>
#include <iterator>
#include <span>
#include <string>

namespace Retrofit
{
//
// RFC 4648 data encoding utilities -- without external dependencies
//

/**
 * @brief Encodes bytes as base16 (hex), directly into an output iterator.
 * @returns the iterator past the last character written.
 */
template <std::output_iterator<char> OutIt>
constexpr OutIt Base16EncodeTo(
    const std::span<const std::uint8_t> bytes,
    OutIt out,
    const bool bUpper = false
)
{
    static constexpr char kAlphabetLower[] = "0123456789abcdef";
    static constexpr char kAlphabetUpper[] = "0123456789ABCDEF";
    const char* chAlphabet = bUpper ? kAlphabetUpper : kAlphabetLower;

    for (const auto byte : bytes)
    {
        *out++ = chAlphabet[(byte >> 4) & 0x0F];
        *out++ = chAlphabet[byte & 0x0F];
    }

    return out;
}

/// Convenience wrapper: base16 into a freshly reserved @ref std::string.
[[nodiscard]] inline std::string Base16FromBytes(
    const std::span<const std::uint8_t> bytes,
    const bool bUpper = false
)
{
    std::string encoded;
    encoded.reserve(bytes.size() * 2);
    Base16EncodeTo(bytes, std::back_inserter(encoded), bUpper);
    return encoded;
}
} // namespace Retrofit
