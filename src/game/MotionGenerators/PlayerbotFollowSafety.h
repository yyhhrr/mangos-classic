/* Narrow ground-follow safety for PlayerBots and their pets. */
#ifndef CMANGOS_PLAYERBOT_FOLLOW_SAFETY_H
#define CMANGOS_PLAYERBOT_FOLLOW_SAFETY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

class Unit;

namespace PlayerbotFollowSafety
{
    struct Point
    {
        float x, y, z;
    };

    constexpr float SupportTolerance = 1.25f;
    constexpr float EndpointTolerance = 1.25f;
    constexpr float SampleSpacing = 0.75f;
    constexpr std::size_t MaxSamples = 512;

    inline bool IsFinite(Point p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }

    inline float Distance(Point a, Point b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // The height callback searches near the expected feet Z, not the floor far below it.
    // Sampling the walking projection deliberately rejects automatic jumps over voids.
    template<class HeightAt>
    bool HasSupportedLine(Point from, Point to, HeightAt heightAt)
    {
        if (!IsFinite(from) || !IsFinite(to))
            return false;

        const float length = Distance(from, to);
        if (!std::isfinite(length) || length > SampleSpacing * (MaxSamples - 1))
            return false;

        const std::size_t steps = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(length / SampleSpacing)));
        for (std::size_t i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const Point sample{from.x + (to.x - from.x) * t,
                               from.y + (to.y - from.y) * t,
                               from.z + (to.z - from.z) * t};
            const float height = heightAt(sample.x, sample.y, sample.z);
            if (!std::isfinite(height) || std::fabs(height - sample.z) > SupportTolerance)
                return false;
        }
        return true;
    }

    // Elective combat spacing needs room at its destination, not merely a NAV
    // route to some standable floor. A start at an edge may still move inward.
    template<class HeightAt>
    bool HasGroundClearance(Point center, HeightAt heightAt, float clearance = 0.75f)
    {
        if (!IsFinite(center) || !std::isfinite(clearance) || clearance < 0.0f)
            return false;
        constexpr float diagonal = 0.70710678118f;
        constexpr float offsets[8][2] = {{1,0},{diagonal,diagonal},{0,1},{-diagonal,diagonal},
            {-1,0},{-diagonal,-diagonal},{0,-1},{diagonal,-diagonal}};
        for (auto const& offset : offsets)
            if (!HasSupportedLine(center, {center.x + offset[0] * clearance,
                    center.y + offset[1] * clearance, center.z}, heightAt))
                return false;
        return true;
    }

    template<class HeightAt>
    bool IsSafeLocalMove(Point from, Point to, HeightAt heightAt)
    {
        return HasGroundClearance(to, heightAt) && HasSupportedLine(from, to, heightAt);
    }

    // Path flags mirror PathType without coupling the policy tests to Detour/core headers.
    template<class Path, class HeightAt>
    bool IsSafePath(unsigned type, Path const& points, Point from, Point to, HeightAt heightAt)
    {
        constexpr unsigned Normal = 0x0001;
        constexpr unsigned Unsafe = 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020;
        if (!(type & Normal) || (type & Unsafe) || points.size() < 2 || points.size() > MaxSamples)
            return false;

        const Point start{points.front().x, points.front().y, points.front().z};
        const Point finish{points.back().x, points.back().y, points.back().z};
        if (!IsFinite(from) || !IsFinite(to) || !IsFinite(start) || !IsFinite(finish) ||
            Distance(from, start) > EndpointTolerance || Distance(to, finish) > EndpointTolerance)
            return false;

        float totalLength = 0.0f;
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const Point a{points[i - 1].x, points[i - 1].y, points[i - 1].z};
            const Point b{points[i].x, points[i].y, points[i].z};
            totalLength += Distance(a, b);
            if (!std::isfinite(totalLength) || totalLength > SampleSpacing * (MaxSamples - 1) ||
                !HasSupportedLine(a, b, heightAt))
                return false;
        }
        return true;
    }


    // Navigation detail heights include a smoothing lift. Project only actual
    // navigation routes locally onto their floor; do not relax formation support.
    constexpr float NavigationProjectionTolerance = 2.25f;
    constexpr float GroundedPathSpacing = 0.5f;
    constexpr float GroundedPathChunk = 100.0f;
    constexpr float NavigationConnectorLimit = 6.0f;

    template<class Path, class ProjectHeight, class SupportHeight>
    bool BuildGroundedPath(unsigned type, Path const& raw, Point from, Point requested,
        std::vector<Point>& output, ProjectHeight projectHeight, SupportHeight supportHeight,
        bool allowPartial = false)
    {
        output.clear();
        constexpr unsigned Normal = 0x0001, Partial = 0x0004;
        constexpr unsigned Unsafe = 0x0002 | 0x0008 | 0x0010 | 0x0020;
        if (!(type & (Normal | Partial)) || (type & Unsafe) ||
            ((type & Partial) && !allowPartial) || raw.size() < 2 || raw.size() > MaxSamples ||
            !IsFinite(from) || !IsFinite(requested) || !HasSupportedLine(from, from, supportHeight))
            return false;

        const Point rawStart{raw.front().x, raw.front().y, raw.front().z};
        const Point rawEnd{raw.back().x, raw.back().y, raw.back().z};
        if (!IsFinite(rawStart) || !IsFinite(rawEnd) || Distance(from, rawStart) > NavigationConnectorLimit ||
            (!(type & Partial) && Distance(requested, rawEnd) > NavigationConnectorLimit))
            return false;

        output.push_back(from);
        auto append = [&](Point candidate)
        {
            if (!IsFinite(candidate))
                return false;
            const float height = projectHeight(candidate.x, candidate.y, candidate.z);
            if (!std::isfinite(height) || std::fabs(height - candidate.z) > NavigationProjectionTolerance)
                return false;
            candidate.z = height;
            const Point previous = output.back();
            if (Distance(previous, candidate) < 0.0001f)
                return true;
            const float dx = candidate.x - previous.x, dy = candidate.y - previous.y;
            const float horizontal = std::sqrt(dx * dx + dy * dy);
            if (std::fabs(candidate.z - previous.z) > std::max(1.0f, horizontal * 1.5f) ||
                !HasSupportedLine(previous, candidate, supportHeight) || output.size() >= MaxSamples)
                return false;
            output.push_back(candidate);
            return true;
        };

        if (!append(rawStart))
        {
            output.clear();
            return false;
        }

        float traveled = 0.0f;
        bool chunked = false;
        for (std::size_t i = 1; i < raw.size(); ++i)
        {
            const Point a{raw[i - 1].x, raw[i - 1].y, raw[i - 1].z};
            const Point b{raw[i].x, raw[i].y, raw[i].z};
            const float length = Distance(a, b);
            if (!IsFinite(a) || !IsFinite(b) || !std::isfinite(length))
            {
                output.clear();
                return false;
            }
            if (length < 0.0001f)
                continue;
            const float portion = std::min(length, GroundedPathChunk - traveled);
            const std::size_t steps = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(portion / GroundedPathSpacing)));
            for (std::size_t j = 1; j <= steps; ++j)
            {
                const float t = (static_cast<float>(j) / static_cast<float>(steps)) * (portion / length);
                if (!append({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}))
                {
                    output.clear();
                    return false;
                }
            }
            traveled += portion;
            if (portion < length || traveled >= GroundedPathChunk - 0.0001f)
            {
                chunked = true;
                break;
            }
        }

        if (!chunked && !(type & Partial) && !append(requested))
        {
            output.clear();
            return false;
        }
        if (output.size() == 1 && Distance(from, requested) <= EndpointTolerance)
            output.push_back(output.front());
        if (output.size() < 2 ||
            (Distance(from, output.back()) < 0.1f && Distance(from, requested) > EndpointTolerance) ||
            !IsSafePath(Normal, output, from, output.back(), supportHeight))
        {
            output.clear();
            return false;
        }
        return true;
    }

    bool BuildSupportedGroundPath(Unit const& owner, Point destination, std::vector<Point>& points,
        bool allowPartial = false);

    bool NeedsCombatPositionSafety(Unit const& owner);
    bool IsSafeCombatPosition(Unit const& owner, Point destination);
    bool NeedsGroundSafety(Unit const& owner, Unit const& target);
    bool HasSupportedGroundLine(Unit const& owner, Point from, Point to);
}

#endif
