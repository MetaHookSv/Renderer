[返回 README](../../README.zh-CN.md) | [English](../en/parallel-shader-compile.md)

# Shader 并行编译

Renderer 在当前 OpenGL 渲染线程批量提交编译与链接，使用驱动提供的
`GL_ARB_parallel_shader_compile`，其次使用可用的桌面 KHR 入口。没有扩展、入口点
缺失或强制关闭时走同步需求路径。不会创建调用 GL 的后台线程，也不改变 GLSL、
`IMetaRenderer` ABI 或公开同步编译接口的返回语义。

## 调度与所有权

```text
缓存 loader / 实际绘制 -> 共用 define 工厂 -> 复制路径、宏、预处理源码 -> Queued
GL_BeginRendering      -> 批量 compile + link                        -> Linking
后续游戏帧             -> completion status -> link status -> Uniform -> Ready
首次实际绘制           -> Require 同一个 ticket -> 一次链接等待 -> 使用
失败                   -> VS/GS/FS 和 program 诊断 -> 删除对象         -> Failed
退出 / Context 重建    -> 清理或丢弃旧句柄，更新 generation            -> Cancelled
```

`Linking` 合并了 stage 提交与 program 链接；一次 Submit 立即提交所有阶段后链接，
不在中间查询编译状态。非阻塞轮询只查 completion token `0x91B1`，完成后才查询
链接状态、Uniform/Attrib location。失败时保留每个阶段的路径、宏与日志。
`Ready` 变体表只发布已链接并初始化 location 的程序；动态 Uniform 值仍在绘制时更新。

任务 ID 与 Context generation 共同标识任务；去重比较类型、state、VS/GS/FS 路径、
完整源码和宏，哈希碰撞仍比较原文。队列拥有源码和回调，回调只捕获值或静态 HUD 字段。
内部调度器独占内部变体和固定 HUD 程序；`g_ShaderTable` 只管理公开同步编译路径的程序，
两者不会重复删除。Map 重载保留同一 Context 的 shader，客户端关闭取消并释放内部任务；
GL 关闭时，在 Context 仍有效时释放 GL 对象，Context 已失效则仅丢弃句柄。

`HUD_Frame` 仅标记游戏帧。`GL_BeginRendering` 在引擎入口返回后最多 pump 一次，
所以多个 SvEngine view 不重复消耗预算；每次 GL 调用前都核对创建调度器的 WGL Context
和线程。一个 GL 调用、一次最终化自身的耗时无法被毫秒预算抢占，单次驱动调用仍可能卡顿。
需求点可临时借用一个 inflight 槽，避免等待与当前绘制无关的预热任务。

## 覆盖范围

十类状态缓存均使用 request-only loader：WSurf、Water、DLight、DFinal、Studio、
Sprite、TriAPI、Portal、DrawTexturedRect、DrawFilledRect。loader 不绑定程序或上传
动态 Uniform。Studio/WSurf 的 multiview geometry stage、各类 OIT/fog/混合宏沿用原工厂。
OIT 能力由 GL 初始化确定，在同一 Context 内保持不变。

`R_InitHUD` 的 23 个固定程序（OIT 关闭时 21 个）也使用批量提交和受控启动屏障，
覆盖 FXAA、HDR、HBAO、OIT、gamma/copy/水下后处理。HBAO location 在完成回调中初始化；
函数返回前所有固定程序均已就绪。SSR 属于 DFinal 状态变体。
`GL_CompileShaderFile[Ex]`、`IMetaRenderer::CompileShaderFile[Ex]` 和启动视频使用的
`GL_TryCompileShaderObject` 继续同步返回可用对象，包括 `R_Init` 前的调用。

缓存是 flags 文本，不是 program binary cache。读取使用真实传入的 filename；重复行
去重，空行/缺失文件不产生请求，含未知或其他类型 token 的行整行跳过，`NONE` 表示零状态。
自动保存发生在清表之前，并合并有效历史缓存，防止尚未完成的预热状态丢失。

## 控制台参数

| 参数 | 默认 | 语义与边界 |
| --- | ---: | --- |
| `r_shader_parallel_compile` | 1 | 自动选 ARB/KHR；0 使新编译走同步需求路径 |
| `r_shader_parallel_threads` | -1 | -1/非法负值/非有限值/超范围值映射 `0xFFFFFFFF`；0 请求不并行；正数截断为整数 hint |
| `r_shader_parallel_max_inflight` | 16 | 预热同时链接数，限制为 1–256 |
| `r_shader_parallel_submit_per_frame` | 8 | 每游戏帧最多提交数，限制为 1–256；固定启动批次另受 inflight 限制 |
| `r_shader_parallel_poll_budget_ms` | 1.0 | pump CPU 预算限制为 0–10 ms；非有限值用 1；0 暂停后台推进 |
| `r_shader_parallel_autosave` | 1 | 客户端或 GL 安全关闭点保存有效状态 |

`shader_compile_stats` 输出 backend、GPU/GL/GLEW、请求/读回 hint、请求与去重数、
queued/inflight/ready/failed/cancelled、同步回退、峰值、poll、提交/轮询耗时、
实际需求等待及独立的启动屏障次数/耗时。计数在同一调度器中累计，直到 GL 重建。
`requests/cacheHits` 是调度器请求统计，不包含变体表直接命中的每次绘制。
`saveprogstate`、`loadprogstate` 保持可用。同步模式的普通预热请求留在队列直到真实需求，
不在每帧预算里执行阻塞同步编译；固定启动程序始终在启动屏障完成。

启动命令行中的 `+r_shader_parallel_*` 会在固定 HUD 编译前提前应用；之后引擎仍按正常
顺序执行配置文件和命令。做 A/B 时应核对配置文件中的存档值和 `shader_compile_stats`，
避免启动后改配置而误认为启动编译也使用了该模式。统计同时记录 shader/program 创建、
删除及 Context 丢失后丢弃的数量；prewarm wall 为已完成预热批次的累计 wall time。

切换关闭不会丢弃已提交任务；它们仍按 completion 状态结束。后续提交使用同步路径。
thread hint 在有效 GL 线程更新。hint 只是请求，`0xFFFFFFFF` 请求实现自选最大值；
`glGetIntegerv` 返回 hint 状态，不能解释成真实线程数或加速保证。

## 验证和性能复现

```powershell
cmake --build build/x86/Debug --config Debug --target Renderer gl_shader_parallel_scheduler_tests gl_shader_cache_tests gl_shader_parallel_integration_tests
ctest --test-dir build/x86/Debug -C Debug --output-on-failure
ctest --test-dir build/x86/Release -C Release --output-on-failure
ctest --test-dir build/x86/Debug -C Debug -R gl_shader_parallel_integration_tests -V
cmake -S . -B build/format -DFORMAT_VALIDATION_ONLY=ON
cmake --build build/format --target format-check
git diff --check
```

独立工程先按[构建说明](build-instruction.md)启用 `RENDERER_BUILD_TESTS=ON` 并指定实际依赖。
聚合工程可把 CTest 目录缩小到 `build/x86/<config>/Plugins/Renderer`。CPU 测试执行生产
调度器和缓存解析器；GL 测试创建三个独立 Context，分别走自动 ARB、仅 KHR 和模拟无扩展
回退（驱动缺少对应扩展时仍测试同步绘制）。只有无法创建 Context 使用退出码 2 / SKIP；
GLEW 初始化失败、编译/像素错误必须失败，Release 保留断言。

集成测试输出 `PERF` CSV：相同 32 个简易程序，分别覆盖无历史的逐项首次需求和已有状态
列表的批量预热；每组五轮交替 sync/parallel 次序，共 20 行。记录提交
平均/峰值、poll 总时长、预热 wall time、需求等待、pump/require 样本 p50/p95/p99
和峰值 inflight。它不清除驱动 binary cache，也不模拟游戏第一帧。小程序在热缓存下可能
没有吞吐收益；这些数据只能证明提交/轮询/需求等待的实际分支，不能替代游戏性能结论。

游戏 A/B 需使用相同 GPU、驱动、分辨率、地图、视角和变体集合，分开记录首次无状态缓存
与再次有状态缓存。保存 `shader_compile_stats`、加载/首帧 p50/p95/p99、private bytes/VRAM，
至少 10 次地图切换和 5 次重新初始化循环。检查模型/水面/粒子/TriAPI/Portal/灯光/后处理，
并测试运行时关闭、thread hint 0 和 Context 重建。驱动缓存冷热与采样次数必须写入报告。
本次实际执行证据及未验证项见 [ParallelShaderCompile](../../memory/ParallelShaderCompile.md)。

规范：[ARB](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_parallel_shader_compile.txt)、
[KHR](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_parallel_shader_compile.txt)。
