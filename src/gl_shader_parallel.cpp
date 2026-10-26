#include "gl_shader_parallel.h"
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
//One turn out of this many is reserved for prewarming.
constexpr uint64_t SHADER_PREWARM_TURN_INTERVAL = 4;

//Used when the requested pump budget is not a finite number.
constexpr double SHADER_PUMP_DEFAULT_BUDGET_MS = 1.0;

std::atomic<uint64_t> s_ContextGeneration{};

class CBusyScope
{
public:
    explicit CBusyScope(bool& busy) : m_Busy(busy)
    {
        m_Busy = true;
    }

    ~CBusyScope()
    {
        m_Busy = false;
    }

private:
    bool& m_Busy;
};

size_t SourceKey(const CShaderProgramSource& source)
{
    size_t key    = std::hash<uint64_t>{}(source.state);
    auto   append = [&](const std::string& text) {
        //Hash only selects a bucket; Request compares the full owned sources.
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

std::string ReadInfoLog(GLuint object, bool shader)
{
    GLint length = 0;

    if (shader)
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &length);
    else
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &length);

    if (length <= 1)
        return "(empty info log)";

    std::string result(static_cast<size_t>(length), '\0');
    GLsizei     written = 0;

    if (shader)
        glGetShaderInfoLog(object, length, &written, result.data());
    else
        glGetProgramInfoLog(object, length, &written, result.data());

    result.resize(static_cast<size_t>(written));

    return result;
}
} // namespace

CShaderGLOperations::CShaderGLOperations(void (*reportError)(const std::string& error)) : m_Context(wglGetCurrentContext()), m_Thread(GetCurrentThreadId()), m_ReportError(reportError)
{
}

bool CShaderGLOperations::IsContextCurrent() const
{
    return m_Context && m_Context == wglGetCurrentContext() && m_Thread == GetCurrentThreadId();
}

double CShaderGLOperations::GetTimeMs() const
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

GLuint CShaderGLOperations::CreateShader(GLenum type)
{
    return glCreateShader(type);
}

void CShaderGLOperations::ShaderSource(GLuint shader, const char* source)
{
    glShaderSource(shader, 1, &source, nullptr);
}

void CShaderGLOperations::CompileShader(GLuint shader)
{
    glCompileShader(shader);
}

void CShaderGLOperations::DeleteShader(GLuint shader)
{
    glDeleteShader(shader);
}

GLint CShaderGLOperations::GetShaderParameter(GLuint shader, GLenum name)
{
    GLint value = 0;
    glGetShaderiv(shader, name, &value);
    return value;
}

std::string CShaderGLOperations::GetShaderLog(GLuint shader)
{
    return ReadInfoLog(shader, true);
}

GLuint CShaderGLOperations::CreateProgram()
{
    return glCreateProgram();
}

void CShaderGLOperations::AttachShader(GLuint program, GLuint shader)
{
    glAttachShader(program, shader);
}

void CShaderGLOperations::DetachShader(GLuint program, GLuint shader)
{
    glDetachShader(program, shader);
}

void CShaderGLOperations::LinkProgram(GLuint program)
{
    glLinkProgram(program);
}

void CShaderGLOperations::DeleteProgram(GLuint program)
{
    glDeleteProgram(program);
}

GLint CShaderGLOperations::GetProgramParameter(GLuint program, GLenum name)
{
    GLint value = 0;
    glGetProgramiv(program, name, &value);
    return value;
}

std::string CShaderGLOperations::GetProgramLog(GLuint program)
{
    return ReadInfoLog(program, false);
}

bool CShaderGLOperations::HasThreadHint(ShaderCompileBackend backend) const
{
    switch (backend)
    {
        case ShaderCompileBackend::ARB:
            return glMaxShaderCompilerThreadsARB != nullptr;
        case ShaderCompileBackend::KHR:
            return glMaxShaderCompilerThreadsKHR != nullptr;
        default:
            return false;
    }
}

void CShaderGLOperations::SetThreadHint(ShaderCompileBackend backend, GLuint hint)
{
    if (backend == ShaderCompileBackend::ARB)
        glMaxShaderCompilerThreadsARB(hint);
    else if (backend == ShaderCompileBackend::KHR)
        glMaxShaderCompilerThreadsKHR(hint);
}

GLuint CShaderGLOperations::GetThreadHint()
{
    GLint hint = 0;
    glGetIntegerv(GL_MAX_SHADER_COMPILER_THREADS_ARB, &hint);
    return static_cast<GLuint>(hint);
}

void CShaderGLOperations::ReportError(const std::string& error)
{
    if (m_ReportError)
        m_ReportError(error);
}

CShaderCompileCapabilities GL_DetectShaderCompileCapabilities(void)
{
    CShaderCompileCapabilities capabilities;
    capabilities.arb = GLEW_ARB_parallel_shader_compile != GL_FALSE;
    capabilities.khr = GLEW_KHR_parallel_shader_compile != GL_FALSE;
    return capabilities;
}

GLuint GL_ShaderCompilerThreadHint(double value)
{
    if (!std::isfinite(value) || value < 0 || value >= std::numeric_limits<GLuint>::max())
        return SHADER_COMPILER_THREADS_MAXIMUM;

    return static_cast<GLuint>(value);
}

CParallelShaderCompiler::CParallelShaderCompiler(IShaderGLOperations& operations) : m_GL(operations)
{
}

CParallelShaderCompiler::~CParallelShaderCompiler()
{
    Reset(HasCurrentContext());
}

bool CParallelShaderCompiler::HasCurrentContext() const
{
    return m_GL.IsContextCurrent();
}

void CParallelShaderCompiler::Initialize(CShaderCompileCapabilities capabilities)
{
    Reset(HasCurrentContext());
    m_Capabilities = capabilities;
    ApplyConfig();
}

void CParallelShaderCompiler::Configure(CShaderCompileConfig config)
{
    m_Config             = config;
    m_Config.maxInflight = std::clamp(m_Config.maxInflight, 1u, SHADER_MAX_INFLIGHT_LIMIT);
    ApplyConfig();
}

ShaderCompileBackend CParallelShaderCompiler::SelectAsyncBackend() const
{
    if (m_Capabilities.arb && m_GL.HasThreadHint(ShaderCompileBackend::ARB))
        return ShaderCompileBackend::ARB;

    if (m_Capabilities.khr && m_GL.HasThreadHint(ShaderCompileBackend::KHR))
        return ShaderCompileBackend::KHR;

    return ShaderCompileBackend::Sync;
}

void CParallelShaderCompiler::ApplyConfig()
{
    //Preserve the matching extension for outstanding async jobs when disabled.
    const auto   hintBackend = SelectAsyncBackend();
    const auto   backend     = m_Config.enabled ? hintBackend : ShaderCompileBackend::Sync;
    const GLuint appliedHint = m_Config.enabled ? GL_ShaderCompilerThreadHint(m_Config.threads) : 0;

    if (HasCurrentContext() && hintBackend != ShaderCompileBackend::Sync && (!m_HintApplied || appliedHint != m_Stats.requestedHint))
    {
        m_GL.SetThreadHint(hintBackend, appliedHint);
        m_Stats.requestedHint = appliedHint;
        m_Stats.reportedHint  = m_GL.GetThreadHint();
        m_HintApplied         = true;
    }

    m_Stats.backend = backend;
}

CShaderProgramTicket CParallelShaderCompiler::Request(CShaderProgramSource source, ShaderPriority priority, Finalizer finalize)
{
    ++m_Stats.requests;

    auto key          = SourceKey(source);
    auto [begin, end] = m_Keys.equal_range(key);

    for (auto found = begin; found != end; ++found)
    {
        auto& job = m_Jobs.at(found->second);

        if (job.source != source)
            continue;

        ++m_Stats.cacheHits;

        if (priority == ShaderPriority::Critical && job.priority != priority && job.state == ShaderJobState::Queued)
        {
            job.priority = priority;
            m_Critical.push_back(found->second);
        }

        return {found->second, m_Generation};
    }

    const auto id = ++m_NextId;

    CJob job;
    job.source   = std::move(source);
    job.finalize = std::move(finalize);
    job.priority = priority;
    m_Jobs.emplace(id, std::move(job));
    m_Keys.emplace(key, id);
    ++m_Queued;

    if (priority == ShaderPriority::Critical)
    {
        m_Critical.push_back(id);
    }
    else
    {
        m_Prewarm.push_back(id);

        if (!m_PrewarmStart)
            m_PrewarmStart = m_GL.GetTimeMs();
    }

    return {id, m_Generation};
}

CParallelShaderCompiler::CJob* CParallelShaderCompiler::Find(CShaderProgramTicket ticket)
{
    if (ticket.contextGeneration != m_Generation)
        return nullptr;

    auto found = m_Jobs.find(ticket.id);

    return found == m_Jobs.end() ? nullptr : &found->second;
}

const CParallelShaderCompiler::CJob* CParallelShaderCompiler::Find(CShaderProgramTicket ticket) const
{
    return const_cast<CParallelShaderCompiler*>(this)->Find(ticket);
}

ShaderJobState CParallelShaderCompiler::State(CShaderProgramTicket ticket) const
{
    auto job = Find(ticket);

    return job ? job->state : ShaderJobState::Cancelled;
}

GLuint CParallelShaderCompiler::TryGet(CShaderProgramTicket ticket) const
{
    auto job = Find(ticket);

    if (!job || job->state != ShaderJobState::Ready || !HasCurrentContext())
        return 0;

    return job->program;
}

std::string CParallelShaderCompiler::Error(CShaderProgramTicket ticket) const
{
    auto job = Find(ticket);

    return job ? job->error : "Shader ticket belongs to an expired context generation";
}

void CParallelShaderCompiler::ReleaseStages(CJob& job)
{
    for (auto& shader : job.shaders)
    {
        if (!shader)
            continue;

        if (job.program)
            m_GL.DetachShader(job.program, shader);

        m_GL.DeleteShader(shader);
        ++m_Stats.shadersDeleted;
        shader = 0;
    }
}

void CParallelShaderCompiler::Fail(CJob& job, std::string error)
{
    job.error = job.source.category + " state=" + std::to_string(job.source.state) + ": " + error;

    for (const auto& stage : job.source.stages)
    {
        if (!stage.source.empty())
            job.error += "\n" + stage.path + "\n" + stage.defines;
    }

    ReleaseStages(job);

    if (job.program)
    {
        m_GL.DeleteProgram(job.program);
        ++m_Stats.programsDeleted;
    }

    job.program     = 0;
    job.finalize    = {};
    job.state       = ShaderJobState::Failed;
    job.completedAt = m_GL.GetTimeMs();
    ++m_Stats.failed;

    m_GL.ReportError(job.error);
}

void CParallelShaderCompiler::Submit(CJob& job)
{
    --m_Queued;
    job.submittedAt  = m_GL.GetTimeMs();
    job.asynchronous = m_Stats.backend != ShaderCompileBackend::Sync;
    ++m_Stats.submitted;

    if (!job.asynchronous)
        ++m_Stats.syncFallbacks;

    try
    {
        //Never query a stage status here. Link handles the stage dependencies.
        for (size_t i = 0; i < job.shaders.size(); ++i)
        {
            const auto& stage = job.source.stages[i];

            if (stage.source.empty())
                continue;

            job.shaders[i] = m_GL.CreateShader(stage.type);

            if (!job.shaders[i])
            {
                Fail(job, "Cannot create shader: " + stage.path);
                return;
            }

            ++m_Stats.shadersCreated;
            m_GL.ShaderSource(job.shaders[i], stage.source.c_str());
            m_GL.CompileShader(job.shaders[i]);
        }

        job.program = m_GL.CreateProgram();

        if (!job.program)
        {
            Fail(job, "Cannot create program");
            return;
        }

        ++m_Stats.programsCreated;

        for (auto shader : job.shaders)
        {
            if (shader)
                m_GL.AttachShader(job.program, shader);
        }

        m_GL.LinkProgram(job.program);
        job.state = ShaderJobState::Linking;
        ++m_Active;
        m_Stats.peakInflight = std::max<uint64_t>(m_Stats.peakInflight, m_Active);

        const double elapsed = m_GL.GetTimeMs() - job.submittedAt;
        m_Stats.submitMs += elapsed;
        m_Stats.peakSubmitMs = std::max(m_Stats.peakSubmitMs, elapsed);

        if (!job.asynchronous)
            Finish(job);
    }
    catch (const std::exception& error)
    {
        if (job.state == ShaderJobState::Linking)
            --m_Active;

        Fail(job, error.what());
    }
}

void CParallelShaderCompiler::Finish(CJob& job)
{
    --m_Active;

    try
    {
        if (m_GL.GetProgramParameter(job.program, GL_LINK_STATUS) != GL_TRUE)
        {
            std::string error = "Program link failed: " + m_GL.GetProgramLog(job.program);

            for (size_t i = 0; i < job.shaders.size(); ++i)
            {
                if (!job.shaders[i])
                    continue;

                //Only inspect shader status/logs after program completion or an explicit demand barrier.
                const auto status = m_GL.GetShaderParameter(job.shaders[i], GL_COMPILE_STATUS);
                error += "\n" + job.source.stages[i].path + " status=" + std::to_string(status) + ": " + m_GL.GetShaderLog(job.shaders[i]);
            }

            Fail(job, std::move(error));
            return;
        }

        if (job.finalize)
            job.finalize(job.program);

        job.finalize = {};
        ReleaseStages(job);
        job.state       = ShaderJobState::Ready;
        job.completedAt = m_GL.GetTimeMs();
        ++m_Stats.ready;
    }
    catch (const std::exception& error)
    {
        Fail(job, error.what());
    }
}

void CParallelShaderCompiler::Pump(double budgetMs, unsigned maxPolls, unsigned maxSubmissions)
{
    if (m_Busy || !HasCurrentContext())
        return;

    CBusyScope scope(m_Busy);
    ApplyConfig();

    if (!m_Queued && !m_Active)
    {
        CompletePrewarmWave();
        return;
    }

    if (!std::isfinite(budgetMs))
        budgetMs = SHADER_PUMP_DEFAULT_BUDGET_MS;

    budgetMs = std::clamp(budgetMs, 0.0, SHADER_PUMP_MAX_BUDGET_MS);

    const auto start = m_GL.GetTimeMs();

    //Submission may itself cost driver CPU time; the budget bounds additional work,
    //not the duration of an individual GL call. Sync prewarming is demand-only.
    const unsigned attempts = std::min(maxSubmissions, SHADER_PUMP_MAX_SUBMISSIONS);

    for (unsigned i = 0; i < attempts && m_Active < m_Config.maxInflight && m_Stats.backend != ShaderCompileBackend::Sync && m_GL.GetTimeMs() - start < budgetMs; ++i)
    {
        //Reserve one turn per interval for prewarming, including when demand has
        //left completed entries in the critical queue. Carry turns across frames.
        const bool prewarmTurn = !m_Prewarm.empty() && (m_Critical.empty() || (++m_QueueTurn % SHADER_PREWARM_TURN_INTERVAL) == 0);
        auto&      queue       = prewarmTurn ? m_Prewarm : m_Critical;

        if (queue.empty())
            break;

        auto id = queue.front();
        queue.pop_front();

        auto& job = m_Jobs.at(id);

        if (job.state != ShaderJobState::Queued)
            continue;

        Submit(job);

        if (job.state == ShaderJobState::Linking)
            m_Inflight.push_back(id);
    }

    const auto   pollStart = m_GL.GetTimeMs();
    const size_t polls     = std::min<size_t>({maxPolls, m_Inflight.size(), SHADER_PUMP_MAX_POLLS});

    for (size_t i = 0; i < polls && m_GL.GetTimeMs() - start < budgetMs; ++i)
    {
        auto id = m_Inflight.front();
        m_Inflight.pop_front();

        auto& job = m_Jobs.at(id);

        if (job.state != ShaderJobState::Linking)
            continue;

        ++m_Stats.polls;

        if (m_GL.GetProgramParameter(job.program, GL_COMPLETION_STATUS_ARB) == GL_TRUE)
            Finish(job);
        else
            m_Inflight.push_back(id);
    }

    m_Stats.pollMs += m_GL.GetTimeMs() - pollStart;
    m_Stats.peakPumpMs = std::max(m_Stats.peakPumpMs, m_GL.GetTimeMs() - start);

    CompletePrewarmWave();
}

void CParallelShaderCompiler::CompletePrewarmWave()
{
    if (m_PrewarmStart && m_Queued == 0 && m_Active == 0)
    {
        m_Stats.prewarmMs += m_GL.GetTimeMs() - m_PrewarmStart;
        m_PrewarmStart = 0;
    }
}

GLuint CParallelShaderCompiler::Require(CShaderProgramTicket ticket, bool startup)
{
    if (m_Busy || !HasCurrentContext())
        return 0;

    auto job = Find(ticket);

    if (!job || job->state == ShaderJobState::Failed)
        return 0;

    if (job->state == ShaderJobState::Ready)
        return job->program;

    CBusyScope scope(m_Busy);
    ApplyConfig();

    const auto start = m_GL.GetTimeMs();

    if (startup)
        ++m_Stats.startupBarriers;
    else
        ++m_Stats.demandStalls;

    //A demand may borrow one slot rather than wait for an unrelated prewarm.
    if (job->state == ShaderJobState::Queued)
        Submit(*job);

    //One blocking link query at the explicit demand point; no spin/flush loop.
    if (job->state == ShaderJobState::Linking)
        Finish(*job);

    const double elapsed = m_GL.GetTimeMs() - start;

    if (startup)
        m_Stats.startupMs += elapsed;
    else
        m_Stats.demandMs += elapsed;

    CompletePrewarmWave();

    return job->state == ShaderJobState::Ready ? job->program : 0;
}

void CParallelShaderCompiler::Reset(bool deleteObjects)
{
    if (m_Busy)
        throw std::logic_error("Cannot reset shader compiler from a finalizer");

    deleteObjects = deleteObjects && HasCurrentContext();

    for (auto& [id, job] : m_Jobs)
    {
        if (deleteObjects)
        {
            ReleaseStages(job);

            if (job.program)
            {
                m_GL.DeleteProgram(job.program);
                ++m_Stats.programsDeleted;
            }
        }
        else
        {
            m_Stats.discardedObjects += job.program ? 1 : 0;

            for (auto shader : job.shaders)
                m_Stats.discardedObjects += shader ? 1 : 0;
        }

        if (job.state == ShaderJobState::Queued || job.state == ShaderJobState::Linking)
            ++m_Stats.cancelled;

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

CShaderCompileStatistics CParallelShaderCompiler::Statistics() const
{
    auto result     = m_Stats;
    result.inflight = m_Active;
    result.queued   = m_Queued;
    return result;
}
