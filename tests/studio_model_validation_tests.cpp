#include <cassert>
#include <cstdio>
#include <cstdlib>

// Compile the production remap cache with engine file/cache I/O stubbed.
#include "../src/gl_local.h"
#include "../src/studio_model_validation.h"
#include "../src/studio_skin_cache.inl"

cl_enginefunc_t gEngfuncs{};
model_t*        mod_known{};

static int  cacheChecks{}, fileLoads{};
static bool cacheAllowed{};

int EngineGetMaxKnownModel() { return 2; }

void* Cache_Check(cache_user_t* cache)
{
    assert(cacheAllowed);
    ++cacheChecks;
    return cache->data;
}

void* Cache_Alloc(cache_user_t* cache, int size, const char*)
{
    cache->data = std::calloc(1, size);
    assert(cache->data);
    return cache->data;
}

struct TextureModelFixture
{
    studiohdr_t      header{};
    mstudiotexture_t textures[99]{};
    byte             pixels[4 + 768]{};

    explicit TextureModelFixture(int numTextures)
    {
        header.numtextures  = numTextures;
        header.textureindex = offsetof(TextureModelFixture, textures);
        header.length       = sizeof(*this);
        for (auto& texture : textures)
        {
            texture.width  = 2;
            texture.height = 2;
            texture.index  = offsetof(TextureModelFixture, pixels);
        }
        pixels[0] = 7;
    }
};

static TextureModelFixture* diskModel{};

static byte* LoadFile(const char*, int, int* length)
{
    ++fileLoads;
    assert(diskModel);
    *length = sizeof(*diskModel);
    return (byte*)diskModel;
}

static void TestRemapCaches()
{
    model_t models[2]{};
    mod_known = models;
    strcpy(models[0].name, "models/test.mdl");
    gEngfuncs.COM_LoadFile = LoadFile;
    gEngfuncs.COM_FreeFile = [](void*) {
    };

    // Invalid requests must not allocate a skin cache or alias slot zero.
    constexpr int key = 1;
    for (int index : {-1, MAX_SKINS, 98})
    {
        assert(nullptr == R_StudioGetSkin(key, index));
        assert(g_StudioSkinCache.empty());
        skin_t skin{};
        assert(nullptr == R_StudioReloadSkin(&models[0], index, &skin));
        assert(0 == cacheChecks);
        assert(0 == fileLoads);
    }

    auto slotZero      = R_StudioGetSkin(key, 0);
    slotZero->model    = &models[1];
    slotZero->gl_index = 2000;
    for (int index : {-1, MAX_SKINS, 98})
        assert(nullptr == R_StudioGetSkin(key, index));
    assert(slotZero == R_StudioGetSkin(key, 0));
    assert(&models[1] == slotZero->model);
    assert(2000 == slotZero->gl_index);
    R_StudioFlushAllSkins();

    for (int numTextures : {32, 33, 34, 99})
    {
        TextureModelFixture fixture(numTextures);
        diskModel              = &fixture;
        const short skinrefs[] = {31, 32, 98, -1, (short)numTextures};
        for (int meshSkinref = 0; meshSkinref < 5; ++meshSkinref)
        {
            const int index    = skinrefs[meshSkinref];
            const int resolved = StudioResolveTextureIndex(skinrefs, 5, numTextures, meshSkinref);
            assert((index >= 0 && index < numTextures ? index : -1) == resolved);
            if (resolved < 0)
                continue;

            const int previousChecks = cacheChecks;
            const int previousLoads  = fileLoads;
            auto      skin           = R_StudioGetSkin(key, resolved);
            if (resolved >= MAX_SKINS)
            {
                // A legal texture can be ineligible for remapping. The caller
                // keeps its original texture when this lookup returns null.
                assert(nullptr == skin);
                assert(g_StudioSkinCache.empty());
                skin_t unused{};
                assert(nullptr == R_StudioReloadSkin(&models[0], resolved, &unused));
                assert(previousChecks == cacheChecks);
                assert(previousLoads == fileLoads);
                continue;
            }

            assert(skin);
            cacheAllowed = true;
            auto pixels  = R_StudioReloadSkin(&models[0], resolved, skin);
            assert(pixels);
            assert(7 == pixels[0]);
            assert(31 == skin->index);
            assert(2 == skin->width);
            assert(2 == skin->height);
            assert(previousChecks + 1 == cacheChecks);
            assert(previousLoads + 1 == fileLoads);
            assert(pixels == R_StudioReloadSkin(&models[0], resolved, skin));
            assert(previousChecks + 2 == cacheChecks);
            assert(previousLoads + 1 == fileLoads);
            cacheAllowed = false;

            std::free(model_texture_cache[0][resolved].data);
            model_texture_cache[0][resolved] = {};
            R_StudioFlushAllSkins();
        }
    }
    diskModel = nullptr;
}

int main()
{
    const short skins[] = {0, 1, -1, 2};
    assert(0 == StudioResolveTextureIndex(skins, 4, 2, 0));
    assert(1 == StudioResolveTextureIndex(skins, 4, 2, 1));
    assert(-1 == StudioResolveTextureIndex(skins, 4, 2, -1));
    assert(-1 == StudioResolveTextureIndex(skins, 4, 2, 4));
    assert(-1 == StudioResolveTextureIndex(skins, 4, 2, 2));
    assert(-1 == StudioResolveTextureIndex(skins, 4, 2, 3));
    assert(-1 == StudioResolveTextureIndex(nullptr, 4, 2, 0));
    assert(-1 == StudioResolveTextureIndex(skins, 0, 2, 0));
    assert(-1 == StudioResolveTextureIndex(skins, 4, 0, 0));

    // Issue #863: the replacement has one skinref, but the body uses 0..25.
    const short errorSkin[] = {0};
    for (int skinref = 1; skinref <= 25; ++skinref)
        assert(-1 == StudioResolveTextureIndex(errorSkin, 1, 1, skinref));
    assert(0 == StudioResolveTextureIndex(errorSkin, 1, 1, 0));

    assert(StudioShouldReplaceMissingTextureModel(true, 0, "models/error.mdl"));
    assert(!StudioShouldReplaceMissingTextureModel(false, 0, "models/error.mdl"));
    assert(!StudioShouldReplaceMissingTextureModel(true, 244, "models/error.mdl"));
    assert(!StudioShouldReplaceMissingTextureModel(true, 0, "models/exampleT.mdl"));
    assert(!StudioShouldReplaceMissingTextureModel(true, 0, nullptr));
    TestRemapCaches();
    std::puts("Studio model validation tests passed.");
}
