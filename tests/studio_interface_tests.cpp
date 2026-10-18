#include "../src/gl_local.h"
#include "../src/studio_interface.inl"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

private_funcs_t gPrivateFuncs{};
mh_dll_info_t   g_EngineDLLInfo{}, g_ClientDLLInfo{};
metahook_api_t* g_pMetaHookAPI{};
DWORD           g_dwEngineBuildnum = 10210;

static std::vector<std::string> clientSymbols, engineSymbols;
static bool                     missingClientSymbol{};

static mh_gamesymbol_status_t ResolveSymbol(PVOID module, const char* name, mh_gamesymbol_kind_t kind, PVOID* address)
{
    if (module == g_ClientDLLInfo.ImageBase)
    {
        assert(MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION == kind);
        clientSymbols.emplace_back(name);
        if (missingClientSymbol)
            return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
    }
    else
    {
        assert(g_EngineDLLInfo.ImageBase == module);
        assert(MH_GAMESYMBOL_KIND_FUNCTION == kind);
        engineSymbols.emplace_back(name);
    }
    *address = (PVOID)0x1234;
    return MH_GAMESYMBOL_OK;
}

static void Fail(const char*, ...)
{
    throw std::runtime_error("missing required studio symbol");
}

int main()
{
    metahook_api_t api{};
    api.ResolveGameSymbol         = ResolveSymbol;
    api.GetGameSymbolStatusString = [](mh_gamesymbol_status_t) {
        return "missing";
    };
    api.SysError              = Fail;
    g_pMetaHookAPI            = &api;
    g_EngineDLLInfo.ImageBase = (PVOID)0x10000;
    g_EngineDLLInfo.ImageSize = 0x1000;
    g_ClientDLLInfo.ImageBase = (PVOID)0x20000;

    r_studio_interface_t studio{};
    auto                 studioInterface = &studio;

    //A client implementation requires every virtual, even if another plugin
    //has supplied only part of the client catalog.
    studio.StudioDrawModel = (decltype(studio.StudioDrawModel))0x20000;
    ClientStudio_FillAddress(&studioInterface);
    assert(7 == clientSymbols.size());
    assert(7 == engineSymbols.size());
    assert(nullptr != gPrivateFuncs.GameStudioRenderer_StudioRenderModel);

    //Engine interfaces must not query client gamedata or retain stale client
    //entry points from an earlier client. Check both ends of the engine image.
    for (ULONG_PTR address : {0x10000u, 0x10fffu})
    {
        clientSymbols.clear();
        engineSymbols.clear();
        missingClientSymbol    = true;
        studio.StudioDrawModel = (decltype(studio.StudioDrawModel))address;
        ClientStudio_FillAddress(&studioInterface);
        assert(clientSymbols.empty());
        assert(7 == engineSymbols.size());
        assert(nullptr == gPrivateFuncs.GameStudioRenderer_StudioDrawModel);
        assert(nullptr == gPrivateFuncs.GameStudioRenderer_StudioDrawPlayer);
        assert(nullptr == gPrivateFuncs.GameStudioRenderer_StudioRenderModel);
        assert(nullptr == gPrivateFuncs.GameStudioRenderer_StudioRenderFinal);
        assert(nullptr == gPrivateFuncs.GameStudioRenderer_StudioSetupBones);
        assert(nullptr == gPrivateFuncs.GameStudioRenderer_StudioSaveBones);
        assert(nullptr == gPrivateFuncs.GameStudioRenderer_StudioMergeBones);
    }

    //Addresses just outside the engine are not an engine-renderer fallback.
    for (ULONG_PTR address : {0xffffu, 0x11000u})
    {
        studio.StudioDrawModel = (decltype(studio.StudioDrawModel))address;
        bool failed            = false;
        try
        {
            ClientStudio_FillAddress(&studioInterface);
        }
        catch (const std::runtime_error&)
        {
            failed = true;
        }
        assert(failed);
    }
    std::puts("studio_interface_tests passed");
}
