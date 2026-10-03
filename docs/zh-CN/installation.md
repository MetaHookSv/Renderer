[返回 README](../../README.md) | [English](../en/installation.md)

# 安装说明

`install/x86/<Debug|Release>/`：

```text
svencoop/
  metahook/plugins/Renderer.dll
  metahook/plugins/Renderer.pdb
  metahook/dlls/FreeImage/FreeImage.dll   (Debug: FreeImaged.dll)
  metahook/gamedata/renderer/            (Renderer 自己的 gamedata json)
  renderer/                           (shaders、textures、配置和本地化资源)
```

将`svencoop/`文件夹下的内容合并到 mod 目录，比如`valve/`, `cstrike/`，并在 MetaHook 的 `metahook/configs/plugins.lst` 中启用 `Renderer.dll`。

然后你就可以从MetaHook启动游戏了。比如： `MetaHook.exe -game <mod>`。