#include "../src/gl_shader_parallel.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <map>
#include <vector>

//Counts blocking link-status queries on top of the production GL boundary.
class CCountingShaderGLOperations : public CShaderGLOperations
{
public:
    CCountingShaderGLOperations() : CShaderGLOperations(nullptr)
    {
    }

    GLint GetProgramParameter(GLuint program, GLenum name) override
    {
        if (name == GL_LINK_STATUS)
            ++linkQueries;

        return CShaderGLOperations::GetProgramParameter(program, name);
    }

    unsigned linkQueries{};
};

static CShaderProgramSource Source(unsigned state, bool geometry = false)
{
    CShaderProgramSource source;
    source.category  = "integration";
    source.state     = state;
    source.stages[0] = {GL_VERTEX_SHADER, "inline.vert", "#version 330 core\nvoid main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2); gl_Position=vec4(p*2.0-1.0,0,1); }", ""};
    if (geometry)
        source.stages[1] = {GL_GEOMETRY_SHADER, "inline.geom", "#version 330 core\nlayout(triangles) in; layout(triangle_strip,max_vertices=3) out; void main(){for(int i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;EmitVertex();}EndPrimitive();}", ""};
    source.stages[2] = {GL_FRAGMENT_SHADER, "inline.frag", "#version 330 core\nuniform vec4 u_color; out vec4 color; void main(){color=u_color;}\n//variant=" + std::to_string(state), ""};
    return source;
}

static void CheckPixel(GLuint program, GLint color)
{
    GLuint framebuffer = 0;
    GLuint texture     = 0;
    GLuint vao         = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    assert(GL_FRAMEBUFFER_COMPLETE == glCheckFramebufferStatus(GL_FRAMEBUFFER));
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glViewport(0, 0, 4, 4);
    glUseProgram(program);
    assert(color >= 0);
    glUniform4f(color, 0.25f, 0.5f, 0.75f, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4] = {};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(pixel[0] >= 63 && pixel[0] <= 65);
    assert(pixel[1] >= 127 && pixel[1] <= 129);
    assert(pixel[2] >= 190 && pixel[2] <= 192);
    assert(255 == pixel[3]);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &framebuffer);
    assert(GL_NO_ERROR == glGetError());
}

static double Percentile(std::vector<double> values, double percentile)
{
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>((values.size() - 1) * percentile)];
}

int main()
{
    if (!glfwInit())
        return 2;

    CShaderProgramTicket previous;

    for (int generation = 0; generation < 3; ++generation)
    {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        auto window = glfwCreateWindow(64, 64, "shader scheduler integration", nullptr, nullptr);
        if (!window)
        {
            glfwDefaultWindowHints();
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            window = glfwCreateWindow(64, 64, "shader scheduler integration", nullptr, nullptr);
        }
        if (!window)
        {
            glfwTerminate();
            return generation == 0 ? 2 : 1;
        }
        glfwMakeContextCurrent(window);
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK)
            return 1;

        while (glGetError() != GL_NO_ERROR)
        {
        }

        if (!GLEW_VERSION_3_3)
            return 1;

        std::printf("generation=%d vendor=%s renderer=%s GL=%s GLEW=%s ARB=%d KHR=%d\n", generation, glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION), glewGetString(GLEW_VERSION), GLEW_ARB_parallel_shader_compile, GLEW_KHR_parallel_shader_compile);

        CCountingShaderGLOperations operations;
        CParallelShaderCompiler     compiler(operations);

        auto capabilities = GL_DetectShaderCompileCapabilities();

        //Exercise KHR on dual-flavor drivers.
        if (generation == 1)
            capabilities.arb = false;

        //Real synchronous fallback.
        if (generation == 2)
            capabilities = {};

        compiler.Initialize(capabilities);
        assert(0 == compiler.TryGet(previous));

        //All drivers execute real forced-sync functionality, including pixel readback.
        compiler.Configure({false, -1, 16});
        GLint location = -1;

        previous = compiler.Request(Source(0), ShaderPriority::Critical, [&](GLuint program) {
            location = glGetUniformLocation(program, "u_color");
        });

        const auto syncProgram = compiler.Require(previous);
        assert(syncProgram);
        CheckPixel(syncProgram, location);
        compiler.Reset(true);
        assert(GL_FALSE == glIsProgram(syncProgram));

        compiler.Configure({true, -1, 16});
        std::vector<CShaderProgramTicket> tickets;
        std::map<GLuint, GLint>           locations;

        auto finalize = [&](GLuint program) {
            locations[program] = glGetUniformLocation(program, "u_color");
        };

        for (unsigned i = 0; i < 6; ++i)
            tickets.push_back(compiler.Request(Source(i, i & 1), ShaderPriority::Prewarm, finalize));

        for (unsigned i = 0; i < 3; ++i)
        {
            auto invalid             = Source(100 + i, true);
            invalid.stages[i].source = "#version 330 core\ninvalid GLSL";
            tickets.push_back(compiler.Request(invalid, ShaderPriority::Prewarm, finalize));
        }

        auto before = operations.linkQueries;
        compiler.Pump(10, 0, 16);
        const bool asynchronous = compiler.Statistics().backend != ShaderCompileBackend::Sync;

        //Submission performed no blocking query.
        assert(before == operations.linkQueries);

        if (asynchronous)
        {
            const auto deadline = glfwGetTime() + 20;
            while ((compiler.Statistics().queued || compiler.Statistics().inflight) && glfwGetTime() < deadline)
            {
                compiler.Pump(1, 64, 16);
                glfwPollEvents();
            }
            assert(0 == compiler.Statistics().queued && 0 == compiler.Statistics().inflight);
        }
        for (size_t i = 0; i < tickets.size(); ++i)
        {
            if (i < 6)
            {
                auto program = compiler.Require(tickets[i]);
                assert(program);
                assert(ShaderJobState::Ready == compiler.State(tickets[i]));
                CheckPixel(program, locations.at(program));
            }
            else
            {
                assert(0 == compiler.Require(tickets[i]));
                assert(ShaderJobState::Failed == compiler.State(tickets[i]));
                assert(std::string::npos != compiler.Error(tickets[i]).find("inline."));
                std::printf("expected failure: %s\n", compiler.Error(tickets[i]).c_str());
            }
        }
        assert(GL_NO_ERROR == glGetError());
        auto stats = compiler.Statistics();
        std::printf("hint requested=%u readback=%u submitted=%llu ready=%llu failed=%llu peak=%llu completion polls=%llu\n", stats.requestedHint, stats.reportedHint, stats.submitted, stats.ready, stats.failed, stats.peakInflight, stats.polls);

        //Same workload, alternating order, five rounds. Driver cache is not purged.
        if (generation == 0)
        {
            std::puts("PERF,history,round,parallel,programs,hits,submit_avg_ms,submit_peak_ms,poll_ms,wall_ms,stalls,stall_ms,pump_p50_ms,pump_p95_ms,pump_p99_ms,peak_inflight");
            for (int history = 0; history < 2; ++history)
            {
                for (int round = 0; round < 5; ++round)
                {
                    for (int order = 0; order < 2; ++order)
                    {
                        CShaderGLOperations     benchmarkOperations(nullptr);
                        CParallelShaderCompiler benchmark(benchmarkOperations);
                        benchmark.Initialize(capabilities);
                        const bool parallel = ((order + round) & 1) != 0;
                        benchmark.Configure({parallel, -1, 16});
                        std::vector<CShaderProgramTicket> workload;
                        std::vector<double>               pumps;
                        const auto                        start = glfwGetTime();
                        for (unsigned i = 0; i < 32; ++i)
                        {
                            auto tick   = glfwGetTime();
                            auto ticket = benchmark.Request(Source(i), history ? ShaderPriority::Prewarm : ShaderPriority::Critical, {});
                            workload.push_back(ticket);
                            if (!history)
                            {
                                assert(benchmark.Require(ticket));
                                pumps.push_back((glfwGetTime() - tick) * 1000);
                            }
                        }
                        if (history && benchmark.Statistics().backend != ShaderCompileBackend::Sync)
                        {
                            while ((benchmark.Statistics().queued || benchmark.Statistics().inflight) && glfwGetTime() - start < 20)
                            {
                                auto tick = glfwGetTime();
                                benchmark.Pump(1, 64, 8);
                                pumps.push_back((glfwGetTime() - tick) * 1000);
                                glfwPollEvents();
                            }
                            assert(0 == benchmark.Statistics().queued && 0 == benchmark.Statistics().inflight);
                        }
                        if (history)
                        {
                            for (auto ticket : workload)
                            {
                                auto tick = glfwGetTime();
                                assert(benchmark.Require(ticket));
                                pumps.push_back((glfwGetTime() - tick) * 1000);
                            }
                        }
                        auto s = benchmark.Statistics();
                        std::printf("PERF,%d,%d,%d,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%llu,%.6f,%.6f,%.6f,%.6f,%llu\n", history, round, parallel, s.submitted, s.cacheHits, s.submitMs / s.submitted, s.peakSubmitMs, s.pollMs, (glfwGetTime() - start) * 1000, s.demandStalls, s.demandMs, Percentile(pumps, .5), Percentile(pumps, .95), Percentile(pumps, .99), s.peakInflight);
                        benchmark.Reset(true);
                    }
                }
            }
        }
        previous = tickets.front();
        compiler.Reset(true);
        assert(locations.size() == 6);
        assert(compiler.Statistics().shadersCreated == compiler.Statistics().shadersDeleted);
        assert(compiler.Statistics().programsCreated == compiler.Statistics().programsDeleted);

        for (const auto& [program, uniform] : locations)
            assert(GL_FALSE == glIsProgram(program));

        assert(GL_NO_ERROR == glGetError());
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(window);
    }
    glfwTerminate();
    std::puts("real shader scheduler integration: passed");
}
