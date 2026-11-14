#pragma once

#include <Windows.h>

#include <type_traits>
#include <utility>

/**
 * @brief Sole owner of one Win32 resource, released with @p fnClose.
 *
 * The empty value is `T{}` (null); for @ref HANDLE, @ref INVALID_HANDLE_VALUE
 * is empty as well, because @ref CreateFileW answers with it where nearly every
 * other API answers with null. The destructor only calls @p fnClose and
 * discards its result, whatever it returns.
 */
template <typename T, auto fnClose>
class UniqueResource
{
    T m_value{};

    [[nodiscard]] static constexpr bool IsValid(const T value) noexcept
    {
        if constexpr (std::is_same_v<T, HANDLE>)
            return value != nullptr && value != INVALID_HANDLE_VALUE;
        else
            return value != T{};
    }

public:
    explicit UniqueResource(const T value = {}) noexcept :
        m_value(value)
    {
    }

    UniqueResource(const UniqueResource& other) = delete;
    UniqueResource& operator=(const UniqueResource& other) = delete;

    UniqueResource(UniqueResource&& other) noexcept :
        m_value(other.Release())
    {
    }

    UniqueResource& operator=(UniqueResource&& other) noexcept
    {
        if (this != &other)
            Reset(other.Release());

        return *this;
    }

    ~UniqueResource() noexcept
    {
        Reset();
    }

    [[nodiscard]] T Get() const noexcept
    {
        return m_value;
    }

    [[nodiscard]] T Release() noexcept
    {
        return std::exchange(m_value, T{});
    }

    void Reset(const T value = {}) noexcept
    {
        if (const T old = std::exchange(m_value, value); IsValid(old))
            (void)fnClose(old);
    }

    /// Closes what is held, then yields the address for an out-parameter API.
    [[nodiscard]] T* Put() noexcept
    {
        Reset();
        return &m_value;
    }

    explicit operator bool() const noexcept
    {
        return IsValid(m_value);
    }
};

using UniqueHandle = UniqueResource<HANDLE, &CloseHandle>;
using UniqueModule = UniqueResource<HMODULE, &FreeLibrary>;
using UniqueKey = UniqueResource<HKEY, &RegCloseKey>;
using UniqueLocal = UniqueResource<HLOCAL, &LocalFree>;
