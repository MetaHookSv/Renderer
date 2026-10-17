// Internal implementation shared by gl_studio.cpp and the CPU regression tests.
static std::unordered_map<int, CStudioSkinCache*> g_StudioSkinCache;

static cache_user_t model_texture_cache[MAX_KNOWN_MODELS_SVENGINE][MAX_SKINS];

void R_StudioFlushAllSkins()
{
    for (auto& itor : g_StudioSkinCache)
    {
        delete itor.second;
    }

    g_StudioSkinCache.clear();
}

void R_StudioFlushSkins(int keynum)
{
    const auto& itor = g_StudioSkinCache.find(keynum);

    if (itor != g_StudioSkinCache.end())
    {
        auto pSkinCache = itor->second;

        if (pSkinCache)
        {
            for (int index = 0; index < MAX_SKINS; index++)
            {
                pSkinCache->skins[index].model = NULL;
            }
        }
    }
}

skin_t* R_StudioGetSkin(int keynum, int index)
{
    // MAX_SKINS limits the remap cache, not the model's texture table.
    if (index < 0 || index >= MAX_SKINS)
        return nullptr;

    const auto& itor = g_StudioSkinCache.find(keynum);

    if (itor != g_StudioSkinCache.end())
    {
        return &itor->second->skins[index];
    }
    else
    {
        auto pSkinCache = new (std::nothrow) CStudioSkinCache;

        if (pSkinCache)
        {
            memset(pSkinCache, 0, sizeof(CStudioSkinCache));

            pSkinCache->skins[index].keynum      = keynum;
            pSkinCache->skins[index].topcolor    = -1;
            pSkinCache->skins[index].bottomcolor = -1;
            g_StudioSkinCache[keynum]            = pSkinCache;

            return &pSkinCache->skins[index];
        }
    }

    return nullptr;
}

byte* R_StudioReloadSkin(model_t* pModel, int index, skin_t* pskin)
{
    if (index < 0 || index >= MAX_SKINS)
        return nullptr;

    int                    modelindex;
    cache_user_t*          pCache;
    model_texture_cache_t* pData;

    byte* pbase;
    int   size;

    modelindex = pModel - mod_known;

    if (modelindex < 0 || modelindex >= EngineGetMaxKnownModel())
        return NULL;

    pCache = &model_texture_cache[modelindex][index];

    if (Cache_Check(pCache))
    {
        pData = (model_texture_cache_t*)pCache->data;
    }
    else
    {
        int fileLength = 0;

        pbase = gEngfuncs.COM_LoadFile(pModel->name, 5, &fileLength);

        if (!pbase)
            return NULL;

        auto studiohdr = (studiohdr_t*)pbase;

        if (fileLength < sizeof(studiohdr_t))
        {
            gEngfuncs.COM_FreeFile(pbase);
            return NULL;
        }

        auto ptexture = (mstudiotexture_t*)((byte*)studiohdr + studiohdr->textureindex);

        ptexture += index;

        if ((byte*)ptexture < (byte*)studiohdr)
        {
            gEngfuncs.COM_FreeFile(pbase);
            return NULL;
        }

        if ((byte*)ptexture + sizeof(mstudiotexture_t) > (byte*)studiohdr + fileLength)
        {
            gEngfuncs.COM_FreeFile(pbase);
            return NULL;
        }

        size = ptexture->height * ptexture->width;

        if (pbase + ptexture->index < pbase)
        {
            gEngfuncs.COM_FreeFile(pbase);
            return NULL;
        }

        if (pbase + ptexture->index + size > pbase + fileLength)
        {
            gEngfuncs.COM_FreeFile(pbase);
            return NULL;
        }

        Cache_Alloc(pCache, size + 768 + offsetof(model_texture_cache_t, data), pskin->name);

        pData         = (model_texture_cache_t*)pCache->data;
        pData->width  = ptexture->width;
        pData->height = ptexture->height;
        memcpy(pData->data, pbase + ptexture->index, size + 768);

        gEngfuncs.COM_FreeFile(pbase);
    }

    pskin->index  = index;
    pskin->width  = pData->width;
    pskin->height = pData->height;
    return pData->data;
}
