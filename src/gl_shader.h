#pragma once

#include <vector>
#include <unordered_map>
#include "gl_shader_cache.h"
#include "gl_shader_parallel.h"

GLuint GL_CompileShaderFile(const char* vsfile, const char* fsfile, const char* vsdefine = nullptr, const char* fsdefine = nullptr);
GLuint GL_CompileShaderFileEx(const CCompileShaderArgs* args);

void GL_UseProgram(GLuint program);

CShaderProgramTicket GL_RequestShaderVariant(const CCompileShaderArgs* args, const char* category, program_state_t state, bool prewarm, CParallelShaderCompiler::Finalizer finalize);
void                 GL_RequireShaderVariant(CShaderProgramTicket ticket);
void                 GL_RequireShaderBatch(const std::vector<CShaderProgramTicket>& tickets);
void                 GL_PumpShaderCompiler(void);
void                 GL_ResetShaderVariants(void);
void                 R_ShaderCompileStats_f(void);

void R_InitShaderCompileCvars(void);
void R_FreeShaderCompileCvars(void);

void R_ClearShaderVariantTables(void);
void R_ClearWSurfProgramStates(void);
void R_ClearWaterProgramStates(void);
void R_ClearDLightProgramStates(void);
void R_ClearDFinalProgramStates(void);
void R_ClearStudioProgramStates(void);
void R_ClearSpriteProgramStates(void);
void R_ClearTriAPIProgramStates(void);
void R_ClearPortalProgramStates(void);
void R_ClearDrawTexturedRectProgramStates(void);
void R_ClearDrawFilledRectProgramStates(void);

void R_RequestWSurfProgramState(program_state_t state);
void R_RequestWaterProgramState(program_state_t state);
void R_RequestDLightProgramState(program_state_t state);
void R_RequestDFinalProgramState(program_state_t state);
void R_RequestStudioProgramState(program_state_t state);
void R_RequestSpriteProgramState(program_state_t state);
void R_RequestTriAPIProgramState(program_state_t state);
void R_RequestPortalProgramState(program_state_t state);
void R_RequestDrawTexturedRectProgramState(program_state_t state);
void R_RequestDrawFilledRectProgramState(program_state_t state);

extern cvar_t* r_shader_parallel_compile;
extern cvar_t* r_shader_parallel_threads;
extern cvar_t* r_shader_parallel_max_inflight;
extern cvar_t* r_shader_parallel_submit_per_frame;
extern cvar_t* r_shader_parallel_poll_budget_ms;
extern cvar_t* r_shader_parallel_autosave;

void R_LoadProgramStateCaches(const char* filename, const program_state_mapping_t* mapping, size_t mapping_size, void (*callback)(program_state_t state));

void R_SaveProgramStatesCaches(const char* filename, const std::vector<program_state_t>& ProgramStates, const program_state_mapping_t* mapping, size_t mapping_size);

#define SHADER_DEFINE(name) name##_program_t name;

#define SHADER_UNIFORM(name, loc, locstring) name##.loc = glGetUniformLocation(name.program, locstring);
#define SHADER_ATTRIB(name, loc, locstring)  name##.loc = glGetAttribLocation(name.program, locstring);
