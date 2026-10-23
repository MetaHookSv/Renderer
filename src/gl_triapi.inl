// TriAPI fixed-function translation, included by the renderer and handler tests.
#include <cmath>

class CTriAPICommand
{
public:
    int                                     GLPrimitiveCode{};
    vec2_t                                  TexCoord{};
    vec4_t                                  DrawColor{};
    std::vector<vertex3f_t>                 Positions{};
    std::vector<triapivertex_t>             Vertices{};
    std::vector<uint32_t>                   Indices{};
    int                                     RenderMode{};
    int                                     DrawRenderMode{};
    bool                                    SmoothShading{true};
    bool                                    InsideBegin{};
    GLuint                                  hVAO{};
    std::shared_ptr<CSpriteModelRenderData> pSpriteRenderData;
};

CTriAPICommand  gTriAPICommand;
IPMBRingBuffer* g_TriAPIVertexBuffer{};
IPMBRingBuffer* g_TriAPIIndexBuffer{};

void triapi_RenderMode(int mode)
{
    gTriAPICommand.RenderMode = mode;
    // Sven particles reset RenderMode before glEnd. Native GL ignores blend,
    // depth-mask and shade-model changes inside Begin/End; preserve that state
    // until our deferred draw has been submitted as well.
    if (gTriAPICommand.InsideBegin)
        return;

    // These states persist across End. TransColor/TransTexture inherit the
    // caller's depth mask, as in the native GoldSrc/Sven TriAPI.
    switch (mode)
    {
        case kRenderNormal:
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            gTriAPICommand.SmoothShading = false;
            break;
        case kRenderTransColor:
        case kRenderTransTexture:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_BLEND);
            gTriAPICommand.SmoothShading = true;
            break;
        case kRenderTransAlpha:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            gTriAPICommand.SmoothShading = true;
            break;
        case kRenderTransAdd:
            glBlendFunc(GL_ONE, GL_ONE);
            glEnable(GL_BLEND);
            glDepthMask(GL_FALSE);
            gTriAPICommand.SmoothShading = true;
            break;
    }
}

void triapi_Begin(int primitiveCode)
{
    if (!(primitiveCode >= TRI_TRIANGLES && primitiveCode <= TRI_QUAD_STRIP))
    {
        Sys_Error(__FUNCTION__ ": invalid primitive %d !", primitiveCode);
        return;
    }

    EngineSurface_ResetCurrentTexture();

    const int tri_GL_Modes[7] =
        {
            GL_TRIANGLES,
            GL_TRIANGLE_FAN,
            GL_QUADS,
            GL_POLYGON,
            GL_LINES,
            GL_TRIANGLE_STRIP,
            GL_QUAD_STRIP};

    gTriAPICommand.GLPrimitiveCode = tri_GL_Modes[primitiveCode];
    gTriAPICommand.DrawRenderMode  = gTriAPICommand.RenderMode;
    gTriAPICommand.InsideBegin     = true;
}

void triapi_EndClear()
{
    gTriAPICommand.Positions.clear();
    gTriAPICommand.Vertices.clear();
    gTriAPICommand.Indices.clear();
}

void triapi_Shutdown()
{
    if (g_TriAPIVertexBuffer)
    {
        g_TriAPIVertexBuffer->Destroy();
        g_TriAPIVertexBuffer = nullptr;
    }

    if (g_TriAPIIndexBuffer)
    {
        g_TriAPIIndexBuffer->Destroy();
        g_TriAPIIndexBuffer = nullptr;
    }

    if (gTriAPICommand.hVAO)
    {
        GL_DeleteVAO(gTriAPICommand.hVAO);
        gTriAPICommand.hVAO = 0;
    }

    gTriAPICommand.pSpriteRenderData.reset();
}

void triapi_End()
{
    gTriAPICommand.InsideBegin = false;
    size_t n                   = gTriAPICommand.Vertices.size();

    if (n == 0)
    {
        triapi_EndClear();
        return;
    }

    if (gTriAPICommand.GLPrimitiveCode == GL_TRIANGLES)
    {
        if (n < 3)
        {
            triapi_EndClear();
            return;
        }

        for (size_t i = 0; i < n; i += 3)
        {
            if (i + 2 < n)
            {
                gTriAPICommand.Indices.push_back((GLuint)i);
                gTriAPICommand.Indices.push_back((GLuint)i + 1);
                gTriAPICommand.Indices.push_back((GLuint)i + 2);
            }
        }
    }
    else if (gTriAPICommand.GLPrimitiveCode == GL_TRIANGLE_FAN)
    {
        if (n < 3)
        {
            triapi_EndClear();
            return;
        }

        for (size_t i = 1; i < n - 1; ++i)
        {
            gTriAPICommand.Indices.push_back(0);
            gTriAPICommand.Indices.push_back((GLuint)i);
            gTriAPICommand.Indices.push_back((GLuint)i + 1);
        }
    }
    else if (gTriAPICommand.GLPrimitiveCode == GL_QUADS)
    {
        if (n < 4)
        {
            triapi_EndClear();
            return;
        }

        for (size_t i = 0; i < n; i += 4)
        {
            if (i + 3 < n)
            {
                gTriAPICommand.Indices.push_back((GLuint)i + 0);
                gTriAPICommand.Indices.push_back((GLuint)i + 1);
                gTriAPICommand.Indices.push_back((GLuint)i + 2);

                gTriAPICommand.Indices.push_back((GLuint)i + 2);
                gTriAPICommand.Indices.push_back((GLuint)i + 3);
                gTriAPICommand.Indices.push_back((GLuint)i + 0);
            }
        }
    }
    else if (gTriAPICommand.GLPrimitiveCode == GL_POLYGON)
    {
        if (n < 3)
        {
            triapi_EndClear();
            return;
        }

        R_PolygonToTriangleList(gTriAPICommand.Positions, gTriAPICommand.Indices);
    }
    else if (gTriAPICommand.GLPrimitiveCode == GL_LINES)
    {
        for (size_t i = 0; i < n - n % 2; i++)
        {
            gTriAPICommand.Indices.push_back((GLuint)i);
        }
    }
    else if (gTriAPICommand.GLPrimitiveCode == GL_TRIANGLE_STRIP)
    {
        if (n < 3)
        {
            triapi_EndClear();
            return;
        }

        for (size_t i = 0; i < n - 2; ++i)
        {
            if (i % 2 == 0)
            {
                gTriAPICommand.Indices.push_back((GLuint)i);
                gTriAPICommand.Indices.push_back((GLuint)i + 1);
                gTriAPICommand.Indices.push_back((GLuint)i + 2);
            }
            else
            {
                gTriAPICommand.Indices.push_back((GLuint)i + 1);
                gTriAPICommand.Indices.push_back((GLuint)i);
                gTriAPICommand.Indices.push_back((GLuint)i + 2);
            }
        }
    }
    else if (gTriAPICommand.GLPrimitiveCode == GL_QUAD_STRIP)
    {
        if (n < 4)
        {
            triapi_EndClear();
            return;
        }

        for (size_t i = 0; i + 3 < n; i += 2)
        {
            // GL_QUAD_STRIP 顶点排列: v0-v1 为第一条边, v2-v3 为第二条边
            // 形成四边形: v0, v1, v3, v2 (注意顺序)
            uint32_t v0 = i;
            uint32_t v1 = i + 1;
            uint32_t v2 = i + 2;
            uint32_t v3 = i + 3;

            // 将四边形 (v0, v1, v3, v2) 分解为两个三角形
            // 三角形1: v0, v1, v3
            // 三角形2: v0, v3, v2
            gTriAPICommand.Indices.push_back(v0);
            gTriAPICommand.Indices.push_back(v1);
            gTriAPICommand.Indices.push_back(v3);

            gTriAPICommand.Indices.push_back(v0);
            gTriAPICommand.Indices.push_back(v3);
            gTriAPICommand.Indices.push_back(v2);
        }
    }

    if (gTriAPICommand.Indices.size() == 0)
    {
        triapi_EndClear();
        return;
    }

    if (!gTriAPICommand.SmoothShading)
    {
        // Expanded triangles must retain the original primitive's provoking
        // colour. Shared quad-strip vertices cannot be recoloured in place.
        std::vector<triapivertex_t> flatVertices;
        flatVertices.reserve(gTriAPICommand.Indices.size());
        const size_t stride = gTriAPICommand.GLPrimitiveCode == GL_LINES ? 2 : 3;
        for (size_t i = 0; i < gTriAPICommand.Indices.size(); i += stride)
        {
            size_t provoking = gTriAPICommand.Indices[i + stride - 1];
            if (gTriAPICommand.GLPrimitiveCode == GL_QUADS)
                provoking = (i / 6) * 4 + 3;
            else if (gTriAPICommand.GLPrimitiveCode == GL_QUAD_STRIP)
                provoking = (i / 6) * 2 + 3;
            else if (gTriAPICommand.GLPrimitiveCode == GL_POLYGON)
                provoking = 0;
            for (size_t corner = 0; corner < stride; ++corner)
            {
                auto vertex = gTriAPICommand.Vertices[gTriAPICommand.Indices[i + corner]];
                memcpy(vertex.color, gTriAPICommand.Vertices[provoking].color, sizeof(vertex.color));
                flatVertices.push_back(vertex);
                gTriAPICommand.Indices[i + corner] = static_cast<uint32_t>(i + corner);
            }
        }
        gTriAPICommand.Vertices = std::move(flatVertices);
    }

    if (!gTriAPICommand.hVAO)
    {
        gTriAPICommand.hVAO = GL_GenVAO();

        if (!g_TriAPIVertexBuffer)
        {
            g_TriAPIVertexBuffer = R_CreatePMBRingBuffer("TriAPIVertexBuffer", 32 * 1024 * 1024, GL_ARRAY_BUFFER);
        }

        if (!g_TriAPIIndexBuffer)
        {
            g_TriAPIIndexBuffer = R_CreatePMBRingBuffer("TriAPIIndexBuffer", 8 * 1024 * 1024, GL_ELEMENT_ARRAY_BUFFER);
        }

        if (g_TriAPIVertexBuffer && g_TriAPIIndexBuffer)
        {
            GL_BindStatesForVAO(gTriAPICommand.hVAO, [] {
                glBindBuffer(GL_ARRAY_BUFFER, g_TriAPIVertexBuffer->GetGLBufferObject());

                glVertexAttribPointer(TRIAPI_VA_POSITION, 3, GL_FLOAT, false, sizeof(triapivertex_t), OFFSET(triapivertex_t, pos));
                glEnableVertexAttribArray(TRIAPI_VA_POSITION);

                glVertexAttribPointer(TRIAPI_VA_TEXCOORD, 2, GL_FLOAT, false, sizeof(triapivertex_t), OFFSET(triapivertex_t, texcoord));
                glEnableVertexAttribArray(TRIAPI_VA_TEXCOORD);

                glVertexAttribPointer(TRIAPI_VA_COLOR, 4, GL_FLOAT, false, sizeof(triapivertex_t), OFFSET(triapivertex_t, color));
                glEnableVertexAttribArray(TRIAPI_VA_COLOR);

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_TriAPIIndexBuffer->GetGLBufferObject());
            });
        }
        else
        {
            Sys_Error("triapi_End: Failed to initialize triapi ring buffer\n");
        }
    }

    uint32_t verticesCount = gTriAPICommand.Vertices.size();
    uint32_t indiceCount   = gTriAPICommand.Indices.size();

    size_t vertexDataSize = verticesCount * sizeof(triapivertex_t);
    size_t indexDataSize  = indiceCount * sizeof(uint32_t);

    CPMBRingBufferAllocation vertexAllocation;
    if (!g_TriAPIVertexBuffer->Allocate(vertexDataSize, vertexAllocation))
    {
        //ring buffer full
        gEngfuncs.Con_DPrintf("triapi_End: g_TriAPIVertexBuffer full!\n");

        triapi_EndClear();
        return;
    }

    CPMBRingBufferAllocation indexAllocation;
    if (!g_TriAPIIndexBuffer->Allocate(indexDataSize, indexAllocation))
    {
        //ring buffer full
        gEngfuncs.Con_DPrintf("triapi_End: g_TriAPIIndexBuffer full!\n");

        triapi_EndClear();
        return;
    }

    memcpy(vertexAllocation.ptr, gTriAPICommand.Vertices.data(), vertexDataSize);

    GLuint baseVertex = (GLuint)(vertexAllocation.offset / sizeof(triapivertex_t));

    memcpy(indexAllocation.ptr, gTriAPICommand.Indices.data(), indexDataSize);

    GLuint baseIndex = (GLuint)(indexAllocation.offset / sizeof(uint32_t));

    triapi_EndClear();

    GL_BeginDebugGroup("triapi_End");

    GL_BindVAO(gTriAPICommand.hVAO);

    uint64_t   ProgramState = 0;
    const bool bDrawHUD     = !g_bIsRenderingView;

    if (bDrawHUD)
    {
        ProgramState |= TRIAPI_HUD_SPACE_ENABLED;
        glDisable(GL_DEPTH_TEST);
    }

    switch (gTriAPICommand.DrawRenderMode)
    {
        case kRenderTransAdd:
            if (!bDrawHUD)
                R_SetGBufferBlend(GL_ONE, GL_ONE);
            ProgramState |= SPRITE_ADDITIVE_BLEND_ENABLED;
            break;
        case kRenderTransAlpha:
        case kRenderTransColor:
        case kRenderTransTexture:
            if (!bDrawHUD)
                R_SetGBufferBlend(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            ProgramState |= SPRITE_ALPHA_BLEND_ENABLED;
            break;
    }

    if (!bDrawHUD)
    {
        if (R_IsRenderingWaterView())
            ProgramState |= SPRITE_CLIP_ENABLED;
        if (R_IsRenderingGammaBlending())
            ProgramState |= SPRITE_GAMMA_BLEND_ENABLED;
        if (!R_IsRenderingGBuffer() && R_IsRenderingFog())
        {
            // Native fog is persistent GL state, including additive draws.
            if (r_fog_mode == GL_LINEAR)
                ProgramState |= SPRITE_LINEAR_FOG_ENABLED;
            else if (r_fog_mode == GL_EXP)
                ProgramState |= SPRITE_EXP_FOG_ENABLED;
            else if (r_fog_mode == GL_EXP2)
                ProgramState |= SPRITE_EXP2_FOG_ENABLED;
        }
        if (r_draw_oitblend && (ProgramState & (SPRITE_ALPHA_BLEND_ENABLED | SPRITE_ADDITIVE_BLEND_ENABLED)))
            ProgramState |= SPRITE_OIT_BLEND_ENABLED;
    }

    if (!bDrawHUD && gTriAPICommand.pSpriteRenderData)
    {
        if (r_draw_opaque)
        {
            int iStencilRef = 0;

            if ((gTriAPICommand.pSpriteRenderData->flags & FMODEL_NOBLOOM))
            {
                iStencilRef |= STENCIL_MASK_NO_BLOOM;

                //has stencil write-in
                GL_BeginStencilWrite(iStencilRef, STENCIL_MASK_ALL);
            }
        }
        else
        {
            int iStencilRef = 0;

            if ((gTriAPICommand.pSpriteRenderData->flags & FMODEL_NOBLOOM))
            {
                iStencilRef |= STENCIL_MASK_NO_BLOOM;

                //has stencil write-in
                GL_BeginStencilWrite(iStencilRef, STENCIL_MASK_NO_BLOOM);
            }
        }
    }

    triapi_program_t prog{};
    R_UseTriAPIProgram(ProgramState, &prog);

    if (gTriAPICommand.GLPrimitiveCode == GL_LINES)
    {
        glDrawElementsBaseVertex(GL_LINES, indiceCount, GL_UNSIGNED_INT, BUFFER_OFFSET(baseIndex), baseVertex);
    }
    else
    {
        glDrawElementsBaseVertex(GL_TRIANGLES, indiceCount, GL_UNSIGNED_INT, BUFFER_OFFSET(baseIndex), baseVertex);
    }

    GL_UseProgram(0);

    GL_EndStencil();

    GL_BindVAO(0);

    GL_EndDebugGroup();
}

void triapi_Color4f(float r, float g, float b, float a)
{
    gTriAPICommand.DrawColor[0] = r;
    gTriAPICommand.DrawColor[1] = g;
    gTriAPICommand.DrawColor[2] = b;
    gTriAPICommand.DrawColor[3] = a;

    if (gTriAPICommand.RenderMode == kRenderTransAlpha)
    {
        // Native x86 truncates to a signed integer and keeps the low byte,
        // including out-of-range colours; it does not clamp before conversion.
        const auto colorByte = [](float value) -> unsigned char {
            constexpr float int32Limit = 2147483648.0f;
            if (!std::isfinite(value) || value < -int32Limit || value >= int32Limit)
                return 0;
            return static_cast<unsigned char>(static_cast<int32_t>(value));
        };
        CoreProfile_glColor4ub(colorByte(r * 255.9f), colorByte(g * 255.9f), colorByte(b * 255.9f), colorByte(a * 255.0f));
    }
    else
    {
        CoreProfile_glColor4f(r * a, g * a, b * a, 1);
    }
}

void triapi_Color4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    gTriAPICommand.DrawColor[0] = static_cast<float>(r / 255.0);
    gTriAPICommand.DrawColor[1] = static_cast<float>(g / 255.0);
    gTriAPICommand.DrawColor[2] = static_cast<float>(b / 255.0);
    gTriAPICommand.DrawColor[3] = static_cast<float>(a / 255.0);

    CoreProfile_glColor4f(gTriAPICommand.DrawColor[0], gTriAPICommand.DrawColor[1], gTriAPICommand.DrawColor[2], 1);
}

void triapi_Brightness(float brightness)
{
    CoreProfile_glColor4f(
        gTriAPICommand.DrawColor[0] * gTriAPICommand.DrawColor[3] * brightness,
        gTriAPICommand.DrawColor[1] * gTriAPICommand.DrawColor[3] * brightness,
        gTriAPICommand.DrawColor[2] * gTriAPICommand.DrawColor[3] * brightness,
        1);
}

void triapi_Color4fRendermode(float r, float g, float b, float a, int rendermode)
{
    // Sven Co-op uses the explicit argument; the older HLND2T source used
    // gRenderMode instead. Neither branch updates the saved RGB components.
    if (rendermode == kRenderTransAlpha)
    {
        gTriAPICommand.DrawColor[3] = a / 255;
    }

    if (rendermode == kRenderTransAlpha)
    {
        CoreProfile_glColor4f(r, g, b, a);
    }
    else
    {
        CoreProfile_glColor4f(r * a, g * a, b * a, 1.0f);
    }
}

void triapi_Vertex3fv(float* v)
{
    vertex3f_t pos{};
    pos.v[0] = v[0];
    pos.v[1] = v[1];
    pos.v[2] = v[2];

    gTriAPICommand.Positions.emplace_back(pos);

    triapivertex_t vertex{};
    vertex.pos[0]      = v[0];
    vertex.pos[1]      = v[1];
    vertex.pos[2]      = v[2];
    vertex.texcoord[0] = gTriAPICommand.TexCoord[0];
    vertex.texcoord[1] = gTriAPICommand.TexCoord[1];
    vertex.color[0]    = math_clamp(g_GLColor[0], 0.0f, 1.0f);
    vertex.color[1]    = math_clamp(g_GLColor[1], 0.0f, 1.0f);
    vertex.color[2]    = math_clamp(g_GLColor[2], 0.0f, 1.0f);
    vertex.color[3]    = math_clamp(g_GLColor[3], 0.0f, 1.0f);

    gTriAPICommand.Vertices.push_back(vertex);
}

void triapi_Vertex3f(float x, float y, float z)
{
    vertex3f_t pos{};
    pos.v[0] = x;
    pos.v[1] = y;
    pos.v[2] = z;

    gTriAPICommand.Positions.emplace_back(pos);

    triapivertex_t vertex{};
    vertex.pos[0]      = x;
    vertex.pos[1]      = y;
    vertex.pos[2]      = z;
    vertex.texcoord[0] = gTriAPICommand.TexCoord[0];
    vertex.texcoord[1] = gTriAPICommand.TexCoord[1];
    vertex.color[0]    = math_clamp(g_GLColor[0], 0.0f, 1.0f);
    vertex.color[1]    = math_clamp(g_GLColor[1], 0.0f, 1.0f);
    vertex.color[2]    = math_clamp(g_GLColor[2], 0.0f, 1.0f);
    vertex.color[3]    = math_clamp(g_GLColor[3], 0.0f, 1.0f);

    gTriAPICommand.Vertices.push_back(vertex);
}

void triapi_TexCoord2f(float s, float t)
{
    gTriAPICommand.TexCoord[0] = s;
    gTriAPICommand.TexCoord[1] = t;
}

void triapi_GetMatrix(const int pname, float* matrix)
{
    if (pname == GL_MODELVIEW_MATRIX)
    {
        memcpy(matrix, R_GetWorldMatrix(), sizeof(mat4));
        return;
    }

    if (pname == GL_PROJECTION_MATRIX)
    {
        memcpy(matrix, R_GetProjectionMatrix(), sizeof(mat4));
        return;
    }

    Sys_Error("triapi_GetMatrix: Invalid matrix type %d !", pname);
}

int triapi_BoxInPVS(float* mins, float* maxs)
{
    return R_PVSNode((*cl_worldmodel)->nodes, mins, maxs) != nullptr;
}

void triapi_Fog(float* flFogColor, float flStart, float flEnd, qboolean bOn)
{
    gPrivateFuncs.triapi_Fog(flFogColor, flStart, flEnd, bOn);
}

void triapi_FogParams(float flDensity, qboolean bFogAffectsSkybox)
{
    gPrivateFuncs.triapi_FogParams(flDensity, bFogAffectsSkybox);
}

qboolean triapi_SpriteTexture(model_t* pSpriteModel, int frame)
{
    gTriAPICommand.pSpriteRenderData = R_GetSpriteRenderDataFromModel(pSpriteModel);

    return gPrivateFuncs.triapi_SpriteTexture(pSpriteModel, frame);
}

void __stdcall triapi_glBegin(int GLPrimitiveCode)
{
    gTriAPICommand.GLPrimitiveCode = GLPrimitiveCode;
    gTriAPICommand.DrawRenderMode  = gTriAPICommand.RenderMode;
    gTriAPICommand.InsideBegin     = true;
}

void __stdcall triapi_glEnd()
{
    triapi_End();
}

void __stdcall CoreProfile_glColor4f(float r, float g, float b, float a)
{
    g_GLColor[0] = r;
    g_GLColor[1] = g;
    g_GLColor[2] = b;
    g_GLColor[3] = a;
}

void __stdcall CoreProfile_glShadeModel(GLenum mode)
{
    if (!gTriAPICommand.InsideBegin && (mode == GL_FLAT || mode == GL_SMOOTH))
        gTriAPICommand.SmoothShading = mode == GL_SMOOTH;
}

void __stdcall CoreProfile_glColor4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    g_GLColor[0] = r / 255.0f;
    g_GLColor[1] = g / 255.0f;
    g_GLColor[2] = b / 255.0f;
    g_GLColor[3] = a / 255.0f;
}
