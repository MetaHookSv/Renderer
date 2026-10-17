---
title: build_and_verification
type: note
permalink: renderer/build-and-verification
---

# 构建与验证

## 2026-10-17：Studio 换色缓存索引边界（Renderer #20）

- 触发信号：实际 mesh 选择编号 >=32 的 DM_Base / Remap 纹理并进入换色分支。
- 根因：`R_StudioGetSkin` 把非法缓存索引映射为 0，调用方仍把原索引传给
  `R_StudioReloadSkin`，后者在检查文件内容之前越界访问固定缓存。
- 做法：两个缓存入口均拒绝负数和 >=`MAX_SKINS` 的索引，返回空指针；调用方
  沿用原纹理路径。32 是换色缓存容量，不能用它限制 MDL / T.mdl 的纹理总数。
  缓存实现移至内部 `src/studio_skin_cache.inl`，供生产代码和 CPU 测试共同编译。
- 验证：测试先后捕获索引映射和未检查缓存访问，修复后 Debug / Release 均通过。
  合成数据覆盖 32/33/34/99 张纹理、编号 31/32/98、负数及 `index == numtextures`，
  并验证槽位 0 隔离、低索引缓存重建与命中。MSVC x86 Debug Renderer / Renderer_AVX2
  构建及 gamedata 校验成功，Renderer Debug CTest 10/10 通过，格式检查 88 文件无差异。
- 适用范围：本次为缓存函数及引用校验的模拟回归；未进行真实游戏、外置 T.mdl
  加载或纹理绘制验收，也未构建 Release 插件 DLL。

## 2026-10-16：恢复 SDL GL_Shutdown 调用链（ThreadGuard #10）

- 触发信号：Sven 10257 同时加载 Renderer 与 ThreadGuard，quit 时出现
  `Illegal termination of worker thread 'SocketThread'` 和 `0xC0000409`。
- 根因：Renderer 重定向 GL_Shutdown 调用点后未调用原目标，绕过 ThreadGuard
  的函数入口 hook；ThreadGuard 又屏蔽引擎较晚的 SteamAPI_Shutdown 调用。
- 做法：通过 InlinePatchRedirectBranch 的 original 输出保存原调用目标，SDL
  路径释放 Renderer 自有 GL 资源后调用它，保留入口 hook。旧 WGL 路径保持原行为，
  避免未经验证地叠加 context/DC 销毁；未新增 gamedata 符号。
- 验证：原部署 DLL 连续 3/3 次 quit 断言并返回 0xC0000409；部署修改版后
  5/5 次独立启动、quit 返回 0 且无断言。另一次加载 osprey、_restart、quit
  返回 0；WinDbg 确认两代引擎均经过 Renderer → ThreadGuard → SteamAPI_Shutdown，
  最终较晚 shutdown hook 的 g_bSteamShutdown 为 true。
- 构建与检查：MSVC x86 Debug Renderer / Renderer_AVX2 构建成功，格式检查
  87 文件无差异，现有 Debug CTest 10/10 通过。两种 DLL/PDB 均部署到本机
  Sven Co-op；只替换 Renderer，ThreadGuard 和插件配置未修改。
- 适用范围：本次实机仅验证 Sven 10257；Release、其他 SDL 引擎及 WGL 引擎
  未进行本次运行验证。测试证据位于 D:/halflife-cli/build/issue10-*.json、
  issue10-fixed-callchain.txt，原部署文件备份在 issue10-renderer-backup/。

本笔记记录 Renderer 的构建入口、依赖解析规则、gamedata 同步与验证状态。命令与参数的完整说明见
[README](../README.md) 及 `docs/en/build-instruction.md`、`docs/zh-CN/build-instruction.md`。

## 2026-10-11：HL25 启动视频（Issue #699）

- 后续状态管理调整：移除逐帧 GL 状态快照，独占播放阶段显式设置所需状态；
  析构时解绑视频资源并重置纹理单元与 unpack 布局。x86 Debug/Release 构建和
  GL 4.4 Core 探针通过，覆盖脏输入状态下的颜色/方向及错误退出后的资源释放。
  此调整未重新进行实机验收；下方状态恢复测试属于调整前的历史记录。
- 当前实现使用固定 Mile.FFmpeg 6.1.0.0 SDK，聚合及独立构建共享
  `cmake/MileFFmpeg.cmake`；`MILE_FFMPEG_ROOT` 可覆盖下载路径。
  DLL 和许可证安装到 `svencoop/metahook/dlls/Mile.FFmpeg/`，运行时仅 HL25
  按 DLL 名称显式加载，复用 MetaHook 加载插件前递归加入 `PATH` 的目录。
  没有增加引擎 WebM/VPX 私有符号；新增消费现有
  `S_PrecacheSound`、`S_StartDynamicSound`、`S_Update` 的 HL25 记录。
- 已验证：聚合 MSVC x86 Debug/Release 构建及 gamedata 校验；Renderer 8 项
  Debug CTest；新增 2 项 Release CTest；clang-format 23.1.3 检查。
  独立 Renderer 配置成功。安装树和 Debug/Release DLL 导入表验证通过。
- 原版 `valve.webm` 通过生产解码代码读出 150 帧，末帧时间戳 9.933 秒。
  实机 HL25 日志记录 `completed, 150 frames presented` 后正常退出；
  `-novid` 不进入视频路径。Sven Co-op 未部署 Mile.FFmpeg 也正常初始化、退出。
- GL 4.4 Core 实测包括 420/422/444、奇数尺寸、负 stride、图像方向、
  HL25 原始转换表的彩色样本，以及 GL 状态恢复。HL25 原代码把 V/U 反向传入
  yuv2rgb 表；shader 保留这一颜色行为，而非直接套用标准 BT.601 矩阵。
- 实机声音听感、三个跳过键、全屏视觉效果及进入地图后的完整渲染仍待人工验收。
  窗口截图未捕获到视频，不能据此声称视觉验收通过。测试部署文件已恢复。
  启动静态图的 ringbuffer 帧外分配警告在 `-novid` 对照中同样存在，属于原有路径。
- 发布前仍需提供与 SDK 对应的完整第三方源码及构建材料，见
  `docs/en/startup-video.md`。本次没有发布或提交二进制。

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
