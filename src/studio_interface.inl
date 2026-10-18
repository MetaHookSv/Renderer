void ClientStudio_FillAddress(struct r_studio_interface_s** ppinterface)
{
    auto clientBase = g_ClientDLLInfo.ImageBase;
    auto engineBase = g_EngineDLLInfo.ImageBase;

    //Clients such as Opposing Force keep the engine's studio interface. Only
    //resolve client virtuals when the returned interface uses a client renderer.
    auto drawModel          = (ULONG_PTR)(*ppinterface)->StudioDrawModel;
    auto engineStart        = (ULONG_PTR)engineBase;
    bool usesEngineRenderer = drawModel >= engineStart && drawModel - engineStart < g_EngineDLLInfo.ImageSize;

    gPrivateFuncs.GameStudioRenderer_StudioDrawModel   = nullptr;
    gPrivateFuncs.GameStudioRenderer_StudioDrawPlayer  = nullptr;
    gPrivateFuncs.GameStudioRenderer_StudioRenderModel = nullptr;
    gPrivateFuncs.GameStudioRenderer_StudioRenderFinal = nullptr;
    gPrivateFuncs.GameStudioRenderer_StudioSetupBones  = nullptr;
    gPrivateFuncs.GameStudioRenderer_StudioSaveBones   = nullptr;
    gPrivateFuncs.GameStudioRenderer_StudioMergeBones  = nullptr;

    if (!usesEngineRenderer)
    {
        //Client CGameStudioRenderer vtable and virtual functions resolved from gamedata

        gPrivateFuncs.GameStudioRenderer_StudioDrawModel = (decltype(gPrivateFuncs.GameStudioRenderer_StudioDrawModel))
            GamedataResolvePtr(clientBase, "client", "GameStudioRenderer_StudioDrawModel", MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION);
        gPrivateFuncs.GameStudioRenderer_StudioDrawPlayer = (decltype(gPrivateFuncs.GameStudioRenderer_StudioDrawPlayer))
            GamedataResolvePtr(clientBase, "client", "GameStudioRenderer_StudioDrawPlayer", MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION);

        gPrivateFuncs.GameStudioRenderer_StudioRenderModel = (decltype(gPrivateFuncs.GameStudioRenderer_StudioRenderModel))
            GamedataResolvePtr(clientBase, "client", "GameStudioRenderer_StudioRenderModel", MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION);
        gPrivateFuncs.GameStudioRenderer_StudioRenderFinal = (decltype(gPrivateFuncs.GameStudioRenderer_StudioRenderFinal))
            GamedataResolvePtr(clientBase, "client", "GameStudioRenderer_StudioRenderFinal", MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION);
        gPrivateFuncs.GameStudioRenderer_StudioSetupBones = (decltype(gPrivateFuncs.GameStudioRenderer_StudioSetupBones))
            GamedataResolvePtr(clientBase, "client", "GameStudioRenderer_StudioSetupBones", MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION);
        gPrivateFuncs.GameStudioRenderer_StudioSaveBones = (decltype(gPrivateFuncs.GameStudioRenderer_StudioSaveBones))
            GamedataResolvePtr(clientBase, "client", "GameStudioRenderer_StudioSaveBones", MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION);
        gPrivateFuncs.GameStudioRenderer_StudioMergeBones = (decltype(gPrivateFuncs.GameStudioRenderer_StudioMergeBones))
            GamedataResolvePtr(clientBase, "client", "GameStudioRenderer_StudioMergeBones", MH_GAMESYMBOL_KIND_VIRTUAL_FUNCTION);
    }

    //Engine Studio render pipeline resolved from gamedata
    gPrivateFuncs.R_StudioDrawModel = (decltype(gPrivateFuncs.R_StudioDrawModel))
        GamedataResolvePtr(engineBase, "engine", "R_StudioDrawModel", MH_GAMESYMBOL_KIND_FUNCTION);
    gPrivateFuncs.R_StudioDrawPlayer = (decltype(gPrivateFuncs.R_StudioDrawPlayer))
        GamedataResolvePtr(engineBase, "engine", "R_StudioDrawPlayer", MH_GAMESYMBOL_KIND_FUNCTION);
    gPrivateFuncs.R_StudioRenderModel = (decltype(gPrivateFuncs.R_StudioRenderModel))
        GamedataResolvePtr(engineBase, "engine", "R_StudioRenderModel", MH_GAMESYMBOL_KIND_FUNCTION);
    gPrivateFuncs.R_StudioRenderFinal = (decltype(gPrivateFuncs.R_StudioRenderFinal))
        GamedataResolvePtr(engineBase, "engine", "R_StudioRenderFinal", MH_GAMESYMBOL_KIND_FUNCTION);
    gPrivateFuncs.R_StudioSetupBones = (decltype(gPrivateFuncs.R_StudioSetupBones))
        GamedataResolvePtr(engineBase, "engine", "R_StudioSetupBones", MH_GAMESYMBOL_KIND_FUNCTION);
    gPrivateFuncs.R_StudioMergeBones = (decltype(gPrivateFuncs.R_StudioMergeBones))
        GamedataResolvePtr(engineBase, "engine", "R_StudioMergeBones", MH_GAMESYMBOL_KIND_FUNCTION);
    gPrivateFuncs.R_StudioSaveBones = (decltype(gPrivateFuncs.R_StudioSaveBones))
        GamedataResolvePtr(engineBase, "engine", "R_StudioSaveBones", MH_GAMESYMBOL_KIND_FUNCTION);
}
