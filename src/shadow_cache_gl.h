#pragma once

#include "shadow_cache.h"

namespace ShadowCache
{
// GLEW/qgl must be included by the caller with the current context's dispatch.
inline void FinishCasterDepth(Cache& cache, unsigned int output, unsigned int stored, bool cubemap, int size)
{
    const bool dirty  = cache.Dirty();
    const auto target = cubemap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    glCopyImageSubData(dirty ? output : stored, target, 0, 0, 0, 0,
                       dirty ? stored : output, target, 0, 0, 0, 0,
                       size, size, cubemap ? 6 : 1);
    cache.Commit();
}
} // namespace ShadowCache
