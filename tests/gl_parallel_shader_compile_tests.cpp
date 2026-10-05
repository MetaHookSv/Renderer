// Probe the local OpenGL driver for parallel shader compilation support.
//
// Unlike the other suites in this directory this test is not a CPU regression
// test: it opens a real GL context through the shared GLFW target and asks the
// driver which parallel-shader-compile flavor it implements. It is diagnostics,
// so it prints a report and does not assert on the capability itself -- a
// missing extension is a valid driver result, not a test failure.
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cstdio>

namespace {

// A function must be reachable through the current context to be callable.
// GLEW only sets the pointer when the driver advertised the extension, so a
// non-null pointer doubles as a support check.
bool HasUsableEntryPoint(void* proc)
{
    return proc != nullptr;
}

} // namespace

int main()
{
    glfwSetErrorCallback([](int error, const char* description) {
        std::fprintf(stderr, "GLFW %d: %s\n", error, description);
    });
    if (glfwInit() != GLFW_TRUE)
    {
        std::fprintf(stderr, "gl_parallel_shader_compile_tests: glfwInit failed\n");
        return 1;
    }

    // A context is needed so GLEW can query the driver; nothing is drawn.
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "parallel shader compile probe", nullptr, nullptr);
    if (!window)
    {
        std::fprintf(stderr, "gl_parallel_shader_compile_tests: glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    // glewInit queries the context version and every advertised extension.
    // Core-profile drivers reject the legacy glGetString(GL_EXTENSIONS) that
    // GLEW probes by default, which only produces a harmless GL_INVALID_ENUM.
    glewExperimental = GL_TRUE;
    GLenum initResult = glewInit();
    if (initResult != GLEW_OK)
    {
        std::fprintf(stderr, "gl_parallel_shader_compile_tests: glewInit failed: %s\n",
            glewGetErrorString(initResult));
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    const bool arbSupported = GLEW_ARB_parallel_shader_compile != GL_FALSE
        && HasUsableEntryPoint(reinterpret_cast<void*>(__glewMaxShaderCompilerThreadsARB));
    const bool khrSupported = GLEW_KHR_parallel_shader_compile != GL_FALSE
        && HasUsableEntryPoint(reinterpret_cast<void*>(__glewMaxShaderCompilerThreadsKHR));

    std::printf("OpenGL vendor   : %s\n", vendor ? vendor : "(null)");
    std::printf("OpenGL renderer : %s\n", renderer ? renderer : "(null)");
    std::printf("OpenGL version  : %s\n", version ? version : "(null)");
    std::printf("GLEW version    : %s\n", glewGetString(GLEW_VERSION));
    std::printf("GL_ARB_parallel_shader_compile : %s\n", arbSupported ? "supported" : "not supported");
    std::printf("GL_KHR_parallel_shader_compile : %s\n", khrSupported ? "supported" : "not supported");

    // Both extensions expose the same query. A count of 0 is the specification's
    // "implementation default"; a failed query leaves the seeded value untouched,
    // so report the GL error instead of printing a bogus worker count.
    if (arbSupported || khrSupported)
    {
        while (glGetError() != GL_NO_ERROR)
        {
        }
        GLint threadCount = -1;
        glGetIntegerv(GL_MAX_SHADER_COMPILER_THREADS_ARB, &threadCount);
        GLenum queryError = glGetError();
        if (queryError == GL_NO_ERROR)
            std::printf("GL_MAX_SHADER_COMPILER_THREADS_ARB : %d%s\n", threadCount,
                threadCount == 0 ? " (driver decides)" : "");
        else
            std::printf("GL_MAX_SHADER_COMPILER_THREADS_ARB : query failed (GL error 0x%04X)\n", queryError);
    }
    else
    {
        std::puts("This driver cannot compile shaders in parallel.");
    }

    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
