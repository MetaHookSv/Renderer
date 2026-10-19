#pragma once

#include <GL/glew.h>

//Scene RGB already includes transparent effects. Its accumulated alpha is not
//coverage and must not blend the completed view into the final framebuffer again.
//Restrict the copy to this view so inset views preserve the surrounding image.
inline void GL_BlitSceneViewport(GLuint src, GLuint dst, const float* viewport)
{
    const int left   = static_cast<int>(viewport[0]);
    const int bottom = static_cast<int>(viewport[1]);
    const int right  = left + static_cast<int>(viewport[2]);
    const int top    = bottom + static_cast<int>(viewport[3]);

    glDisable(GL_BLEND);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
    glBlitFramebuffer(left, bottom, right, top, left, bottom, right, top, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}
