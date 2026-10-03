[Back to README](../../README.md) | [中文](../zh-CN/installation.md)

# Installation

`install/x86/<Debug|Release>/`:

```text
svencoop/
  metahook/plugins/Renderer.dll
  metahook/plugins/Renderer.pdb
  metahook/dlls/FreeImage/FreeImage.dll   (Debug: FreeImaged.dll)
  metahook/gamedata/renderer/            (Renderer's own gamedata json)
  renderer/                           (shaders, textures, config and localization resources)
```

Merge the contents under the `svencoop/` folder into the mod directory, for example
`valve/` or `cstrike/`, and enable `Renderer.dll` in MetaHook's
`metahook/configs/plugins.lst`.

Then you can launch the game through MetaHook. i.e. `MetaHook.exe -game <mod>`.