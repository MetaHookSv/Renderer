#pragma once

// Caching policy for local-light shadow maps. This header has no engine or GL
// dependency so that the GPU regression test can use the exact same logic.
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
    Vec3 mins{};
    Vec3 maxs{};
    bool valid = false;

    bool operator==(const Bounds&) const = default;

    void Add(const Vec3& point)
    {
        if (!valid)
        {
            mins  = point;
            maxs  = point;
            valid = true;
            return;
        }

        for (int i = 0; i < 3; ++i)
        {
            mins[i] = (std::min)(mins[i], point[i]);
            maxs[i] = (std::max)(maxs[i], point[i]);
        }
    }
};

// Everything that affects what a light's shadow map looks like. Any change
// here means the cached depth can no longer be used.
struct Light
{
    Vec3  origin{};
    Vec3  forward{};
    Vec3  right{};
    Vec3  up{};
    float range        = 0;
    float coneTangent  = 0;
    int   size         = 0;
    int   staticSize   = 0;
    bool  spot         = false;
    bool  worldEnabled = true;

    // Entity the light is attached to; it never casts into its own light.
    std::uintptr_t source = 0;

    bool operator==(const Light&) const = default;
};

// Transforms an AABB by a row-major 3x4 affine matrix (brush entity transform
// or Studio bone matrix) and returns the AABB of the eight transformed corners.
// Returns invalid bounds if the input is invalid or the result is not finite.
inline Bounds TransformBounds(const Bounds& bounds, const float* matrix)
{
    Bounds result;
    if (!bounds.valid)
        return result;

    for (int corner = 0; corner < 8; ++corner)
    {
        Vec3 local{};
        for (int i = 0; i < 3; ++i)
            local[i] = (corner & (1 << i)) ? bounds.maxs[i] : bounds.mins[i];

        Vec3 point{};
        for (int row = 0; row < 3; ++row)
        {
            const float* m = matrix + row * 4;
            point[row]     = m[3] + m[0] * local[0] + m[1] * local[1] + m[2] * local[2];
            if (!std::isfinite(point[row]))
                return {};
        }
        result.Add(point);
    }
    return result;
}

struct Caster
{
    std::uintptr_t        id    = 0;
    std::uintptr_t        model = 0;
    Bounds                bounds;
    std::array<float, 16> transform{};
    int                   body  = 0;
    int                   skin  = 0;
    int                   frame = 0;

    // True when the geometry may change without any of the fields above
    // changing (animated poses, masked textures, unknown geometry). Such a
    // caster forces the light to redraw every frame.
    bool volatileGeometry = true;

    bool operator==(const Caster&) const = default;
};

// Conservative test: returns true unless the bounds are provably outside the
// light. Invalid or non-finite input is always treated as intersecting.
inline bool Intersects(const Light& light, const Bounds& bounds)
{
    if (!bounds.valid || !std::isfinite(light.range) || light.range <= 0)
        return true;

    for (int i = 0; i < 3; ++i)
    {
        if (!std::isfinite(bounds.mins[i]) || !std::isfinite(bounds.maxs[i]) || !std::isfinite(light.origin[i]))
            return true;
        if (bounds.mins[i] > bounds.maxs[i])
            return true;
    }

    // Sphere vs AABB, using the closest point of the box to the light.
    float distanceSquared = 0;
    for (int i = 0; i < 3; ++i)
    {
        const float below = bounds.mins[i] - light.origin[i];
        const float above = light.origin[i] - bounds.maxs[i];
        const float delta = (std::max)(below, (std::max)(0.0f, above));
        distanceSquared += delta * delta;
    }
    if (distanceSquared > light.range * light.range)
        return false;

    if (!light.spot || !std::isfinite(light.coneTangent) || light.coneTangent <= 0)
        return true;

    // Spot cone vs AABB: test the box against the four side planes of the cone
    // pyramid and the plane through the light facing forward. There is no near
    // plane. A box touching or crossing a plane is kept.
    Vec3 center{};
    Vec3 extent{};
    for (int i = 0; i < 3; ++i)
    {
        center[i] = (bounds.mins[i] + bounds.maxs[i]) * 0.5f - light.origin[i];
        extent[i] = (bounds.maxs[i] - bounds.mins[i]) * 0.5f;
    }

    const Vec3* sideAxes[]  = {&light.right, &light.right, &light.up, &light.up};
    const float sideSigns[] = {1.0f, -1.0f, 1.0f, -1.0f};

    for (int plane = 0; plane < 5; ++plane)
    {
        Vec3 normal = light.forward;
        if (plane < 4)
        {
            for (int i = 0; i < 3; ++i)
                normal[i] = light.forward[i] * light.coneTangent + sideSigns[plane] * (*sideAxes[plane])[i];
        }

        float distance = 0;
        float radius   = 0;
        for (int i = 0; i < 3; ++i)
        {
            distance += normal[i] * center[i];
            radius += std::abs(normal[i]) * extent[i];
        }
        if (distance + radius < 0)
            return false;
    }
    return true;
}

// Per-light cache state. Each frame:
//   Prepare() picks the casters that reach the light and decides if the
//             cached depth is still usable,
//   Dirty()   tells the renderer whether to redraw casters,
//   Commit()  records what is now stored in the cache texture.
class Cache
{
public:
    bool ProjectionChanged(const Light& light) const
    {
        return !m_valid || !(light == m_light);
    }

    bool Prepare(const Light& light, const std::vector<Caster>& casters, bool forceUpdate = false)
    {
        m_pending.clear();

        bool hasVolatileCaster = false;
        for (const auto& caster : casters)
        {
            if (caster.id == light.source)
                continue;
            if (!Intersects(light, caster.bounds))
                continue;

            m_pending.push_back(caster);
            if (caster.volatileGeometry)
                hasVolatileCaster = true;
        }

        // Sort so that a reordered entity list compares equal.
        std::sort(m_pending.begin(), m_pending.end(), [](const Caster& a, const Caster& b) {
            return a.id < b.id;
        });

        m_pendingLight = light;
        m_dirty        = forceUpdate || ProjectionChanged(light) || hasVolatileCaster || m_pending != m_rendered;
        return m_dirty;
    }

    bool Contains(std::uintptr_t id) const
    {
        return std::any_of(m_pending.begin(), m_pending.end(), [id](const Caster& caster) {
            return caster.id == id;
        });
    }

    bool Dirty() const
    {
        return m_dirty;
    }

    std::size_t CasterCount() const
    {
        return m_pending.size();
    }

    void Invalidate()
    {
        m_valid = false;
    }

    void Commit()
    {
        m_light    = m_pendingLight;
        m_rendered = m_pending;
        m_valid    = true;
        m_dirty    = false;
    }

private:
    // State of the depth currently stored in the cache texture.
    Light               m_light{};
    std::vector<Caster> m_rendered;
    bool                m_valid = false;

    // State computed by the last Prepare().
    Light               m_pendingLight{};
    std::vector<Caster> m_pending;
    bool                m_dirty = true;
};
} // namespace ShadowCache
