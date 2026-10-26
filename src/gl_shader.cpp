#include "gl_local.h"
#include "gl_shader.h"
#include "gl_shader_compile.h"
#include <string>
#include <sstream>
#include <regex>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <ScopeExit/ScopeExit.h>

cvar_t* r_shader_parallel_compile          = nullptr;
cvar_t* r_shader_parallel_threads          = nullptr;
cvar_t* r_shader_parallel_max_inflight     = nullptr;
cvar_t* r_shader_parallel_submit_per_frame = nullptr;
cvar_t* r_shader_parallel_poll_budget_ms   = nullptr;
cvar_t* r_shader_parallel_autosave         = nullptr;

class CShaderCompileCvar
{
public:
    cvar_t**    cvar;
    const char* name;
    const char* defaultValue;
};

static const CShaderCompileCvar s_ShaderCompileCvars[] = {
    {&r_shader_parallel_compile, "r_shader_parallel_compile", "1"},
    {&r_shader_parallel_threads, "r_shader_parallel_threads", "-1"},
    {&r_shader_parallel_max_inflight, "r_shader_parallel_max_inflight", "16"},
    {&r_shader_parallel_submit_per_frame, "r_shader_parallel_submit_per_frame", "8"},
    {&r_shader_parallel_poll_budget_ms, "r_shader_parallel_poll_budget_ms", "1.0"},
    {&r_shader_parallel_autosave, "r_shader_parallel_autosave", "1"},
};

//Polls per frame pump; bounded again by the scheduler.
static constexpr unsigned SHADER_PUMP_POLLS_PER_FRAME = 64;

//The operations must outlive the compiler that borrows them, so declare them first.
static std::unique_ptr<IShaderGLOperations>     s_ShaderOperations;
static std::unique_ptr<CParallelShaderCompiler> s_ShaderCompiler;

static void GL_ConfigureShaderCompiler(void);

void R_ClearShaderVariantTables(void)
{
    R_ClearWSurfProgramStates();
    R_ClearWaterProgramStates();
    R_ClearDLightProgramStates();
    R_ClearDFinalProgramStates();
    R_ClearStudioProgramStates();
    R_ClearSpriteProgramStates();
    R_ClearTriAPIProgramStates();
    R_ClearPortalProgramStates();
    R_ClearDrawTexturedRectProgramStates();
    R_ClearDrawFilledRectProgramStates();
}

class CCompileShaderContext
{
public:
    std::string vscode;
    std::string gscode;
    std::string fscode;
};

std::vector<GLuint> g_ShaderTable;
static HGLRC        s_SynchronousShaderContext;
static DWORD        s_SynchronousShaderThread;

static void R_ReportShaderCompileError(const std::string& error)
{
    gEngfuncs.Con_Printf("Shader prewarm failed: %s\n", error.c_str());
}

void GL_InitShaders(void)
{
    //Startup/core-context initialization and the engine GL_Init hook can run in
    //either order. Keep programs when both enter with the same live context.
    if (s_ShaderCompiler && s_ShaderCompiler->HasCurrentContext())
        return;

    //Recreate operations after GLEW initialization for each context generation.
    if (s_ShaderCompiler)
    {
        s_ShaderCompiler->Reset(false);
        s_ShaderCompiler.reset();
    }

    //A public synchronous compile may precede Renderer initialization.
    if (s_SynchronousShaderContext != wglGetCurrentContext())
        g_ShaderTable.clear();

    s_ShaderOperations = std::make_unique<CShaderGLOperations>(R_ReportShaderCompileError);
    s_ShaderCompiler   = std::make_unique<CParallelShaderCompiler>(*s_ShaderOperations);

    GL_ConfigureShaderCompiler();
    s_ShaderCompiler->Initialize(GL_DetectShaderCompileCapabilities());
    R_ClearShaderVariantTables();
}

void GL_FreeShaders(void)
{
    if (s_ShaderCompiler)
        s_ShaderCompiler->Reset(s_ShaderCompiler->HasCurrentContext());

    R_ClearShaderVariantTables();

    const bool synchronousContextCurrent = s_SynchronousShaderContext == wglGetCurrentContext() && s_SynchronousShaderThread == GetCurrentThreadId();

    for (size_t i = 0; i < g_ShaderTable.size(); ++i)
    {
        auto prog = g_ShaderTable[i];

        if (synchronousContextCurrent)
            glDeleteProgram(prog);
    }

    g_ShaderTable.clear();
    s_SynchronousShaderContext = nullptr;
    s_SynchronousShaderThread  = 0;
    s_ShaderCompiler.reset();
    s_ShaderOperations.reset();
}

static void R_DumpShaderSource(const char* code, const char* filename)
{
    if (developer && developer->value >= 255)
    {
        FILESYSTEM_ANY_CREATEDIR("logs");
        FILESYSTEM_ANY_CREATEDIR("logs/renderer");
        FILESYSTEM_ANY_CREATEDIR("logs/renderer/shader");

        char filepath[256] = {0};
        snprintf(filepath, sizeof(filepath), "logs\\%s", filename);

        auto FileHandle = FILESYSTEM_ANY_OPEN(filepath, "wb");
        if (FileHandle)
        {
            FILESYSTEM_ANY_WRITE(code, strlen(code), FileHandle);
            FILESYSTEM_ANY_CLOSE(FileHandle);
        }
    }
}

GLuint R_CompileShaderObject(int type, const char* code, const char* filename)
{
    R_DumpShaderSource(code, filename);
    std::string error;

    auto obj = GL_TryCompileShaderObject(type, code, error);

    if (!obj)
        Sys_Error("Shader %s compiled with error:\n%s", filename, error.c_str());

    return obj;
}

GLuint R_CompileShader(const CCompileShaderArgs* args, const CCompileShaderContext* context)
{
    GLuint shader_objects[32] = {};
    int    shader_object_used = 0;
    GLuint program            = 0;
    bool   published          = false;

    SCOPE_EXIT
    {
        for (int i = 0; i < shader_object_used; ++i)
        {
            if (program)
                glDetachShader(program, shader_objects[i]);

            glDeleteShader(shader_objects[i]);
        }

        if (program && !published)
            glDeleteProgram(program);
    };

#define APPEND_SHADER_STAGE(stage, GLStage)                                                                                   \
    if (context->stage##code.size() > 0)                                                                                      \
    {                                                                                                                         \
        shader_objects[shader_object_used] = R_CompileShaderObject(GLStage, context->stage##code.c_str(), args->stage##file); \
        if (!shader_objects[shader_object_used]) return 0;                                                                    \
        shader_object_used++;                                                                                                 \
    }

    APPEND_SHADER_STAGE(vs, GL_VERTEX_SHADER);
    APPEND_SHADER_STAGE(gs, GL_GEOMETRY_SHADER);
    APPEND_SHADER_STAGE(fs, GL_FRAGMENT_SHADER);

#undef APPEND_SHADER_STAGE

    program = glCreateProgram();

    if (!program)
    {
        Sys_Error("Cannot create shader program");
        return 0;
    }

    for (int i = 0; i < shader_object_used; ++i)
        glAttachShader(program, shader_objects[i]);
    glLinkProgram(program);

    GLint iStatus = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &iStatus);

    if (GL_FALSE == iStatus)
    {
        int  nInfoLength;
        char szCompilerLog[1024] = {0};
        glGetProgramInfoLog(program, sizeof(szCompilerLog), &nInfoLength, szCompilerLog);

        Sys_Error("Shader linked with error:\n%s\n", szCompilerLog);
        return 0;
    }

    if (s_SynchronousShaderContext != wglGetCurrentContext())
        g_ShaderTable.clear();

    s_SynchronousShaderContext = wglGetCurrentContext();
    s_SynchronousShaderThread  = GetCurrentThreadId();
    g_ShaderTable.emplace_back(program);
    published = true;

    gEngfuncs.Con_DPrintf("R_CompileShaderObject [%d] ...\n", program);

    return program;
}

void R_CompileShaderAppendInclude(std::string& str, const char* filename)
{
    static const std::regex pattern("#include[< \"]+([a-zA-Z_\\.]+)[> \"]");
    std::smatch             result;
    std::regex_search(str, result, pattern);

    std::string skipped;

    std::string::const_iterator searchStart(str.cbegin());

    while (std::regex_search(searchStart, str.cend(), result, pattern) && result.size() >= 2)
    {
        std::string prefix = result.prefix();
        std::string suffix = result.suffix();

        auto includeFileName = result[1].str();

        char slash = '/';

        std::string includePath = filename;
        for (size_t j = includePath.length() - 1; j > 0; --j)
        {
            if (includePath[j] == '\\' || includePath[j] == '/')
            {
                slash = includePath[j];
                includePath.resize(j);
                break;
            }
        }

        includePath += slash;
        includePath += includeFileName;

        auto pFile = (char*)gEngfuncs.COM_LoadFile(includePath.c_str(), 5, NULL);
        if (pFile)
        {
            std::string wbinding(pFile);

            gEngfuncs.COM_FreeFile(pFile);

            if (searchStart != str.cbegin())
            {
                str = skipped + prefix;
            }
            else
            {
                str = prefix;
            }
            str += wbinding;

            auto currentLength = str.length();

            str += suffix;

            skipped     = str.substr(0, currentLength);
            searchStart = str.cbegin() + currentLength;
            continue;
        }

        searchStart = result.suffix().first;
    }
}

void R_CompileShaderAppendDefine(std::string& str, const std::string& def)
{
    static const std::regex pattern("(#version [0-9a-z ]+)");
    std::smatch             result;
    std::regex_search(str, result, pattern);

    if (result.size() >= 1)
    {
        std::string prefix = result[0];
        std::string suffix = result.suffix();

        str = prefix;
        str += "\n\n";
        str += def;
        str += "\n\n";
        str += suffix;
    }
    else
    {
        std::string suffix = str;

        str = def;
        str += "\n\n";
        str += suffix;
    }
}

static bool R_PrepareShaderSources(const CCompileShaderArgs* args, CCompileShaderContext& context)
{
#define LOAD_SHADER_STAGE(stage, macro)                                                        \
    if (args->stage##file)                                                                     \
    {                                                                                          \
        auto stage##code = (char*)gEngfuncs.COM_LoadFile(args->stage##file, 5, 0);             \
        if (!stage##code)                                                                      \
        {                                                                                      \
            Sys_Error("GL_CompileShaderFileEx: \"%s\" not found!", args->stage##file);         \
            return false;                                                                      \
        }                                                                                      \
        SCOPE_EXIT { gEngfuncs.COM_FreeFile(stage##code); };                                   \
        gEngfuncs.Con_DPrintf("GL_CompileShaderFileEx: compiling %s...\n", args->stage##file); \
        context.stage##code = stage##code;                                                     \
        R_CompileShaderAppendDefine(context.stage##code, macro);                               \
        if (args->stage##define)                                                               \
        {                                                                                      \
            R_CompileShaderAppendDefine(context.stage##code, args->stage##define);             \
        }                                                                                      \
        if (context.stage##code.find("#include") != std::string::npos)                         \
        {                                                                                      \
            R_CompileShaderAppendInclude(context.stage##code, args->stage##file);              \
        }                                                                                      \
    }

    LOAD_SHADER_STAGE(vs, "#define IS_VERTEX_SHADER\n")
    LOAD_SHADER_STAGE(gs, "#define IS_GEOMETRY_SHADER\n")
    LOAD_SHADER_STAGE(fs, "#define IS_FRAGMENT_SHADER\n")

#undef LOAD_SHADER_STAGE
    return true;
}

GLuint GL_CompileShaderFileEx(const CCompileShaderArgs* args)
{
    CCompileShaderContext context;

    if (!R_PrepareShaderSources(args, context))
        return 0;

    return R_CompileShader(args, &context);
}

GLuint GL_CompileShaderFile(const char* vsfile, const char* fsfile, const char* vsdefine, const char* fsdefine)
{
    CCompileShaderArgs args = {};
    args.vsfile             = vsfile;
    args.fsfile             = fsfile;
    args.vsdefine           = vsdefine;
    args.fsdefine           = fsdefine;

    return GL_CompileShaderFileEx(&args);
}

void GL_UseProgram(GLuint program)
{
#if 0
	static int currentprogram = -1;

	if (currentprogram != program)
	{
		currentprogram = program;
		glUseProgram(program);
	}
#else
    //Best compatibility with other guys
    glUseProgram(program);
#endif
}

static const CShaderCompileCvar& R_FindShaderCompileCvar(cvar_t** cvar)
{
    for (const auto& entry : s_ShaderCompileCvars)
    {
        if (entry.cvar == cvar)
            return entry;
    }

    Sys_Error("R_FindShaderCompileCvar: unknown shader compile cvar");
    return s_ShaderCompileCvars[0];
}

//Returns the value of an explicit "+name value" command-line control, or nullptr.
static const char* R_GetShaderCompileCvarParm(const CShaderCompileCvar& entry)
{
    const auto  option = std::string("+") + entry.name;
    const char* value  = nullptr;

    if (!gEngfuncs.CheckParm(option.c_str(), &value))
        return nullptr;

    return value;
}

/*
    Startup programs can be requested before the cvars are registered and before the engine
    executes +commands, so fall back to the command line and then to the registered default.
*/
static double R_GetShaderCompileCvarValue(cvar_t** cvar)
{
    if (*cvar)
        return (*cvar)->value;

    const auto& entry = R_FindShaderCompileCvar(cvar);
    const char* value = R_GetShaderCompileCvarParm(entry);

    if (value)
    {
        char*        end    = nullptr;
        const double parsed = std::strtod(value, &end);

        if (end != value)
            return parsed;
    }

    return std::atof(entry.defaultValue);
}

static unsigned R_GetShaderCompileCvarLimit(cvar_t** cvar, unsigned maximum)
{
    double value = R_GetShaderCompileCvarValue(cvar);

    if (!std::isfinite(value))
        value = std::atof(R_FindShaderCompileCvar(cvar).defaultValue);

    return static_cast<unsigned>(std::clamp(value, 1.0, static_cast<double>(maximum)));
}

void R_InitShaderCompileCvars(void)
{
    for (const auto& entry : s_ShaderCompileCvars)
    {
        *entry.cvar = gEngfuncs.pfnRegisterVariable(entry.name, entry.defaultValue, FCVAR_CLIENTDLL | FCVAR_ARCHIVE);

        //These startup programs precede the engine's normal execution of +commands.
        //Honor explicit command-line controls now so forced Sync also covers HUD init.
        const char* value = R_GetShaderCompileCvarParm(entry);

        if (value)
            gEngfuncs.Cvar_Set(entry.name, value);
    }

    gEngfuncs.pfnAddCommand("shader_compile_stats", R_ShaderCompileStats_f);
}

void R_FreeShaderCompileCvars(void)
{
    for (const auto& entry : s_ShaderCompileCvars)
    {
        *entry.cvar = nullptr;
    }
}

static void GL_ConfigureShaderCompiler(void)
{
    if (!s_ShaderCompiler)
        return;

    CShaderCompileConfig config;
    config.enabled     = R_GetShaderCompileCvarValue(&r_shader_parallel_compile) != 0;
    config.threads     = R_GetShaderCompileCvarValue(&r_shader_parallel_threads);
    config.maxInflight = R_GetShaderCompileCvarLimit(&r_shader_parallel_max_inflight, SHADER_MAX_INFLIGHT_LIMIT);
    s_ShaderCompiler->Configure(config);
}

CShaderProgramTicket GL_RequestShaderVariant(const CCompileShaderArgs* args, const char* category, program_state_t state, bool prewarm, CParallelShaderCompiler::Finalizer finalize)
{
    if (!s_ShaderCompiler || !s_ShaderCompiler->HasCurrentContext())
    {
        Sys_Error("Shader request without the initialized renderer GL context");
        return {};
    }

    GL_ConfigureShaderCompiler();

    CCompileShaderContext context;

    if (!R_PrepareShaderSources(args, context))
        return {};

    CShaderProgramSource source;
    source.category = category;
    source.state    = state;

#define COPY_SHADER_STAGE(stage, index, GLStage)                                                                                             \
    if (args->stage##file)                                                                                                                   \
    {                                                                                                                                        \
        source.stages[index] = {GLStage, args->stage##file, std::move(context.stage##code), args->stage##define ? args->stage##define : ""}; \
        R_DumpShaderSource(source.stages[index].source.c_str(), args->stage##file);                                                          \
    }

    COPY_SHADER_STAGE(vs, 0, GL_VERTEX_SHADER);
    COPY_SHADER_STAGE(gs, 1, GL_GEOMETRY_SHADER);
    COPY_SHADER_STAGE(fs, 2, GL_FRAGMENT_SHADER);

#undef COPY_SHADER_STAGE

    const auto priority = prewarm ? ShaderPriority::Prewarm : ShaderPriority::Critical;

    return s_ShaderCompiler->Request(std::move(source), priority, std::move(finalize));
}

void GL_RequireShaderVariant(CShaderProgramTicket ticket)
{
    if (!s_ShaderCompiler)
    {
        Sys_Error("Shader demand failed: No renderer GL context");
        return;
    }

    if (!s_ShaderCompiler->Require(ticket))
        Sys_Error("Shader demand failed: %s", s_ShaderCompiler->Error(ticket).c_str());
}

void GL_PumpShaderCompiler(void)
{
    if (!s_ShaderCompiler || !s_ShaderCompiler->HasCurrentContext())
        return;

    GL_ConfigureShaderCompiler();

    const double   budget      = R_GetShaderCompileCvarValue(&r_shader_parallel_poll_budget_ms);
    const unsigned submissions = R_GetShaderCompileCvarLimit(&r_shader_parallel_submit_per_frame, SHADER_PUMP_MAX_SUBMISSIONS);

    s_ShaderCompiler->Pump(budget, SHADER_PUMP_POLLS_PER_FRAME, submissions);
}

void GL_RequireShaderBatch(const std::vector<CShaderProgramTicket>& tickets)
{
    if (!s_ShaderCompiler)
    {
        Sys_Error("No renderer GL context for startup shader batch");
        return;
    }

    for (auto ticket : tickets)
    {
        //Controlled startup barrier: fill available slots before resolving one.
        s_ShaderCompiler->Pump(SHADER_PUMP_MAX_BUDGET_MS, 0, SHADER_PUMP_MAX_SUBMISSIONS);

        if (!s_ShaderCompiler->Require(ticket, true))
            Sys_Error("Startup shader failed: %s", s_ShaderCompiler->Error(ticket).c_str());
    }
}

void GL_ResetShaderVariants(void)
{
    if (s_ShaderCompiler)
        s_ShaderCompiler->Reset(s_ShaderCompiler->HasCurrentContext());

    R_ClearShaderVariantTables();
}

static const char* R_GetShaderCompileBackendName(ShaderCompileBackend backend)
{
    switch (backend)
    {
        case ShaderCompileBackend::ARB:
            return "ARB";
        case ShaderCompileBackend::KHR:
            return "KHR";
        default:
            return "Sync";
    }
}

void R_ShaderCompileStats_f(void)
{
    if (!s_ShaderCompiler || !s_ShaderCompiler->HasCurrentContext())
        return;

    GL_ConfigureShaderCompiler();

    auto s = s_ShaderCompiler->Statistics();

    gEngfuncs.Con_Printf("Shader compiler: %s, vendor=%s renderer=%s GL=%s GLEW=%s\n", R_GetShaderCompileBackendName(s.backend), glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION), glewGetString(GLEW_VERSION));
    gEngfuncs.Con_Printf("hint requested=%u reported=%u (not actual worker count), requests=%llu hits=%llu submitted=%llu queued=%u inflight=%u ready=%llu failed=%llu cancelled=%llu sync=%llu peak=%llu polls=%llu\n",
                         s.requestedHint, s.reportedHint, s.requests, s.cacheHits, s.submitted, unsigned(s.queued), unsigned(s.inflight), s.ready, s.failed, s.cancelled, s.syncFallbacks, s.peakInflight, s.polls);
    gEngfuncs.Con_Printf("CPU ms: submit total=%.3f peak=%.3f poll total=%.3f pump peak=%.3f demand stalls=%llu total=%.3f startup barriers=%llu total=%.3f prewarm wall=%.3f\n", s.submitMs, s.peakSubmitMs, s.pollMs, s.peakPumpMs, s.demandStalls, s.demandMs, s.startupBarriers, s.startupMs, s.prewarmMs);
    gEngfuncs.Con_Printf("GL objects: shaders created=%llu deleted=%llu programs created=%llu deleted=%llu discarded after context loss=%llu\n", s.shadersCreated, s.shadersDeleted, s.programsCreated, s.programsDeleted, s.discardedObjects);
}

static std::string R_ReadShaderCacheFile(const char* filename)
{
    std::string content;
    auto        FileHandle = FILESYSTEM_ANY_OPEN(filename, "rt");

    if (!FileHandle)
        return content;

    SCOPE_EXIT { FILESYSTEM_ANY_CLOSE(FileHandle); };

    char szReadLine[PROGRAM_STATE_CACHE_MAX_LINE];

    while (FILESYSTEM_ANY_READLINE(szReadLine, sizeof(szReadLine), FileHandle))
        content += szReadLine;

    return content;
}

void R_SaveProgramStatesCaches(const char* filename, const std::vector<program_state_t>& ProgramStates, const program_state_mapping_t* mapping, size_t mapping_size)
{
    std::stringstream ss;

    //Keep valid historical prewarm states that have not finished this session.
    std::set<program_state_t> states(ProgramStates.begin(), ProgramStates.end());
    R_ReadProgramStateCache(filename, mapping, mapping_size, R_ReadShaderCacheFile, [&](program_state_t state) {
        states.insert(state);
    });

    for (auto p : states)
    {
        if (p == 0)
        {
            ss << "NONE";
        }
        else
        {
            for (size_t i = 0; i < mapping_size; ++i)
            {
                if (p & mapping[i].state)
                {
                    ss << mapping[i].name << " ";
                }
            }
        }
        ss << "\n";
    }

    auto FileHandle = FILESYSTEM_ANY_OPEN(filename, "wt");
    if (FileHandle)
    {
        auto str = ss.str();
        FILESYSTEM_ANY_WRITE(str.data(), str.length(), FileHandle);
        FILESYSTEM_ANY_CLOSE(FileHandle);
    }
}

/*
	Purpose: parse file content like:

	NONE
	WATER_DEPTH_ENABLED WATER_REFRACT_ENABLED WATER_ALPHA_BLEND_ENABLED WATER_LINEAR_FOG_SHIFT_ENABLED 
	WATER_UNDERWATER_ENABLED WATER_REFRACT_ENABLED WATER_ALPHA_BLEND_ENABLED 

	into 3 instances of "uint64_t ProgramState", and call "callback" for each of them
*/
void R_LoadProgramStateCaches(const char* filename, const program_state_mapping_t* mapping, size_t mapping_size, void (*callback)(program_state_t state))
{
    R_ReadProgramStateCache(filename, mapping, mapping_size, R_ReadShaderCacheFile, callback);
}
