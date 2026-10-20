// Run the installed TriAPI shaders on a real core context and read their output.
#include "gl_shader_compile.h"
#include <GLFW/glfw3.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
constexpr int                  TargetSize   = 16;
constexpr int                  SkipExitCode = 2;
constexpr float                Gamma        = 1.8f;
constexpr std::array<float, 4> FogColor     = {0.1f, 0.2f, 0.3f, 1};
constexpr float                FogDensity   = 0.8f;

std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

GLuint Compile(const std::filesystem::path& directory, GLenum type, const std::string& defines)
{
    auto       text       = Read(directory / (type == GL_VERTEX_SHADER ? "triapi_shader.vert.glsl" : "triapi_shader.frag.glsl"));
    const auto versionEnd = text.find('\n') + 1;
    text.insert(versionEnd, defines + (type == GL_VERTEX_SHADER ? "#define IS_VERTEX_SHADER\n" : "#define IS_FRAGMENT_SHADER\n"));
    const auto include = text.find("#include \"common.h\"");
    assert(std::string::npos != include);
    text.replace(include, std::strlen("#include \"common.h\""), Read(directory / "common.h"));
    std::string error;
    GLuint      shader = GL_TryCompileShaderObject(type, text.c_str(), error);
    if (!shader) std::fprintf(stderr, "%s\n", error.c_str());
    assert(0 != shader);
    return shader;
}

void SetMember(GLuint program, std::vector<unsigned char>& bytes, const char* name, const void* value, size_t size)
{
    GLuint index = GL_INVALID_INDEX;
    glGetUniformIndices(program, 1, &name, &index);
    if (index == GL_INVALID_INDEX) return; // Unused fields are optimized out.
    GLint offset = 0;
    glGetActiveUniformsiv(program, 1, &index, GL_UNIFORM_OFFSET, &offset);
    assert(offset >= 0 && offset + size <= bytes.size());
    std::memcpy(bytes.data() + offset, value, size);
}

GLuint BindBlock(GLuint program, const char* name, GLuint binding)
{
    GLuint block = glGetUniformBlockIndex(program, name);
    if (block == GL_INVALID_INDEX) return 0;
    GLint size = 0;
    glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &size);
    std::vector<unsigned char>  bytes(size);
    const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float                 texGamma = 2.2f, inverseGamma = 1 / Gamma, blendShift = 0.8f, fogEnd = 1;
    if (binding == 0)
    {
        SetMember(program, bytes, "SceneUBO.v_gamma", &Gamma, sizeof(Gamma));
        SetMember(program, bytes, "SceneUBO.v_texgamma", &texGamma, sizeof(texGamma));
        SetMember(program, bytes, "SceneUBO.r_g", &inverseGamma, sizeof(inverseGamma));
        SetMember(program, bytes, "SceneUBO.r_linear_blend_shift", &blendShift, sizeof(blendShift));
        SetMember(program, bytes, "SceneUBO.fogColor", FogColor.data(), sizeof(FogColor));
        SetMember(program, bytes, "SceneUBO.fogEnd", &fogEnd, sizeof(fogEnd));
        SetMember(program, bytes, "SceneUBO.fogDensity", &FogDensity, sizeof(FogDensity));
    }
    else
    {
        SetMember(program, bytes, "CameraUBO.views[0].worldMatrix", identity.data(), sizeof(identity));
        SetMember(program, bytes, "CameraUBO.views[0].projMatrix", identity.data(), sizeof(identity));
    }
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferData(GL_UNIFORM_BUFFER, bytes.size(), bytes.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
    return buffer;
}

void TestModulation(const std::filesystem::path& directory, bool hud, bool gammaBlend, int blend, int fog)
{
    std::string defines;
    if (hud) defines += "#define TRIAPI_HUD_SPACE_ENABLED\n";
    if (gammaBlend) defines += "#define GAMMA_BLEND_ENABLED\n";
    if (blend == 1) defines += "#define ALPHA_BLEND_ENABLED\n";
    if (blend == 2) defines += "#define ADDITIVE_BLEND_ENABLED\n";
    if (fog == 1) defines += "#define LINEAR_FOG_ENABLED\n";
    if (fog == 2) defines += "#define EXP_FOG_ENABLED\n";
    if (fog == 3) defines += "#define EXP2_FOG_ENABLED\n";
    GLuint vertex   = Compile(directory, GL_VERTEX_SHADER, defines);
    GLuint fragment = Compile(directory, GL_FRAGMENT_SHADER, defines);
    GLuint program  = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    assert(GL_TRUE == linked);
    glUseProgram(program);
    const std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glUniformMatrix4fv(glGetUniformLocation(program, "u_hudMatrix"), 1, GL_FALSE, identity.data());
    std::array<GLuint, 2> blocks = {BindBlock(program, "SceneBlock", 0), BindBlock(program, "CameraBlock", 1)};

    const std::array<float, 4> background = {0.05f, 0.1f, 0.15f, 1};
    glClearColor(background[0], background[1], background[2], background[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    if (blend)
    {
        glEnable(GL_BLEND);
        glBlendFunc(blend == 1 ? GL_SRC_ALPHA : GL_ONE, blend == 1 ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE);
    }
    else
        glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::array<float, 4> pixel{};
    // The off-axis sample distinguishes eye-space Z from radial distance.
    glReadPixels(TargetSize - 2, TargetSize - 2, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
    const std::array<float, 3> modulated   = {0.2f, 0.15f, 0.6f};
    constexpr float            sourceAlpha = 0.4f * 0.5f;
    for (int channel = 0; channel < 3; ++channel)
    {
        float factor = 1;
        if (!hud && fog == 1) factor = 0.5f;
        if (!hud && fog == 2) factor = std::exp(-FogDensity * 0.5f);
        if (!hud && fog == 3) factor = std::exp(-std::pow(FogDensity * 0.5f, 2.0f));
        float expected = modulated[channel] * factor + FogColor[channel] * (1 - factor);
        if (!hud && !gammaBlend) expected = std::pow(expected, Gamma);
        if (blend == 1) expected = expected * sourceAlpha + background[channel] * (1 - sourceAlpha);
        if (blend == 2) expected += background[channel];
        if (std::abs(expected - pixel[channel]) > 0.0001f)
            std::fprintf(stderr, "HUD=%d gamma=%d blend=%d channel=%d expected=%f actual=%f\n", hud, gammaBlend, blend, channel, expected, pixel[channel]);
        assert(std::abs(expected - pixel[channel]) <= 0.0001f);
    }
    assert(GL_NO_ERROR == glGetError());
    glDeleteBuffers(blocks.size(), blocks.data());
    glDeleteProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
}
} // namespace

int main(int argc, char** argv)
{
    assert(2 == argc);
    if (!glfwInit()) return SkipExitCode;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(TargetSize, TargetSize, "TriAPI shader regression", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return SkipExitCode;
    }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    assert(GLEW_OK == glewInit());
    while (glGetError() != GL_NO_ERROR) {}

    GLuint vao = 0, buffer = 0, target = 0, framebuffer = 0, sprite = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const float triangle[] = {-1, -1, -0.5f, 3, -1, -0.5f, -1, 3, -0.5f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttrib2f(1, 0.5f, 0.5f);
    glVertexAttrib4f(2, 0.4f, 0.6f, 0.8f, 0.5f);

    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, TargetSize, TargetSize);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    assert(GL_FRAMEBUFFER_COMPLETE == glCheckFramebufferStatus(GL_FRAMEBUFFER));
    glViewport(0, 0, TargetSize, TargetSize);

    glGenTextures(1, &sprite);
    glBindTexture(GL_TEXTURE_2D, sprite);
    const float texel[] = {0.5f, 0.25f, 0.75f, 0.4f};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, texel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (bool hud : {false, true})
        for (bool gammaBlend : {false, true})
            for (int blend = 0; blend < 3; ++blend)
                for (int fog = 0; fog < 4; ++fog)
                    TestModulation(argv[1], hud, gammaBlend, blend, fog);

    glDeleteTextures(1, &sprite);
    glDeleteTextures(1, &target);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteBuffers(1, &buffer);
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::puts("TriAPI HUD/world modulation and blend shader tests passed");
}
