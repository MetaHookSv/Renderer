[Back to README](../../README.md) | [中文](../zh-CN/parallel-shader-compile.md)

# Parallel shader compilation

Renderer submits compilation and linking in batches on its current OpenGL rendering thread.
It prefers `GL_ARB_parallel_shader_compile`, then the usable desktop KHR entry point.
Missing extensions/entry points or an explicit disable select synchronous demand compilation.
No application worker thread calls GL. GLSL, the `IMetaRenderer` ABI and public synchronous
compilation return semantics are preserved.

## Scheduling and ownership

```text
cache loader / draw -> shared define factory -> owned paths, defines, sources -> Queued
GL_BeginRendering  -> batch compile + link                                   -> Linking
later game frame   -> completion status -> link status -> locations          -> Ready
first draw         -> Require the same ticket -> explicit link wait -> use
failure            -> program and VS/GS/FS diagnostics -> delete objects     -> Failed
shutdown / context -> cleanup or discard handles, advance generation         -> Cancelled
```

`Linking` combines stage submission and program linking: Submit sends every stage and then
links immediately, without intermediate compile-status queries. Poll uses completion token
`0x91B1`; only completion permits link-status and Uniform/Attrib queries. Diagnostics retain
stage paths, defines and logs. Ready variant tables publish linked programs with initialized
locations; dynamic uniform values are still uploaded at draw time.

Tickets contain stable IDs and context generations. Deduplication compares category, state,
all stage paths, complete sources and defines; hash collisions still compare the full data.
Jobs own sources and callbacks, which capture values or static HUD fields. The scheduler owns
internal variants and fixed HUD programs; `g_ShaderTable` owns programs returned by the public
synchronous path, preventing double deletion. Map reload retains shaders in the same context.
Client shutdown cancels/releases internal jobs. GL shutdown deletes objects while the context
is valid; a lost context only discards application handles.

`HUD_Frame` marks a game tick without issuing GL calls. After the engine's `GL_BeginRendering`
returns, at most one pump runs per tick, avoiding repeated budgets across SvEngine views.
Every operation verifies the captured WGL context and thread. A single driver call or finalizer
cannot be preempted by the CPU budget and may still stall. A real demand can temporarily borrow
one inflight slot instead of waiting for unrelated prewarming.

## Coverage

Request-only loaders cover WSurf, Water, DLight, DFinal, Studio, Sprite, TriAPI, Portal,
DrawTexturedRect and DrawFilledRect. They do not bind programs or upload dynamic uniforms.
Existing factories retain Studio/WSurf multiview geometry stages and OIT/fog/blending defines.
OIT capability is established at GL initialization and remains constant within that context.

`R_InitHUD` also batches its 23 fixed programs (21 without OIT), with a controlled startup
completion barrier: FXAA, HDR, HBAO, OIT, gamma/copy/underwater processing. HBAO locations are
initialized in completion callbacks; all fixed programs are usable before initialization
returns. SSR is covered by the DFinal variant factory. `GL_CompileShaderFile[Ex]`,
`IMetaRenderer::CompileShaderFile[Ex]` and startup-video `GL_TryCompileShaderObject` still return
usable objects synchronously, including calls before `R_Init`.

State caches contain flags text, not program binaries. The reader uses the supplied filename,
deduplicates lines, ignores empty/missing files, and rejects a whole line containing unknown
or cross-category tokens. `NONE` means zero state. Autosave precedes table cleanup and merges
valid historical states so unfinished prewarming does not erase them.

## Console controls

| Control | Default | Meaning and bounds |
| --- | ---: | --- |
| `r_shader_parallel_compile` | 1 | Auto ARB/KHR; 0 selects synchronous new demand compilation |
| `r_shader_parallel_threads` | -1 | -1/invalid negative/non-finite/out-of-range maps to `0xFFFFFFFF`; 0 requests no parallel work; positive values truncate to an integer hint |
| `r_shader_parallel_max_inflight` | 16 | Simultaneous prewarm links, clamped to 1–256 |
| `r_shader_parallel_submit_per_frame` | 8 | Submissions per game frame, clamped to 1–256; fixed startup batches have their own inflight bound |
| `r_shader_parallel_poll_budget_ms` | 1.0 | Pump CPU budget, clamped to 0–10 ms; non-finite uses 1; zero pauses background progress |
| `r_shader_parallel_autosave` | 1 | Save valid states at safe client/GL shutdown points |

`shader_compile_stats` reports backend, GPU/GL/GLEW, requested/reported hint, request/dedup
counts, queued/inflight/ready/failed/cancelled, synchronous fallbacks, peaks, polls, submission
and polling times, demand stalls, and separate startup-barrier counts/times. Counts accumulate
within one scheduler until GL recreation. `requests/cacheHits` count scheduler requests,
excluding draws that directly hit an existing variant table. `saveprogstate` and `loadprogstate`
remain available. In synchronous mode ordinary prewarm requests stay queued until a real demand;
the frame pump does not perform blocking synchronous compilation. Fixed startup shaders always
complete at their barrier.

Command-line `+r_shader_parallel_*` controls are applied before fixed HUD compilation. The
engine subsequently executes configuration files and commands in its normal order. For A/B
runs, check archived values and `shader_compile_stats`; a change after startup does not change
the mode used for startup compilation. Statistics also count shader/program creation, deletion
and objects discarded after context loss. Prewarm wall time accumulates completed waves.

Disabling preserves already submitted jobs, which still complete through the completion query;
new submissions use Sync. Thread hints update on the valid GL thread. `0xFFFFFFFF` requests an
implementation-specific maximum. The query returns hint state, never the actual worker count
or a guarantee of acceleration.

## Verification and performance reproduction

```powershell
cmake --build build/x86/Debug --config Debug --target Renderer gl_shader_parallel_scheduler_tests gl_shader_cache_tests gl_shader_parallel_integration_tests
ctest --test-dir build/x86/Debug -C Debug --output-on-failure
ctest --test-dir build/x86/Release -C Release --output-on-failure
ctest --test-dir build/x86/Debug -C Debug -R gl_shader_parallel_integration_tests -V
cmake -S . -B build/format -DFORMAT_VALIDATION_ONLY=ON
cmake --build build/format --target format-check
git diff --check
```

Standalone builds first enable `RENDERER_BUILD_TESTS=ON` with actual dependencies as described
in the [build instructions](build-instruction.md). Aggregator CTest can be scoped to
`build/x86/<config>/Plugins/Renderer`. CPU tests run the production scheduler and cache parser.
GL tests create three independent contexts for automatic ARB, KHR-only and simulated missing
extension fallback; unsupported flavors still execute synchronous drawing. Only inability to
create a context returns 2 / SKIP. GLEW initialization, shader or pixel failures fail the test.
Release retains assertions.

The integration test emits `PERF` CSV for the same 32 simple programs, covering sequential
first demand without history and batch prewarming from a known state list. Each case has five
rounds with alternating sync/parallel order, producing 20 rows: mean/peak submission CPU,
total poll time, prewarm wall time,
demand stalls, pump/require sample p50/p95/p99 and peak inflight. It does not clear driver binary
caches or simulate a game's first frame. Tiny shaders with warm caches may show no throughput
benefit. These measurements demonstrate actual branch behavior, not game performance.

Game A/B requires identical GPU, driver, resolution, map, camera and variant sets, with separate
first-run/no-state-cache and subsequent/cached runs. Record stats, loading/first-frame
p50/p95/p99, private bytes/VRAM, at least ten map transitions and five reinitialization cycles.
Check models/water/particles/TriAPI/portals/lights/post-processing, runtime disable, thread hint
zero and context recreation. Report cache conditions and sample counts. Current evidence and
unverified items are tracked in [ParallelShaderCompile](../../memory/ParallelShaderCompile.md).

Specifications: [ARB](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_parallel_shader_compile.txt),
[KHR](https://registry.khronos.org/OpenGL/extensions/KHR/KHR_parallel_shader_compile.txt).
