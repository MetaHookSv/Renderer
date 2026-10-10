#pragma once

// Engine/GL-independent shadow invalidation policy, also exercised by the GPU test.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ShadowCache
{
using Vec3 = std::array<float, 3>;

struct Bounds
{
    Vec3 mins{}, maxs{};
    bool valid                           = false;
    bool operator==(const Bounds&) const = default;

    void Add(const Vec3& point)
    {
        if (!valid)
            mins = maxs = point;
        else
            for (int i = 0; i < 3; ++i)
            {
                mins[i] = (std::min)(mins[i], point[i]);
                maxs[i] = (std::max)(maxs[i], point[i]);
            }
        valid = true;
    }
};

struct Light
{
    Vec3           origin{}, forward{}, right{}, up{};
    float          range = 0, coneTangent = 0;
    int            size = 0, staticSize = 0;
    bool           spot                           = false;
    bool           worldEnabled                   = true;
    std::uintptr_t source                         = 0;
    bool           operator==(const Light&) const = default;
};

// Row-major affine matrix, shared by brush transforms and Studio bone matrices.
inline Bounds TransformBounds(const Bounds& bounds, const float* matrix)
{
    Bounds result;
    if (!bounds.valid)
        return result;
    for (int corner = 0; corner < 8; ++corner)
    {
        Vec3 point{};
        for (int axis = 0; axis < 3; ++axis)
        {
            point[axis] = matrix[axis * 4 + 3];
            for (int component = 0; component < 3; ++component)
                point[axis] += matrix[axis * 4 + component] * ((corner & (1 << component)) ? bounds.maxs[component] : bounds.mins[component]);
            if (!std::isfinite(point[axis]))
                return {};
        }
        result.Add(point);
    }
    return result;
}

struct Caster
{
    std::uintptr_t        id = 0, model = 0;
    Bounds                bounds;
    std::array<float, 16> transform{};
    int                   body = 0, skin = 0, frame = 0;
    // Animated poses, masked materials and unknown geometry must not freeze.
    bool volatileGeometry                = true;
    bool operator==(const Caster&) const = default;
};

inline bool Intersects(const Light& light, const Bounds& bounds)
{
    if (!bounds.valid || !std::isfinite(light.range) || light.range <= 0)
        return true;
    float distanceSquared = 0;
    Vec3  center{}, extent{};
    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(bounds.mins[i]) || !std::isfinite(bounds.maxs[i]) ||
            bounds.mins[i] > bounds.maxs[i] || !std::isfinite(light.origin[i]))
            return true;
        const float delta = (std::max)(bounds.mins[i] - light.origin[i],
                                       (std::max)(0.0f, light.origin[i] - bounds.maxs[i]));
        distanceSquared += delta * delta;
        center[i] = (bounds.mins[i] + bounds.maxs[i]) * 0.5f - light.origin[i];
        extent[i] = (bounds.maxs[i] - bounds.mins[i]) * 0.5f;
    }
    if (distanceSquared > light.range * light.range)
        return false;
    if (!light.spot || !std::isfinite(light.coneTangent) || light.coneTangent <= 0)
        return true;

    // Four side planes and the plane through the light; no near-plane rejection.
    // Touching/crossing a plane is retained, including large boxes around the cone.
    for (int plane = 0; plane < 5; ++plane)
    {
        float distance = 0, radius = 0;
        for (int i = 0; i < 3; ++i)
        {
            float normal = light.forward[i];
            if (plane < 4)
                normal = light.forward[i] * light.coneTangent +
                    (plane % 2 ? -1.0f : 1.0f) * (plane < 2 ? light.right[i] : light.up[i]);
            distance += normal * center[i];
            radius += std::abs(normal) * extent[i];
        }
        if (distance + radius < 0)
            return false;
    }
    return true;
}

class Cache
{
public:
    bool ProjectionChanged(const Light& light) const { return !m_valid || !(light == m_light); }

    bool Prepare(const Light& light, const std::vector<Caster>& casters, bool forceUpdate = false)
    {
        m_pending.clear();
        bool animated = false;
        for (const auto& caster : casters)
        {
            if (caster.id != light.source && Intersects(light, caster.bounds))
            {
                m_pending.push_back(caster);
                animated |= caster.volatileGeometry;
            }
        }
        std::sort(m_pending.begin(), m_pending.end(), [](const Caster& a, const Caster& b) { return a.id < b.id; });
        m_pendingLight = light;
        m_dirty        = forceUpdate || ProjectionChanged(light) || animated || m_pending != m_rendered;
        return m_dirty;
    }

    bool Contains(std::uintptr_t id) const
    {
        return std::any_of(m_pending.begin(), m_pending.end(), [id](const Caster& caster) { return caster.id == id; });
    }
    bool        Dirty() const { return m_dirty; }
    std::size_t CasterCount() const { return m_pending.size(); }
    void        Invalidate() { m_valid = false; }
    void        Commit()
    {
        m_light    = m_pendingLight;
        m_rendered = m_pending;
        m_valid    = true;
        m_dirty    = false;
    }

private:
    Light               m_light{}, m_pendingLight{};
    std::vector<Caster> m_rendered, m_pending;
    bool                m_valid = false, m_dirty = true;
};
} // namespace ShadowCache
