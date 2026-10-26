#include "gl_shader_parallel.h"
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <atomic>

namespace
{
std::atomic<uint64_t> s_ContextGeneration{};
struct BusyScope
{
    bool& busy;
    explicit BusyScope(bool& value) : busy(value) { busy = true; }
    ~BusyScope() { busy = false; }
};

size_t SourceKey(const ShaderProgramSource& source)
{
    size_t key    = std::hash<uint64_t>{}(source.state);
    auto   append = [&](const std::string& text) {
        // Hash only selects a bucket; Request compares the full owned sources.
        key ^= std::hash<std::string>{}(text) + (key << 6) + (key >> 2);
    };
    append(source.category);
    append(std::to_string(source.state));
    for (const auto& stage : source.stages)
    {
        append(std::to_string(stage.type));
        append(stage.path);
        append(stage.source);
        append(stage.defines);
    }
    return key;
}

std::string ReadLog(GLuint object, bool shader)
{
    GLint length = 0;
    if (shader) glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    else
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) return "(empty info log)";
    std::string result(static_cast<size_t>(length), '\0');
    GLsizei     written = 0;
    if (shader) glGetShaderInfoLog(object, length, &written, result.data());
    else
        glGetProgramInfoLog(object, length, &written, result.data());
    result.resize(static_cast<size_t>(written));
    return result;
}
} // namespace

ShaderGLOperations GL_MakeShaderOperations()
{
    ShaderGLOperations gl;
    const auto         context = wglGetCurrentContext();
    const auto         thread  = GetCurrentThreadId();
    gl.current                 = [context, thread] {
        return context && context == wglGetCurrentContext() && thread == GetCurrentThreadId();
    };
    gl.now = [] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    gl.createShader = [](GLenum type) {
        return glCreateShader(type);
    };
    gl.shaderSource = [](GLuint shader, const char* source) {
        glShaderSource(shader, 1, &source, nullptr);
    };
    gl.compileShader = [](GLuint shader) {
        glCompileShader(shader);
    };
    gl.createProgram = [] {
        return glCreateProgram();
    };
    gl.attachShader = [](GLuint program, GLuint shader) {
        glAttachShader(program, shader);
    };
    gl.detachShader = [](GLuint program, GLuint shader) {
        glDetachShader(program, shader);
    };
    gl.linkProgram = [](GLuint program) {
        glLinkProgram(program);
    };
    gl.shaderParameter = [](GLuint shader, GLenum name) {
        GLint value = 0;
        glGetShaderiv(shader, name, &value);
        return value;
    };
    gl.programParameter = [](GLuint program, GLenum name) {
        GLint value = 0;
        glGetProgramiv(program, name, &value);
        return value;
    };
    gl.shaderLog = [](GLuint shader) {
        return ReadLog(shader, true);
    };
    gl.programLog = [](GLuint program) {
        return ReadLog(program, false);
    };
    gl.deleteShader = [](GLuint shader) {
        glDeleteShader(shader);
    };
    gl.deleteProgram = [](GLuint program) {
        glDeleteProgram(program);
    };
    if (glMaxShaderCompilerThreadsARB) gl.arbHint = [](GLuint hint) {
        glMaxShaderCompilerThreadsARB(hint);
    };
    if (glMaxShaderCompilerThreadsKHR) gl.khrHint = [](GLuint hint) {
        glMaxShaderCompilerThreadsKHR(hint);
    };
    gl.readHint = [] {
        GLint hint = 0;
        glGetIntegerv(GL_MAX_SHADER_COMPILER_THREADS_ARB, &hint);
        return static_cast<GLuint>(hint);
    };
    return gl;
}

ShaderCompileCapabilities GL_DetectShaderCompileCapabilities()
{
    return {GLEW_ARB_parallel_shader_compile != GL_FALSE, GLEW_KHR_parallel_shader_compile != GL_FALSE};
}

GLuint GL_ShaderCompilerThreadHint(double value)
{
    if (!std::isfinite(value) || value < 0 || value >= std::numeric_limits<GLuint>::max()) return ~0u;
    return static_cast<GLuint>(value);
}

CParallelShaderCompiler::CParallelShaderCompiler(ShaderGLOperations operations) : m_GL(std::move(operations)) {}
CParallelShaderCompiler::~CParallelShaderCompiler() { Reset(HasCurrentContext()); }
bool CParallelShaderCompiler::HasCurrentContext() const { return m_GL.current && m_GL.current(); }

void CParallelShaderCompiler::Initialize(ShaderCompileCapabilities capabilities)
{
    Reset(HasCurrentContext());
    m_Capabilities = capabilities;
    ApplyConfig();
}

void CParallelShaderCompiler::Configure(ShaderCompileConfig config)
{
    m_Config             = config;
    m_Config.maxInflight = std::clamp(m_Config.maxInflight, 1u, 256u);
    ApplyConfig();
}

void CParallelShaderCompiler::ApplyConfig()
{
    auto backend = ShaderCompileBackend::Sync;
    if (m_Config.enabled)
    {
        if (m_Capabilities.arb && m_GL.arbHint) backend = ShaderCompileBackend::ARB;
        else if (m_Capabilities.khr && m_GL.khrHint)
            backend = ShaderCompileBackend::KHR;
    }
    const GLuint hint = GL_ShaderCompilerThreadHint(m_Config.threads);
    // Preserve the matching extension for outstanding async jobs when disabled.
    auto hintBackend = backend;
    if (hintBackend == ShaderCompileBackend::Sync)
    {
        if (m_Capabilities.arb && m_GL.arbHint) hintBackend = ShaderCompileBackend::ARB;
        else if (m_Capabilities.khr && m_GL.khrHint)
            hintBackend = ShaderCompileBackend::KHR;
    }
    const GLuint appliedHint = m_Config.enabled ? hint : 0;
    if (HasCurrentContext() && hintBackend != ShaderCompileBackend::Sync && (!m_HintApplied || appliedHint != m_Stats.requestedHint))
    {
        if (hintBackend == ShaderCompileBackend::ARB) m_GL.arbHint(appliedHint);
        else
            m_GL.khrHint(appliedHint);
        m_Stats.requestedHint = appliedHint;
        m_Stats.reportedHint  = m_GL.readHint();
        m_HintApplied         = true;
    }
    m_Stats.backend = backend;
}

ShaderProgramTicket CParallelShaderCompiler::Request(ShaderProgramSource source, ShaderPriority priority, Finalizer finalize)
{
    ++m_Stats.requests;
    auto key          = SourceKey(source);
    auto [begin, end] = m_Keys.equal_range(key);
    for (auto found = begin; found != end; ++found)
    {
        if (m_Jobs.at(found->second).source != source) continue;
        ++m_Stats.cacheHits;
        auto& job = m_Jobs.at(found->second);
        if (priority == ShaderPriority::Critical && job.priority != priority && job.state == ShaderJobState::Queued)
        {
            job.priority = priority;
            m_Critical.push_back(found->second);
        }
        return {found->second, m_Generation};
    }
    const auto id = ++m_NextId;
    Job        job;
    job.source   = std::move(source);
    job.finalize = std::move(finalize);
    job.priority = priority;
    m_Jobs.emplace(id, std::move(job));
    ++m_Queued;
    m_Keys.emplace(key, id);
    (priority == ShaderPriority::Critical ? m_Critical : m_Prewarm).push_back(id);
    if (priority == ShaderPriority::Prewarm && !m_PrewarmStart) m_PrewarmStart = m_GL.now();
    return {id, m_Generation};
}

CParallelShaderCompiler::Job* CParallelShaderCompiler::Find(ShaderProgramTicket ticket)
{
    if (ticket.contextGeneration != m_Generation) return nullptr;
    auto found = m_Jobs.find(ticket.id);
    return found == m_Jobs.end() ? nullptr : &found->second;
}
const CParallelShaderCompiler::Job* CParallelShaderCompiler::Find(ShaderProgramTicket ticket) const
{
    return const_cast<CParallelShaderCompiler*>(this)->Find(ticket);
}
ShaderJobState CParallelShaderCompiler::State(ShaderProgramTicket ticket) const
{
    auto job = Find(ticket);
    return job ? job->state : ShaderJobState::Cancelled;
}
GLuint CParallelShaderCompiler::TryGet(ShaderProgramTicket ticket) const
{
    auto job = Find(ticket);
    return job && job->state == ShaderJobState::Ready && HasCurrentContext() ? job->program : 0;
}
std::string CParallelShaderCompiler::Error(ShaderProgramTicket ticket) const
{
    auto job = Find(ticket);
    return job ? job->error : "Shader ticket belongs to an expired context generation";
}

void CParallelShaderCompiler::ReleaseStages(Job& job)
{
    for (auto& shader : job.shaders)
    {
        if (!shader) continue;
        if (job.program) m_GL.detachShader(job.program, shader);
        m_GL.deleteShader(shader);
        ++m_Stats.shadersDeleted;
        shader = 0;
    }
}

void CParallelShaderCompiler::Fail(Job& job, std::string error)
{
    job.error = job.source.category + " state=" + std::to_string(job.source.state) + ": " + error;
    for (const auto& stage : job.source.stages)
        if (!stage.source.empty()) job.error += "\n" + stage.path + "\n" + stage.defines;
    ReleaseStages(job);
    if (job.program)
    {
        m_GL.deleteProgram(job.program);
        ++m_Stats.programsDeleted;
    }
    job.program     = 0;
    job.finalize    = {};
    job.state       = ShaderJobState::Failed;
    job.completedAt = m_GL.now();
    ++m_Stats.failed;
    if (m_GL.reportError) m_GL.reportError(job.error);
}

void CParallelShaderCompiler::Submit(Job& job)
{
    --m_Queued;
    job.submittedAt  = m_GL.now();
    job.asynchronous = m_Stats.backend != ShaderCompileBackend::Sync;
    ++m_Stats.submitted;
    if (!job.asynchronous) ++m_Stats.syncFallbacks;
    try
    {
        // Never query a stage status here. Link handles the stage dependencies.
        for (size_t i = 0; i < job.shaders.size(); ++i)
        {
            const auto& stage = job.source.stages[i];
            if (stage.source.empty()) continue;
            job.shaders[i] = m_GL.createShader(stage.type);
            if (!job.shaders[i])
            {
                Fail(job, "Cannot create shader: " + stage.path);
                return;
            }
            ++m_Stats.shadersCreated;
            m_GL.shaderSource(job.shaders[i], stage.source.c_str());
            m_GL.compileShader(job.shaders[i]);
        }
        job.program = m_GL.createProgram();
        if (!job.program)
        {
            Fail(job, "Cannot create program");
            return;
        }
        ++m_Stats.programsCreated;
        for (auto shader : job.shaders)
            if (shader) m_GL.attachShader(job.program, shader);
        m_GL.linkProgram(job.program);
        job.state = ShaderJobState::Linking;
        ++m_Active;
        m_Stats.peakInflight = std::max<uint64_t>(m_Stats.peakInflight, m_Active);
        const double elapsed = m_GL.now() - job.submittedAt;
        m_Stats.submitMs += elapsed;
        m_Stats.peakSubmitMs = std::max(m_Stats.peakSubmitMs, elapsed);
        if (!job.asynchronous) Finish(job);
    }
    catch (const std::exception& error)
    {
        if (job.state == ShaderJobState::Linking) --m_Active;
        Fail(job, error.what());
    }
}

void CParallelShaderCompiler::Finish(Job& job)
{
    --m_Active;
    try
    {
        if (m_GL.programParameter(job.program, GL_LINK_STATUS) != GL_TRUE)
        {
            std::string error = "Program link failed: " + m_GL.programLog(job.program);
            for (size_t i = 0; i < job.shaders.size(); ++i)
            {
                if (!job.shaders[i]) continue;
                // Only inspect shader status/logs after program completion or an explicit demand barrier.
                error += "\n" + job.source.stages[i].path + " status=" + std::to_string(m_GL.shaderParameter(job.shaders[i], GL_COMPILE_STATUS)) + ": " + m_GL.shaderLog(job.shaders[i]);
            }
            Fail(job, std::move(error));
            return;
        }
        if (job.finalize) job.finalize(job.program);
        job.finalize = {};
        ReleaseStages(job);
        job.state       = ShaderJobState::Ready;
        job.completedAt = m_GL.now();
        ++m_Stats.ready;
    }
    catch (const std::exception& error)
    {
        Fail(job, error.what());
    }
}

void CParallelShaderCompiler::Pump(double budgetMs, unsigned maxPolls, unsigned maxSubmissions)
{
    if (m_Busy || !HasCurrentContext()) return;
    BusyScope scope(m_Busy);
    ApplyConfig();
    if (!m_Queued && !m_Active)
    {
        CompletePrewarmWave();
        return;
    }
    if (!std::isfinite(budgetMs)) budgetMs = 1;
    budgetMs         = std::clamp(budgetMs, 0.0, 10.0);
    const auto start = m_GL.now();
    // Submission may itself cost driver CPU time; the budget bounds additional work,
    // not the duration of an individual GL call. Sync prewarming is demand-only.
    const unsigned attempts = std::min(maxSubmissions, 256u);
    for (unsigned i = 0; i < attempts && m_Active < m_Config.maxInflight && m_Stats.backend != ShaderCompileBackend::Sync && m_GL.now() - start < budgetMs; ++i)
    {
        // Reserve one turn in four for prewarming, including when demand has
        // left completed entries in the critical queue. Carry turns across frames.
        const bool prewarmTurn = !m_Prewarm.empty() && (m_Critical.empty() || (++m_QueueTurn % 4) == 0);
        auto&      queue       = prewarmTurn ? m_Prewarm : m_Critical;
        if (queue.empty()) break;
        auto id = queue.front();
        queue.pop_front();
        auto& job = m_Jobs.at(id);
        if (job.state != ShaderJobState::Queued) continue;
        Submit(job);
        if (job.state == ShaderJobState::Linking) m_Inflight.push_back(id);
    }
    const auto   pollStart = m_GL.now();
    const size_t polls     = std::min<size_t>({maxPolls, m_Inflight.size(), 1024u});
    for (size_t i = 0; i < polls && m_GL.now() - start < budgetMs; ++i)
    {
        auto id = m_Inflight.front();
        m_Inflight.pop_front();
        auto& job = m_Jobs.at(id);
        if (job.state != ShaderJobState::Linking) continue;
        ++m_Stats.polls;
        if (m_GL.programParameter(job.program, GL_COMPLETION_STATUS_ARB) == GL_TRUE) Finish(job);
        else
            m_Inflight.push_back(id);
    }
    m_Stats.pollMs += m_GL.now() - pollStart;
    m_Stats.peakPumpMs = std::max(m_Stats.peakPumpMs, m_GL.now() - start);
    CompletePrewarmWave();
}

void CParallelShaderCompiler::CompletePrewarmWave()
{
    if (m_PrewarmStart && m_Queued == 0 && m_Active == 0)
    {
        m_Stats.prewarmMs += m_GL.now() - m_PrewarmStart;
        m_PrewarmStart = 0;
    }
}

GLuint CParallelShaderCompiler::Require(ShaderProgramTicket ticket, bool startup)
{
    if (m_Busy || !HasCurrentContext()) return 0;
    auto job = Find(ticket);
    if (!job || job->state == ShaderJobState::Failed) return 0;
    if (job->state == ShaderJobState::Ready) return job->program;
    BusyScope scope(m_Busy);
    ApplyConfig();
    const auto start = m_GL.now();
    if (startup) ++m_Stats.startupBarriers;
    else
        ++m_Stats.demandStalls;
    if (job->state == ShaderJobState::Queued)
    {
        // A demand may borrow one slot rather than wait for an unrelated prewarm.
        Submit(*job);
    }
    // One blocking link query at the explicit demand point; no spin/flush loop.
    if (job->state == ShaderJobState::Linking) Finish(*job);
    if (startup) m_Stats.startupMs += m_GL.now() - start;
    else
        m_Stats.demandMs += m_GL.now() - start;
    CompletePrewarmWave();
    return job->state == ShaderJobState::Ready ? job->program : 0;
}

void CParallelShaderCompiler::Reset(bool deleteObjects)
{
    if (m_Busy) throw std::logic_error("Cannot reset shader compiler from a finalizer");
    deleteObjects = deleteObjects && HasCurrentContext();
    for (auto& [id, job] : m_Jobs)
    {
        if (deleteObjects)
        {
            ReleaseStages(job);
            if (job.program)
            {
                m_GL.deleteProgram(job.program);
                ++m_Stats.programsDeleted;
            }
        }
        else
        {
            m_Stats.discardedObjects += job.program ? 1 : 0;
            for (auto shader : job.shaders) m_Stats.discardedObjects += shader ? 1 : 0;
        }
        if (job.state == ShaderJobState::Queued || job.state == ShaderJobState::Linking) ++m_Stats.cancelled;
        job.state = ShaderJobState::Cancelled;
    }
    m_Jobs.clear();
    m_Keys.clear();
    m_Critical.clear();
    m_Prewarm.clear();
    m_Inflight.clear();
    m_Generation   = ++s_ContextGeneration;
    m_Active       = 0;
    m_Queued       = 0;
    m_PrewarmStart = 0;
    m_HintApplied  = false;
}

ShaderCompileStatistics CParallelShaderCompiler::Statistics() const
{
    auto result     = m_Stats;
    result.inflight = m_Active;
    result.queued   = m_Queued;
    return result;
}
