---
title: R_ForceCVars 内联构建的 gamedata 缺失处理
type: note
permalink: renderer/r-forcecvars-inlined-optional-symbol
tags:
- gamedata
- symbol-resolution
- force-cvars
- optional-symbol
- manifest
- 3266
- case-study
---

# R_ForceCVars 内联构建的 gamedata 缺失处理

## 触发信号

GoldSrc BLOB 引擎 CS 1.6 build **3266**（以及 `hl-3248` / `hl-3329` / `hl-3647`）带 `Renderer.dll`
启动时，进程在菜单出现前约 1 秒即崩溃并以 `0xC0000005` 退出。两种表现形式，取决于 gamedata 状态：

1. **数据错误**（上游未修时）：dump 中访问违例，栈为
   `Renderer!AllowCheats → Renderer!R_ForceCVars → metahook_blob!MH_Cvar_DirectSet`，
   `movss xmm0, [ecx+0Ch]` 且 `ecx = 0`，`AllowCheats` 读到未初始化的 `sv_cheats`。
2. **数据已修、消费端未改时**：弹窗 fatal error
   `[Renderer] Could not resolve gamedata symbol: R_ForceCVars (module engine, symbol was not found in the matched gamedata snapshot)`。

## 根因

- 这四个 build 上**没有独立的 `R_ForceCVars` 函数体**，force-cvar 逻辑被内联进引擎 setup 路径。
- 上游 locator（`find-R_ForceCVars_R-AnimateLight.py`）曾用「邻接 `R_CheckVariables` 调用」的启发式，
  把 `Cvar_DirectSet`（`func_rva 0x311b0`）误贴成 `R_ForceCVars`，二者函数体与签名完全相同。
- Renderer 按普通必需符号 `GamedataResolvePtr` 解析它并**无条件** `Install_InlineHook`，于是引擎的
  `Cvar_DirectSet` 入口被改写成 `Renderer!R_ForceCVars(qboolean)`。引擎 setup 调用
  `Cvar_DirectSet(cvar, value)` 时，`cvar_t*` 被当作 `mp` 布尔参数，最终在 `AllowCheats()` 读取尚未由
  `R_InitCvars()` 赋值的 `sv_cheats` 时崩溃。
- 这是**跨仓库的两端缺陷**：数据生产端发布了错误映射，消费端既未甄别该符号的缺失，也无 optional 语义。

## 正确做法

### 数据端（GoldSrc_VibeSignatures）

- PR #337 删除 `bin_artifacts/{hl-3248,hl-3266,hl-3329,hl-3647}/engine/R_ForceCVars.windows.yaml`，
  并把这四个 build 的 config 中该符号声明与 `expected_output` 一并删除（`Cvar_DirectSet` 进入 `expected_input`）。
- **缺符号用「config + artifact 同步删除」表达，不新增符号级 `optional` 标记**：PR-validation 的 plan job
  跑的是受信 baseline planner，任何新 schema 字段无法在引入它的那个 PR 里生效；且「已声明但缺 artifact」会
  触发 `require_complete` 清单检查失败。参见上游 `memory/locators/R_ForceCVars.md`。
- 修复**必须重新发布**才生效：`main` 合并只改源码/artifacts，线上 `gamesymbols/index.json` 与
  `<gamever>.<sha256>.json` 由 `release-build.yml` 产出、`deploy-pages.yml` 部署。入口是
  `.claude/skills/trigger-release-build/scripts/trigger_release_build.py <VERSION> --source-artifact-mode tracked`。
  tracked 模式跳过 warmup-idb/IDA，仅绑定已提交 artifacts，适合此类纯配置+artifact 变更。

### 消费端（Renderer）

1. `scripts/manifests/renderer.json`：把 `R_ForceCVars` 从「全 11 版本必需」组**移出**，放进显式列出
   **仍发布该符号的 7 个版本**的独立组（`cof-5936, hl-10210, hl-4554, hl-6153, hl-8684, svencoop-10257,
   svencoop-8948`）。不要图省事塞进别的已有组（如 `DT_Initialize` 的 3 版本组）——那会在其余版本静默
   跳过「符号意外消失」的校验。缺符号的版本一律**不进任何组**。
2. `src/gl_hooks.cpp`：解析改用仓库既有的 `GamedataResolvePtrIfAvailable`（缺失返回 `nullptr`，而非
   `GamedataResolvePtr` 的 fatal），并在装 hook 处加 `if (gPrivateFuncs.R_ForceCVars)` 守卫。
3. **不要**只给 `AllowCheats` 加判空：那只能避免崩溃，无法阻止全局 `Cvar_DirectSet` 被劫持。

## 验证方式

| 步骤 | 命令 | 期望 |
| --- | --- | --- |
| gamedata 同步 | `python scripts/sync-gamedata.py --manifest scripts/manifests/renderer.json --target-dir <assets>/svencoop/metahook/gamedata/renderer --temp-root <build>/gamedata-sync` | passed |
| gamedata 校验 | `python scripts/validate-gamedata.py <assets 同上> --manifest scripts/manifests/renderer.json` | 修复前报 `missing conditional symbol 'R_ForceCVars'`；修复后 passed |
| 构建 | `cmake --build <build> --config Debug --target Renderer` | 成功 |
| 实机 | 安装后运行 `metahook_blob.exe -insecure -game cstrike` | 进程稳定存活、`Responding=True`、无新 `*.mdmp` |

排障要点：

- dump 分析用 `cdb -z <dump> -c ".symfix; .reload; .ecxr; kb 40; q"`，符号路径加上构建输出目录
  （`build/lib/Debug`）与 game 目录（`MetaHook.pdb`/`MetaHook_blob.pdb`）。
- 判定 gamedata 是否含某符号时，**以 pruned 产物（`assets/.../gamedata/renderer/<gv>.json`）或线上快照为准**；
  `build/**/gamedata-sync/raw` 可能残留旧缓存快照，会得出相反结论。

## 适用范围

- 结论适用于「引擎私有符号在某些 build 上被内联/不存在」的一般情形，`R_ForceCVars`、`DT_Initialize` 是
  同一模式的实例。
- 具体版本清单（7 个存在 / 4 个内联）绑定到上游 Release `v20261006b` 时点的 gamedata；上游新增/移除
  build 后需重新核对，不要照抄。
- 「不新增符号级 optional 字段」是上游 planner 的契约约束，属生产端；消费端 manifest 的 `when`/`optionalSymbols`/
  `symbolExemptions` 仍是有效表达手段。
