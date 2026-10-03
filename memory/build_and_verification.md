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
- MetaHook、VGUI2Extension、FreeImage、GLEW 默认使用 FetchContent 获取固定提交，显式 `*_SOURCE_PATH` 跳过对应获取；Capstone 的当前来源见下方宿主优先条目，ScopeExit、tinyobjloader 仍使用固定 submodule。外部源码不被写入。
- VGUI2Extension 仅提供编译期公共接口头文件，接口目录优先于 MetaHook 的历史副本；Renderer 不构建该插件，运行时插件仍可选。
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

### VGUI2Extension 接口改用独立仓库（2026-10-03）

- 触发：用户从 `D:/MetaHook/include/Interface` 删除 `IVGUI2Extension.h`，并确认 Renderer 应接入独立 VGUI2Extension 仓库。
- 根因 / 约束：Renderer 的 `VGUI2ExtensionImport.h`、`BaseUI.cpp`、`GameUI.cpp` 直接包含该头文件，原构建只提供 MetaHook 的接口目录，隐含依赖宿主历史副本。编译期接口必须可用；`VGUI2Extension_Init` 在 DLL 未加载时直接返回，所以运行时插件保持可选。
- 实现：新增 `VGUI2EXTENSION_SOURCE_PATH` cache 参数与首次配置同名环境变量，指向仓库根目录。显式路径相对本工程根目录规范化，并在下载前检查 `IVGUI2Extension.h`、`IDpiManager.h`、`IInput2.h`、`IScheme2.h`、`ISurface2.h`；无效路径明确报错。空值复用 `renderer_fetch_source()` 获取 `MetaHookSv/VGUI2Extension@cd7ef6e3b7fb51d3c98e6d7dadec02dd1fa08c4f`，只下载源码。`renderer_settings` 将其 `include/Interface`、`include/Interface/VGUI` 排在 MetaHook 路径之前，插件与已有测试共用。
- CI：共用 composite action 在 Renderer 同级目录克隆 MetaHook、VGUI2Extension 的 `main`，分别记录 SHA，写入环境变量，并显式向 Release BAT 传入新参数。本地默认获取仍使用固定提交。英文、中文构建说明与项目依赖约定同步更新。
- 验证：Release 传入相对路径 `../VGUI2Extension`，解析为 `D:/VGUI2Extension`，在宿主缺失 `IVGUI2Extension.h` 的条件下配置、编译、安装退出 0，CTest 4/4。Debug 新参数设为空值，实际下载 HEAD 为固定 SHA，配置、编译、安装退出 0，CTest 4/4。编译器 `CL.read.1.tlog` 确认接口来自各自 VGUI2Extension 树，未读取 MetaHook 的同名扩展接口。
- 错误路径：首次配置通过环境变量指定不存在的目录，以及显式指定仅含 `IVGUI2Extension.h` 的残缺目录，两例均在配置阶段返回 1，分别提示缺少 `IVGUI2Extension.h`、`IDpiManager.h`。
- CI 本地验证：actionlint 退出 0；PyYAML 解析 composite action 并在独立临时目录执行实际克隆步骤，退出 0，MetaHook HEAD 为 `ace5d9f7d2567fa6fd00f2b8cf2a35d47fd80066`，VGUI2Extension HEAD 为上述固定 SHA，四项环境变量均写入。日志在 `build/verification/vgui2extension-*.log`，临时 CI 目录为 `build/verification/ci-vgui2extension-412d11002542466fa418c4a26c340ebd/`。
- 接口核对：DPI、Scheme、Surface 头文件与本地 MetaHook 历史副本哈希一致；Input 头文件在末尾追加 `CancelIMEComposition`，既有方法顺序未变。Renderer 本次未修改运行代码或接口调用。
- 适用范围：本地构建及模拟回归验证。未执行本次改动的云端 workflow 或真实游戏验证；外部 MetaHook、VGUI2Extension 源码工作区保持原状。

### Capstone 头文件改为宿主优先（2026-10-03）

- 触发：用户提出 Capstone 必定存在于 MetaHook 的 `thirdparty/capstone_fork`，能否直接作为 `CAPSTONE_INCLUDE_DIRS` 来源。
- 约束：「必定存在」只在宿主为完整克隆且已初始化子模块时成立。默认路径下 MetaHook 由 FetchContent 获取，`GIT_SUBMODULES ""` 使其 `thirdparty/capstone_fork` 为空目录（实测），故不能作为唯一来源。两处仓库同为 `hzqst/capstone`，且 MetaHook 在 FetchContent pin `4d23b6fe` 处的 gitlink 与原 Renderer pin 同为 `e81e390f`；但 CI 克隆的是 MetaHook `main`（浮动），版本没有契约，所以只能作为优先项。
- 实现：`CAPSTONE_INCLUDE_DIRS` 显式值时优先；否则先用 `${METAHOOK_SOURCE_PATH}/thirdparty/capstone_fork/include/capstone`（存在 `capstone.h` 时），缺失则 `renderer_fetch_source(renderer_capstone, hzqst/capstone, e81e390f)`。删除 `thirdparty/capstone_fork` 的 gitlink、`.gitmodules` 条目、本地目录和 `.git/config` 段；仓库仅剩 ScopeExit、tinyobjloader 两个 submodule。
- 验证：宿主优先（Release + `-DMETAHOOK_SOURCE_PATH=D:/MetaHook`）解析为 `D:/MetaHook/thirdparty/capstone_fork/include/capstone`，配置/编译/安装退出 0，CTest 4/4；兜底（Debug，无 MetaHook 路径）解析为 `_deps/renderer_capstone-src/include/capstone`，同样退出 0、CTest 4/4；显式 `-DCAPSTONE_INCLUDE_DIRS=<另一目录>` 覆盖宿主路径；无效目录返回 1。日志在 `build/verification/{capstone-host-release,capstone-fetch-debug,cap-precedence,neg-capstone}.log`。
- 适用范围：仅改变头文件来源与依赖获取方式，未执行游戏验证。宿主副本仅在宿主已初始化其子模块时可用。CI 同步删去 `CAPSTONE_INCLUDE_DIRS` 的显式传参（`.github/actions/build-windows-x86/action.yml`），改由宿主优先分支解析，`capstone_fork` 的子模块初始化保留；CI 本身未在本机执行，待下次 push 覆盖。

### GLEW / FreeImage 改用 FetchContent（2026-10-03）

- 触发：用户要求 `GLEW_SOURCE_PATH`、`FREEIMAGE_SOURCE_PATH` 为空时也回退到 FetchContent，与 MetaHook 模式一致。
- 约束：`FetchContent_MakeAvailable` 会在依赖准备阶段（根 `CMakeLists.txt:17`）就 `add_subdirectory`，早于 VC-LTL helper 的 include（`:40`）和 GLEW 的三个选项强制（`:44-46`），并会激活 FreeImage 子工程自身的 install 规则；因此只能下载源码，不能接管加入时机。单参数 `FetchContent_Populate` 已由 CMP0169（CMake 3.30）废弃，故采用完整参数形式（官方文档明确仍完全支持）。
- 实现：`cmake/Dependencies.cmake` 新增 `renderer_fetch_source()`，固定提交 `hzqst/glew-cmake@56ed32d4…`、`hzqst/FreeImage_clone@c68700b9…`，下载到 `build/x86/<配置>/_deps/renderer_{glew,freeimage}-src`，`GIT_SUBMODULES ""` 不初始化嵌套依赖；根 CMakeLists 的 `add_subdirectory` 与 target 校验不变。移除 `.gitmodules` 两条目、两个 gitlink、本地目录及 `.git/config` 段；ScopeExit、tinyobjloader 仍为 submodule（Capstone 的归属见上方同日条目）。
- 验证：Debug 走 FetchContent（联网）配置、编译、安装退出 0，CTest 4/4，安装产物 `FreeImaged.dll`；Release 以相对路径显式指向 Debug 下载的源码副本，退出 0，CTest 4/4，产物 `FreeImage.dll`，两棵下载树的文件哈希前后一致。无效 GLEW、无效 FreeImage 两例在配置阶段返回 1。日志在 `build/verification/fetch-debug.log`、`explicit-path-release.log`、`neg-{glew,freeimage}.log`。
- 适用范围：仅改变依赖获取方式，未执行游戏验证。首次配置需联网获取 GLEW 和 FreeImage，离线需显式传 `*_SOURCE_PATH` 或复用已预热的 build 目录。CI 未传这两个路径，将在下次 push 时首次走 FetchContent 分支，本机未验证 CI。
- 取代：上方「原因与构建边界」中「其他源码依赖使用固定 submodule」的表述，以及 2026-10-02 实测表中 GLEW/FreeImage 的 submodule 记录，均为本次改动前的历史状态。

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
