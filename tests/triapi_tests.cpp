// Exercise the real TriAPI handlers with captured submissions and no GL context.
#include "../src/gl_local.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

static bool          blendEnabled{}, depthWrite = true;
static GLenum        blendSource{}, blendDestination{};
static int           textureResets{};
static void APIENTRY TestEnable(GLenum cap)
{
    if (cap == GL_BLEND) blendEnabled = true;
}
static void APIENTRY TestDisable(GLenum cap)
{
    if (cap == GL_BLEND) blendEnabled = false;
}
static void APIENTRY TestDepthMask(GLboolean enabled) { depthWrite = enabled != GL_FALSE; }
static void APIENTRY TestBlendFunc(GLenum source, GLenum destination)
{
    blendSource      = source;
    blendDestination = destination;
}
static void APIENTRY TestDraw(GLenum, GLsizei, GLenum, const void*, GLint) {}
#define glEnable    TestEnable
#define glDisable   TestDisable
#define glDepthMask TestDepthMask
#define glBlendFunc TestBlendFunc
#undef glDrawElementsBaseVertex
#define glDrawElementsBaseVertex TestDraw
mbasenode_t* R_PVSNode(mbasenode_t*, vec3_t, vec3_t);
#include "../src/gl_triapi.inl"
#undef glEnable
#undef glDisable
#undef glDepthMask
#undef glBlendFunc
#undef glDrawElementsBaseVertex

cl_enginefunc_t  gEngfuncs{};
private_funcs_t  gPrivateFuncs{};
vec4_t           g_GLColor{1, 1, 1, 1};
bool             g_bIsRenderingView{};
bool             r_draw_opaque{}, r_draw_oitblend{};
cvar_t*          r_fog_trans{};
cvar_t*          r_linear_fog_shift{};
model_t**        cl_worldmodel{};
metahook_api_t*  g_pMetaHookAPI{};
bool             R_IsRenderingGBuffer() { return false; }
bool             R_IsRenderingFog() { return false; }
bool             R_IsRenderingGammaBlending() { return false; }
bool             R_IsRenderingWaterView() { return false; }
int              r_fog_mode{};
FBO_Container_t* GL_GetCurrentRenderingFBO() { return nullptr; }
void             GL_BeginDebugGroup(const char*) {}
void             GL_EndDebugGroup() {}
void             GL_BindVAO(GLuint) {}
void             EngineSurface_ResetCurrentTexture() { ++textureResets; }
void             GL_DeleteVAO(GLuint) {}
GLuint           GL_GenVAO()
{
    assert(false);
    return 0;
}
void                                    GL_BindStatesForVAO(GLuint, const std::function<void()>&) { assert(false); }
void                                    GL_UseProgram(GLuint) {}
void                                    GL_EndStencil() {}
void                                    GL_BeginStencilWrite(int, int) {}
void                                    R_SetGBufferBlend(int, int) {}
void                                    R_UseTriAPIProgram(program_state_t, triapi_program_t*) {}
std::shared_ptr<CSpriteModelRenderData> R_GetSpriteRenderDataFromModel(model_t*) { return {}; }
float*                                  R_GetWorldMatrix()
{
    static float matrix[16]{};
    return matrix;
}
float* R_GetProjectionMatrix()
{
    static float matrix[16]{};
    return matrix;
}
mbasenode_t*    R_PVSNode(mbasenode_t*, vec3_t, vec3_t) { return nullptr; }
IPMBRingBuffer* R_CreatePMBRingBuffer(const char*, size_t, GLenum)
{
    assert(false);
    return nullptr;
}
// The production triangulator is irrelevant to the provoking colour: use a
// valid fan whose last indices differ from GL_POLYGON's first-vertex rule.
void R_PolygonToTriangleList(const std::vector<vertex3f_t>& positions, std::vector<uint32_t>& indices)
{
    for (uint32_t i = 1; i + 1 < positions.size(); ++i)
        indices.insert(indices.end(), {0, i, i + 1});
}

class CaptureBuffer : public IPMBRingBuffer
{
public:
    std::vector<unsigned char> bytes;
    void                       Destroy() override {}
    bool                       Allocate(size_t size, CPMBRingBufferAllocation& allocation) override
    {
        bytes.resize(size);
        allocation = {bytes.data(), 0, size, true};
        return true;
    }
    void   BeginFrame() override {}
    void   EndFrame() override {}
    GLuint GetGLBufferObject() const override { return 1; }
    bool   IsEmpty() const override { return bytes.empty(); }
    bool   IsFull() const override { return false; }
    size_t GetUsedSize() const override { return bytes.size(); }
};

static void ExpectColor(float r, float g, float b, float a)
{
    const float expected[] = {r, g, b, a};
    for (int i = 0; i < 4; ++i)
        assert(std::abs(expected[i] - g_GLColor[i]) < 0.000001f);
}

static void TestColorEntries()
{
    triapi_RenderMode(kRenderTransAlpha);
    triapi_Color4f(0.5f, 0.3f, 0.9f, 0.5f);
    ExpectColor(127 / 255.0f, 76 / 255.0f, 230 / 255.0f, 127 / 255.0f);
    triapi_Brightness(0.5f);
    ExpectColor(0.125f, 0.075f, 0.225f, 1);
    triapi_Color4f(2, -1, std::numeric_limits<float>::infinity(), -0.5f);
    ExpectColor(255 / 255.0f, 1 / 255.0f, 0, 129 / 255.0f);
    triapi_Color4f(0.5f, 0.3f, 0.9f, 0.5f);

    triapi_RenderMode(kRenderNormal);
    // Sven uses the explicit argument, independent of the current mode.
    triapi_Color4fRendermode(0.2f, 0.4f, 0.8f, 128, kRenderTransAlpha);
    ExpectColor(0.2f, 0.4f, 0.8f, 128);
    triapi_Brightness(1);
    ExpectColor(0.5f * 128 / 255.0f, 0.3f * 128 / 255.0f, 0.9f * 128 / 255.0f, 1);

    triapi_RenderMode(kRenderTransAlpha);
    triapi_Color4fRendermode(0.2f, 0.4f, 0.8f, 0.5f, kRenderNormal);
    ExpectColor(0.1f, 0.2f, 0.4f, 1);
    triapi_Color4ub(32, 64, 128, 16);
    ExpectColor(32 / 255.0f, 64 / 255.0f, 128 / 255.0f, 1);
    triapi_Brightness(0.5f);
    ExpectColor(32 / 255.0f * 16 / 255.0f * 0.5f,
                64 / 255.0f * 16 / 255.0f * 0.5f,
                128 / 255.0f * 16 / 255.0f * 0.5f, 1);

    triapi_RenderMode(kRenderTransColor);
    triapi_Color4f(2, -1, 0.5f, 0.75f);
    const int resetsBeforeBegin = textureResets;
    triapi_Begin(TRI_TRIANGLES);
    assert(resetsBeforeBegin + 1 == textureResets);
    triapi_Vertex3f(0, 0, 0);
    const auto& vertex = gTriAPICommand.Vertices.back();
    assert(1 == vertex.color[0] && 0 == vertex.color[1]);
    assert(0.375f == vertex.color[2] && 1 == vertex.color[3]);
    triapi_End();
}

static void TestRenderModes()
{
    triapi_RenderMode(kRenderNormal);
    assert(!blendEnabled && depthWrite);
    CoreProfile_glShadeModel(GL_SMOOTH);
    assert(gTriAPICommand.SmoothShading);
    CoreProfile_glShadeModel(GL_FLAT);
    assert(!gTriAPICommand.SmoothShading);
    CoreProfile_glShadeModel(0);
    assert(!gTriAPICommand.SmoothShading);
    triapi_RenderMode(kRenderTransColor);
    assert(blendEnabled && depthWrite);
    assert(GL_SRC_ALPHA == blendSource && GL_ONE_MINUS_SRC_ALPHA == blendDestination);
    triapi_RenderMode(kRenderTransAdd);
    assert(blendEnabled && !depthWrite);
    assert(GL_ONE == blendSource && GL_ONE == blendDestination);
    triapi_RenderMode(kRenderTransTexture);
    assert(blendEnabled && !depthWrite);
    triapi_RenderMode(kRenderNormal);
    triapi_RenderMode(kRenderTransAlpha);
    assert(blendEnabled && !depthWrite);
}

static void TestPrimitive(int primitive, int count, const std::vector<int>& provoking, bool flat)
{
    CaptureBuffer vertices, indices;
    g_TriAPIVertexBuffer = &vertices;
    g_TriAPIIndexBuffer  = &indices;
    gTriAPICommand.hVAO  = 1;
    triapi_RenderMode(flat ? kRenderNormal : kRenderTransAlpha);
    triapi_Begin(primitive);
    for (int i = 0; i < count; ++i)
    {
        CoreProfile_glColor4f(i / 16.0f, 0, 0, 1);
        triapi_Vertex3f(float(i), 0, 0);
    }
    triapi_End();
    const auto*  submitted = reinterpret_cast<const triapivertex_t*>(vertices.bytes.data());
    const auto*  elements  = reinterpret_cast<const uint32_t*>(indices.bytes.data());
    const size_t stride    = primitive == TRI_LINES ? 2 : 3;
    assert(provoking.size() * stride == indices.bytes.size() / sizeof(uint32_t));
    for (size_t face = 0; face < provoking.size(); ++face)
        for (size_t corner = 0; corner < stride; ++corner)
        {
            const auto& vertex   = submitted[elements[face * stride + corner]];
            float       expected = (flat ? provoking[face] : int(vertex.pos[0])) / 16.0f;
            assert(expected == vertex.color[0]);
        }
    assert(flat ? !blendEnabled && depthWrite : blendEnabled && !depthWrite);
    assert(gTriAPICommand.Vertices.empty() && gTriAPICommand.Indices.empty());
    g_TriAPIVertexBuffer = g_TriAPIIndexBuffer = nullptr;
    gTriAPICommand.hVAO                        = 0;
}

static void TestParticleRenderModeReset(bool legacyBegin, int count, int mode)
{
    CaptureBuffer vertices, indices;
    g_TriAPIVertexBuffer = &vertices;
    g_TriAPIIndexBuffer  = &indices;
    gTriAPICommand.hVAO  = 1;
    triapi_RenderMode(mode);
    if (legacyBegin)
        triapi_glBegin(GL_TRIANGLE_FAN);
    else
        triapi_Begin(TRI_TRIANGLE_FAN);
    for (int i = 0; i < count; ++i)
    {
        CoreProfile_glColor4f(i / 4.0f, 0, 0, 0.5f);
        triapi_Vertex3f(float(i), 0, 0);
    }

    // Sven resets the mode before glEnd: native GL ignores these state changes.
    triapi_RenderMode(kRenderNormal);
    assert(kRenderNormal == gTriAPICommand.RenderMode);
    assert(blendEnabled && !depthWrite);
    assert((mode == kRenderTransAdd ? GL_ONE : GL_SRC_ALPHA) == blendSource);
    assert((mode == kRenderTransAdd ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA) == blendDestination);
    CoreProfile_glShadeModel(GL_FLAT);
    assert(gTriAPICommand.SmoothShading);
    triapi_glEnd();
    assert(blendEnabled && !depthWrite);
    if (count >= 3)
    {
        const auto* submitted = reinterpret_cast<const triapivertex_t*>(vertices.bytes.data());
        assert(0 == submitted[0].color[0]);
        assert(0.5f == submitted[0].color[3]);
    }
    // End, including its empty/incomplete paths, must allow subsequent resets.
    triapi_RenderMode(kRenderNormal);
    assert(!blendEnabled && depthWrite);
    g_TriAPIVertexBuffer = g_TriAPIIndexBuffer = nullptr;
    gTriAPICommand.hVAO                        = 0;
}

int main()
{
    TestColorEntries();
    TestRenderModes();
    for (bool legacyBegin : {false, true})
        for (int count : {0, 2, 4})
            for (int mode : {kRenderTransAlpha, kRenderTransAdd})
                TestParticleRenderModeReset(legacyBegin, count, mode);
    for (bool flat : {false, true})
    {
        TestPrimitive(TRI_TRIANGLES, 2, {}, flat);
        TestPrimitive(TRI_QUADS, 3, {}, flat);
        TestPrimitive(TRI_QUAD_STRIP, 3, {}, flat);
        TestPrimitive(TRI_TRIANGLES, 7, {2, 5}, flat);
        TestPrimitive(TRI_TRIANGLE_FAN, 5, {2, 3, 4}, flat);
        TestPrimitive(TRI_QUADS, 9, {3, 3, 7, 7}, flat);
        TestPrimitive(TRI_POLYGON, 5, {0, 0, 0}, flat);
        TestPrimitive(TRI_LINES, 5, {1, 3}, flat);
        TestPrimitive(TRI_TRIANGLE_STRIP, 5, {2, 3, 4}, flat);
        TestPrimitive(TRI_QUAD_STRIP, 7, {3, 3, 5, 5}, flat);
    }
    puts("TriAPI native colour, mode and primitive tests passed");
}
