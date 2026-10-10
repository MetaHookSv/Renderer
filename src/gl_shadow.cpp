#include "gl_local.h"
#include <sstream>
#include <math.h>
#include <chrono>
#include "shadow_cache_gl.h"

//cvar
cvar_t*        r_shadow       = NULL;
static cvar_t* r_shadow_cache = NULL;

// Studio and brush casters around the camera, rebuilt once per frame.
static std::vector<ShadowCache::Caster> s_shadowCasters;

// Set while R_PrepareShadowCasters runs a Studio model through the normal
// draw path only to compute its bounds.
static ShadowCache::Caster* s_pCollectingCaster       = nullptr;
static bool                 s_bCollectingBoundsFailed = false;

// Light whose dynamic shadow pass is currently drawing with the cache.
static CDynamicLight* s_pCachedShadowLight = nullptr;

// Expanded on all sides of a caster's bounds, so that small differences
// between CPU and GPU skinning never cull a caster that is actually visible.
static constexpr float SHADOW_CASTER_BOUNDS_MARGIN = 1.0f;

struct ShadowCacheStats_t
{
    int    updates             = 0;
    int    reused              = 0;
    int    submissions         = 0;
    int    boneBoxes           = 0;
    double prepareMilliseconds = 0;
};

static ShadowCacheStats_t s_ShadowCacheStats;

bool R_IsCollectingShadowCasters(void)
{
    return s_pCollectingCaster != nullptr;
}

static bool R_IsStudioRangeValid(const studiohdr_t* hdr, int offset, int count, size_t stride)
{
    if (offset < 0 || count < 0)
        return false;

    const auto length = static_cast<size_t>(hdr->length);
    if (static_cast<size_t>(offset) > length)
        return false;

    return static_cast<size_t>(count) <= (length - offset) / stride;
}

/*
    Build the local-space bounds of the vertices skinned to each bone.

    All bodygroups are included, so switching body/skin can never make the
    bounds smaller than the mesh actually drawn. The collision hull is not used.
*/
static bool R_BuildStudioShadowBounds(studiohdr_t* hdr, CStudioModelRenderData* pRenderData)
{
    if (hdr->length < static_cast<int>(sizeof(studiohdr_t)))
        return false;

    if (!R_IsStudioRangeValid(hdr, hdr->bodypartindex, hdr->numbodyparts, sizeof(mstudiobodyparts_t)))
        return false;

    auto base  = reinterpret_cast<byte*>(hdr);
    auto parts = reinterpret_cast<mstudiobodyparts_t*>(base + hdr->bodypartindex);

    for (int i = 0; i < hdr->numbodyparts; ++i)
    {
        const auto& part = parts[i];

        if (!R_IsStudioRangeValid(hdr, part.modelindex, part.nummodels, sizeof(mstudiomodel_t)))
            return false;

        auto models = reinterpret_cast<mstudiomodel_t*>(base + part.modelindex);

        for (int j = 0; j < part.nummodels; ++j)
        {
            const auto& model = models[j];

            if (!R_IsStudioRangeValid(hdr, model.vertindex, model.numverts, sizeof(vec3_t)))
                return false;

            if (!R_IsStudioRangeValid(hdr, model.vertinfoindex, model.numverts, sizeof(byte)))
                return false;

            auto vertices    = reinterpret_cast<vec3_t*>(base + model.vertindex);
            auto vertexBones = base + model.vertinfoindex;

            for (int k = 0; k < model.numverts; ++k)
            {
                const auto& v    = vertices[k];
                const int   bone = vertexBones[k];

                if (bone >= hdr->numbones)
                    return false;

                if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
                    return false;

                pRenderData->shadowBoneBounds[bone].Add({v[0], v[1], v[2]});
            }
        }
    }

    return true;
}

static bool R_AddStudioShadowBounds(ShadowCache::Bounds& bounds)
{
    auto hdr = (*pstudiohdr);
    if (!hdr)
        return false;

    auto pRenderData = R_GetStudioRenderDataFromStudioHeaderFast(hdr);
    if (!pRenderData)
        pRenderData = R_GetStudioRenderDataFromStudioHeaderSlow(hdr);
    if (!pRenderData)
        return false;

    if (hdr->numbones <= 0 || hdr->numbones > MAXSTUDIOBONES)
        return false;

    // Per-bone bounds only depend on the model, so build them once.
    if (!pRenderData->shadowBoneBoundsPrepared)
    {
        pRenderData->shadowBoneBoundsPrepared = true;
        pRenderData->shadowBoneBoundsValid    = R_BuildStudioShadowBounds(hdr, pRenderData.get());
    }

    if (!pRenderData->shadowBoneBoundsValid)
        return false;

    for (int bone = 0; bone < hdr->numbones; ++bone)
    {
        const auto& boneBounds = pRenderData->shadowBoneBounds[bone];
        if (!boneBounds.valid)
            continue;

        ++s_ShadowCacheStats.boneBoxes;

        const auto& matrix      = (*pbonetransform)[bone];
        auto        transformed = ShadowCache::TransformBounds(boneBounds, &matrix[0][0]);
        if (!transformed.valid)
            return false;

        bounds.Add(transformed.mins);
        bounds.Add(transformed.maxs);
    }

    return true;
}

// Replaces StudioRenderFinal while collecting casters. By this point the bone
// transforms are set up, so no drawing is needed to get the posed bounds.
void R_CollectStudioShadowBounds(void)
{
    if (!s_pCollectingCaster || !R_AddStudioShadowBounds(s_pCollectingCaster->bounds))
        s_bCollectingBoundsFailed = true;
}

static void R_ComputeBrushCasterBounds(cl_entity_t* ent, ShadowCache::Caster& caster)
{
    float matrix[4][4];
    R_RotateForEntity(ent, matrix);
    memcpy(caster.transform.data(), matrix, sizeof(matrix));

    // Leave the bounds unknown if the model is drawn through shadow proxies.
    auto pWorldModel = R_GetWorldSurfaceModel(ent->model);
    if (!pWorldModel || !pWorldModel->m_pShadowProxyDraws.empty())
        return;

    ShadowCache::Bounds local;
    local.Add({ent->model->mins[0], ent->model->mins[1], ent->model->mins[2]});
    local.Add({ent->model->maxs[0], ent->model->maxs[1], ent->model->maxs[2]});
    caster.bounds = ShadowCache::TransformBounds(local, &matrix[0][0]);
}

static void R_ComputeStudioCasterBounds(cl_entity_t* ent, ShadowCache::Caster& caster)
{
    // Run the normal Studio draw path so the bone cache is used as usual.
    // Events are suppressed and StudioRenderFinal is replaced by
    // R_CollectStudioShadowBounds, so nothing is drawn.
    (*currententity)          = ent;
    s_pCollectingCaster       = &caster;
    s_bCollectingBoundsFailed = false;

    R_DrawCurrentEntity(false);

    s_pCollectingCaster = nullptr;

    if (s_bCollectingBoundsFailed)
        caster.bounds.valid = false;
}

static void R_PrepareShadowCasters()
{
    s_shadowCasters.clear();

    if (!r_drawentities->value || (*r_refdef.onlyClientDraws))
        return;

    const auto savedEntity        = (*currententity);
    const auto savedStudioHeader  = (*pstudiohdr);
    const auto savedSubmodel      = (*psubmodel);
    const auto savedShadowView    = r_draw_shadowview;
    const auto savedNoFrustumCull = r_draw_nofrustumcull;

    r_draw_shadowview    = true;
    r_draw_nofrustumcull = true;

    for (int i = 0; i < (*cl_numvisedicts); ++i)
    {
        auto ent = cl_visedicts[i];

        if (!ent || !ent->model)
            continue;

        if (ent->curstate.rendermode != kRenderNormal)
            continue;

        if (ent->model->type == mod_sprite)
            continue;

        if (R_IsViewmodelAttachment(ent))
            continue;

        ShadowCache::Caster caster;
        caster.id    = reinterpret_cast<uintptr_t>(ent);
        caster.model = reinterpret_cast<uintptr_t>(ent->model);
        caster.body  = ent->curstate.body;
        caster.skin  = ent->curstate.skin;

        // Studio poses and animated brush textures can change without any
        // entity state changing, so every caster stays volatile for now
        // (volatileGeometry defaults to true).
        if (ent->model->type == mod_brush)
        {
            R_ComputeBrushCasterBounds(ent, caster);
        }
        else if (ent->model->type == mod_studio)
        {
            // Followers (attached models) and entities with renderfx keep
            // unknown bounds, which are never culled.
            if (ent->curstate.movetype != MOVETYPE_FOLLOW && ent->curstate.renderfx == 0)
                R_ComputeStudioCasterBounds(ent, caster);
        }

        if (caster.bounds.valid)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                caster.bounds.mins[axis] -= SHADOW_CASTER_BOUNDS_MARGIN;
                caster.bounds.maxs[axis] += SHADOW_CASTER_BOUNDS_MARGIN;
            }
        }

        s_shadowCasters.push_back(caster);
    }

    (*currententity)     = savedEntity;
    (*pstudiohdr)        = savedStudioHeader;
    (*psubmodel)         = savedSubmodel;
    r_draw_shadowview    = savedShadowView;
    r_draw_nofrustumcull = savedNoFrustumCull;
}

// True when the current shadow pass reuses cached depth, so the world and
// cached entities do not need to be drawn.
static bool R_IsReusingCachedShadow()
{
    return s_pCachedShadowLight && !s_pCachedShadowLight->shadowCache.Dirty();
}

bool R_ShouldDrawCachedShadowEntity(cl_entity_t* ent)
{
    if (!s_pCachedShadowLight)
        return true;

    const auto& cache = s_pCachedShadowLight->shadowCache;
    if (!cache.Dirty())
        return false;

    if (!cache.Contains(reinterpret_cast<uintptr_t>(ent)))
        return false;

    ++s_ShadowCacheStats.submissions;
    return true;
}

/*
    Called right before ClientDLL_DrawNormalTriangles in the shadow pass.

    Client triangles are not cached because there is no way to tell when they
    change. The cache texture only ever holds world + entity depth, and the
    client draws on top of it every frame. Copying the cache back also clears
    whatever the client drew in the previous frame.
*/
void R_FinishShadowCasterPass(void)
{
    if (!s_pCachedShadowLight)
        return;

    auto& light   = *s_pCachedShadowLight;
    auto  pOutput = light.pDynamicShadowTexture;
    auto  pCached = light.pShadowCasterCache;

    if (light.shadowCache.Dirty())
        ++s_ShadowCacheStats.updates;
    else
        ++s_ShadowCacheStats.reused;

    ShadowCache::FinishCasterDepth(light.shadowCache,
                                   pOutput->GetDepthTexture(),
                                   pCached->GetDepthTexture(),
                                   pOutput->IsCubemap(),
                                   pOutput->GetTextureSize());

    // Entities drawn by the client callback itself must not be filtered.
    s_pCachedShadowLight = nullptr;
}

class CBaseShadowTexture : public IShadowTexture
{
public:
    CBaseShadowTexture(uint32_t size, bool bStatic) : m_size(size), m_bStatic(bStatic)
    {
    }

    ~CBaseShadowTexture()
    {
        if (m_depthtex)
        {
            gEngfuncs.Con_DPrintf("CBaseShadowTexture: delete m_depthtex [%d].\n", m_depthtex);
            GL_DeleteTexture(m_depthtex);
            m_depthtex = 0;
        }
    }

    bool IsReady() const override
    {
        return m_ready;
    }

    void SetReady(bool bReady) override
    {
        m_ready = bReady;
    }

    bool IsCascaded() const override
    {
        return false;
    }

    bool IsCubemap() const override
    {
        return false;
    }

    bool IsStatic() const override
    {
        return m_bStatic;
    }

    GLuint GetDepthTexture() const override
    {
        return m_depthtex;
    }

    uint32_t GetTextureSize() const override
    {
        return m_size;
    }

    void SetViewport(float x, float y, float w, float h) override
    {
        m_viewport[0] = x;
        m_viewport[1] = y;
        m_viewport[2] = w;
        m_viewport[3] = h;
    }
    const float* GetViewport() const override
    {
        return m_viewport;
    }

    void SetCSMDistance(int cascadedIndex, float distance) override
    {
    }

    float GetCSMDistance(int cascadedIndex) const override
    {
        return 0;
    }

protected:
    GLuint   m_depthtex{};
    uint32_t m_size{};
    float    m_viewport[4]{};
    bool     m_ready{};
    bool     m_bStatic{};
};

class CSingleShadowTexture : public CBaseShadowTexture
{
public:
    CSingleShadowTexture(uint32_t size, bool bStatic) : CBaseShadowTexture(size, bStatic)
    {
        m_depthtex = GL_GenShadowTexture(size, size, true);
    }

    bool IsSingleLayer() const override
    {
        return true;
    }

    void SetWorldMatrix(int index, const mat4* mat) override
    {
        memcpy(m_worldmatrix, mat, sizeof(mat4));
    }
    void SetProjectionMatrix(int index, const mat4* mat) override
    {
        memcpy(m_projmatrix, mat, sizeof(mat4));
    }
    void SetShadowMatrix(int index, const mat4* mat) override
    {
        memcpy(m_shadowmatrix, mat, sizeof(mat4));
    }

    const mat4* GetWorldMatrix(int index) const override
    {
        return &m_worldmatrix;
    }
    const mat4* GetProjectionMatrix(int index) const override
    {
        return &m_projmatrix;
    }
    const mat4* GetShadowMatrix(int index) const override
    {
        return &m_shadowmatrix;
    }

private:
    mat4 m_worldmatrix{};
    mat4 m_projmatrix{};
    mat4 m_shadowmatrix{};
};

class CCascadedShadowTexture : public CBaseShadowTexture
{
public:
    CCascadedShadowTexture(uint32_t size, bool bStatic) : CBaseShadowTexture(size, bStatic)
    {
        // Use texture array for CSM: size x size x 4 layers
        m_depthtex = GL_GenShadowTextureArray(size, size, CSM_LEVELS, true);
    }

    bool IsCascaded() const override
    {
        return true;
    }

    bool IsSingleLayer() const override
    {
        return false;
    }

    void SetWorldMatrix(int index, const mat4* mat) override
    {
        memcpy(&m_worldmatrix[index], mat, sizeof(mat4));
    }
    void SetProjectionMatrix(int index, const mat4* mat) override
    {
        memcpy(&m_projmatrix[index], mat, sizeof(mat4));
    }
    void SetShadowMatrix(int index, const mat4* mat) override
    {
        memcpy(&m_shadowmatrix[index], mat, sizeof(mat4));
    }

    const mat4* GetWorldMatrix(int index) const override
    {
        return &m_worldmatrix[index];
    }
    const mat4* GetProjectionMatrix(int index) const override
    {
        return &m_projmatrix[index];
    }
    const mat4* GetShadowMatrix(int index) const override
    {
        return &m_shadowmatrix[index];
    }

    void SetCSMDistance(int index, float distance) override
    {
        m_csmDistances[index] = distance;
    }

    float GetCSMDistance(int index) const override
    {
        return m_csmDistances[index];
    }

private:
    float m_csmDistances[CSM_LEVELS]{};
    mat4  m_worldmatrix[CSM_LEVELS]{};
    mat4  m_projmatrix[CSM_LEVELS]{};
    mat4  m_shadowmatrix[CSM_LEVELS]{};
};

class CCubemapShadowTexture : public CBaseShadowTexture
{
public:
    CCubemapShadowTexture(uint32_t size, bool bStatic) : CBaseShadowTexture(size, bStatic)
    {
        m_depthtex = GL_GenCubemapShadowTexture(size, size, true);
    }

    bool IsCubemap() const override
    {
        return true;
    }

    bool IsSingleLayer() const override
    {
        return false;
    }

    void SetWorldMatrix(int index, const mat4* mat) override
    {
        memcpy(&m_worldmatrix[index], mat, sizeof(mat4));
    }
    void SetProjectionMatrix(int index, const mat4* mat) override
    {
        memcpy(&m_projmatrix[index], mat, sizeof(mat4));
    }
    void SetShadowMatrix(int index, const mat4* mat) override
    {
        memcpy(&m_shadowmatrix[index], mat, sizeof(mat4));
    }

    const mat4* GetWorldMatrix(int index) const override
    {
        return &m_worldmatrix[index];
    }
    const mat4* GetProjectionMatrix(int index) const override
    {
        return &m_projmatrix[index];
    }
    const mat4* GetShadowMatrix(int index) const override
    {
        return &m_shadowmatrix[index];
    }

private:
    mat4 m_worldmatrix[6]{};
    mat4 m_projmatrix[6]{};
    mat4 m_shadowmatrix[6]{};
};

int StudioGetSequenceActivityType(model_t* mod, entity_state_t* entstate)
{
    if (mod->type != mod_studio)
        return 0;

    auto studiohdr = (studiohdr_t*)IEngineStudio.Mod_Extradata(mod);

    if (!studiohdr)
        return 0;

    int sequence = entstate->sequence;
    if (sequence >= studiohdr->numseq)
        return 0;

    auto pseqdesc = (mstudioseqdesc_t*)((byte*)studiohdr + studiohdr->seqindex) + sequence;

    if (
        pseqdesc->activity == ACT_DIESIMPLE ||
        pseqdesc->activity == ACT_DIEBACKWARD ||
        pseqdesc->activity == ACT_DIEFORWARD ||
        pseqdesc->activity == ACT_DIEVIOLENT ||
        pseqdesc->activity == ACT_DIEVIOLENT ||
        pseqdesc->activity == ACT_DIE_HEADSHOT ||
        pseqdesc->activity == ACT_DIE_CHESTSHOT ||
        pseqdesc->activity == ACT_DIE_GUTSHOT ||
        pseqdesc->activity == ACT_DIE_BACKSHOT)
    {
        return 1;
    }

    if (
        pseqdesc->activity == ACT_BARNACLE_HIT ||
        pseqdesc->activity == ACT_BARNACLE_PULL ||
        pseqdesc->activity == ACT_BARNACLE_CHOMP ||
        pseqdesc->activity == ACT_BARNACLE_CHEW)
    {
        return 2;
    }

    return 0;
}

std::shared_ptr<IShadowTexture> R_CreateSingleShadowTexture(uint32_t size, bool bStatic)
{
    return std::make_shared<CSingleShadowTexture>(size, bStatic);
}

std::shared_ptr<IShadowTexture> R_CreateCascadedShadowTexture(uint32_t size, bool bStatic)
{
    return std::make_shared<CCascadedShadowTexture>(size, bStatic);
}

std::shared_ptr<IShadowTexture> R_CreateCubemapShadowTexture(uint32_t size, bool bStatic)
{
    return std::make_shared<CCubemapShadowTexture>(size, bStatic);
}

void R_InitShadow(void)
{
    r_shadow = gEngfuncs.pfnRegisterVariable("r_shadow", "1", FCVAR_ARCHIVE | FCVAR_CLIENTDLL);

    // 0 = off, 1 = on, 2 = on and print statistics about once a second
    r_shadow_cache = gEngfuncs.pfnRegisterVariable("r_shadow_cache", "1", FCVAR_ARCHIVE | FCVAR_CLIENTDLL);
}

void R_ShutdownShadow(void)
{
}

bool R_ShouldRenderShadow(void)
{
    if (R_IsRenderingShadowView())
        return false;

    if (R_IsRenderingWaterView())
        return false;

    if (R_IsRenderingPortal())
        return false;

    if (gPrivateFuncs.CL_IsDevOverviewMode())
        return false;

    return r_shadow->value ? true : false;
}

bool R_ShouldCastShadow(cl_entity_t* ent)
{
    if (!ent)
        return false;

    if (!ent->model)
        return false;

    if (ent->curstate.rendermode != kRenderNormal)
        return false;

    if (ent->model->type == mod_studio)
    {
        if (ent->curstate.effects & EF_NODRAW)
            return false;

        //player model always render shadow
        if (!strcmp(ent->model->name, "models/player.mdl"))
            return true;

        if (ent->player)
            return true;

        //BulletPhysics ragdoll corpse
        if (ent->curstate.iuser4 == PhyCorpseFlag)
            return true;

        if (ent->index == 0)
            return false;

        if (ent->curstate.movetype == MOVETYPE_NONE && ent->curstate.solid == SOLID_NOT)
            return false;

        if (g_iEngineType == ENGINE_SVENGINE)
        {
            if (ent->curstate.effects & EF_NOSHADOW)
                return false;
        }

        return true;
    }

    return false;
}

void R_SetupShadowMatrix(float out[4][4], const float worldMatrix[4][4], const float projMatrix[4][4])
{
    /*
	Counterpart of following matrix:
		const float bias[16] = {
				0.5f, 0.0f, 0.0f, 0.0f,
				0.0f, 0.5f, 0.0f, 0.0f,
				0.0f, 0.0f, 0.5f, 0.0f,
				0.5f, 0.5f, 0.5f, 1.0f
			};
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();
		glLoadIdentity();
		glLoadMatrixf(bias);
		glMultMatrixf(offsetMatrix); // CSM offset matrix
		glMultMatrixf(r_projection_matrix);
		glMultMatrixf(r_world_matrix);
		glGetFloatv(GL_TEXTURE_MATRIX, (float *)shadowmatrix);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
	*/

    const float bias[16] = {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.5f, 0.5f, 0.5f, 1.0f};

    // First multiply projection matrix with world matrix
    float projWorldMatrix[4][4];
    Matrix4x4_Multiply(projWorldMatrix, worldMatrix, projMatrix);

    // Then multiply bias matrix with the result
    Matrix4x4_Multiply(out, projWorldMatrix, (const float (*)[4])bias);
}

static ShadowCache::Light R_GetShadowCacheKey(const CDynamicLight& light)
{
    ShadowCache::Light key;

    key.origin       = {light.origin[0], light.origin[1], light.origin[2]};
    key.spot         = (light.type == DynamicLightType_Spot);
    key.range        = key.spot ? light.distance : light.size;
    key.size         = light.dynamic_shadow_size;
    key.staticSize   = light.static_shadow_size;
    key.worldEnabled = !(*r_refdef.onlyClientDraws);

    if (key.spot)
    {
        AngleVectors(light.angles, key.forward.data(), key.right.data(), key.up.data());
        key.coneTangent = tanf(light.coneAngle);
    }

    // The source entity is hidden only in the spotlight pass and in the point
    // light pass that has a static layer, see R_RenderShadowmapForDynamicLights.
    if (light.source_entity_index && (key.spot || key.staticSize > 0))
    {
        auto sourceEntity = gEngfuncs.GetEntityByIndex(light.source_entity_index);
        key.source        = reinterpret_cast<uintptr_t>(sourceEntity);
    }

    return key;
}

static bool R_IsShadowCacheEnabled()
{
    return r_shadow_cache->value && glCopyImageSubData;
}

static void R_BeginShadowCasterCache(CDynamicLight* light)
{
    s_pCachedShadowLight = nullptr;

    if (!light || !R_IsShadowCacheEnabled())
        return;

    auto  pOutput = light->pDynamicShadowTexture;
    auto& pCached = light->pShadowCasterCache;

    const bool bCubemap = pOutput->IsCubemap();
    const auto size     = pOutput->GetTextureSize();

    if (!pCached || pCached->IsCubemap() != bCubemap || pCached->GetTextureSize() != size)
    {
        if (bCubemap)
            pCached = R_CreateCubemapShadowTexture(size, false);
        else
            pCached = R_CreateSingleShadowTexture(size, false);

        // The new texture holds no depth yet. Prepare() already ran this frame
        // and may have decided to reuse the cache, so force a redraw.
        light->shadowCache.Invalidate();
        light->shadowCache.Prepare(R_GetShadowCacheKey(*light), s_shadowCasters, true);
    }

    if (pCached)
        s_pCachedShadowLight = light;
}

void R_RenderShadowmapForDynamicLights(void)
{
    if (!R_CanRenderGBuffer())
        return;

    if (R_ShouldRenderShadow())
    {
        GL_BeginDebugGroup("R_RenderShadowmapForDynamicLights");

        const auto PointLightCallback = [](PointLightCallbackArgs* args, void* context) {
            if (args->ppStaticShadowTexture && args->staticShadowSize > 0)
            {
                if ((*args->ppStaticShadowTexture) == nullptr ||
                    (*args->ppStaticShadowTexture)->IsCubemap() != true ||
                    (*args->ppStaticShadowTexture)->IsStatic() != true ||
                    (*args->ppStaticShadowTexture)->GetTextureSize() != args->staticShadowSize)
                {
                    (*args->ppStaticShadowTexture) = R_CreateCubemapShadowTexture(args->staticShadowSize, true);
                }

                if ((*args->ppStaticShadowTexture) && !(*args->ppStaticShadowTexture)->IsReady())
                {
                    r_draw_shadowview    = true;
                    r_draw_multiview     = true;
                    r_draw_nofrustumcull = true;
                    r_draw_lineardepth   = true;

                    const auto& pCurrentShadowTexture = (*args->ppStaticShadowTexture);

                    pCurrentShadowTexture->SetViewport(0, 0, pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());

                    GL_BeginDebugGroup("PointlightStaticShadowPass");

                    GL_BindFrameBufferWithTextures(&s_ShadowFBO, 0, 0, pCurrentShadowTexture->GetDepthTexture(), pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());
                    glDrawBuffer(GL_NONE);
                    glReadBuffer(GL_NONE);

                    GL_ClearDepthStencil(1.0f, STENCIL_MASK_NONE, STENCIL_MASK_ALL);

                    R_PushRefDef();

                    R_SetViewport(
                        pCurrentShadowTexture->GetViewport()[0],
                        pCurrentShadowTexture->GetViewport()[1],
                        pCurrentShadowTexture->GetViewport()[2],
                        pCurrentShadowTexture->GetViewport()[3]);

                    // Calculate 6 faces for cubemap shadow mapping
                    // OpenGL cubemap face order: +X, -X, +Y, -Y, +Z, -Z
                    const vec3_t cubemapAngles[] = {
                        {0, 0, 90},
                        {0, 180, 270},
                        {0, 90, 0},
                        {0, 270, 180},
                        {-90, 90, 0},
                        {90, 270, 0},
                    };

                    camera_ubo_t CameraUBO{};

                    CameraUBO.numViews = 6;

                    for (int i = 0; i < 6; ++i)
                    {
                        VectorCopy(args->origin, (*r_refdef.vieworg));
                        VectorCopy(cubemapAngles[i], (*r_refdef.viewangles));
                        R_UpdateRefDef();

                        R_LoadIdentityForProjectionMatrix();
                        R_SetupPerspective(90, 90, 0.1f, args->radius);

                        R_LoadIdentityForWorldMatrix();
                        R_SetupPlayerViewWorldMatrix((*r_refdef.vieworg), (*r_refdef.viewangles));

                        R_SetFrustum(90, 90, r_frustum_right, r_frustum_top);

                        auto worldMatrix = (float (*)[4][4])R_GetWorldMatrix();
                        auto projMatrix  = (float (*)[4][4])R_GetProjectionMatrix();

                        mat4 shadowMatrix;
                        R_SetupShadowMatrix(shadowMatrix, (*worldMatrix), (*projMatrix));

                        pCurrentShadowTexture->SetWorldMatrix(i, worldMatrix);
                        pCurrentShadowTexture->SetProjectionMatrix(i, projMatrix);
                        pCurrentShadowTexture->SetShadowMatrix(i, &shadowMatrix);

                        R_SetupCameraView(&CameraUBO.views[i]);
                    }

                    GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hCameraUBO, 0, sizeof(CameraUBO), &CameraUBO);

                    bool bAnyPolyRendered = false;

                    {
                        auto old_brush_polys = (*c_brush_polys);
                        (*c_brush_polys)     = 0;

                        auto old_draw_classify = r_draw_classify;
                        r_draw_classify        = DRAW_CLASSIFY_WORLD;

                        if (args->sourceEntityIndex != 0)
                        {
                            r_draw_hide_entity       = true;
                            r_draw_hide_entity_index = args->sourceEntityIndex;
                        }

                        R_RenderScene();

                        bAnyPolyRendered = (*c_brush_polys) > 0 ? true : false;

                        r_draw_hide_entity = false;
                        r_draw_classify    = old_draw_classify;
                        (*c_brush_polys)   = old_brush_polys;
                    }

                    R_PopRefDef();

                    r_draw_shadowview    = false;
                    r_draw_multiview     = false;
                    r_draw_nofrustumcull = false;
                    r_draw_lineardepth   = false;

                    GL_EndDebugGroup();

                    pCurrentShadowTexture->SetReady(bAnyPolyRendered);
                }
            }

            if (args->ppDynamicShadowTexture && args->dynamicShadowSize > 0)
            {
                if ((*args->ppDynamicShadowTexture) == nullptr ||
                    (*args->ppDynamicShadowTexture)->IsCubemap() != true ||
                    (*args->ppDynamicShadowTexture)->IsStatic() != false ||
                    (*args->ppDynamicShadowTexture)->GetTextureSize() != args->dynamicShadowSize)
                {
                    (*args->ppDynamicShadowTexture) = R_CreateCubemapShadowTexture(args->dynamicShadowSize, false);
                }

                if ((*args->ppDynamicShadowTexture) && !(*args->ppDynamicShadowTexture)->IsReady())
                {
                    r_draw_shadowview    = true;
                    r_draw_multiview     = true;
                    r_draw_nofrustumcull = true;
                    r_draw_lineardepth   = true;

                    const auto& pCurrentShadowTexture = (*args->ppDynamicShadowTexture);

                    R_BeginShadowCasterCache(args->light);

                    pCurrentShadowTexture->SetViewport(0, 0, pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());

                    GL_BeginDebugGroup("PointlightDynamicShadowPass");

                    GL_BindFrameBufferWithTextures(&s_ShadowFBO, 0, 0, pCurrentShadowTexture->GetDepthTexture(), pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());
                    glDrawBuffer(GL_NONE);
                    glReadBuffer(GL_NONE);

                    GL_ClearDepthStencil(1.0f, STENCIL_MASK_NONE, STENCIL_MASK_ALL);

                    R_PushRefDef();

                    R_SetViewport(
                        pCurrentShadowTexture->GetViewport()[0],
                        pCurrentShadowTexture->GetViewport()[1],
                        pCurrentShadowTexture->GetViewport()[2],
                        pCurrentShadowTexture->GetViewport()[3]);

                    // Calculate 6 faces for cubemap shadow mapping
                    // OpenGL cubemap face order: +X, -X, +Y, -Y, +Z, -Z
                    const vec3_t cubemapAngles[] = {
                        {0, 0, 90},
                        {0, 180, 270},
                        {0, 90, 0},
                        {0, 270, 180},
                        {-90, 90, 0},
                        {90, 270, 0},
                    };

                    camera_ubo_t CameraUBO{};

                    CameraUBO.numViews = 6;

                    for (int i = 0; i < 6; ++i)
                    {
                        VectorCopy(args->origin, (*r_refdef.vieworg));
                        VectorCopy(cubemapAngles[i], (*r_refdef.viewangles));
                        R_UpdateRefDef();

                        R_LoadIdentityForProjectionMatrix();
                        R_SetupPerspective(90, 90, 0.1f, args->radius);

                        R_LoadIdentityForWorldMatrix();
                        R_SetupPlayerViewWorldMatrix((*r_refdef.vieworg), (*r_refdef.viewangles));

                        R_SetFrustum(90, 90, r_frustum_right, r_frustum_top);

                        auto worldMatrix = (float (*)[4][4])R_GetWorldMatrix();
                        auto projMatrix  = (float (*)[4][4])R_GetProjectionMatrix();

                        mat4 shadowMatrix;
                        R_SetupShadowMatrix(shadowMatrix, (*worldMatrix), (*projMatrix));

                        pCurrentShadowTexture->SetWorldMatrix(i, worldMatrix);
                        pCurrentShadowTexture->SetProjectionMatrix(i, projMatrix);
                        pCurrentShadowTexture->SetShadowMatrix(i, &shadowMatrix);

                        R_SetupCameraView(&CameraUBO.views[i]);
                    }

                    GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hCameraUBO, 0, sizeof(CameraUBO), &CameraUBO);

                    //Only draw non-world stuffs when we have static shadow
                    if (args->staticShadowSize > 0)
                    {
                        auto old_draw_classify = r_draw_classify;
                        r_draw_classify        = DRAW_CLASSIFY_OPAQUE_ENTITIES;

                        if (args->sourceEntityIndex != 0)
                        {
                            r_draw_hide_entity       = true;
                            r_draw_hide_entity_index = args->sourceEntityIndex;
                        }

                        R_RenderScene();

                        r_draw_hide_entity = false;

                        r_draw_classify = old_draw_classify;
                    }
                    else
                    {
                        auto old_draw_classify = r_draw_classify;
                        r_draw_classify        = DRAW_CLASSIFY_WORLD | DRAW_CLASSIFY_OPAQUE_ENTITIES;
                        if (R_IsReusingCachedShadow())
                            r_draw_classify &= ~DRAW_CLASSIFY_WORLD;

                        R_RenderScene();

                        r_draw_classify = old_draw_classify;
                    }

                    R_PopRefDef();

                    r_draw_shadowview    = false;
                    r_draw_multiview     = false;
                    r_draw_nofrustumcull = false;
                    r_draw_lineardepth   = false;

                    GL_EndDebugGroup();

                    pCurrentShadowTexture->SetReady(true);
                }
            }
        };

        const auto SpotLightCallback = [](SpotLightCallbackArgs* args, void* context) {
            if (args->ppDynamicShadowTexture && args->dynamicShadowSize > 0)
            {
                if ((*args->ppDynamicShadowTexture) == nullptr ||
                    (*args->ppDynamicShadowTexture)->IsSingleLayer() != true ||
                    (*args->ppDynamicShadowTexture)->IsStatic() != false ||
                    (*args->ppDynamicShadowTexture)->GetTextureSize() != args->dynamicShadowSize)
                {
                    (*args->ppDynamicShadowTexture) = R_CreateSingleShadowTexture(args->dynamicShadowSize, false);
                }

                if ((*args->ppDynamicShadowTexture) && !(*args->ppDynamicShadowTexture)->IsReady())
                {
                    r_draw_shadowview  = true;
                    r_draw_multiview   = true;
                    r_draw_lineardepth = true;

                    const auto& pCurrentShadowTexture = (*args->ppDynamicShadowTexture);

                    R_BeginShadowCasterCache(args->light);

                    pCurrentShadowTexture->SetViewport(0, 0, pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());

                    GL_BeginDebugGroup("DrawSpotlightDynamicShadowPass");

                    GL_BindFrameBufferWithTextures(&s_ShadowFBO, 0, 0, pCurrentShadowTexture->GetDepthTexture(), pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());
                    glDrawBuffer(GL_NONE);
                    glReadBuffer(GL_NONE);

                    GL_ClearDepthStencil(1.0f, STENCIL_MASK_NONE, STENCIL_MASK_ALL);

                    R_PushRefDef();

                    VectorCopy(args->origin, (*r_refdef.vieworg));
                    VectorCopy(args->angles, (*r_refdef.viewangles));
                    R_UpdateRefDef();

                    R_SetViewport(
                        pCurrentShadowTexture->GetViewport()[0],
                        pCurrentShadowTexture->GetViewport()[1],
                        pCurrentShadowTexture->GetViewport()[2],
                        pCurrentShadowTexture->GetViewport()[3]);

                    R_LoadIdentityForWorldMatrix();
                    R_SetupPlayerViewWorldMatrix((*r_refdef.vieworg), (*r_refdef.viewangles));

                    float cone_fov = args->coneAngle * 2 * 360 / (M_PI * 2);

                    R_LoadIdentityForProjectionMatrix();
                    R_SetupPerspective(cone_fov, cone_fov, 0.1f, args->distance);

                    R_SetFrustum(r_xfov_currentpass, r_yfov_currentpass, r_frustum_right, r_frustum_top);

                    auto worldMatrix = (float (*)[4][4])R_GetWorldMatrix();
                    auto projMatrix  = (float (*)[4][4])R_GetProjectionMatrix();

                    mat4 shadowMatrix;
                    R_SetupShadowMatrix(shadowMatrix, (*worldMatrix), (*projMatrix));

                    pCurrentShadowTexture->SetWorldMatrix(0, worldMatrix);
                    pCurrentShadowTexture->SetProjectionMatrix(0, projMatrix);
                    pCurrentShadowTexture->SetShadowMatrix(0, &shadowMatrix);

                    camera_ubo_t CameraUBO;
                    R_SetupCameraView(&CameraUBO.views[0]);
                    CameraUBO.numViews = 1;
                    GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hCameraUBO, 0, sizeof(CameraUBO), &CameraUBO);

                    {
                        auto old_draw_classify = r_draw_classify;
                        r_draw_classify &= ~DRAW_CLASSIFY_TRANS_ENTITIES;
                        r_draw_classify &= ~DRAW_CLASSIFY_PARTICLES;
                        r_draw_classify &= ~DRAW_CLASSIFY_DECAL;
                        r_draw_classify &= ~DRAW_CLASSIFY_WATER;
                        if (R_IsReusingCachedShadow())
                            r_draw_classify &= ~DRAW_CLASSIFY_WORLD;

                        if (args->sourceEntityIndex != 0)
                        {
                            r_draw_hide_entity       = true;
                            r_draw_hide_entity_index = args->sourceEntityIndex;
                        }

                        R_RenderScene();

                        r_draw_hide_entity = false;

                        r_draw_classify = old_draw_classify;
                    }

                    R_PopRefDef();

                    r_draw_multiview   = false;
                    r_draw_shadowview  = false;
                    r_draw_lineardepth = false;

                    GL_EndDebugGroup();

                    pCurrentShadowTexture->SetReady(true);
                }
            }
        };

        const auto DirectionalLightCallback = [](DirectionalLightCallbackArgs* args, void* context) {
            if (args->ppStaticShadowTexture && args->staticShadowSize > 0)
            {
                if ((*args->ppStaticShadowTexture) == nullptr ||
                    (*args->ppStaticShadowTexture)->IsSingleLayer() != true ||
                    (*args->ppStaticShadowTexture)->IsStatic() != true ||
                    (*args->ppStaticShadowTexture)->GetTextureSize() != args->staticShadowSize)
                {
                    (*args->ppStaticShadowTexture) = R_CreateSingleShadowTexture(args->staticShadowSize, true);
                }

                if ((*args->ppStaticShadowTexture) && !(*args->ppStaticShadowTexture)->IsReady())
                {
                    auto pWorldSurfaceModel = R_GetWorldSurfaceModel(*(cl_worldmodel));

                    r_draw_shadowview    = true;
                    r_draw_multiview     = true;
                    r_draw_nofrustumcull = true;

                    const auto& pCurrentShadowTexture = (*args->ppStaticShadowTexture);

                    pCurrentShadowTexture->SetViewport(0, 0, pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());

                    GL_BeginDebugGroup("DrawDirectionalLightStaticShadow");

                    GL_BindFrameBufferWithTextures(&s_ShadowFBO, 0, 0, pCurrentShadowTexture->GetDepthTexture(), pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());
                    glDrawBuffer(GL_NONE);
                    glReadBuffer(GL_NONE);

                    GL_ClearDepthStencil(1.0f, STENCIL_MASK_NONE, STENCIL_MASK_ALL);

                    R_PushRefDef();

                    VectorCopy(args->origin, (*r_refdef.vieworg));
                    VectorCopy(args->angles, (*r_refdef.viewangles));
                    R_UpdateRefDef();

                    R_SetViewport(
                        pCurrentShadowTexture->GetViewport()[0],
                        pCurrentShadowTexture->GetViewport()[1],
                        pCurrentShadowTexture->GetViewport()[2],
                        pCurrentShadowTexture->GetViewport()[3]);

                    R_LoadIdentityForWorldMatrix();
                    R_SetupPlayerViewWorldMatrix((*r_refdef.vieworg), (*r_refdef.viewangles));

                    // Set up orthographic projection for this cascade
                    float orthoSize = args->size; // Increase size for further cascades

                    R_LoadIdentityForProjectionMatrix();
                    R_SetupOrthoProjectionMatrix(-orthoSize / 2, orthoSize / 2, -orthoSize / 2, orthoSize / 2, 2048, -2048, true);

                    r_ortho            = true;
                    r_frustum_right    = 0;
                    r_frustum_top      = 0;
                    r_znear            = 2048;
                    r_zfar             = -2048;
                    r_xfov_currentpass = 0;
                    r_yfov_currentpass = 0;

                    auto worldMatrix = (float (*)[4][4])R_GetWorldMatrix();
                    auto projMatrix  = (float (*)[4][4])R_GetProjectionMatrix();

                    mat4 shadowMatrix;
                    R_SetupShadowMatrix(shadowMatrix, (*worldMatrix), (*projMatrix));

                    pCurrentShadowTexture->SetWorldMatrix(0, worldMatrix);
                    pCurrentShadowTexture->SetProjectionMatrix(0, projMatrix);
                    pCurrentShadowTexture->SetShadowMatrix(0, &shadowMatrix);

                    camera_ubo_t CameraUBO;
                    R_SetupCameraView(&CameraUBO.views[0]);
                    CameraUBO.numViews = 1;
                    GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hCameraUBO, 0, sizeof(CameraUBO), &CameraUBO);

                    bool bAnyPolyRendered = false;

                    {
                        auto old_brush_polys = (*c_brush_polys);
                        (*c_brush_polys)     = 0;

                        auto old_draw_classify = r_draw_classify;
                        r_draw_classify        = DRAW_CLASSIFY_WORLD;

                        R_RenderScene();

                        bAnyPolyRendered = (*c_brush_polys) > 0 ? true : false;

                        r_draw_classify  = old_draw_classify;
                        (*c_brush_polys) = old_brush_polys;
                    }

                    R_PopRefDef();

                    r_draw_shadowview    = false;
                    r_draw_multiview     = false;
                    r_draw_nofrustumcull = false;

                    GL_EndDebugGroup();

                    pCurrentShadowTexture->SetReady(bAnyPolyRendered);
                }
            }

            if (args->ppDynamicShadowTexture)
            {
                // Allocate dynamicShadowSize x dynamicShadowSize CSM texture if not already allocated
                if ((*args->ppDynamicShadowTexture) == nullptr ||
                    (*args->ppDynamicShadowTexture)->IsCascaded() != true ||
                    (*args->ppDynamicShadowTexture)->IsStatic() != false ||
                    (*args->ppDynamicShadowTexture)->GetTextureSize() != args->dynamicShadowSize)
                {
                    (*args->ppDynamicShadowTexture) = R_CreateCascadedShadowTexture(args->dynamicShadowSize, false);
                }

                if ((*args->ppDynamicShadowTexture) && !(*args->ppDynamicShadowTexture)->IsReady())
                {
                    const auto& pCurrentShadowTexture = (*args->ppDynamicShadowTexture);

                    r_draw_shadowview    = true;
                    r_draw_multiview     = true;
                    r_draw_nofrustumcull = true;

                    const float lambda      = args->csmLambda;        // 例如0.8，也可来自cvar
                    const float orthoMargin = 1.0f + args->csmMargin; // 外扩，避免裁边

                    // Calculate cascade distances based on camera frustum
                    // These could be configurable via cvars in the future
                    float nearPlane = R_GetMainViewNearPlane(); // Should match r_nearclip or similar, 4.0 by default
                    float farPlane  = R_GetMainViewFarPlane();  // Should match r_farclip or similar, 8192.0 by default

                    float xfov = 0, yfov = 0;
                    R_CalcMainViewFov(xfov, yfov);

                    float tanHalfFovY = tanf(0.5f * yfov * (M_PI / 360.0));
                    float tanHalfFovX = tanf(0.5f * xfov * (M_PI / 360.0));

                    float splits[CSM_LEVELS + 1]{};
                    splits[0] = nearPlane;

                    // Use logarithmic distribution for cascades
                    for (int i = 1; i <= CSM_LEVELS; ++i)
                    {
                        float si    = (float)i / (float)CSM_LEVELS; // [0,1]
                        float d_lin = nearPlane + (farPlane - nearPlane) * si;
                        float d_log = nearPlane * powf(farPlane / nearPlane, si);
                        splits[i]   = d_lin * (1.0f - lambda) + d_log * lambda;
                    }

                    for (int i = 0; i < CSM_LEVELS; ++i)
                    {
                        float csmFar = splits[i + 1];
                        pCurrentShadowTexture->SetCSMDistance(i, csmFar);
                    }

                    pCurrentShadowTexture->SetViewport(0, 0, pCurrentShadowTexture->GetTextureSize(), pCurrentShadowTexture->GetTextureSize());

                    GL_BeginDebugGroup("DrawDirectionalLightDynamicCSM");

                    GL_BindFrameBuffer(&s_ShadowFBO);

                    // Bind texture array layers to framebuffer - we'll use geometry shader to select layer
                    // Note: We can't use glFramebufferTexture because that requires all layers,
                    // but clearing needs to be done per-layer in a loop
                    for (int i = 0; i < CSM_LEVELS; ++i)
                    {
                        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, pCurrentShadowTexture->GetDepthTexture(), 0, i);
                        GL_ClearDepthStencil(1.0f, STENCIL_MASK_NONE, STENCIL_MASK_ALL);
                    }

                    // Now bind all layers for rendering
                    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, pCurrentShadowTexture->GetDepthTexture(), 0);

                    glDrawBuffer(GL_NONE);
                    glReadBuffer(GL_NONE);

                    R_PushRefDef();

                    // All cascades use same viewangles and vieworg
                    VectorCopy(args->angles, (*r_refdef.viewangles));
                    R_UpdateRefDef();

                    // All cascades use same worldmatrix
                    R_LoadIdentityForWorldMatrix();
                    R_SetupPlayerViewWorldMatrix((*r_refdef.vieworg), (*r_refdef.viewangles));

                    R_SetViewport(
                        pCurrentShadowTexture->GetViewport()[0],
                        pCurrentShadowTexture->GetViewport()[1],
                        pCurrentShadowTexture->GetViewport()[2],
                        pCurrentShadowTexture->GetViewport()[3]);

                    // Setup camera UBO with all cascade views
                    camera_ubo_t CameraUBO;
                    CameraUBO.numViews = CSM_LEVELS;

                    // Calculate projection matrices for all cascades and setup shadow matrices
                    for (int cascadeIndex = 0; cascadeIndex < CSM_LEVELS; ++cascadeIndex)
                    {
                        float splitNear = splits[cascadeIndex + 0];
                        float splitFar  = splits[cascadeIndex + 1];

                        // 该级联在相机视锥上界面的半宽/半高（取far端，因为更大）
                        float halfW_far = splitFar * tanHalfFovX;
                        float halfH_far = splitFar * tanHalfFovY;

                        // 该级联厚度的一半
                        float halfDepth = 0.5f * (splitFar - splitNear);

                        // 用包含该截头棱锥的最小球近似，半径为到far平面角点的最大距离
                        // 与光方向无关，稳定且不会裁边
                        float radius = sqrtf(halfW_far * halfW_far + halfH_far * halfH_far + halfDepth * halfDepth);

                        // 正交投影尺寸（正方形），加一点margin避免抖动时裁边
                        float orthoSize = radius * orthoMargin;

                        R_LoadIdentityForProjectionMatrix();
                        R_SetupOrthoProjectionMatrix(-orthoSize, orthoSize, -orthoSize, orthoSize, 2048, -2048, true);

                        r_ortho            = true;
                        r_frustum_right    = 0;
                        r_frustum_top      = 0;
                        r_znear            = 2048;
                        r_zfar             = -2048;
                        r_xfov_currentpass = 0;
                        r_yfov_currentpass = 0;

                        auto worldMatrix = (float (*)[4][4])R_GetWorldMatrix();
                        auto projMatrix  = (float (*)[4][4])R_GetProjectionMatrix();

                        mat4 shadowMatrix;
                        R_SetupShadowMatrix(shadowMatrix, (*worldMatrix), (*projMatrix));

                        pCurrentShadowTexture->SetWorldMatrix(cascadeIndex, worldMatrix);
                        pCurrentShadowTexture->SetProjectionMatrix(cascadeIndex, projMatrix);
                        pCurrentShadowTexture->SetShadowMatrix(cascadeIndex, &shadowMatrix);

                        // Setup camera view for this cascade in the UBO
                        R_SetupCameraView(&CameraUBO.views[cascadeIndex]);
                    }

                    // Upload all views to UBO
                    GL_UploadSubDataToUBO(g_WorldSurfaceRenderer.hCameraUBO, 0, sizeof(CameraUBO), &CameraUBO);

                    {
                        auto old_draw_classify = r_draw_classify;
                        r_draw_classify        = (DRAW_CLASSIFY_OPAQUE_ENTITIES);

                        // Render all cascades in a single draw call using multiview geometry shader
                        R_RenderScene();

                        r_draw_classify = old_draw_classify;
                    }

                    R_PopRefDef();

                    r_draw_shadowview    = false;
                    r_draw_multiview     = false;
                    r_draw_nofrustumcull = false;

                    GL_EndDebugGroup();

                    pCurrentShadowTexture->SetReady(true);
                }
            }
        };

        R_IterateVisibleDynamicLights(PointLightCallback, SpotLightCallback, DirectionalLightCallback, nullptr);

        GL_EndDebugGroup();
    }
}

/*

	Purpose : Reset shadow textures as unprepared so we will clear and render it this frame later.
*/

static bool R_HasVisibleLocalShadowLight()
{
    for (const auto& entry : g_VisibleDynamicLights)
    {
        const auto& light = entry.m_pDynamicLight;

        if (light && light->type != DynamicLightType_Directional && light->shadow > 0 && light->dynamic_shadow_size > 0)
            return true;
    }

    return false;
}

static bool R_WorldHasAnimatedTextures()
{
    auto worldmodel = (*cl_worldmodel);
    if (!worldmodel)
        return false;

    for (int i = 0; i < worldmodel->numtextures; ++i)
    {
        auto texture = worldmodel->textures[i];

        if (texture && texture->anim_total)
            return true;
    }

    return false;
}

static void R_PrepareShadowCacheForLight(CDynamicLight& light, bool bCacheEnabled, bool bAnimatedWorld)
{
    auto key = R_GetShadowCacheKey(light);

    // The static shadow layer has no cache of its own, but it also has to be
    // redrawn whenever the light moves or changes shape.
    const bool bProjectionChanged = !light.shadowProjectionValid || !(light.shadowProjection == key);
    if (bProjectionChanged && light.pStaticShadowTexture)
        light.pStaticShadowTexture->SetReady(false);

    light.shadowProjection      = key;
    light.shadowProjectionValid = true;

    // World depth is cached together with entities unless a static layer
    // holds it, so animated world textures force a redraw in that case.
    const bool bWorldInCache = key.spot || key.staticSize == 0;

    bool bForceUpdate = false;
    if (!bCacheEnabled)
        bForceUpdate = true;
    else if ((*r_refdef.onlyClientDraws))
        bForceUpdate = true;
    else if (bAnimatedWorld && bWorldInCache)
        bForceUpdate = true;

    light.shadowCache.Prepare(key, s_shadowCasters, bForceUpdate);

    if (!bCacheEnabled)
        light.shadowCache.Invalidate();
}

void R_ResetShadowTextures(void)
{
    s_ShadowCacheStats = {};

    const auto prepareStart  = std::chrono::steady_clock::now();
    const bool bCacheEnabled = R_IsShadowCacheEnabled();

    if (bCacheEnabled && R_HasVisibleLocalShadowLight())
        R_PrepareShadowCasters();

    const bool bAnimatedWorld = R_WorldHasAnimatedTextures();

    for (size_t i = 0; i < g_VisibleDynamicLights.size(); ++i)
    {
        auto& entry = g_VisibleDynamicLights[i];

        if (entry.m_pDynamicLight && entry.m_pDynamicLight->type != DynamicLightType_Directional)
        {
            R_PrepareShadowCacheForLight(*entry.m_pDynamicLight, bCacheEnabled, bAnimatedWorld);
        }

        if (entry.m_pDynamicLight && entry.m_pDynamicLight->pDynamicShadowTexture)
        {
            entry.m_pDynamicLight->pDynamicShadowTexture->SetReady(false);
        }
    }

    const auto prepareTime                 = std::chrono::steady_clock::now() - prepareStart;
    s_ShadowCacheStats.prepareMilliseconds = std::chrono::duration<double, std::milli>(prepareTime).count();
}

static void R_ReportShadowCacheStats()
{
    static double lastReportTime = -1;

    // cl_time can go backwards, e.g. after a level change.
    if ((*cl_time) >= lastReportTime && (*cl_time) - lastReportTime < 1)
        return;

    lastReportTime = (*cl_time);

    gEngfuncs.Con_Printf("Shadow cache: updated %d, reused %d, caster submissions %d, bone boxes %d, prepare %.3f ms\n",
                         s_ShadowCacheStats.updates,
                         s_ShadowCacheStats.reused,
                         s_ShadowCacheStats.submissions,
                         s_ShadowCacheStats.boneBoxes,
                         s_ShadowCacheStats.prepareMilliseconds);
}

/*

	Purpose : Rendering textures for shadow mapping

*/

void R_RenderShadowMap(void)
{
    if (R_ShouldRenderShadow() && R_CanRenderGBuffer())
    {
        R_ResetShadowTextures();
        R_RenderShadowmapForDynamicLights();

        if (r_shadow_cache->value >= 2)
            R_ReportShadowCacheStats();
    }
}
