#pragma once

#include <GL/glew.h>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>

enum class ShaderCompileBackend
{
    Sync,
    ARB,
    KHR
};
// Linking includes submission of all stages followed immediately by glLinkProgram.
enum class ShaderJobState
{
    Queued,
    Linking,
    Ready,
    Failed,
    Cancelled
};
enum class ShaderPriority
{
    Critical,
    Prewarm
};

struct ShaderProgramTicket
{
    uint64_t id{}, contextGeneration{};
    bool     operator==(const ShaderProgramTicket&) const = default;
};

struct ShaderStageSource
{
    GLenum      type{};
    std::string path, source, defines;
    bool        operator==(const ShaderStageSource&) const = default;
};

struct ShaderProgramSource
{
    std::string                      category;
    uint64_t                         state{};
    std::array<ShaderStageSource, 3> stages;
    bool                             operator==(const ShaderProgramSource&) const = default;
};

struct ShaderCompileConfig
{
    bool     enabled{true};
    double   threads{-1};
    unsigned maxInflight{16};
};

struct ShaderCompileStatistics
{
    uint64_t             requests{}, cacheHits{}, submitted{}, ready{}, failed{}, cancelled{}, syncFallbacks{}, demandStalls{}, startupBarriers{}, polls{}, peakInflight{};
    uint64_t             shadersCreated{}, shadersDeleted{}, programsCreated{}, programsDeleted{}, discardedObjects{};
    size_t               queued{}, inflight{};
    double               submitMs{}, peakSubmitMs{}, pollMs{}, peakPumpMs{}, demandMs{}, startupMs{}, prewarmMs{};
    GLuint               requestedHint{~0u}, reportedHint{~0u};
    ShaderCompileBackend backend{ShaderCompileBackend::Sync};
};

// Thin injectable GL boundary. Tests execute the production scheduler and finalizer.
struct ShaderGLOperations
{
    std::function<bool()>                    current;
    std::function<double()>                  now;
    std::function<GLuint(GLenum)>            createShader;
    std::function<void(GLuint, const char*)> shaderSource;
    std::function<void(GLuint)>              compileShader, linkProgram, deleteShader, deleteProgram;
    std::function<GLuint()>                  createProgram;
    std::function<void(GLuint, GLuint)>      attachShader, detachShader;
    std::function<GLint(GLuint, GLenum)>     shaderParameter, programParameter;
    std::function<std::string(GLuint)>       shaderLog, programLog;
    std::function<void(GLuint)>              arbHint, khrHint;
    std::function<GLuint()>                  readHint;
    std::function<void(const std::string&)>  reportError;
};

struct ShaderCompileCapabilities
{ bool arb{}, khr{}; };
ShaderGLOperations        GL_MakeShaderOperations();
ShaderCompileCapabilities GL_DetectShaderCompileCapabilities();
GLuint                    GL_ShaderCompilerThreadHint(double value);

class CParallelShaderCompiler
{
public:
    using Finalizer = std::function<void(GLuint)>;
    explicit CParallelShaderCompiler(ShaderGLOperations operations);
    ~CParallelShaderCompiler();
    CParallelShaderCompiler(const CParallelShaderCompiler&)            = delete;
    CParallelShaderCompiler& operator=(const CParallelShaderCompiler&) = delete;
    void                     Initialize(ShaderCompileCapabilities capabilities);
    void                     Configure(ShaderCompileConfig config);
    ShaderProgramTicket      Request(ShaderProgramSource source, ShaderPriority priority, Finalizer finalize);
    void                     Pump(double budgetMs, unsigned maxPolls, unsigned maxSubmissions);
    GLuint                   Require(ShaderProgramTicket ticket, bool startup = false);
    GLuint                   TryGet(ShaderProgramTicket ticket) const;
    ShaderJobState           State(ShaderProgramTicket ticket) const;
    std::string              Error(ShaderProgramTicket ticket) const;
    void                     Reset(bool deleteObjects);
    ShaderCompileStatistics  Statistics() const;
    bool                     HasCurrentContext() const;

private:
    struct Job
    {
        ShaderProgramSource   source;
        Finalizer             finalize;
        ShaderJobState        state{ShaderJobState::Queued};
        ShaderPriority        priority{ShaderPriority::Prewarm};
        std::array<GLuint, 3> shaders{};
        GLuint                program{};
        bool                  asynchronous{};
        std::string           error;
        double                submittedAt{}, completedAt{};
    };
    Job*                                      Find(ShaderProgramTicket ticket);
    const Job*                                Find(ShaderProgramTicket ticket) const;
    void                                      Submit(Job& job);
    void                                      Finish(Job& job);
    void                                      Fail(Job& job, std::string error);
    void                                      ReleaseStages(Job& job);
    void                                      ApplyConfig();
    void                                      CompletePrewarmWave();
    ShaderGLOperations                        m_GL;
    ShaderCompileCapabilities                 m_Capabilities;
    ShaderCompileConfig                       m_Config;
    ShaderCompileStatistics                   m_Stats;
    uint64_t                                  m_Generation{}, m_NextId{}, m_QueueTurn{};
    std::map<uint64_t, Job>                   m_Jobs;
    std::unordered_multimap<size_t, uint64_t> m_Keys;
    std::deque<uint64_t>                      m_Critical, m_Prewarm, m_Inflight;
    size_t                                    m_Active{}, m_Queued{};
    bool                                      m_Busy{}, m_HintApplied{};
    double                                    m_PrewarmStart{};
};
