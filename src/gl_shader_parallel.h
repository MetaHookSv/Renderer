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

//Linking includes submission of all stages followed immediately by glLinkProgram.
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

//Hint value requesting the implementation-specific maximum of compiler threads.
static constexpr GLuint SHADER_COMPILER_THREADS_MAXIMUM = ~0u;

//Upper bounds of a single Pump and of concurrently linking programs.
static constexpr double   SHADER_PUMP_MAX_BUDGET_MS   = 10.0;
static constexpr unsigned SHADER_PUMP_MAX_SUBMISSIONS = 256;
static constexpr unsigned SHADER_PUMP_MAX_POLLS       = 1024;
static constexpr unsigned SHADER_MAX_INFLIGHT_LIMIT   = 256;

class CShaderProgramTicket
{
public:
    uint64_t id{};
    uint64_t contextGeneration{};

    bool operator==(const CShaderProgramTicket&) const = default;
};

class CShaderStageSource
{
public:
    GLenum      type{};
    std::string path;
    std::string source;
    std::string defines;

    bool operator==(const CShaderStageSource&) const = default;
};

class CShaderProgramSource
{
public:
    std::string                       category;
    uint64_t                          state{};
    std::array<CShaderStageSource, 3> stages;

    bool operator==(const CShaderProgramSource&) const = default;
};

class CShaderCompileConfig
{
public:
    bool     enabled{true};
    double   threads{-1};
    unsigned maxInflight{16};
};

class CShaderCompileStatistics
{
public:
    uint64_t requests{};
    uint64_t cacheHits{};
    uint64_t submitted{};
    uint64_t ready{};
    uint64_t failed{};
    uint64_t cancelled{};
    uint64_t syncFallbacks{};
    uint64_t demandStalls{};
    uint64_t startupBarriers{};
    uint64_t polls{};
    uint64_t peakInflight{};

    uint64_t shadersCreated{};
    uint64_t shadersDeleted{};
    uint64_t programsCreated{};
    uint64_t programsDeleted{};
    uint64_t discardedObjects{};

    size_t queued{};
    size_t inflight{};

    double submitMs{};
    double peakSubmitMs{};
    double pollMs{};
    double peakPumpMs{};
    double demandMs{};
    double startupMs{};
    double prewarmMs{};

    GLuint               requestedHint{SHADER_COMPILER_THREADS_MAXIMUM};
    GLuint               reportedHint{SHADER_COMPILER_THREADS_MAXIMUM};
    ShaderCompileBackend backend{ShaderCompileBackend::Sync};
};

class CShaderCompileCapabilities
{
public:
    bool arb{};
    bool khr{};
};

/*
    Thin injectable GL boundary. Tests execute the production scheduler and finalizer.
*/
class IShaderGLOperations
{
public:
    virtual ~IShaderGLOperations() = default;

    virtual bool   IsContextCurrent() const = 0;
    virtual double GetTimeMs() const        = 0;

    virtual GLuint      CreateShader(GLenum type)                       = 0;
    virtual void        ShaderSource(GLuint shader, const char* source) = 0;
    virtual void        CompileShader(GLuint shader)                    = 0;
    virtual void        DeleteShader(GLuint shader)                     = 0;
    virtual GLint       GetShaderParameter(GLuint shader, GLenum name)  = 0;
    virtual std::string GetShaderLog(GLuint shader)                     = 0;

    virtual GLuint      CreateProgram()                                  = 0;
    virtual void        AttachShader(GLuint program, GLuint shader)      = 0;
    virtual void        DetachShader(GLuint program, GLuint shader)      = 0;
    virtual void        LinkProgram(GLuint program)                      = 0;
    virtual void        DeleteProgram(GLuint program)                    = 0;
    virtual GLint       GetProgramParameter(GLuint program, GLenum name) = 0;
    virtual std::string GetProgramLog(GLuint program)                    = 0;

    //Returns false when the extension entry point of the given backend is unavailable.
    virtual bool   HasThreadHint(ShaderCompileBackend backend) const        = 0;
    virtual void   SetThreadHint(ShaderCompileBackend backend, GLuint hint) = 0;
    virtual GLuint GetThreadHint()                                          = 0;

    virtual void ReportError(const std::string& error) = 0;
};

/*
    Production GL boundary bound to the GL context and thread that are current at creation.
*/
class CShaderGLOperations : public IShaderGLOperations
{
public:
    explicit CShaderGLOperations(void (*reportError)(const std::string& error));

    bool   IsContextCurrent() const override;
    double GetTimeMs() const override;

    GLuint      CreateShader(GLenum type) override;
    void        ShaderSource(GLuint shader, const char* source) override;
    void        CompileShader(GLuint shader) override;
    void        DeleteShader(GLuint shader) override;
    GLint       GetShaderParameter(GLuint shader, GLenum name) override;
    std::string GetShaderLog(GLuint shader) override;

    GLuint      CreateProgram() override;
    void        AttachShader(GLuint program, GLuint shader) override;
    void        DetachShader(GLuint program, GLuint shader) override;
    void        LinkProgram(GLuint program) override;
    void        DeleteProgram(GLuint program) override;
    GLint       GetProgramParameter(GLuint program, GLenum name) override;
    std::string GetProgramLog(GLuint program) override;

    bool   HasThreadHint(ShaderCompileBackend backend) const override;
    void   SetThreadHint(ShaderCompileBackend backend, GLuint hint) override;
    GLuint GetThreadHint() override;

    void ReportError(const std::string& error) override;

private:
    //HGLRC and DWORD, kept opaque so this header does not depend on <Windows.h>.
    void*         m_Context{};
    unsigned long m_Thread{};

    void (*m_ReportError)(const std::string& error){};
};

CShaderCompileCapabilities GL_DetectShaderCompileCapabilities(void);
GLuint                     GL_ShaderCompilerThreadHint(double value);

/*
    The operations are borrowed and must outlive the compiler.
*/
class CParallelShaderCompiler
{
public:
    using Finalizer = std::function<void(GLuint)>;

    explicit CParallelShaderCompiler(IShaderGLOperations& operations);
    ~CParallelShaderCompiler();

    CParallelShaderCompiler(const CParallelShaderCompiler&)            = delete;
    CParallelShaderCompiler& operator=(const CParallelShaderCompiler&) = delete;

    void                     Initialize(CShaderCompileCapabilities capabilities);
    void                     Configure(CShaderCompileConfig config);
    CShaderProgramTicket     Request(CShaderProgramSource source, ShaderPriority priority, Finalizer finalize);
    void                     Pump(double budgetMs, unsigned maxPolls, unsigned maxSubmissions);
    GLuint                   Require(CShaderProgramTicket ticket, bool startup = false);
    GLuint                   TryGet(CShaderProgramTicket ticket) const;
    ShaderJobState           State(CShaderProgramTicket ticket) const;
    std::string              Error(CShaderProgramTicket ticket) const;
    void                     Reset(bool deleteObjects);
    CShaderCompileStatistics Statistics() const;
    bool                     HasCurrentContext() const;

private:
    class CJob
    {
    public:
        CShaderProgramSource  source;
        Finalizer             finalize;
        ShaderJobState        state{ShaderJobState::Queued};
        ShaderPriority        priority{ShaderPriority::Prewarm};
        std::array<GLuint, 3> shaders{};
        GLuint                program{};
        bool                  asynchronous{};
        std::string           error;
        double                submittedAt{};
        double                completedAt{};
    };

    CJob*                Find(CShaderProgramTicket ticket);
    const CJob*          Find(CShaderProgramTicket ticket) const;
    ShaderCompileBackend SelectAsyncBackend() const;
    void                 Submit(CJob& job);
    void                 Finish(CJob& job);
    void                 Fail(CJob& job, std::string error);
    void                 ReleaseStages(CJob& job);
    void                 ApplyConfig();
    void                 CompletePrewarmWave();

    IShaderGLOperations&                      m_GL;
    CShaderCompileCapabilities                m_Capabilities;
    CShaderCompileConfig                      m_Config;
    CShaderCompileStatistics                  m_Stats;
    uint64_t                                  m_Generation{};
    uint64_t                                  m_NextId{};
    uint64_t                                  m_QueueTurn{};
    std::map<uint64_t, CJob>                  m_Jobs;
    std::unordered_multimap<size_t, uint64_t> m_Keys;
    std::deque<uint64_t>                      m_Critical;
    std::deque<uint64_t>                      m_Prewarm;
    std::deque<uint64_t>                      m_Inflight;
    size_t                                    m_Active{};
    size_t                                    m_Queued{};
    bool                                      m_Busy{};
    bool                                      m_HintApplied{};
    double                                    m_PrewarmStart{};
};
