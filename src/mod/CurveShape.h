#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace flux
{

// One breakpoint of a drawable LFO shape. x and y in 0..1; curve -1..1 bends the segment that
// ends at this point (0 = straight, > 0 = slow start / fast end, < 0 = fast start / slow end).
struct CurvePoint
{
    float x = 0.0f, y = 0.0f, curve = 0.0f;
};

struct CurveShape
{
    static constexpr int tableSize = 256;
    static constexpr int maxPoints = 32;

    std::vector<CurvePoint> points;

    // A breathing default: quick swell, slow fall, a smaller second breath.
    static CurveShape defaultShape()
    {
        return { { { 0.0f, 0.0f, 0.0f }, { 0.18f, 1.0f, -0.5f }, { 0.5f, 0.15f, 0.4f },
                   { 0.62f, 0.7f, -0.4f }, { 1.0f, 0.0f, 0.5f } } };
    }

    // Sorted, clamped, at least two points, first at x = 0, last at x = 1.
    void sanitise()
    {
        for (auto& p : points)
        {
            p.x = std::clamp (p.x, 0.0f, 1.0f);
            p.y = std::clamp (p.y, 0.0f, 1.0f);
            p.curve = std::clamp (p.curve, -1.0f, 1.0f);
        }
        std::sort (points.begin(), points.end(), [] (auto& a, auto& b) { return a.x < b.x; });
        if (points.size() > (size_t) maxPoints)
            points.resize ((size_t) maxPoints);
        if (points.size() < 2)
            points = { { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.5f, 0.0f } };
        points.front().x = 0.0f;
        points.back().x = 1.0f;
    }

    static float bend (float t, float curve) noexcept
    {
        if (std::abs (curve) < 1.0e-3f)
            return t;
        const float k = curve * 6.0f;
        return (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
    }

    // Value 0..1 at phase x.
    float evaluate (float x) const noexcept
    {
        for (size_t i = 1; i < points.size(); ++i)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            if (x <= b.x)
            {
                const float span = b.x - a.x;
                const float t = span > 1.0e-6f ? (x - a.x) / span : 1.0f;
                return a.y + (b.y - a.y) * bend (t, b.curve);
            }
        }
        return points.back().y;
    }

    // Bipolar table (-1..1); one extra entry for wrap-around interpolation.
    void bake (std::array<float, tableSize + 1>& table) const noexcept
    {
        for (int i = 0; i <= tableSize; ++i)
            table[(size_t) i] = 2.0f * evaluate ((float) i / (float) tableSize) - 1.0f;
    }

    // "x,y,c;x,y,c;..." in 1/10000 units for saving in the plugin state. Integers on purpose:
    // float formatting depends on the locale (decimal commas would break the separators).
    std::string toString() const
    {
        std::string s;
        char buf[64];
        auto fixed = [] (float v) { return (int) std::lround (v * 10000.0f); };
        for (const auto& p : points)
        {
            std::snprintf (buf, sizeof (buf), "%d,%d,%d;", fixed (p.x), fixed (p.y), fixed (p.curve));
            s += buf;
        }
        return s;
    }

    static CurveShape fromString (const std::string& s)
    {
        CurveShape c;
        size_t pos = 0;
        while (pos < s.size())
        {
            const size_t end = s.find (';', pos);
            const std::string item = s.substr (pos, end == std::string::npos ? std::string::npos : end - pos);
            int x = 0, y = 0, k = 0;
            if (std::sscanf (item.c_str(), "%d,%d,%d", &x, &y, &k) == 3)
                c.points.push_back ({ (float) x / 10000.0f, (float) y / 10000.0f, (float) k / 10000.0f });
            if (end == std::string::npos) break;
            pos = end + 1;
        }
        c.sanitise();
        return c;
    }
};

// Hands a baked table from the message thread to the audio thread without locks on the audio side:
// the writer fills a staging table and marks it ready; the audio thread copies it when it can.
class CurveTableExchange
{
public:
    using Table = std::array<float, CurveShape::tableSize + 1>;

    // Any non-audio thread.
    void publish (const CurveShape& shape)
    {
        while (busy.exchange (true, std::memory_order_acquire))
            std::this_thread::yield();
        shape.bake (staging);
        ready.store (true, std::memory_order_release);
        busy.store (false, std::memory_order_release);
    }

    // Audio thread: picks up a new table if one is waiting and nobody is writing it.
    void update (Table& live) noexcept
    {
        if (! ready.load (std::memory_order_acquire))
            return;
        if (busy.exchange (true, std::memory_order_acquire))
            return;   // writer active: try again next tick
        live = staging;
        ready.store (false, std::memory_order_release);
        busy.store (false, std::memory_order_release);
    }

private:
    Table staging {};
    std::atomic<bool> busy { false }, ready { false };
};

} // namespace flux
