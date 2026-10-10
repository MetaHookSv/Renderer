#pragma once

#include "shadow_cache.h"

namespace ShadowCache
{
// Ends the caster part of a shadow pass. If the casters were redrawn, the new
// depth is saved into the cache texture; otherwise the cached depth is copied
// back into the output texture. GL headers must be included by the caller.
inline void FinishCasterDepth(Cache& cache, unsigned int output, unsigned int stored, bool cubemap, int size)
{
    const auto target = cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    const int  layers = cubemap ? 6 : 1;

    unsigned int source      = stored;
    unsigned int destination = output;
    if (cache.Dirty())
    {
        source      = output;
        destination = stored;
    }

    glCopyImageSubData(source, target, 0, 0, 0, 0,
                       destination, target, 0, 0, 0, 0,
                       size, size, layers);
    cache.Commit();
}
} // namespace ShadowCache
