---
title: ParallelShaderCompile
type: note
permalink: renderer/parallel-shader-compile
---

# ParallelShaderCompile

## Overview

Renderer #32 将内部 shader 变体改为 GL 渲染线程上的有界编译/链接调度。支持 ARB、KHR 和 Sync；公开同步接口和 IMetaRenderer ABI 不变。

本笔记记录 2026-10-26 的实现和验证，**验收未完成**：一次启动以 0xC0000374 退出，未取得转储、未确定根因；8 轮 CDB 重试没有复现。完整视觉矩阵、游戏帧百分位与 VRAM 趋势尚未验证。不能据此关闭 issue 或宣称 production-ready。

## Responsibilities

- 拥有复制的源码/路径/宏、稳定 ticket、context generation、GL shader/program 和完成回调。
- 批量提交不查 compile/link status；completion status 就绪后才查 link status 和初始化 Uniform。
- 真实绘制 Require 同一任务，只在明确需求屏障允许等待；同步模式不在帧 pump 内预热。
- 缓存 loader 仅 request，保存合并有效历史，清理发生在正确 context/thread。

## Involved Files & Symbols

- `src/gl_shader_parallel.{h,cpp}`：CParallelShaderCompiler、IShaderGLOperations/CShaderGLOperations、GL_ShaderCompilerThreadHint。
- `src/gl_shader_cache.h`：R_ReadProgramStateCache，独立文本解析器。
- `src/gl_shader.{h,cpp}`：源预处理、GL_RequestShaderVariant、GL_RequireShaderVariant、GL_RequireShaderBatch、GL_InitShaders、GL_FreeShaders、统计。
- `src/gl_{wsurf,water,light,studio,sprite,portal,hud}.cpp`：十类状态工厂、request-only loader、Ready 回调；HUD 固定 23 个（无 OIT 为 21 个）启动批次。
- `src/gl_rmain.cpp`：HUD_Frame 标帧、GL_BeginRendering 一帧一次 pump、CVar 注册、启动 +command、shutdown/save。
- `tests/gl_shader_parallel_scheduler_tests.cpp`、`gl_shader_cache_tests.cpp`、`gl_shader_parallel_integration_tests.cpp`：生产调度器 mock、解析器、真实 GL/FBO。
- `docs/{en,zh-CN}/parallel-shader-compile.md`：控制台参数、架构、验证说明。

## Architecture

```text
cache/draw -> shared factory -> owned request -> Queued
frame      -> compile all stages + link      -> Linking
later poll -> completion -> link status -> Uniform -> Ready
draw       -> Require same ticket -> one demand wait -> use
shutdown   -> save -> reset/clear -> delete or discard -> new generation
```

优先 ARB，必须同时具备扩展标志和匹配 hint 入口；其次 KHR，否则 Sync。hint 0 请求不并行，~0u 请求实现自选最大值；读回不是 worker 数。运行中关闭仍完成已提交任务。inflight 默认 16，帧提交默认 8，预算默认 1 ms；单个驱动调用无法抢占。

去重 hash 仅选桶，再比较 category、state、完整 VS/GS/FS 路径/源码/宏。std::map 保证任务地址稳定，critical/prewarm 队列按 3:1 让出调度机会。需求可借用一个 inflight 槽，不等待无关预热任务。回调可追加任务，禁止重入 Reset。重建使旧 ticket 失效；context 丢失时丢弃句柄，禁止在新 context 删除旧 ID。

固定 HUD 程序启动时先批量排队，再通过有界提交和逐项 startup barrier 保证 R_InitHUD 返回时就绪。动态 uniform 仍由实际绘制上传。公开编译结果留在 g_ShaderTable，内部结果由调度器独占，避免双重释放。启动视频等 R_Init 前公开调用保留同步语义。

## Dependencies

- GLEW 和 Windows WGL/current-thread 检查；不创建执行 GL 的后台线程。
- GLFW 只用于测试，真实驱动异步性由 OpenGL 实现决定。
- 所有 GLSL、第三方源码、gamedata、公开虚表均未改动。
- 相关记录：[[Renderer]]、[[build_and_verification]]。

## Notes

### 根因与正确做法

- 触发信号：旧 helper 仅向驱动传递 thread hint，随即查询 shader compile status；缓存读取硬编码 Studio 文件；loader 使用绘制路径导致绑定和 location 初始化阻塞。
- 根因 / 约束：阻塞查询抵消异步提交；预热与实际绘制、源码生命周期和 GL 对象所有权耦合。
- 正确做法：共享 define 工厂，拆分 request/submit/poll/finalize/require；复制输入；缓存按真实 filename 读取；只在 Ready 发布 location。
- 实机补充：部分启动路径在 R_Init 前未命中预期 GL_Init hook，初版触发 “Shader request without the initialized renderer GL context”。现于实际 graphic-engine 初始化和 R_Init 调用幂等 GL_InitShaders，同一 context 保留公开程序。后续多轮启动/重启成功。
- 适用范围：Renderer 内部变体及固定 HUD；公开 API 仍同步，不能用 GLFW 测试代替完整游戏画面验收。

### 本次构建与自动化证据

聚合工作目录 D:/MetaHookSv，VS 2022 / MSVC 14.44 / Windows SDK 26100，Win32/x86；组件基线 7225f589548fcc664c4d0e540eb91ccff22c3650。Debug/Release 均启用测试，Release 另启用 Renderer_AVX2。

- Debug 和 Release 的 Renderer、Renderer_AVX2 及全部 18 个测试目标构建：exit 0。
- `ctest --test-dir build/x86/Debug/Plugins/Renderer -C Debug --output-on-failure`：exit 0，18/18，0 failed/skip，65.67 s。
- `ctest --test-dir build/x86/Release/Plugins/Renderer -C Release --output-on-failure`：exit 0，18/18，0 failed/skip，27.63 s。
- `ctest --test-dir build/x86/Release/Plugins/Renderer -C Release -R "^gl_shader_parallel_integration_tests$" -V`：exit 0，1/1，3.03 s；Release 保留断言。
- `cmake --build build/renderer32-format --target format-check`：exit 0，103 files，0 differences；`git diff --check` exit 0。
- 原始日志位于聚合树忽略目录 `build/renderer32-*-final-build.log`、`renderer32-ctest-{debug,release}.log`、`renderer32-performance-release.log`、`renderer32-format-check.log`。
- mock 覆盖扩展/入口矩阵、hint 边界、Pending 禁止阻塞查询/Uniform、源码所有权、去重/GS/category、预算/公平性、需求借槽、编译/创建失败、重入、取消/context generation、资源计数平衡。缓存覆盖十个 reader 路径与非法/重复/NONE/过长行。
- 真实 GL：NVIDIA Corporation / GeForce RTX 5060 PCIe/SSE2，621.01，GLEW 2.2.0；测试 GL 3.3 core。三个 context 分别自动 ARB、强制 KHR-only、屏蔽扩展走 Sync；各先执行强制 Sync。测试 VS/GS/FS、三种故意阶段错误混合、Uniform、RGBA8 FBO readback、context 重建、glIsProgram 删除检查，无新增 GL error。请求/读回 hint 4294967295。

### 同 GPU 工作负载性能

同一真实 GL 测试32个简易 program，两种 history × Sync/ARB，各五轮交替顺序，共20行；未清驱动 binary cache，属于热缓存小负载。下表为每组五轮中位数；提交峰值取所有轮最大值。p50/95/99 是 CPU pump/require 样本百分位，**不是游戏帧时间**。

| 历史 | 模式 | wall ms | submit avg / peak ms | poll total ms | demand 次数 / ms | CPU 样本 p50/95/99 ms | peak inflight |
| --- | --- | ---: | ---: | ---: | ---: | --- | ---: |
| 无 | Sync | 2.9226 | 0.033481 / 0.4234 | 0 | 32 / 2.7800 | 0.0870 / 0.1184 / 0.1400 | 1 |
| 无 | ARB | 2.2451 | 0.024575 / 0.1450 | 0 | 32 / 2.1682 | 0.0630 / 0.1094 / 0.1232 | 1 |
| 已知32状态 | Sync | 1.6444 | 0.018284 / 0.0763 | 0 | 32 / 1.5863 | 0.0386 / 0.0936 / 0.0968 | 1 |
| 已知32状态 | ARB | 2.3538 | 0.062372 / 0.3120 | 0.2419 | 0 / 0 | 0.0001 / 0.4474 / 0.5430 | 15 |

32 个请求均唯一，scheduler cache hits=0。已知列表预热让32个 demand 均在 Ready 命中；无历史首次使用均要等待。此工作负载预热总 wall 较慢，不能宣称必然加速；无历史 Sync 有9.8053 ms离群轮。真实GPU bulk submit、completion poll和Ready统计与mock无阻塞状态断言共同提供异步路径证据。

### 游戏验证与尚未完成项

Sven Co-op 5.26 / SvEngine 10257，GL 4.4.0，1920×1080；临时部署本次 Debug/Release Renderer DLL/PDB。

- 空缓存启动、菜单、stadium4 和 osprey 可运行并截图；运行中 mode 0/1、threads 0/-1、saveprogstate/loadprogstate 已执行。
- 两图交替10次，ready/program稳定97、failed0，无额外提交；这是有限运行回归，不代表所有特效视觉一致。
- Debug新代码5次 _restart，均ready97、shader创建/删除194/194、failed0；原部署版也做5次对照。
- 进程 private bytes 新版第1→5轮为1,267,200,000→1,475,379,200，handles969→1052；原部署版1,229,418,496→1,417,662,464，handles935→1016。两版均线性增长，尚未确定原部署版与新版进程增长的根因，也不能证明全部为基线泄漏。GL临时对象平衡不能替代进程/VRAM无泄漏结论。
- Release严格A/B准备时，已知状态缓存+强制Sync一次在 execing config.cfg 附近以0xC0000374退出，没有本次转储或fatalerror。**未解决**。同配置再次启动、WinDbg附加重启、x86 CDB启动/地图加载成功，随后8轮x86 CDB强制Sync启动/统计/退出全部成功，未复现，不能据此关闭问题。
- 后续8轮日志：`build/renderer32-cdb-repeat-{1..8}.log`。采样JSON：`build/renderer32-game-results.json`。早期游戏A/B配置在启动后才纠正，不能用于严格性能结论。
- 原部署4个DLL/PDB、11个state cache文件、config.cfg共16个文件均已恢复并逐项SHA-256校验。测试创建的根目录steam_appid.txt已清除；本次源码未提交。
- NOT RUN / 未验证：其他GPU/驱动/GoldSrc版本、完整Water/Portal/阴影/HBAO/OIT/multiview视觉对照、真实显示模式重建、游戏首帧/加载帧p50/p95/p99、VRAM趋势、可比的真实游戏完整性能A/B。
- 下一步：取得间歇性堆损坏转储并定根因；解释进程重启内存增长；补完整视觉和游戏性能矩阵，然后执行最终代码审查。

## Callers

- R_LoadProgramStates_f → 十类 R_Request*ProgramState → GL_RequestShaderVariant。
- 十类 R_Use*Program → 共享 R_Resolve* 工厂 → GL_RequireShaderVariant。
- R_InitHUD → GL_RequestShaderVariant → GL_RequireShaderBatch。
- R_GameFrameStart 标帧，GL_BeginRendering → GL_PumpShaderCompiler。
- R_Shutdown / GL_Shutdown → 保存、GL_ResetShaderVariants / GL_FreeShaders。
