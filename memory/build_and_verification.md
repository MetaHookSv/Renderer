---
title: build_and_verification
type: note
permalink: renderer/build-and-verification
---

# 构建与验证

## 2026-10-22：缺失 UI 图片重载后残留旧纹理（MetaHookSv #936）

- 触发信号：czero 10210 任期界面中，候选 Gus 的武器为空白，选入曾显示 Stanley / Quincy
  的槽位后却显示 P90 / UMP45；图标随槽位历史变化。
- 根因：`enginesurface_drawSetTextureFile` 在 DDS/TGA/BMP 全部失败后仍保留旧缓存和 GPU
  内容；候选卡没有旧缓存，已选卡则复用已有 texture ID。原版 `WeaponPreference = none`
  会清空偏好数量，本次以运行时 count=1、ID=0 构造 `640_none` 缺失路径，未确认报告者的
  自定义 BotProfile 如何产生缺失图标。
- 做法：加载失败或空路径清图时先提交待绘制文字，再移除该 ID 的缓存并同步当前指针及绑定。
  未缓存的 surface ID 绑定 0，纹理绘制跳过；保留 GL texture name 供后续成功重载。
  成功加载后统一绑定新缓存，覆盖首次加载；加法字符绘制也检查失效状态。
- 验证：新增测试直接编译生产 `EngineSurfaceHook.cpp`，旧实现因重载失败后 ID 仍有效而
  断言失败；修复后覆盖缺失图、重复失败、空/空指针路径清图、同 ID 恢复、DDS/TGA、缓存命中
  和待绘制文字提交。MSVC x86 Debug Renderer / Renderer_AVX2、Release Renderer 构建成功，
  gamedata 校验 23 个快照，Debug / Release CTest 各 15/15 通过，clang-format 23.1.3
  检查 96 文件无差异，git diff --check 通过。
- 实机：czero 10210 Debug Renderer_AVX2，重放 P90→缺失图和 UMP45→缺失图后 Gus 均为空白，
  再选 Stanley 可正常显示 P90。仅对 BotProfile 做运行时条件构造，未对 bitmap 参数施加修补。
  当前插件需配套新 gamedata，旧部署首次启动缺少 `R_DrawBrushModel`，临时同步后正常运行。
  游戏退出码 0，测试 DLL/PDB 和 24 个 catalog 文件随后恢复，原文件逐项校验 SHA-256。
- 适用范围：修复共享 surface 文件加载失败路径；其他游戏/引擎及 Release 实机未本次验证。
  聚合树证据在 `build/issue936/fixed-verification.md`，不覆盖同 Issue 的 Esc 胜利界面问题。

## 2026-10-20：TriAPI 原生行为对齐

- 触发信号：renderer-triapi 运行 `39359243122` 的原生对照与上游 oracle 不一致，普通模式
  的渐变/纯色色块和 world 混合结果不同。任务要求原生行为，不能用旧 Renderer 行为反推规范。
- 参考：`D:/HLND2T_official/engine/r_triangle.c` 与
  `D:/goldsrc-reverse/src/svencoop/engine/consolidated/r_triangle.cpp`。
  `Color4fRendermode` 按 Sven 的显式模式参数判断；HLND2T 的对应函数看全局模式，是已知差别。
- 做法：TriAPI handler 集中到 `src/gl_triapi.inl`，由 `gl_rmain.cpp` 和真实 handler 回归共同包含。
  Normal 使用 flat colour；quads、quad strip、polygon 拆分后保留原图元的 provoking colour。
  TransColor/TransTexture 继承当前 depth mask，RenderMode 即时设置状态，End 不重置 blend/depth。
  Color4f 的 TransAlpha 分支执行字节截断/低字节转换，顶点输入时夹取颜色；保留独立的 saved RGBA
  供 Brightness 使用。Begin 复用已保存的 BaseUISurface DrawSetTexture 回调执行原生纹理状态重置，
  不新增 gamedata 或扫描回退。
- Shader：先按原生顺序做纹理与顶点色调制、眼空间 Z 雾，再为 linear scene target 解码；
  TriAPI 不再使用 texture-only gamma remap、linear blend/fog shift 或 additive fog 黑色替换。
  现有 Renderer 的场景目标、后处理和 OIT 集成继续生效，不能据此声称全场景像素等同原生引擎。
- 回归证据：旧 handler 在颜色量化断言失败；旧 shader 对输入样本输出 0.041826，目标为
  0.055189。新增 CPU 回归直接调用生产 handler；GPU 回归编译真实 shader，读回 HUD/world ×
  gamma/linear × opaque/alpha/additive × off/linear/exp/exp2 fog 的 48 组输出。
- 验证：MSVC x86 Debug Renderer / Renderer_AVX2 与 Release Renderer 构建成功，gamedata
  校验覆盖 23 个快照；Debug / Release CTest 各 14/14 通过，clang-format 23.1.3 检查
  95 文件无差异，两个仓库的 git diff --check 均通过。LLMBenchmark 基础检查共 246 项，
  其中 22 项因当前 uv 环境缺少 pywin32 跳过；剥离补丁正向应用、反向还原及重复应用拒绝均已验证。
- 发布与实机边界：本次不部署游戏、不提交或发布二进制，不更新 LLMBenchmark 的旧 oracle。
  新 oracle 必须在新的 upstream revision 上重生成剥离补丁，并重新执行真实游戏 qualify；
  完整游戏、UI 缓存交互、OIT 与其他引擎版本仍需实机验收。

## 2026-10-20：启动静态图 ringbuffer 帧生命周期

- 触发信号：启动时出现 `[TexturedRectVertexBuffer] Warning: Allocate called outside frame`。
- 根因：`CVideoMode_Common_DrawStartupGraphic` 在正常 Renderer 帧回调之前调用
  `R_DrawTexturedRect`，没有开启 ringbuffer 帧；分配器拒绝申请，导致启动图片绘制被跳过。
  本机 WinDbg 原版对照确认申请 128 字节时 `m_InFrame=false`、`m_UsedSize=0`，并捕获完整警告。
- 做法：在启动静态图绘制循环前调用 `R_BeginRingBufferFrame`，循环后、窗口交换前调用
  `R_EndRingBufferFrame`；沿用现有惰性创建和 GPU fence 机制，保留帧外分配保护。
- 验证：MSVC x86 Debug Renderer / Renderer_AVX2、Release Renderer 构建成功；Debug CTest
  12/12、Release ringbuffer_tests 1/1 通过；clang-format 23.1.3 检查 92 文件无差异。
  复用现有生产分配器测试覆盖惰性创建、帧外拒绝和 GPU 未完成帧的区域隔离。
- 实机：Sven Co-op Debug Renderer，WinDbg 确认启动时三种分配均 valid，大小依次为
  128/64/24 字节；绘制结束后 frame-active 与 m_InFrame 均为 false，顶点缓冲有一个
  已提交帧，三个缓冲的帧外警告标志均为 false。随后独立启动、进入 osprey，截图确认
  世界、武器及 HUD 可见，退出码为 0。证据位于聚合树
  `build/startup-ringbuffer-{baseline-trace,fixed-trace}.txt` 与 `build/startup-ringbuffer-fixed-game.log`。
  验收后恢复原部署的四个 Renderer DLL/PDB，SHA-256 均与备份一致。
- 适用范围：本次实机验证 Sven Co-op 的 SDL 启动路径与 Debug 普通版；未覆盖 WGL、其他游戏、
  Release/AVX2 实机及启动图片完整视觉对比。2026-10-11 记录的同类启动静态图警告由本次修复。

## 2026-10-18：资料片 Studio 接口与 gamedata（MetaHookSv #934）

- 触发信号：Blue Shift build 10210 加载 Renderer 时，
  `GameStudioRenderer_StudioRenderModel` 报 matched snapshot 中没有符号；Opposing Force 也无法启动。
- 根因：Renderer manifest 未选择 bshift 客户端；其他插件的裁剪 catalog 只提供部分 Studio 符号。
  另一个独立约束是 gearbox Windows 客户端不导出 HUD_GetStudioModelInterface，
  使用引擎默认 Studio 接口，不能强制要求客户端 CGameStudioRenderer。
- 做法：manifest 为 bshift-8684 / bshift-10210 打包七个必需 Studio 虚函数。
  `src/studio_interface.inl` 根据返回接口的 StudioDrawModel 是否位于引擎映像判断渲染路径；
  引擎路径清空客户端入口并保留引擎 hooks，客户端路径仍严格要求完整 gamedata。
  不需要为 gearbox Windows 打包不存在的客户端 Studio 符号，也不引入扫描回退。
- 验证：MSVC x86 Debug Renderer / Renderer_AVX2 与 Release Renderer 构建成功，
  manifest 同步及校验覆盖 23 个快照；Debug CTest 11/11、新增 Release CTest 1/1 通过，
  clang-format 23.1.3 检查 90 文件无差异。新增测试直接编译生产解析代码，覆盖客户端解析、
  引擎映像首尾、旧入口清理及引擎范围外缺符号仍然报错。
- 实机：HL25 10210，单独启用新 Debug Renderer_AVX2，Blue Shift 进入 ba_security1，
  WinDbg 确认客户端 RenderModel hook 非空；Opposing Force 进入 of1a1，
  客户端 RenderModel 为空、引擎 RenderModel hook 非空。日志位于聚合树
  `build/issue934-{bshift,gearbox}-debug.txt`。测试前补齐了缺失依赖和配套 shader，
  测试后恢复原 DLL、资源、插件列表及 config.cfg，文件恢复做了 hash 比对。
- 适用范围：本次验证启动、地图加载和 hook 路由；未验证完整画面或所有插件组合。
  用户确认本机未复现鼠标转视角问题，本次仅修复启动崩溃；8684 和 Release 未做实机验收。

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


## 2026-10-19：最终场景 alpha 重复混合（MetaHookSv #937）

- 触发信号：czero / engine 10210，关闭 HDR 和 FXAA 后烟雾变黑，开枪与烟雾重叠出现灰色矩形。
- 根因：透明绘制后的场景 RGB 已完成混合，alpha 却仍含烟雾衰减和加法精灵累加值；最终 `R_CopyColor` 再次按该 alpha 混到黑底。FXAA 输出 alpha=1，掩盖了错误。用户抓取的 frame 2685 中矩形实际对应 `wall_puff*.spr` 弹着烟尘，不能将该帧归因为 muzzleflash 材质本身。
- 做法：`R_RenderView_SvEngine` 对普通世界视图使用 `GL_BlitSceneViewport` 覆盖当前 viewport 的颜色并重新绑定 FinalBuffer；viewport 外画面保留。`onlyClientDraws` 仍使用原透明合成；公开 `R_CopyColor` 语义和 portal FBO 路径未改变。
- 验证：MSVC x86 Debug / Release Renderer 构建及 23 个 gamedata 快照校验成功；两种配置 CTest 各 12/12 通过；clang-format 23.1.3 检查 92 文件无差异。新增真实 GL GPU 回归直接调用生产 helper，从 RGBA16F 拷贝到 RGBA8，覆盖 alpha 0 / 0.2323 / 0.4402 / 1 / 5、完整 viewport、偏移 inset、空 viewport 和区外保留像素。
- 证据：用户原始四帧及离线 shader 替换验证位于聚合树 `build/issue937/`；frame 2000 最终 RGB 从 0.668 降到 0.153，离线不透明拷贝恢复至 0.667，并消除 2439 / 2685 的矩形。
- 适用范围与状态：修复版 Debug `Renderer.dll` / PDB 已部署到本机 czero，DLL SHA-256 为 `E04CB9BCC0B819D94E9D0EBAFAD694A9A1BB720991B0BB16B296C99330A58AAC`。用户已于 2026-10-19 确认修复版实机手动验收通过（本次黑烟和开枪灰色矩形）。inset / onlyClientDraws / portal 场景未进行本次实机验收，GPU viewport 回归不能代替这些路径的完整游戏兼容性验证。


## 2026-10-21：onlyClientDraws 视图跳过水面 pass（MetaHookSv #942 / StandaloneView）

- 触发信号：StandaloneView 在主菜单为独立视图安装内存中的空 worldmodel，并以 `onlyClientDraw = 1` 调用 `V_RenderView`。
- 根因：`R_RenderWaterPass` 不检查 `onlyClientDraws`。空世界不在 `mod_known` 中，`R_GetWorldSurfaceModel` 返回 NULL 后被解引用；真实世界下，每个 client-only 视图还会向世界实体容器重复追加水面引用。
- 做法：`src/gl_water.cpp` 的 `R_RenderWaterPass` 在 `R_IsRenderingWaterView` 判断之后对 `(*r_refdef.onlyClientDraws)` 直接返回。client-only 视图本来就不绘制世界，也就不需要水面反射。
- 验证：聚合树 Debug Renderer / Renderer_AVX2 构建通过，`RENDERER_BUILD_TESTS=ON` 时 CTest 14/14 通过。Sven Co-op 10257 实机（StandaloneView + 临时消费者插件）：主菜单空世界 pass 正常合成、可回读；`stadium4` 中独立视图与主视图正常，载入、断开、重进均未崩溃。
- 适用范围与状态：分支 `fix/onlyclientdraws-water-pass`，经 PR 合入。HL25、inset 视图及其他 `onlyClientDraws` 消费者未做实机验证。
