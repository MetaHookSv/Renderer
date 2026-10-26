#include "../src/gl_shader_parallel.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

class CFakeShaderGLOperations : public IShaderGLOperations
{
public:
    bool current{true};
    bool complete{};
    bool link{true};
    bool createFailure{};
    bool programCreateFailure{};
    bool arbEntry{true};
    bool khrEntry{true};

    //Fails the Nth shader creation counted from the last reset of shaderCreateCalls; 0 disables it.
    unsigned shaderCreateFailureAt{};
    unsigned shaderCreateCalls{};

    unsigned creates{};
    unsigned links{};
    unsigned status{};
    unsigned completions{};
    unsigned finalized{};
    unsigned arbCalls{};
    unsigned khrCalls{};

    mutable double clock{};
    GLuint         hint{};
    GLuint         next{};

    std::set<GLuint>         shaders;
    std::set<GLuint>         programs;
    std::vector<std::string> sources;

    bool IsContextCurrent() const override
    {
        return current;
    }

    double GetTimeMs() const override
    {
        return clock;
    }

    GLuint CreateShader(GLenum) override
    {
        ++shaderCreateCalls;

        if (createFailure || shaderCreateCalls == shaderCreateFailureAt)
            return 0;

        shaders.insert(++next);
        ++creates;
        return next;
    }

    void ShaderSource(GLuint, const char* source) override
    {
        sources.emplace_back(source);
    }

    void CompileShader(GLuint) override
    {
    }

    void DeleteShader(GLuint id) override
    {
        assert(1 == shaders.erase(id));
    }

    GLint GetShaderParameter(GLuint, GLenum) override
    {
        return GL_FALSE;
    }

    std::string GetShaderLog(GLuint) override
    {
        return "stage error";
    }

    GLuint CreateProgram() override
    {
        if (programCreateFailure)
            return 0;

        programs.insert(++next);
        return next;
    }

    void AttachShader(GLuint, GLuint) override
    {
    }

    void DetachShader(GLuint, GLuint) override
    {
    }

    void LinkProgram(GLuint) override
    {
        ++links;
    }

    void DeleteProgram(GLuint id) override
    {
        assert(1 == programs.erase(id));
    }

    GLint GetProgramParameter(GLuint, GLenum name) override
    {
        clock += 0.1;

        if (name == GL_COMPLETION_STATUS_ARB)
        {
            ++completions;
            return GLint(complete);
        }

        ++status;
        return GLint(link);
    }

    std::string GetProgramLog(GLuint) override
    {
        return "link error";
    }

    bool HasThreadHint(ShaderCompileBackend backend) const override
    {
        if (backend == ShaderCompileBackend::ARB)
            return arbEntry;

        if (backend == ShaderCompileBackend::KHR)
            return khrEntry;

        return false;
    }

    void SetThreadHint(ShaderCompileBackend backend, GLuint value) override
    {
        if (backend == ShaderCompileBackend::ARB)
            ++arbCalls;
        else
            ++khrCalls;

        hint = value;
    }

    GLuint GetThreadHint() override
    {
        return hint;
    }

    void ReportError(const std::string&) override
    {
    }
};

static CShaderProgramSource Source(uint64_t state = 0, const char* category = "Studio")
{
    CShaderProgramSource source;
    source.category  = category;
    source.state     = state;
    source.stages[0] = {GL_VERTEX_SHADER, "test.vert", "owned vertex", "VERTEX_DEFINE"};
    source.stages[2] = {GL_FRAGMENT_SHADER, "test.frag", "owned fragment", "FRAGMENT_DEFINE"};
    return source;
}

static ShaderCompileBackend ExpectedBackend(unsigned mask)
{
    if (mask & 4)
        return ShaderCompileBackend::Sync;

    if (mask & 1)
        return ShaderCompileBackend::ARB;

    if (mask & 2)
        return ShaderCompileBackend::KHR;

    return ShaderCompileBackend::Sync;
}

int main()
{
    assert(SHADER_COMPILER_THREADS_MAXIMUM == GL_ShaderCompilerThreadHint(-1));
    assert(SHADER_COMPILER_THREADS_MAXIMUM == GL_ShaderCompilerThreadHint(-2));
    assert(SHADER_COMPILER_THREADS_MAXIMUM == GL_ShaderCompilerThreadHint(NAN));
    assert(SHADER_COMPILER_THREADS_MAXIMUM == GL_ShaderCompilerThreadHint(1e40));
    assert(0 == GL_ShaderCompilerThreadHint(0));
    assert(8 == GL_ShaderCompilerThreadHint(8));

    //Bit 0/1 advertise ARB/KHR; bit 2 removes both extension entry points.
    for (unsigned mask = 0; mask < 8; ++mask)
    {
        CFakeShaderGLOperations fake;

        if (mask & 4)
        {
            fake.arbEntry = false;
            fake.khrEntry = false;
        }

        CParallelShaderCompiler compiler(fake);
        compiler.Initialize({bool(mask & 1), bool(mask & 2)});
        assert(ExpectedBackend(mask) == compiler.Statistics().backend);
    }

    CFakeShaderGLOperations fake;
    CParallelShaderCompiler compiler(fake);
    compiler.Initialize({true, true});
    assert(1 == fake.arbCalls && 0 == fake.khrCalls);

    auto finalize = [&](GLuint program) {
        assert(fake.programs.count(program));
        ++fake.finalized;
    };

    auto ticket = compiler.Request(Source(), ShaderPriority::Prewarm, finalize);
    assert(ticket == compiler.Request(Source(), ShaderPriority::Critical, finalize));
    assert(ShaderJobState::Queued == compiler.State(ticket));
    compiler.Pump(1, 8, 8);
    assert(ShaderJobState::Linking == compiler.State(ticket));
    assert(0 == fake.status && 0 == fake.finalized && 0 == compiler.TryGet(ticket));

    auto other = compiler.Request(Source(1), ShaderPriority::Prewarm, finalize);
    compiler.Pump(1, 1, 8);
    assert(2 == fake.links);

    auto queries = fake.completions;
    compiler.Pump(0.15, 100, 0);
    assert(fake.completions - queries <= 2);

    fake.complete = true;
    compiler.Pump(1, 8, 0);
    assert(2 == fake.finalized && 2 == fake.status);
    assert(compiler.TryGet(ticket) == compiler.Require(ticket));
    assert(0 == compiler.Statistics().demandStalls);

    auto pending = compiler.Request(Source(2), ShaderPriority::Prewarm, finalize);
    compiler.Pump(1, 0, 8);
    assert(compiler.Require(pending));
    assert(1 == compiler.Statistics().demandStalls);
    assert(3 == fake.links);

    //Identity includes category, stages and owned source, independent of caller lifetime.
    auto source             = Source(2, "Water");
    source.stages[1]        = {GL_GEOMETRY_SHADER, "test.geom", "owned geometry", "MULTIVIEW"};
    auto geometry           = compiler.Request(source, ShaderPriority::Prewarm, finalize);
    source.stages[1].source = "destroyed caller buffer";
    assert(compiler.Require(geometry));
    assert("owned geometry" == fake.sources[fake.sources.size() - 2]);

    fake.link    = false;
    auto failure = compiler.Request(Source(3), ShaderPriority::Critical, finalize);
    assert(0 == compiler.Require(failure));
    assert(ShaderJobState::Failed == compiler.State(failure));
    assert(std::string::npos != compiler.Error(failure).find("test.vert"));
    assert(std::string::npos != compiler.Error(failure).find("VERTEX_DEFINE"));

    fake.createFailure = true;
    auto allocation    = compiler.Request(Source(4), ShaderPriority::Critical, finalize);
    assert(0 == compiler.Require(allocation));
    compiler.Reset(true);
    assert(fake.shaders.empty() && fake.programs.empty());
    assert(compiler.Statistics().shadersCreated == compiler.Statistics().shadersDeleted);
    assert(compiler.Statistics().programsCreated == compiler.Statistics().programsDeleted);

    compiler.Reset(true);
    assert(0 == compiler.TryGet(ticket));

    auto fresh = compiler.Request(Source(), ShaderPriority::Prewarm, finalize);
    assert(fresh.contextGeneration != ticket.contextGeneration);
    fake.current = false;
    compiler.Pump(1, 8, 8);
    assert(ShaderJobState::Queued == compiler.State(fresh));
    compiler.Reset(false);

    //Sync mode never uses the completion token and ignores unavailable extensions.
    fake.current       = true;
    fake.createFailure = false;
    fake.link          = true;
    compiler.Configure({false, 0, 1});

    auto sync = compiler.Request(Source(), ShaderPriority::Critical, finalize);
    queries   = fake.completions;
    assert(compiler.Require(sync));
    assert(queries == fake.completions);
    assert(1 == compiler.Statistics().syncFallbacks);
    compiler.Reset(true);

    //Reconfiguration applies on the GL thread; already submitted jobs retain
    //completion semantics when new requests switch to synchronous demand mode.
    compiler.Configure({true, 8, 1});
    fake.complete  = false;
    auto first     = compiler.Request(Source(10), ShaderPriority::Prewarm, finalize);
    auto second    = compiler.Request(Source(11), ShaderPriority::Prewarm, finalize);
    auto third     = compiler.Request(Source(12), ShaderPriority::Critical, finalize);
    auto submitted = compiler.Statistics().submitted;
    compiler.Pump(1, 0, 8);
    assert(ShaderJobState::Linking == compiler.State(third));
    assert(ShaderJobState::Queued == compiler.State(first));
    assert(submitted + 1 == compiler.Statistics().submitted);

    compiler.Configure({false, 0, 1});
    assert(0 == fake.hint);
    fake.complete = true;
    compiler.Pump(1, 1, 8);
    assert(ShaderJobState::Ready == compiler.State(third));
    assert(compiler.Require(second));
    assert(compiler.Require(first));
    compiler.Reset(true);

    compiler.Configure({true, -1, 2});
    fake.complete = false;
    first         = compiler.Request(Source(20), ShaderPriority::Prewarm, finalize);
    second        = compiler.Request(Source(21), ShaderPriority::Prewarm, finalize);
    compiler.Pump(1, 0, 2);
    auto cancelled = compiler.Statistics().cancelled;
    compiler.Reset(true);
    assert(cancelled + 2 == compiler.Statistics().cancelled);
    assert(fake.shaders.empty() && fake.programs.empty());

    //Finalizers can enqueue work, but cannot recursively pump/require/reset it.
    CShaderProgramTicket enqueued;

    auto reentrant = compiler.Request(Source(30), ShaderPriority::Critical, [&](GLuint) {
        enqueued = compiler.Request(Source(31), ShaderPriority::Prewarm, finalize);
        assert(0 == compiler.Require(enqueued));
        compiler.Pump(1, 8, 8);
    });

    assert(compiler.Require(reentrant));
    assert(ShaderJobState::Queued == compiler.State(enqueued));
    assert(compiler.Require(enqueued));
    compiler.Reset(true);

    auto throwing = compiler.Request(Source(40), ShaderPriority::Critical, [&](GLuint) {
        compiler.Reset(true);
    });

    assert(0 == compiler.Require(throwing));
    assert(ShaderJobState::Failed == compiler.State(throwing));
    assert(fake.shaders.empty() && fake.programs.empty());

    //A partial allocation failure releases earlier stages exactly once.
    fake.shaderCreateCalls     = 0;
    fake.shaderCreateFailureAt = 2;

    CParallelShaderCompiler partial(fake);
    partial.Initialize({true, false});
    auto partialTicket = partial.Request(Source(), ShaderPriority::Critical, finalize);
    assert(0 == partial.Require(partialTicket));
    assert(fake.shaders.empty() && fake.programs.empty());

    fake.shaderCreateFailureAt = 0;
    fake.programCreateFailure  = true;

    CParallelShaderCompiler noProgram(fake);
    noProgram.Initialize({true, false});
    auto noProgramTicket = noProgram.Request(Source(), ShaderPriority::Critical, finalize);
    assert(0 == noProgram.Require(noProgramTicket));
    assert(fake.shaders.empty() && fake.programs.empty());

    fake.programCreateFailure = false;

    //An advertised ARB flavor without its entry point can still use valid KHR.
    CFakeShaderGLOperations khr;
    khr.arbEntry = false;

    CParallelShaderCompiler khrCompiler(khr);
    khrCompiler.Initialize({true, true});
    assert(ShaderCompileBackend::KHR == khrCompiler.Statistics().backend);
    assert(1 == khr.khrCalls && 0 == khr.arbCalls);

    //Lost contexts discard handles without invoking GL against another context.
    CFakeShaderGLOperations lost;
    CParallelShaderCompiler lostCompiler(lost);
    lostCompiler.Initialize({true, false});
    auto lostTicket = lostCompiler.Request(Source(), ShaderPriority::Prewarm, {});
    lostCompiler.Pump(1, 0, 1);
    lost.current = false;
    lostCompiler.Reset(true);
    assert(2 == lost.shaders.size() && 1 == lost.programs.size());
    assert(3 == lostCompiler.Statistics().discardedObjects);
    assert(ShaderJobState::Cancelled == lostCompiler.State(lostTicket));

    //Model the driver's context destruction separately from application cleanup.
    lost.shaders.clear();
    lost.programs.clear();
    lost.current     = true;
    auto replacement = lostCompiler.Request(Source(), ShaderPriority::Critical, {});
    assert(replacement.contextGeneration != lostTicket.contextGeneration);
    assert(lostCompiler.Require(replacement));
    lostCompiler.Reset(true);

    //Continuous demands leave completed critical entries, but cannot starve prewarm.
    CFakeShaderGLOperations fair;
    CParallelShaderCompiler fairCompiler(fair);
    fairCompiler.Initialize({true, false});
    auto prewarm = fairCompiler.Request(Source(), ShaderPriority::Prewarm, {});

    for (unsigned i = 0; i < 20; ++i)
    {
        auto demand = fairCompiler.Request(Source(i + 1), ShaderPriority::Critical, {});
        assert(fairCompiler.Require(demand));
    }

    for (unsigned i = 0; i < 4; ++i)
        fairCompiler.Pump(1, 0, 1);

    assert(ShaderJobState::Linking == fairCompiler.State(prewarm));
    fairCompiler.Reset(true);
    assert(fair.shaders.empty() && fair.programs.empty());

    std::puts("production scheduler: passed");
}
