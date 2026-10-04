# MetaRenderer

[中文文档](README.zh-CN.md)

MetaRenderer is a graphics enhancement plugin for MetaHookSv. It replaces the original
GoldSrc renderer with a modern OpenGL pipeline, greatly improving rendering performance
and adding HDR, deferred shading, dynamic lights and shadows, a water shader, studio
model celshade and more.

* A GPU supporting the OpenGL 4.4 Core Profile is required.
* It is not compatible with ReShade.

## Compatibility

|        Engine               |      |
|        ----                 | ---- |
| GoldSrc_blob   (3248~4554)  | √    |
| GoldSrc_legacy (4554~6153)  | √    |
| GoldSrc_new    (8684 ~)     | √    |
| SvEngine       (8832 ~)     | √    |
| GoldSrc_HL25   (>= 9884)    | √    |
| GoldSrc_CoF    (5936)       | √    |

## Quick start

Download `Renderer-windows-x86.7z` from
[GitHub Releases](https://github.com/MetaHookSv/Renderer/releases) (built on `v*` tag pushes).

Merge the extracted `svencoop/` into the target mod directory and enable Renderer in
MetaHook's `metahook/configs/plugins.lst`. Don't forget to launch game from MetaHook.

## Documentation

- [Build instruction](docs/en/build-instruction.md)
- [Installation](docs/en/installation.md)
- [Features](docs/en/features.md)

## F5 debugging (optional)

Install MetaHook and enable this plugin in the game's `plugins.lst` first. Configure a standalone Visual Studio Win32 solution:

```powershell
cmake -S . -B build/launch -G "Visual Studio 17 2022" -A Win32 -DMETAHOOKSV_ENABLE_LAUNCH_GAME=ON
```

Open the solution, select **LaunchGame** and press **F5**. **DeployGame** builds this plugin and its dependencies, stages Install, and copies plugin DLLs/PDBs/resources before the native debugger starts the existing game launcher. Root launchers/runtime files and plugin lists remain unchanged. Set VS to build before running and **Do not launch** on build errors; stop the game before redeploying. Ordinary builds do not deploy.

`METAHOOKSV_GAME_DIRECTORY` defaults to Steam discovery; `METAHOOKSV_GAME_APPID` defaults to `225840`. Set `METAHOOKSV_GAME_MOD` for a custom mod and `METAHOOKSV_GAME_ARGUMENTS` for extra arguments. Debug and Release are supported.

The shared module uses `METAHOOKSV_LAUNCH_GAME_MODULE_DIR`, the surrounding MetaHookSv checkout, or a pinned source archive. Without Installer sources, it downloads the self-contained CLI from GitHub `latest` (no .NET required); `METAHOOKSV_INSTALLER_RELEASE` selects a fixed tag, and `METAHOOKSV_INSTALLER_CLI_EXECUTABLE` supplies an offline EXE. Plugin mode requires v20261004c or later. Valid caches under `build/launch/launch-game/installer/<release>` are reused without update checks; select another tag or clear that private cache to upgrade. `GH_TOKEN`/`GITHUB_TOKEN` may be supplied through the environment if GitHub API rate limits prevent the first download. The feature defaults OFF and performs no extra downloads when disabled.

## License

Licensed under the [MIT License](LICENSE); each dependency keeps its own license.
