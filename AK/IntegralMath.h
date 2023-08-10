/*
 * Copyright (c) 2022, Leon Albrecht <leon2002.la@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/BuiltinWrappers.h>
#include <AK/Concepts.h>
#include <AK/NumericLimits.h>
#include <AK/StdLibExtras.h>
#include <AK/Types.h>

namespace AK {

template<Integral T>
constexpr T exp2(T exponent)
{
    if (exponent < 0)
        return 0;

    if (static_cast<MakeUnsigned<T>>(exponent) >= (8 * sizeof(T) - NumericLimits<T>::is_signed()))
        return NumericLimits<T>::max();

    return static_cast<T>(1) << exponent;
}

template<Integral T>
constexpr T log2(T x)
{
    if (x <= 0)
        return NumericLimits<T>::min();

    return 8 * sizeof(T) - 1 - count_leading_zeroes(static_cast<MakeUnsigned<T>>(x));
}

template<Integral T>
constexpr T ceil_log2(T x)
{
    if (x <= 0)
        return NumericLimits<T>::min();

    if (x == static_cast<T>(1))
        return 0;

    return log2(x) + !is_power_of_two(x);
}

template<Integral I>
constexpr I pow(I base, I exponent)
{
    // https://en.wikipedia.org/wiki/Exponentiation_by_squaring
    if (exponent < 0)
        return 0;
    if (exponent == 0)
        return 1;

    I res = 1;
    while (exponent > 0) {
        if (exponent & 1)
            res *= base;
        base *= base;
        exponent /= 2u;
    }
    return res;
}

template<auto base, Unsigned U = decltype(base)>
constexpr bool is_power_of(U x)
{
    if constexpr (base == 2)
        return is_power_of_two(x);

    // FIXME: I am naive! A log2-based approach (pow<U>(base, (log2(x) / log2(base))) == x) does not work due to rounding errors.
    for (U exponent = 0; exponent <= log2(x) / log2(base) + 1; ++exponent) {
        if (pow<U>(base, exponent) == x)
            return true;
    }
    return false;
}

}
