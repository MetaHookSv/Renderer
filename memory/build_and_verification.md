---
title: build_and_verification
type: note
permalink: renderer/build-and-verification
---

# Renderer CMake 迁移与验证

## 触发与范围

用户要求按 MetaHook 独立工程规则，将 MetaHookSv 的 Renderer 迁入独立仓库，支持
CMake 和手动指定 `FREEIMAGE_SOURCE_PATH`、`METAHOOK_SOURCE_PATH`。
基线为 MetaHookSv `11a852774b1725d02735aeb348c32a7bf454507c`。

## 原因与构建边界

原 `.vcxproj` 依赖 SolutionDir、公共 props、MetaHook 的 include 源码和预先安装的库，
post-build 还会复制到本机游戏。独立工程使用显式源码清单和 CMake target 依赖，
统一安装到 `install/x86/<configuration>/svencoop`。

- `cmake/Sources.cmake` 保留原 139 个编译单元：Renderer、HLSDK、SourceSDK、VGUI、tinyobjloader。
- `METAHOOK_SOURCE_PATH` 仅提供公共源码，既不构建宿主，也不递归初始化它的依赖。
- `FREEIMAGE_SOURCE_PATH` 接入原有 FreeImage CMake shared target，在本工程 build 树内构建其内置图像格式库。
- MetaHook 默认使用 FetchContent 获取固定提交，显式 `METAHOOK_SOURCE_PATH` 跳过获取；其他源码依赖使用固定 submodule，支持的显式路径覆盖会跳过对应初始化。外部源码不被写入。
- GLEW 构建静态库；SDL 和 Capstone 只使用头文件。
- SDL2/SDL3 源码和构建归 MetaHook 所有；本工程通过必需的 `SDL2_INCLUDE_DIRS` 和可选的 `SDL3_INCLUDE_DIRS` 消费外部头文件，不初始化或构建 SDL。
- VC-LTL 5.3.1 从官方二进制包下载并校验 SHA-256，使用包内 helper。所有相关目标继承同一设置，关闭 vendor 工程内的重复 helper。
- Renderer C++20、静态 CRT `/MTd`/`/MT`、Release LTCG 等与原配置对应。两种配置增加明确的 PDB 安装。
- 源码和资源保持字节一致；旧 vcxproj、个人工程设置、旧 MSBuild 测试 runner 不迁入。
- gamedata 查询/解析 API 由宿主 MetaHook 提供，Renderer 不改变该契约。Renderer 自带
  `scripts/manifests/renderer.json`（与 MetaHook 同 schema）与同步/校验脚本：构建时
  （`RENDERER_SYNC_GAMEDATA`，默认 ON）裁剪上游 catalog 到自身所需符号并发布到嵌套目录
  `metahook/gamedata/renderer/`，再由宿主 launcher 的 catalog 加载器与其取并集。原始快照
  持久缓存在 `build/x86/<config>/gamedata-sync/`，支持离线构建。

## 本次实测（2026-10-02）

环境：Windows，Visual Studio 2022，MSVC 19.44，Windows SDK 10.0.26100.0，CMake 3.31.12。

| 验证 | 结果 |
| --- | --- |
| 无构建产物的 Debug 入口，默认固定 submodule | 配置、编译、链接、安装退出 0；VC-LTL 首次下载校验成功 |
| 无构建产物的 Release 入口，指定 `D:/MetaHook`、`D:/FreeImage_clone` | 配置、编译、链接、安装退出 0 |
| 工程外 `D:/` 调用两种入口并增量构建 | 退出 0；同时覆盖默认目录推导及显式 `SolutionDir` |
| Debug / Release CTest | 每种配置 4/4 通过：模型校验、模型加载、ring buffer、Sven client hooks |
| DLL PE / imports / exports | Renderer、FreeImage 均为 x86 DLL；Renderer 导出 `CreateInterface`；按配置导入 FreeImaged.dll / FreeImage.dll；CRT 使用 msvcrt.dll |
| 生成工程设置 | Renderer、GLEW、FreeImage 均为 Debug MultiThreadedDebug / Release MultiThreaded，包含 VC-LTL 目录 |
| 源码/资源对照 | 72 个源码及测试文件、58 个运行资源、LICENSE 与源仓库字节一致；两配置各 139 个编译单元 |
| 安装核对 | DLL 与构建产物一致，PDB 存在，58 个运行资源逐一匹配 |
| 无效 MetaHook 路径 / 损坏 VC-LTL 下载缓存 | 两个 BAT 均返回 1，在 configure 阶段停止 |
| 无效 FreeImage 路径 | Release BAT 返回 1，给出缺少 CMakeLists.txt / Source/FreeImage.h 的诊断 |
| CMake 首次 submodule 初始化 | 在单独临时 Git 工程用同一函数初始化 ScopeExit，得到固定 SHA `bd345da...` |
| 源仓库和外部源码状态 | MetaHookSv、MetaHook、FreeImage_clone 工作区均保持干净 |
| 独立性 | 两个构建目录生成的 vcxproj 均无 `D:/MetaHookSv` 引用 |

构建有原有 SDK、FreeImage 图像格式库的编译警告以及 GLEW 旧 CMake 最低版本提示，未改动这些源码。
本机日志与 PE 报告位于被忽略的 `build/verification/`。

## 复验与适用范围

### 外部 GLEW 与有效 include 目录（2026-10-02）

- 触发：用户要求 `GLEW_SOURCE_PATH`，并要求检查 Capstone、SDL include 参数，避免漏传后在编译阶段失败。
- 根因：依赖准备函数已输出规范化的 `RENDERER_CAPSTONE_INCLUDE_DIRS` 和 `RENDERER_SDL2/3_INCLUDE_DIRS`，但根 CMake 使用原始 cache 参数，丢弃了默认 Capstone 目录和路径转换结果。
- 实现：`GLEW_SOURCE_PATH` 非空时使用外部 glew-cmake，空值初始化固定 submodule；检查源码文件和 `libglew_static` target。根 CMake 校验有效 include 目录及必要头文件，并将 `RENDERER_*_INCLUDE_DIRS` 用于编译。保留 Capstone 空值使用默认依赖、SDL2 必填、SDL3 可选的规则。
- 验证：Debug 使用默认 GLEW、空 Capstone、空 SDL3；Release 使用独立 GLEW 源码副本及相对路径的 Capstone `include`、SDL2/3。两种配置构建安装均退出 0，CTest 各 4/4 通过，外部 GLEW 工作区未修改。无效 GLEW、无效 Capstone、缺失或无效 SDL2、无效 SDL3 五组配置均按预期退出 1。
- 适用范围：只调整依赖输入和编译目录；日志在 `build/verification/dependency-inputs-<配置>.log` 及对应错误场景日志中。未执行游戏验证；此前 FreeImage 条目中暂未修复的 Capstone 路径问题现已解决。

### GLEW 头文件来自 target（2026-10-02）

- 触发：用户要求移除硬编码的 `thirdparty/glew_fork/include/GL`。
- 约束：`libglew_static` 公开的是 `include`，原 `qgl.h` 使用 `<glew.h>`，直接删除目录会导致头文件无法定位。
- 实现：`qgl.h` 改用 `<GL/glew.h>`；`renderer_settings` 从 `libglew_static` 的 `INTERFACE_INCLUDE_DIRECTORIES` 获取目录，让测试只消费头文件，保留插件的原有链接依赖。
- 验证：Debug / Release 脚本构建安装均退出 0，CTest 各 4/4 通过。日志位于 `build/verification/glew-target-<配置>.log`。
- 适用范围：仅修改头文件引用及目录来源，未修改第三方源码，未执行游戏验证；迁移时源码字节一致的历史记录不再适用于本次调整后的 `qgl.h`。

### FreeImage 头文件来自 target（2026-10-02）

- 触发：用户要求避免在消费者中重复拼接 FreeImage 源码布局。
- 约束：FreeImage target 已公开头文件目录；现有测试只需要其头文件，不链接 FreeImage 运行库。
- 实现：`renderer_settings` 使用 `$<TARGET_PROPERTY:FreeImage,INTERFACE_INCLUDE_DIRECTORIES>` 获取目录，供插件和测试共用，不再拼接 `${FREEIMAGE_SOURCE_PATH}/Source`。
- 验证：Debug / Release 构建安装退出 0，各自 CTest 4/4 通过。Release 首次因既有 Capstone 缓存指向 `include` 而找不到 `capstone.h`；显式改传 `include/capstone` 后通过，该路径处理问题未在本次修改中修复。
- 适用范围：仅调整头文件目录来源，保留 FreeImage 构建、链接和安装方式；未执行游戏验证。

### SDL 构建归属 MetaHook（2026-10-02）

- 触发：用户要求 Renderer 仅消费 SDL INCLUDE_DIRS，并确认同时在独立 MetaHook 中接入 SDL 构建和安装。
- 约束：Renderer 当前仅直接包含 SDL2/SDL_video.h，无 SDL3 直接头文件引用；不需要 SDL 库链接。移走依赖时保留原 fork SHA，不修改运行代码。
- 实现：移除 Renderer 的 SDL3_fork、sdl2-compat-fork gitlink、目录及初始化逻辑；新增 SDL2_INCLUDE_DIRS（必需）、SDL3_INCLUDE_DIRS（可选），支持分号列表和首次配置环境变量，先验证目录与头文件再准备其他依赖。MetaHook 构建 DLL、安装 SDK，Renderer 只读取其 include 目录。
- 验证：MetaHook Debug/Release 构建安装均退出 0；Renderer 使用对应 `install/x86/<配置>/include` 也均构建安装成功并各通过 4/4 CTest。Debug 不提供 SDL3_INCLUDE_DIRS 仍成功；依赖日志确认实际读取 MetaHook 安装目录的 SDL2 头文件。缺少 SDL2 路径或无效 SDL3 头文件目录均返回 1。生成工程无 SDL2/SDL3 构建目标。
- 适用范围：SDL 编译和分发职责调整。SDL 无窗口加载测试通过，完整游戏、窗口/设备和视觉兼容性尚未验证；早期表中的 SDL 源码依赖记录是历史状态。

### MetaHook 改用 FetchContent（2026-10-02）

- 触发：用户要求移除 MetaHook submodule，使用 FetchContent，并最终确认保留 `METAHOOK_SOURCE_PATH` 参数名。
- 约束：参数指向仓库根目录，SDK 包含 Renderer 要编译的 HLSDK、SourceSDK 和 VGUI 源码；仅有头文件的安装包不满足要求。
- 实现：非空路径直接消费并验证；空值通过 `FetchContent_Declare` / `FetchContent_MakeAvailable` 获取 `MetaHookSv/MetaHook` 的 `4d23b6fecd79dc949aabc2e145480cd1328d4a35`。`SOURCE_SUBDIR include` 指向没有 CMakeLists.txt 的 SDK 目录，避免执行宿主根构建；`GIT_SUBMODULES ""` 禁止递归依赖初始化。删除 `thirdparty/MetaHook` gitlink、目录及 `.gitmodules` 条目。
- 验证：Debug 从网络获取固定提交后构建、安装成功；Release 使用 `D:/MetaHook` 构建、安装成功；两配置各 CTest 4/4 通过。检查下载仓库 HEAD、全部嵌套 submodule 均未初始化、无 MetaHook.vcxproj 目标。Release 无 MetaHook 下载目录。无效显式路径返回 1，且未下载回退依赖。
- 范围：改变 SDK 获取方式，保留原编译单元、运行行为及 `METAHOOK_SOURCE_PATH` 用法。上方首次迁移验证表中的 MetaHook submodule 记录是历史状态，已由本项取代。

### 复用外部 Capstone 头文件（2026-10-02）

- 触发：用户希望通过 build 脚本传入 Capstone 路径，共用已有依赖。
- 约束：Renderer 仅使用 Capstone 头文件解析宿主反汇编回调，原本就不编译或链接 Capstone 库；头文件版本应与宿主保持一致。
- 实现：`CAPSTONE_INCLUDE_DIRS` 支持分号分隔目录、`include` / `include/capstone` 两种布局及首次配置环境变量。指定后跳过自带 Capstone submodule 初始化；空值恢复固定依赖。`CAPSTONE_LIBRARY_DIRS` 若传入则提示忽略。
- 验证：使用 `D:/MetaHook/thirdparty/capstone_fork` 的两种头文件路径完成 Debug / Release 构建、安装和各自 4/4 CTest；编译器依赖日志确认读取外部 capstone.h。无效路径使 BAT 在配置阶段返回 1。
- 范围：仅改变编译期头文件来源，不新增链接库或改变反汇编行为。

### Renderer 公共接口迁入（2026-10-02）

- 触发：用户要求将 MetaHook 的 `include/Interface/IMetaRenderer.h` 一并迁入。
- 约束：保持接口内容和 ABI；沿用来源仓库不改动的约定，保留 MetaHook 的历史副本。
- 实现：原样复制到本仓库同名路径，CMake 将本地 `include/Interface` 排在 MetaHook include 路径之前，并安装到 `include/Interface`。
- 验证：Debug、Release 重建及安装退出 0，各自 CTest 4/4 通过；编译器依赖记录确认读取本仓库头文件；本地及两份安装头文件与来源字节一致。MetaHook 工作区仍干净。
- 范围：仅迁移 Renderer 公共接口的提供位置；外部消费者需加入 Renderer 的接口头文件目录，基础 `interface.h` 仍使用 MetaHook SDK。

### GitHub Actions LiveBuild / Release（2026-10-02）

- 触发：用户要求复用 MetaHook 的 LiveBuild / Release 约定，MetaHook 本体克隆到同级目录，
  `METAHOOK_SOURCE_PATH`、Capstone、SDL2、SDL3 include 参数均指向该仓库。
- 约束：Renderer 只消费宿主 SDK 和依赖头文件；发布包只包含运行时 `svencoop/`。
  `upload-artifact` 默认 ZIP 封装会导致 7z 被套在 ZIP 中，单文件直接上传需显式 `archive: false`。
- 实现：`main` push/PR/manual 的 LiveBuild 与 `v*` 标签 Release 共用 composite action。
  在 workspace 同级 clone MetaHook `main`，只初始化 Capstone、sdl2-compat、SDL3，记录宿主 SHA；
  显式传入四个外部路径，用 Release BAT 构建安装，执行已有 CTest 与 gamedata 校验。
  清除同名旧生成归档后仅打包安装树 `svencoop/` 为 `Renderer-windows-x86.7z`，执行 `7z t`。
- 验证方式：actionlint、实际执行 composite 各步骤、CTest、gamedata validator、7z 解压后对照
  安装树文件集合与内容；推送后检查 hosted LiveBuild 和下载文件格式。
- 本地实测：actionlint 退出 0；临时全新 Renderer checkout 执行 composite 全部五个步骤均退出 0。
  MetaHook 克隆提交为 `1d23fe946e6f0f09a1a892aa2156c3b462774026`，四个外部路径生效；
  Release 构建安装成功，CTest 4/4、安装后 11 个 gamedata 快照校验通过。
  官方 7-Zip 26.03 创建及完整性检测成功；解压后 73 个文件的集合和 SHA-256 与安装树
  `svencoop/` 完全一致，归档为 5,037,694 字节，原始 7z magic 为 `377abcaf271c`。
  MetaHook checkout 保持干净，Renderer 自带 Capstone 未初始化；验证日志和报告位于
  `build/verification/ci-workspace-727319e26125443eadd04d98ae2f2820/`。
- 云端实测：功能提交 `06b62ebc246b5d2762fbae6471028f6b85917678` 的 LiveBuild run
  `37026929672` 成功。日志确认 MetaHook 位于 `D:/a/Renderer/MetaHook`，四个外部路径生效，
  CTest 4/4、11 个 gamedata 快照校验、7z 完整性检查及上传均成功。
  artifact `11235374746` 名为 `Renderer-windows-x86.7z`，4,939,562 字节；实际下载得到
  原始 7z magic `377abcaf271c`，没有 ZIP 外层。再次完整性检查、解压及 gamedata 校验通过；
  73 个文件路径与本地期望集合一致，58 个运行资源与仓库内容逐字节一致。
  云端日志为 `build/verification/ci-hosted-37026929672.log`，下载及复验产物位于
  `build/verification/ci-runtime-download-37026929672/`。Release 共用该构建步骤；标签发布事件
  需在实际版本标签推送时运行。
- 适用范围：自动构建和运行包发布，不代表真实游戏或 OpenGL 视觉兼容性验证。

构建命令、source path 参数、可选 CTest 开关见 [README](../README.md)。
源树或公共 API 更新后重新配置并执行相关构建与测试，避免把历史结果用于证明新改动。
测试保持 Release 断言启用；包含生产 translation unit 的测试通过 `/Gy` 和 `/OPT:REF` 移除未调用 handler。

本次没有启动游戏、创建真实 OpenGL context 或验证视觉效果。默认依赖和手动外部源码均经本机构建；
远端全新 checkout 已通过上述 hosted LiveBuild。CI 仅初始化 Renderer 的直接依赖和
MetaHook 的三个头文件 submodule，无需下载宿主的其余递归依赖。
