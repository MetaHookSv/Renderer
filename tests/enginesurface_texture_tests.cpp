// Exercise production surface loading, binding and drawing with file/GL I/O stubbed.
#include "../src/gl_local.h"
#include <cassert>
#include <cstdio>
#include <string>

static GLuint        boundTexture{};
static void APIENTRY TestBindTexture(GLenum, GLuint id) { boundTexture = id; }
#define glBindTexture TestBindTexture
#include "../src/EngineSurfaceHook.cpp"
#undef glBindTexture

private_funcs_t      gPrivateFuncs{};
int                  g_iEngineType      = ENGINE_GOLDSRC;
DWORD                g_dwEngineBuildnum = 10210;
static int           currentTextureId{};
int*                 currenttexture = &currentTextureId;
cl_enginefunc_t      gEngfuncs{};
metahook_api_t*      g_pMetaHookAPI{};
IEngineSurface*      staticSurface{};
IEngineSurface_HL25* staticSurface_HL25{};
void***              pmainwindow{};

// Unused engine hooks share this translation unit; fail if a test enters them.
PVOID GetVFunctionFromVFTable(PVOID*, int, const mh_dll_info_t&, const mh_dll_info_t&, const mh_dll_info_t&)
{
    assert(false);
    return nullptr;
}
void   R_DrawFilledRect(const filledrectvertex_t*, size_t, const uint32_t*, size_t, uint64_t, const char*) { assert(false); }
void   R_PolygonToTriangleList(const std::vector<vertex3f_t>&, std::vector<uint32_t>&) { assert(false); }
GLuint GL_GenTexture()
{
    assert(false);
    return 0;
}
void  R_PushWorldMatrix() { assert(false); }
void  R_PopWorldMatrix() { assert(false); }
void  R_LoadIdentityForWorldMatrix() { assert(false); }
void  R_TranslateWorldMatrix(float, float, float) { assert(false); }
void  R_PushProjectionMatrix() { assert(false); }
void  R_PopProjectionMatrix() { assert(false); }
void  R_SetupOrthoProjectionMatrix(float, float, float, float, float, float, bool) { assert(false); }
void* Sys_GetMainWindow()
{
    assert(false);
    return nullptr;
}

static std::string                 availableImage;
static bool                        compressedImage{};
static int                         fileAttempts{}, uploads{}, draws{}, lastDrawTexture{};
static bool                        scissor{};
static EngineSurfaceVertexBuffer_t textVertices[MAXVERTEXBUFFERS]{};
static int                         textVertexCount{};

bool LoadDDS(const char* filename, const char*, gl_loadtexture_context_t* ctx)
{
    ++fileAttempts;
    if (!compressedImage || availableImage != filename)
        return false;
    ctx->width      = 256;
    ctx->height     = 80;
    ctx->compressed = true;
    ctx->mipmaps.emplace_back();
    return ctx->callback(ctx);
}

bool LoadImageGenericFileIO(const char* filename, const char*, gl_loadtexture_context_t* ctx)
{
    ++fileAttempts;
    if (compressedImage || availableImage != filename)
        return false;
    ctx->width  = 256;
    ctx->height = 80;
    ctx->mipmaps.emplace_back();
    return ctx->callback(ctx);
}

void GL_Bind(int id)
{
    currentTextureId = id;
    boundTexture     = id;
}
void GL_UploadCompressedTexture(gl_loadtexture_context_t*, int) { ++uploads; }
void GL_UploadUncompressedTexture(gl_loadtexture_context_t*, int) { ++uploads; }
void GL_SetTextureDebugNameFormat(GLuint, const char*, ...) {}
void R_DrawTexturedRect(int id, const texturedrectvertex_t*, size_t, const uint32_t*, size_t, uint64_t, const char*)
{
    ++draws;
    lastDrawTexture = id;
}

static void ExpectInvalid(void* surface, int id)
{
    assert(false == enginesurface_isTextureIDValid(surface, 0, id));
    int wide = -1, tall = -1;
    enginesurface_drawGetTextureSize(surface, 0, id, wide, tall);
    assert(0 == wide && 0 == tall);
    enginesurface_drawSetTexture(surface, 0, id);
    assert(nullptr == staticTextureCurrent);
    assert(0 == boundTexture);
    const int previousDraws = draws;
    enginesurface_drawTexturedRect(surface, 0, 0, 0, 256, 80);
    enginesurface_drawTexturedRectAdd(surface, 0, 0, 0, 256, 80);
    enginesurface_drawPrintCharAdd(surface, 0, 0, 0, 16, 16, 0, 0, 1, 1);
    assert(previousDraws == draws);
}

static void LoadValid(void* surface, int id, bool compressed)
{
    compressedImage           = compressed;
    availableImage            = compressed ? "gfx/career/weapons/640_p90.dds" : "gfx/career/weapons/640_p90.tga";
    const int previousUploads = uploads;
    enginesurface_drawSetTextureFile(surface, 0, id, "gfx/career/weapons/640_p90", 0, true);
    assert(previousUploads + 1 == uploads);
    assert(true == enginesurface_isTextureIDValid(surface, 0, id));
    assert(id == staticTextureCurrent->_id);
    assert(staticGetTextureById(id) == staticTextureCurrent);
    assert(static_cast<GLuint>(id) == boundTexture);
    int wide = 0, tall = 0;
    enginesurface_drawGetTextureSize(surface, 0, id, wide, tall);
    assert(256 == wide && 80 == tall);
    const int previousDraws = draws;
    enginesurface_drawTexturedRect(surface, 0, 0, 0, 256, 80);
    assert(previousDraws + 1 == draws);
    assert(id == lastDrawTexture);
}

int main()
{
    int surface[8]{};
    gPrivateFuncs.offset_enginesurface_drawColor     = 0;
    gPrivateFuncs.offset_enginesurface_drawTextColor = 4 * sizeof(int);
    g_bScissor                                       = &scissor;
    g_VertexBuffer                                   = &textVertices;
    g_iVertexBufferEntriesUsed                       = &textVertexCount;
    constexpr int textureId                          = 421;

    // Regression: replacing an existing weapon with a missing icon must clear it.
    availableImage = "gfx/career/weapons/640_p90.tga";
    enginesurface_drawSetTextureFile(surface, 0, textureId, "gfx/career/weapons/640_p90", 0, true);
    enginesurface_drawSetTexture(surface, 0, textureId);
    enginesurface_drawSetTextureFile(surface, 0, textureId, "gfx/career/weapons/640_none", 0, true);
    assert(false == enginesurface_isTextureIDValid(surface, 0, textureId));

    // A new missing image cannot borrow the previously bound surface texture.
    enginesurface_drawSetTextureFile(surface, 0, textureId, "gfx/career/weapons/640_none", 0, true);
    ExpectInvalid(surface, textureId);

    for (bool compressed : {false, true})
    {
        LoadValid(surface, textureId, compressed);
        const int previousAttempts = fileAttempts;
        enginesurface_drawSetTextureFile(surface, 0, textureId, "gfx/career/weapons/640_p90", 0, false);
        assert(previousAttempts == fileAttempts);

        // Flush queued text with its original texture before clearing that texture.
        textVertexCount         = 4;
        const int previousDraws = draws;
        enginesurface_drawSetTextureFile(surface, 0, textureId, "gfx/career/weapons/640_none", 0, true);
        assert(previousDraws + 1 == draws);
        assert(textureId == lastDrawTexture);
        assert(0 == textVertexCount);
        ExpectInvalid(surface, textureId);
        enginesurface_drawSetTextureFile(surface, 0, textureId, "gfx/career/weapons/640_none", 0, false);
        ExpectInvalid(surface, textureId);

        LoadValid(surface, textureId, compressed);
        const int attemptsBeforeClear = fileAttempts;
        enginesurface_drawSetTextureFile(surface, 0, textureId, "", 0, false);
        assert(attemptsBeforeClear == fileAttempts);
        ExpectInvalid(surface, textureId);
        LoadValid(surface, textureId, compressed);
        enginesurface_drawSetTextureFile(surface, 0, textureId, nullptr, 0, true);
        ExpectInvalid(surface, textureId);
    }
    LoadValid(surface, textureId, false);
    staticFreeTextureId(textureId);
    assert(nullptr == staticTextureCurrent);
    puts("enginesurface texture tests passed");
}
