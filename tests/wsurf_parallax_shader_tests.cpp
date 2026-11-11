// Exercise the production world-surface fragment shader on a real GL context.
#include "gl_shader_compile.h"
#include <GLFW/glfw3.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <crtdbg.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
constexpr int                  TargetSize    = 16;
constexpr int                  TextureSize   = 64;
constexpr int                  SkipExitCode  = 2;
constexpr float                Tolerance     = 0.0002f;
constexpr std::array<float, 2> BaseUV        = {0.4f, 0.45f};
constexpr std::array<float, 2> MaterialScale = {0.75f, 1.25f};

std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

GLuint Compile(GLenum type, const std::string& text)
{
    std::string error;
    GLuint      shader = GL_TryCompileShaderObject(type, text.c_str(), error);
    if (!shader) std::fprintf(stderr, "%s\n", error.c_str());
    assert(0 != shader);
    return shader;
}

std::string Source(const std::filesystem::path& directory, const char* file, const std::string& defines)
{
    auto text = Read(directory / file);
    text.insert(text.find('\n') + 1, defines);
    const auto include = text.find("#include \"common.h\"");
    assert(std::string::npos != include);
    text.replace(include, std::strlen("#include \"common.h\""), Read(directory / "common.h"));
    return text;
}

GLuint Link(const std::vector<GLuint>& shaders)
{
    GLuint program = glCreateProgram();
    for (GLuint shader : shaders) glAttachShader(program, shader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[4096]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "%s\n", log);
    }
    assert(GL_TRUE == linked);
    for (GLuint shader : shaders) glDeleteShader(shader);
    return program;
}

void TestProductionVariants(const std::filesystem::path& directory)
{
    for (bool parallax : {false, true})
        for (bool multiview : {false, true})
            for (const char* mode : {"", "#define ALPHA_SOLID_ENABLED\n", "#define GLOW_COLOR_ENABLED\n#define ALPHA_SOLID_ENABLED\n", "#define SHADOW_CASTER_ENABLED\n#define LINEAR_DEPTH_ENABLED\n#define ALPHA_SOLID_ENABLED\n", "#define GBUFFER_ENABLED\n#define NORMALTEXTURE_ENABLED\n#define DETAILTEXTURE_ENABLED\n#define SPECULARTEXTURE_ENABLED\n", "#define GBUFFER_ENABLED\n#define DECAL_ENABLED\n"})
            {
                std::string defines = std::string("#define DIFFUSE_ENABLED\n#define FULLBRIGHT_ENABLED\n") + mode;
                if (parallax) defines += "#define PARALLAXTEXTURE_ENABLED\n";
                if (multiview) defines += "#define MULTIVIEW_ENABLED\n";
                std::vector<GLuint> shaders = {
                    Compile(GL_VERTEX_SHADER, Source(directory, "wsurf_shader.vert.glsl", "#define IS_VERTEX_SHADER\n" + defines)),
                    Compile(GL_FRAGMENT_SHADER, Source(directory, "wsurf_shader.frag.glsl", "#define IS_FRAGMENT_SHADER\n" + defines))};
                if (multiview) shaders.push_back(Compile(GL_GEOMETRY_SHADER, Source(directory, "wsurf_shader.geom.glsl", defines)));
                glDeleteProgram(Link(shaders));
            }
}

GLuint Program(const std::filesystem::path& directory, const std::string& defines)
{
    const std::string vertexText = R"(#version 430
uniform vec3 u_testView;
out vec3 v_worldpos, v_normal, v_tangent, v_bitangent;
out vec2 v_diffusetexcoord, v_detailtexcoord, v_normaltexcoord;
out vec2 v_parallaxtexcoord, v_speculartexcoord;
out vec3 v_lightmaptexcoord;
out vec4 v_projpos;
flat out uvec4 v_styles;
void main()
{
    vec2 corners[3] = vec2[3](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
    gl_Position = vec4(corners[gl_VertexID], 0, 1);
    v_worldpos = u_testView;
    v_normal = vec3(0, 0, 1);
    v_tangent = vec3(1, 0, 0);
    v_bitangent = vec3(0, 1, 0);
    v_diffusetexcoord = vec2(0.4, 0.45);
    v_detailtexcoord = vec2(0.75, 1.25);
    v_normaltexcoord = v_detailtexcoord;
    v_speculartexcoord = v_detailtexcoord;
    v_parallaxtexcoord = v_detailtexcoord;
    v_lightmaptexcoord = vec3(0.3, 0.6, 0);
    v_projpos = gl_Position;
    v_styles = uvec4(0);
}
)";
    return Link({Compile(GL_VERTEX_SHADER, vertexText), Compile(GL_FRAGMENT_SHADER, Source(directory, "wsurf_shader.frag.glsl", "#define IS_FRAGMENT_SHADER\n" + defines))});
}

void SetMember(GLuint program, std::vector<unsigned char>& bytes, const char* name, const void* value, size_t size)
{
    GLuint index = GL_INVALID_INDEX;
    glGetUniformIndices(program, 1, &name, &index);
    if (index == GL_INVALID_INDEX) return;
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
    std::vector<unsigned char> bytes(size);
    const float                one = 1, threshold = 0.125f;
    const std::array<float, 4> white = {1, 1, 1, 1};
    if (binding == 0)
    {
        for (const char* field : {"SceneUBO.v_gamma", "SceneUBO.v_texgamma", "SceneUBO.v_lightgamma", "SceneUBO.r_lightmap_scale", "SceneUBO.r_lightmap_pow", "SceneUBO.r_lightscale"})
            SetMember(program, bytes, field, &one, sizeof(one));
        SetMember(program, bytes, "SceneUBO.r_g3", &threshold, sizeof(threshold));
        SetMember(program, bytes, "SceneUBO.r_lightstylevalue[0]", white.data(), sizeof(white));
    }
    else if (binding == 3)
        SetMember(program, bytes, "EntityUBO.r_color", white.data(), sizeof(white));
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferData(GL_UNIFORM_BUFFER, bytes.size(), bytes.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
    return buffer;
}

GLuint Texture(GLuint unit, GLenum target, const std::vector<float>& pixels)
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, texture);
    if (target == GL_TEXTURE_2D_ARRAY)
        glTexImage3D(target, 0, GL_RGBA32F, TextureSize, TextureSize, 1, 0, GL_RGBA, GL_FLOAT, pixels.data());
    else
        glTexImage2D(target, 0, GL_RGBA32F, TextureSize, TextureSize, 0, GL_RGBA, GL_FLOAT, pixels.data());
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

void Expect(float expected, float actual, const char* channel)
{
    if (std::abs(expected - actual) > Tolerance || !std::isfinite(actual))
    {
        std::fprintf(stderr, "%s: expected=%f actual=%f\n", channel, expected, actual);
        std::exit(EXIT_FAILURE);
    }
}

void Test(const std::filesystem::path& directory, bool parallax, bool decal, bool detail)
{
    std::string defines = "#define DIFFUSE_ENABLED\n#define GBUFFER_ENABLED\n#define NORMALTEXTURE_ENABLED\n#define SPECULARTEXTURE_ENABLED\n";
    defines += decal ? "#define DECAL_ENABLED\n#define FULLBRIGHT_ENABLED\n" : "#define LIGHTMAP_ENABLED\n#define LIGHTMAP_INDEX_0_ENABLED\n";
    if (parallax) defines += "#define PARALLAXTEXTURE_ENABLED\n";
    if (detail) defines += "#define DETAILTEXTURE_ENABLED\n";
    GLuint program = Program(directory, defines);
    glUseProgram(program);
    std::array<GLuint, 3> blocks = {BindBlock(program, "SceneBlock", 0), BindBlock(program, "CameraBlock", 1), BindBlock(program, "EntityBlock", 3)};
    std::vector<float>    ramp(TextureSize * TextureSize * 4), normals(ramp.size()), heights(ramp.size());
    for (int y = 0; y < TextureSize; ++y)
        for (int x = 0; x < TextureSize; ++x)
        {
            size_t i = (y * TextureSize + x) * 4;
            float  u = (x + 0.5f) / TextureSize, v = (y + 0.5f) / TextureSize;
            ramp[i]        = u;
            ramp[i + 1]    = v;
            ramp[i + 2]    = 1;
            ramp[i + 3]    = 1;
            normals[i]     = u;
            normals[i + 1] = 0.75f;
            normals[i + 2] = v;
            normals[i + 3] = 1;
        }
    std::array<GLuint, 6> textures = {Texture(0, GL_TEXTURE_2D, ramp), Texture(1, GL_TEXTURE_2D, ramp), Texture(2, GL_TEXTURE_2D, normals), Texture(3, GL_TEXTURE_2D, heights), Texture(4, GL_TEXTURE_2D, ramp), Texture(5, GL_TEXTURE_2D_ARRAY, ramp)};
    for (bool heightRamp : {false, true})
        for (float height : {0.5f, 0.0f, 0.25f, 0.75f, 1.0f})
        {
            if (heightRamp && height != 0.5f) continue;
            for (size_t i = 0; i < heights.size(); i += 4) heights[i] = heightRamp ? ramp[i] : height;
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, textures[3]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TextureSize, TextureSize, GL_RGBA, GL_FLOAT, heights.data());
            for (float scale : {-0.02f, 0.0f, 0.02f})
                for (const auto& view : {std::array<float, 3>{0, 0, -1}, std::array<float, 3>{1, 0.5f, -1}, std::array<float, 3>{5, -2, -1}})
                {
                    glUniform1f(glGetUniformLocation(program, "u_parallaxScale"), scale);
                    glUniform3fv(glGetUniformLocation(program, "u_testView"), 1, view.data());
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                    float px = parallax ? view[0] / view[2] * scale : 0;
                    float py = parallax ? view[1] / view[2] * scale : 0;
                    // A linear height ramp has an analytic ray/plane intersection.
                    float                u         = heightRamp ? (BaseUV[0] - 0.5f * px) / (1 - px * MaterialScale[0]) : BaseUV[0] + px * (height - 0.5f);
                    float                v         = BaseUV[1] + py * ((heightRamp ? u * MaterialScale[0] : height) - 0.5f);
                    float                materialU = u * MaterialScale[0], materialV = v * MaterialScale[1];
                    std::array<float, 4> pixel{};
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glReadPixels(TargetSize / 2, TargetSize / 2, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
                    Expect(u * (detail ? 2 * materialU : 1), pixel[0], "diffuse/detail U");
                    Expect(v * (detail ? 2 * materialV : 1), pixel[1], "diffuse/detail V");
                    glReadBuffer(decal ? GL_COLOR_ATTACHMENT1 : GL_COLOR_ATTACHMENT2);
                    glReadPixels(TargetSize / 2, TargetSize / 2, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
                    float nx = 2 * materialU - 1, nz = 2 * materialV - 1;
                    float norm = std::abs(nx) + 0.5f + std::abs(nz);
                    Expect(0.5f + 0.5f * nx / norm, pixel[0], "normal U");
                    Expect(0.5f + 0.5f * nz / norm, pixel[1], "normal V");
                    glReadBuffer(decal ? GL_COLOR_ATTACHMENT2 : GL_COLOR_ATTACHMENT3);
                    glReadPixels(TargetSize / 2, TargetSize / 2, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
                    Expect(materialU, pixel[0], "specular U");
                    Expect(materialV, pixel[1], "specular V");
                    if (!decal)
                    {
                        glReadBuffer(GL_COLOR_ATTACHMENT1);
                        glReadPixels(TargetSize / 2, TargetSize / 2, 1, 1, GL_RGBA, GL_FLOAT, pixel.data());
                        Expect(0.3f, pixel[0], "unchanged lightmap U");
                        Expect(0.6f, pixel[1], "unchanged lightmap V");
                    }
                    assert(GL_NO_ERROR == glGetError());
                }
        }
    glDeleteTextures(textures.size(), textures.data());
    glDeleteBuffers(blocks.size(), blocks.data());
    glDeleteProgram(program);
}
} // namespace

int main(int argc, char** argv)
{
    // Keep failures in CTest's output instead of opening a blocking CRT dialog.
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    assert(2 == argc);
    if (!glfwInit()) return SkipExitCode;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(TargetSize, TargetSize, "World-surface parallax regression", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return SkipExitCode;
    }
    glfwMakeContextCurrent(window);
    glewExperimental = GL_TRUE;
    assert(GLEW_OK == glewInit());
    while (glGetError() != GL_NO_ERROR) {}
    std::printf("GPU: %s; OpenGL: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    TestProductionVariants(argv[1]);
    GLuint                          vao = 0, framebuffer = 0;
    std::array<GLuint, 4>           targets{};
    constexpr std::array<GLenum, 4> attachments = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(targets.size(), targets.data());
    for (size_t i = 0; i < targets.size(); ++i)
    {
        glBindTexture(GL_TEXTURE_2D, targets[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, TargetSize, TargetSize);
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachments[i], GL_TEXTURE_2D, targets[i], 0);
    }
    glDrawBuffers(attachments.size(), attachments.data());
    assert(GL_FRAMEBUFFER_COMPLETE == glCheckFramebufferStatus(GL_FRAMEBUFFER));
    glViewport(0, 0, TargetSize, TargetSize);
    for (bool parallax : {false, true})
        for (bool decal : {false, true})
            for (bool detail : {false, true})
                Test(argv[1], parallax, decal, detail);
    glDeleteTextures(targets.size(), targets.data());
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::puts("24 production shader variants and 432 midpoint/ray intersection/material alignment/lightmap cases passed");
}
