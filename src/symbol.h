#pragma once

namespace Retrofit
{
/** @brief Turns the literal argument into a string. */
#define SYMBOL(x) #x

/** @brief Evaluates to a string literal. */
#define STRINGIFY(x) SYMBOL(x)

/** @brief Evaluates to a wide string literal. */
#define WIDE(x) WIDE_(x)
#define WIDE_(x) L##x
} // namespace Retrofit
