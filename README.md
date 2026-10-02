# Renderer

MetaHook Renderer 插件的独立 Windows x86 CMake 工程。源码和运行资源迁自
`hzqst/MetaHookSv` 的 `11a852774b1725d02735aeb348c32a7bf454507c`，保留原插件 API、
渲染实现和着色器；公共 SDK 源码由 MetaHook 提供。

Renderer 自身的公共接口位于 `include/Interface/IMetaRenderer.h`，从 MetaHook 原样迁入，
构建优先使用本仓库副本，并安装到 `install/x86/<配置>/include/Interface/`。
其他插件使用此接口时，将 Renderer 的 `include/Interface` 加入头文件搜索路径；
其 `interface.h` 依赖仍由 MetaHook SDK 提供。来源仓库中的旧副本保留不变。

## 构建

需要 Windows、Visual Studio 2022 的 C++ 桌面开发工具和 Windows SDK、CMake 3.21+、Git。
首次配置需要网络，CMake 通过 FetchContent 获取 MetaHook、初始化其他所需 submodule，并下载、校验和解压 VC-LTL 5.3.1。
依赖准备不使用 PowerShell。VC-LTL 是二进制缓存，不是 submodule。

```bat
scripts\build-Renderer-x86-Debug.bat "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Debug/include"
scripts\build-Renderer-x86-Release.bat "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

两个入口均使用 `Visual Studio 17 2022 -A Win32`，执行 configure、build 和 install。
可从工程外调用；未设置 `SolutionDir` 时由脚本位置定位工程。初始化、下载、配置、编译或安装
失败返回非零退出码。构建目录为 `build/x86/Debug` 和 `build/x86/Release`。
本工程提供普通 Debug、Release，不提供 AVX2 入口。

## 手动指定源码路径

`METAHOOK_SOURCE_PATH` 指向包含 `include/metahook.h`、`include/SourceSDK/` 和
`include/vgui_controls/` 的 **MetaHook 仓库根目录**。
未指定时，使用 `FetchContent_Declare` / `FetchContent_MakeAvailable` 获取固定提交
`4d23b6fecd79dc949aabc2e145480cd1328d4a35`，默认放在当前 build 目录的
`_deps/renderer_metahook-src/`。只消费 SDK 文件，不添加 MetaHook 本体构建目标，
也不初始化它的 submodule。显式指定有效路径后，完全跳过 MetaHook 的 FetchContent 获取。
`FREEIMAGE_SOURCE_PATH` 指向包含 `CMakeLists.txt` 和 `Source/FreeImage.h` 的
**FreeImage 源码根目录**，需要提供 `FreeImage` shared CMake target。
已验证 `hzqst/FreeImage_clone` 的下述固定提交；原始无 CMake 支持的 FreeImage 压缩包不适用。

```bat
scripts\build-Renderer-x86-Debug.bat "-DMETAHOOK_SOURCE_PATH=D:/MetaHook" "-DFREEIMAGE_SOURCE_PATH=D:/FreeImage_clone" "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Debug/include"
scripts\build-Renderer-x86-Release.bat "-DMETAHOOK_SOURCE_PATH=D:/MetaHook" "-DFREEIMAGE_SOURCE_PATH=D:/FreeImage_clone" "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

首次配置也可以设置同名环境变量。`-D` 显式参数优先于已有 CMake cache；环境变量只用于 cache
初始值。`-DMETAHOOK_SOURCE_PATH=` 切回 FetchContent 获取；`-DFREEIMAGE_SOURCE_PATH=` 切回 FreeImage submodule。
外部源码只作为输入，构建产物仍留在 Renderer 的 build 目录；不会构建 MetaHook.exe，
也不会初始化 MetaHook 的递归依赖。切换源码版本后建议使用新的 build 目录。

SDL2、SDL3 的构建和安装由 MetaHook 工程负责。先运行对应配置的 MetaHook build 脚本，
再通过 `SDL2_INCLUDE_DIRS` 传入其安装树的 `include` 目录（必须包含 `SDL2/SDL_video.h`）。
该参数必需；Renderer 不下载、构建、链接或安装 SDL，也不再保存 SDL submodule。
`SDL3_INCLUDE_DIRS` 可选，若设置应指向包含 `SDL3/SDL.h` 的 include 目录，例如
`-DSDL3_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include`。当前 Renderer 没有直接使用 SDL3 头文件。
两个参数都支持分号分隔的目录列表和首次配置的同名环境变量；无效路径在配置阶段报错。

Capstone 只需要头文件，可复用 MetaHook 已有的源码或安装目录：

```bat
scripts\build-Renderer-x86-Release.bat "-DCAPSTONE_INCLUDE_DIRS=D:/MetaHook/thirdparty/capstone_fork/include" "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

支持直接包含 `capstone.h` 的目录，或含 `capstone/capstone.h` 的上级 include 目录；
多个目录用分号分隔，并将整个 `-D` 参数放在引号中。指定后跳过 Renderer 自带 Capstone
submodule 的初始化；传 `-DCAPSTONE_INCLUDE_DIRS=` 恢复默认。首次配置也支持同名环境变量。
头文件应与宿主 MetaHook 使用的 Capstone 版本一致，因为回调传递其结构体。
Renderer 从不构建或链接 Capstone 库，`CAPSTONE_LIBRARY_DIRS` 无需传入；
若共用调用命令传入了它，配置时会明确提示忽略该参数。

直接调用 CMake：

```bat
cmake -S . -B build/x86/Release -G "Visual Studio 17 2022" -A Win32 -DCMAKE_INSTALL_PREFIX=install/x86/Release -DMETAHOOK_SOURCE_PATH=D:/MetaHook -DFREEIMAGE_SOURCE_PATH=D:/FreeImage_clone -DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include
cmake --build build/x86/Release --config Release --target install --parallel
```

## 安装与运行

`install/x86/<Debug|Release>/`：

```text
svencoop/
  metahook/plugins/Renderer.dll
  metahook/plugins/Renderer.pdb
  metahook/dlls/FreeImage/FreeImage.dll   (Debug: FreeImaged.dll)
  renderer/                           (shaders、textures、配置和本地化资源)
```

不自动复制到本机游戏目录。由使用者合并到对应 mod 目录，并在 MetaHook 的
`metahook/configs/plugins.lst` 中启用 Renderer。运行还需要宿主 MetaHook 及其有效 gamedata；
gamedata 由 MetaHook 工程负责同步。VGUI2Extension 是可选运行时插件，不在本工程内构建。

## 依赖与构建约定

| Submodule | 固定提交 | 用途 |
| --- | --- | --- |
| hzqst/FreeImage_clone | `c68700b9fe699dbbf99f88a611065f101cba1a41` | FreeImage DLL 及内置图像格式库 |
| hzqst/glew-cmake | `56ed32d4a929f993f0e6b7f905af9be4d38fda04` | GLEW 静态库 |
| hzqst/capstone | `e81e390f621ee59d14f70e16fe065dd00f78ee71` | 反汇编数据类型头文件 |
| SergiusTheBest/ScopeExit | `bd345da594a4675d04de663d93d00cb81b6678b2` | ScopeExit 头文件 |
| hzqst/tinyobjloader | `cab4ad7254cbf7eaaafdb73d272f99e92f166df8` | OBJ 读取源码 |

以上 submodule 沿用源仓库的 URL 和 SHA；URL 见 `.gitmodules`。MetaHook 使用 FetchContent，已从 submodule 列表移除。
无需 `git submodule update --recursive`；Renderer 仅初始化自己直接使用的依赖。
Capstone、SDL 不作为库链接到 Renderer，宿主接口和已有运行时加载逻辑保持原样。

VC-LTL 来自官方 `Chuyu-Team/VC-LTL5` v5.3.1 的 `VC-LTL-Binary.7z`，SHA-256：
`7a18799ed3aa84a225610a5447a56bc534c5c98ccb8dec05caba0e3f633431ad`。
缓存位于 `thirdparty/cache/`；可通过 `RENDERER_DEPENDENCY_CACHE_DIR` 设置首次配置的缓存目录。
使用包内 helper，统一应用于 Renderer、GLEW、FreeImage 及其静态依赖。

Renderer 使用 C++20、Debug `/MTd`、Release `/MT`，保留原 VGUI 宏、Release LTCG、
`/OPT:REF`、`/OPT:ICF` 和 LargeAddressAware。两种配置都生成 Renderer PDB。
显式编译清单位于 `cmake/Sources.cmake`；FreeImage 和 GLEW 使用各自原有 CMake 构建描述。

## 回归验证

四组既有测试随源码迁入，通过可选 CTest 目标运行：

```bat
scripts\build-Renderer-x86-Release.bat -DRENDERER_BUILD_TESTS=ON "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
ctest --test-dir build/x86/Release -C Release --output-on-failure
```

涵盖模型校验、模型加载、ring buffer、Sven client hooks；断言在 Release 仍启用。
不会启动游戏或创建 OpenGL context。真实游戏中的插件加载、画面效果及兼容性需要另行验证。
本次构建验证记录见 `memory/build_and_verification.md`。

许可证见 `LICENSE`；各依赖保留自己的许可证。主工程不携带预编译第三方产物。
