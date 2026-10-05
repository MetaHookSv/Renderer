---
title: build_and_verification
type: note
permalink: renderer/build-and-verification
---

# 构建与验证

本笔记记录 Renderer 的构建入口、依赖解析规则、gamedata 同步与验证状态。命令与参数的完整说明见
[README](../README.md) 及 `docs/en/build-instruction.md`、`docs/zh-CN/build-instruction.md`。

## 构建入口

- 约束：Windows + MSVC x86（`-A Win32`）、CMake ≥ 3.21、仅 Debug / Release 两种配置。
- 脚本：`scripts/build-Renderer-x86-Debug.bat` / `-Release.bat` 固定 `Configuration` 后调用
  `scripts/build-Renderer-x86.bat`。后者 configure 到 `build/x86/<config>`、安装前缀
  `install/x86/<config>`，再 `--target install`，一步完成构建与安装。
- 追加参数直接透传，例如 `-DRENDERER_BUILD_TESTS=ON -DMETAHOOK_SOURCE_PATH=...`；未定义 `SolutionDir`
  时默认为脚本所在目录的上一级（即仓库根）。
- 测试：配置时加 `-DRENDERER_BUILD_TESTS=ON`，随后
  `ctest --test-dir build/x86/<config> -C <config> --output-on-failure`。
- 安装布局（`install/x86/<config>/`）：

| 路径 | 内容 |
| --- | --- |
| `svencoop/metahook/plugins/` | `Renderer.dll` 与 PDB |
| `svencoop/metahook/dlls/FreeImage/` | 按配置的 `FreeImaged.dll` / `FreeImage.dll` |
| `svencoop/renderer/` | 运行资源（着色器、贴图、配置、本地化） |
| `svencoop/metahook/gamedata/renderer/` | 裁剪后的 gamedata catalog |
| `include/Interface/IMetaRenderer.h` | 公共接口 |

## 依赖解析

规则集中在 `cmake/Dependencies.cmake` 的 `renderer_prepare_dependencies()`。显式路径优先并**先校验**
（目录存在 + 必需头文件），外部源码是只读输入，不被写入。

| 参数 | 必需性 | 空值回退 |
| --- | --- | --- |
| `METAHOOK_SOURCE_PATH` | 可选 | FetchContent 跟踪最新 `main`，`SOURCE_SUBDIR include` 只取 SDK 头与源码，不构建宿主、不初始化其子模块 |
| `VGUI2EXTENSION_SOURCE_PATH` | 可选 | FetchContent 固定提交 `cd7ef6e3…`，只取公共接口头 |
| `UTILTHREADTASK_SOURCE_PATH` | 可选 | FetchContent 固定提交 `8d36bef6…`，只取公共接口头 `include/Interface/IUtilThreadTask.h` |
| `FREEIMAGE_SOURCE_PATH` | 可选 | FetchContent 固定提交 `c68700b9…` |
| `GLEW_SOURCE_PATH` | 可选 | FetchContent 固定提交 `56ed32d4…` |
| `CAPSTONE_INCLUDE_DIRS` | 可选 | 优先宿主 `thirdparty/capstone_fork/include/capstone`；该目录不存在时 FetchContent 固定提交 `e81e390f…` |
| `SDL2_INCLUDE_DIRS` | **必需** | 无：本工程不获取也不构建 SDL，必须指向宿主 SDL 构建提供的 include |
| `SDL3_INCLUDE_DIRS` | 可选 | 无 |

- 仓库地址与各依赖版本（MetaHook 跟踪最新 `main`，其余为固定提交）以 `cmake/Dependencies.cmake` 为准；`*_SOURCE_PATH` 非空时跳过对应获取。下载目录在
  `build/x86/<config>/_deps/`，只取源码、不初始化嵌套依赖。
- Capstone 只使用头文件（类型需跨宿主 API 边界保持一致），不编译不链接；传入 `CAPSTONE_LIBRARY_DIRS`
  会被忽略并提示。
- GLEW 以静态库 `libglew_static` 构建；FreeImage 使用其 shared target，在本工程 build 树内构建。根
  `CMakeLists.txt` 校验二者提供的 target 存在。
- 构建期只初始化两个子模块：`thirdparty/ScopeExit`、`thirdparty/tinyobjloader`。
- VC-LTL 5.3.1 从官方二进制包下载并校验 SHA-256（`7a18799e…`），缓存与解压在 `thirdparty/cache/`，
  Debug / Release 共用；校验失败或解压不完整均 FATAL。
- 编译设置：C++20、静态 CRT（`/MTd` / `/MT`）、Release 启用 LTO 与 `/OPT:REF`、`/OPT:ICF`。
- 编译单元：`cmake/Sources.cmake` 的显式清单，共 **139** 个，含宿主 SDK 的 HLSDK / SourceSDK / VGUI 源码、
  本仓库 `src/`，以及 ScopeExit / tinyobjloader。

## gamedata 同步与校验

- `scripts/manifests/renderer.json` 声明 Renderer 消费的 `(module, symbol, kind)` 及按引擎家族的条件分组，
  与宿主同 schema。
- 构建期 `RENDERER_SYNC_GAMEDATA`（默认 ON）驱动 `RendererGameData` 目标 → `scripts/sync-gamedata.py`：
  下载上游 catalog 到持久缓存 `build/x86/<config>/gamedata-sync/`（缓存预热后可离线），按 manifest 裁剪，
  发布到构建树的 `assets/svencoop/metahook/gamedata/renderer`，再安装到
  `svencoop/metahook/gamedata/renderer`，由宿主的 catalog 加载器与其取并集。
- `RendererGameDataValidate` 目标 → `scripts/validate-gamedata.py`，通过 `add_dependencies` 成为 `Renderer`
  构建的前置步骤，校验裁剪结果满足 manifest 契约（模块、kind、按家族覆盖）。
- Renderer 侧消费的符号清单见 `PrivateSymbols.md`。

## 测试

- 四个套件位于 `tests/`：`studio_model_validation_tests`、`studio_model_load_tests`、`ringbuffer_tests`、
  `sc_client_tests`。
- 仅在 `RENDERER_BUILD_TESTS=ON` 时构建并注册到 CTest。测试包含生产 translation unit，因此 Release 下
  保持断言（`/UNDEBUG`），并用 `/Gy` + `/OPT:REF` 丢弃未被调用的 handler。

## CI

- 共用 composite action `.github/actions/build-windows-x86`：在 workspace 同级克隆宿主 MetaHook 与其
  VGUI2Extension、UtilThreadTask（均为 `main`），初始化宿主的 `capstone_fork` / `sdl2-compat-fork` /
  `SDL3_fork` 三个子模块，导出 `METAHOOK_SOURCE_PATH`、`VGUI2EXTENSION_SOURCE_PATH`、
  `UTILTHREADTASK_SOURCE_PATH`、`SDL2_INCLUDE_DIRS`、
  `SDL3_INCLUDE_DIRS`；以 Release 脚本加 `-DRENDERER_BUILD_TESTS=ON` 构建安装；运行 CTest；校验安装后的
  gamedata；把安装树的 `svencoop/` 打包为 `Renderer-windows-x86.7z` 并做 `7z t` 完整性检查。
- `livebuild.yml`：`main` 的 push / PR / 手动触发 → 上传构建产物。
- `msbuild.yml`：`v*` 标签 → 用同一 action 构建并创建 GitHub Release。

## 验证状态

最近一次实测（2026-10-02 / 10-03，Windows + VS2022 + MSVC 19.44 + Windows SDK 10.0.26100 + CMake 3.31.12）：

| 项目 | 结果 |
| --- | --- |
| Debug / Release 配置·编译·链接·安装 | 均退出 0（覆盖无构建产物的全新入口与增量构建） |
| CTest | 每种配置 4/4 通过 |
| 依赖参数负路径 | 缺失 / 无效 `SDL2_INCLUDE_DIRS`、无效显式 `*_SOURCE_PATH`、损坏的 VC-LTL 缓存，均在配置阶段返回非 0 |
| PE | x86 DLL，导出 `CreateInterface`，按配置导入 `FreeImaged.dll` / `FreeImage.dll`，CRT 为 msvcrt |
| 生成工程 | 不含 SDL、宿主或其他第三方构建目标 |
| CI | 本机执行 composite 全部步骤退出 0；`main` 上的 hosted LiveBuild 成功，产物 7z 解压后与本地安装树逐文件一致 |

**未验证**：真实游戏运行、OpenGL context 创建与视觉兼容性；SDL 只验证了无窗口加载。源码或公共接口更新后
必须重新配置并重跑相关构建与测试，不要把历史结果用于证明新改动。

增补 `UTILTHREADTASK_SOURCE_PATH`（2026-10-03，Release 单配置）：显式路径配置·编译·安装退出 0，CTest 4/4；
无效显式路径在配置期 FATAL；空值 FetchContent 拉取固定提交 `8d36bef6…` 成功。Debug 未本次复测。
