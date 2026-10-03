[返回 README](../../README.md) | [English](../en/build-instruction.md)

# 构建说明

本页涵盖 Renderer 插件的构建、依赖与 CI 细节。安装目录布局与启用插件见[安装说明](installation.md)；引擎/GPU 兼容性、功能特性和控制台参数见[功能说明](features.md)。

Renderer 是 MetaHook Renderer （内部名称MetaRenderer） 插件的 Windows x86 CMake 工程。

Renderer 自身的公共接口位于 `include/Interface/IMetaRenderer.h`

## 依赖要求

- Windows、Visual Studio 2022 的 C++ 桌面开发工具和 Windows SDK
- CMake 3.21+
- Git
- 首次配置需要网络：CMake 通过 FetchContent 获取 MetaHook、FreeImage 和 GLEW（宿主未自带 Capstone 时也包括 Capstone），初始化其他所需 submodule，并下载、校验和解压 VC-LTL 5.3.1
- 与目标配置对应的 MetaHook 构建，用于提供 `SDL2_INCLUDE_DIRS` 目录（见下文）

## 构建

```bat
scripts\build-Renderer-x86-Debug.bat "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Debug/include"
scripts\build-Renderer-x86-Release.bat "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

两个入口均使用 `Visual Studio 17 2022 -A Win32`，执行 configure、build 和 install。
可从工程外调用；未设置 `SolutionDir` 时由脚本位置定位工程。初始化、下载、配置、编译或安装
失败返回非零退出码。构建目录为 `build/x86/Debug` 和 `build/x86/Release`。
本工程提供普通 Debug、Release，不提供 AVX2 入口。

## 手动指定源码路径

MetaHook、FreeImage 和 GLEW 默认自动下载固定版本。需要复用本地源码时，可按需传入以下可选参数：

| 参数 | 本地源码目录 |
| --- | --- |
| `METAHOOK_SOURCE_PATH` | MetaHook 仓库根目录，包含 `include/metahook.h` 及 HLSDK、SourceSDK、VGUI 源码 |
| `FREEIMAGE_SOURCE_PATH` | FreeImage_clone 根目录，包含 `CMakeLists.txt` 和 `Source/FreeImage.h` |
| `GLEW_SOURCE_PATH` | glew-cmake 根目录，包含 `CMakeLists.txt` 和 `include/GL/glew.h`，提供 `libglew_static` |

先构建对应配置的 MetaHook，再将必填的 `SDL2_INCLUDE_DIRS` 设为其安装目录下的 `include`
目录，其中应包含 `SDL2/SDL_video.h`。Renderer 仅使用 SDL 头文件，SDL 的构建和安装由 MetaHook 负责。

例如，使用本地源码构建 Release：

```bat
scripts\build-Renderer-x86-Release.bat ^
  "-DMETAHOOK_SOURCE_PATH=D:/MetaHook" ^
  "-DFREEIMAGE_SOURCE_PATH=D:/FreeImage_clone" ^
  "-DGLEW_SOURCE_PATH=D:/glew-cmake" ^
  "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

可选头文件路径：

- `CAPSTONE_INCLUDE_DIRS`：包含 `capstone.h` 或 `capstone/capstone.h` 的目录。
  头文件版本必须与宿主 MetaHook 使用的 Capstone 一致。默认使用 MetaHook 自带的副本，
  缺失时自动下载固定版本，无需指定 Capstone 库路径。
- `SDL3_INCLUDE_DIRS`：包含 `SDL3/SDL.h` 的目录，通常无需设置。

以上路径也可在首次配置时通过同名环境变量设置。修改已缓存的值请使用 `-D参数名=值`；
将源码路径或 Capstone 参数设为空值（`-D参数名=`）可恢复自动获取或默认查找。
头文件路径支持分号分隔的多个目录，请将整个 `-D` 参数放在引号中。

## gamedata

Renderer 通过 `scripts/manifests/renderer.json`（与 MetaHook 同一 schema）声明它解析的引擎/客户端私有符号（含按 gameVersion 生效的条件组与 module 归属）。

构建时 `RENDERER_SYNC_GAMEDATA`（默认 `ON`）调用 `scripts/sync-gamedata.py`，把上游 catalog 裁剪为仅含这些符号并发布到 `metahook/gamedata/renderer/`，随后 `scripts/validate-gamedata.py` 按 manifest 校验

原始快照持久缓存在 `build/x86/<config>/gamedata-sync/` 并支持离线构建。

`OFF` 时只安装已有数据，不下载。该嵌套目录被宿主 launcher 的 catalog 加载器合并。

## 回归验证

四组既有测试随源码迁入，通过可选 CTest 目标运行：

```bat
scripts\build-Renderer-x86-Release.bat -DRENDERER_BUILD_TESTS=ON "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
ctest --test-dir build/x86/Release -C Release --output-on-failure
```
