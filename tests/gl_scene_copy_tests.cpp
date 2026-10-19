#include "gl_scene_copy.h"
#include <GLFW/glfw3.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{

constexpr int TargetSize   = 8;
constexpr int SkipExitCode = 2;

GLuint CreateTarget(GLenum format, GLuint& texture)
{
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, format, TargetSize, TargetSize);

    GLuint framebuffer = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    assert(GL_FRAMEBUFFER_COMPLETE == glCheckFramebufferStatus(GL_FRAMEBUFFER));
    return framebuffer;
}

void TestSceneColor(GLuint scene, GLuint finalBuffer, const float* viewport)
{
    constexpr std::array<float, 3> smokeColor = {0.668457f, 0.666504f, 0.666504f};
    constexpr std::array<float, 3> background = {0.125f, 0.25f, 0.5f};

    //Smoke reduces alpha while additive sprite rectangles can increase it above
    //one. Neither may change the RGB of an already composited scene.
    for (float alpha : {0.0f, 0.2323f, 0.4402f, 1.0f, 5.0f})
    {
        glBindFramebuffer(GL_FRAMEBUFFER, scene);
        glClearColor(smokeColor[0], smokeColor[1], smokeColor[2], alpha);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, finalBuffer);
        glClearColor(background[0], background[1], background[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GL_BlitSceneViewport(scene, finalBuffer, viewport);

        assert(!glIsEnabled(GL_BLEND));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, finalBuffer);
        std::array<unsigned char, TargetSize * TargetSize * 4> pixels{};
        glReadPixels(0, 0, TargetSize, TargetSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        for (int y = 0; y < TargetSize; ++y)
        {
            for (int x = 0; x < TargetSize; ++x)
            {
                const bool inside = x >= viewport[0] && x < viewport[0] + viewport[2] &&
                    y >= viewport[1] && y < viewport[1] + viewport[3];
                const auto& expected = inside ? smokeColor : background;
                for (int channel = 0; channel < 3; ++channel)
                {
                    assert(std::abs(expected[channel] * 255.0f - pixels[(y * TargetSize + x) * 4 + channel]) <= 1.0f);
                }
            }
        }
        assert(GL_NO_ERROR == glGetError());
    }
}

} // namespace

int main()
{
    if (!glfwInit())
        return SkipExitCode;

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(TargetSize, TargetSize, "scene copy regression", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return SkipExitCode;
    }

    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    assert(GLEW_OK == glewInit());
    while (glGetError() != GL_NO_ERROR)
    {
    }

    GLuint       textures[2]    = {};
    const GLuint scene          = CreateTarget(GL_RGBA16F, textures[0]);
    const GLuint finalBuffer    = CreateTarget(GL_RGBA8, textures[1]);
    const float  viewports[][4] = {
        {0, 0, TargetSize, TargetSize},
        {2, 1, 4, 5},
        {2, 1, 0, 0},
    };
    for (const auto& viewport : viewports)
    {
        TestSceneColor(scene, finalBuffer, viewport);
    }
    glDeleteFramebuffers(1, &scene);
    glDeleteFramebuffers(1, &finalBuffer);
    glDeleteTextures(2, textures);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::puts("Scene RGB ignores accumulated alpha and preserves pixels outside the viewport.");
}
