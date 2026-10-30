// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QtGlobal>
#include <compare>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace jianpu
{
// Whole-note units. Keep exact positions through parsing and layout; only MIDI uses ticks.
struct MusicalTime
{
    qint64 n = 0, d = 1;
    MusicalTime(qint64 numerator = 0, qint64 denominator = 1) : n(numerator), d(denominator)
    {
        Q_ASSERT(d != 0);
        if (d < 0)
        {
            n = -n;
            d = -d;
        }
        auto g = std::gcd(n, d);
        n /= g;
        d /= g;
    }
    double value() const { return double(n) / double(d); }
    qint64 ticks() const { return qRound64(value() * 1920.0); }
    static qint64 product(qint64 a, qint64 b)
    {
        if (a && b &&
            (a == std::numeric_limits<qint64>::min() || b == std::numeric_limits<qint64>::min() ||
             std::abs(a) > std::numeric_limits<qint64>::max() / std::abs(b)))
            throw std::overflow_error("musical time overflow");
        return a * b;
    }
    static qint64 sum(qint64 a, qint64 b)
    {
        if ((b > 0 && a > std::numeric_limits<qint64>::max() - b) ||
            (b < 0 && a < std::numeric_limits<qint64>::min() - b))
            throw std::overflow_error("musical time overflow");
        return a + b;
    }
    friend MusicalTime operator+(MusicalTime a, MusicalTime b)
    {
        auto g = std::gcd(a.d, b.d);
        return {sum(product(a.n, b.d / g), product(b.n, a.d / g)), product(a.d, b.d / g)};
    }
    friend MusicalTime operator-(MusicalTime a, MusicalTime b)
    {
        return a + MusicalTime(-b.n, b.d);
    }
    friend MusicalTime operator*(MusicalTime a, MusicalTime b)
    {
        auto g = std::gcd(a.n, b.d), h = std::gcd(b.n, a.d);
        return {product(a.n / g, b.n / h), product(a.d / h, b.d / g)};
    }
    friend MusicalTime operator/(MusicalTime a, MusicalTime b)
    {
        Q_ASSERT(b.n);
        return a * MusicalTime(b.d, b.n);
    }
    bool operator==(const MusicalTime &) const = default;
    std::strong_ordering operator<=>(const MusicalTime &b) const
    {
        auto g = std::gcd(d, b.d);
        return product(n, b.d / g) <=> product(b.n, d / g);
    }
    MusicalTime &operator+=(MusicalTime b) { return *this = *this + b; }
};
} // namespace jianpu
