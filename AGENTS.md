# AGENTS.md

## 项目上下文

- 本工程是从 MetaHookSv 迁出的 Renderer 插件，优先读 `memory/project_overview.md`，构建细节读 `memory/build_and_verification.md`，渲染架构读 `memory/Renderer.md`，再定位具体源码。
- 笔记为 Git 跟踪的 YAML frontmatter Markdown，permalink 使用 `renderer/` 前缀。
- 仅在 Basic Memory 项目路径绑定到本仓库 `memory/` 时使用对应 MCP 工具；现有 `metahooksv` 项目属于源仓库，不用于写入本仓库。未注册匹配项目时直接读写本地 notes。
- 源码在 `src/`，运行资源在 `assets/svencoop/renderer/`，原有测试在 `src/tests/`。
- Renderer 公共接口由本仓库 `include/Interface/IMetaRenderer.h` 提供并安装；优先于 MetaHook 中的历史副本。
- 构建入口是根 `CMakeLists.txt` 和 `scripts/build-Renderer-x86-{Debug,Release}.bat`。`cmake/Sources.cmake` 显式保留原工程编译清单。
- 公共 API、SourceSDK 和 VGUI 源码来自 `METAHOOK_SOURCE_PATH`；`FREEIMAGE_SOURCE_PATH`、`GLEW_SOURCE_PATH` 分别指向外部 FreeImage、glew-cmake 源码树。外部源码和第三方 submodule 均为只读输入。
- 三个 `*_SOURCE_PATH` 空值时都用 FetchContent 获取固定提交，只下载源码、不接管 `add_subdirectory` 时机。`GLEW_SOURCE_PATH` 需提供 `libglew_static` target，`FREEIMAGE_SOURCE_PATH` 需包含 `Source/FreeImage.h`。Capstone 未显式指定时优先取宿主 MetaHook 树的 `thirdparty/capstone_fork/include/capstone`，宿主无该副本才 FetchContent 固定提交。Capstone/SDL 的编译目录使用依赖准备函数输出的 `RENDERER_*_INCLUDE_DIRS`，不可绕过规范化结果直接使用原始 cache 输入。
- 未指定 `METAHOOK_SOURCE_PATH` 时用 FetchContent 获取固定提交，只消费 SDK，不构建宿主或初始化其递归依赖；MetaHook、GLEW、FreeImage、Capstone 均不作为本仓库 submodule。
- SDL 构建及安装归 MetaHook 所有；本工程只读取必需的 `SDL2_INCLUDE_DIRS` 和可选的 `SDL3_INCLUDE_DIRS`，不获取 SDL 源码或引入 SDL 构建目标。
- 依赖准备由 CMake 执行；VC-LTL 下载二进制并校验哈希，不作为 submodule。输出留在 `build/`、`install/`，不自动部署游戏。

## 修改与验证

- 保持 MetaHook API、插件导出、调用约定和原有渲染行为；沿用具体文件的命名、缩进和注释风格。
- 引擎和客户端私有符号由宿主的 `ResolveGameSymbol`/gamedata 契约提供。对已由 gamedata 提供的符号，不引入扫描 fallback。
- 原有四组测试通过 `RENDERER_BUILD_TESTS=ON` 和 CTest 运行；按改动范围选择构建及行为验证，不用测试固定文档或配置文本。
- 区分构建/模拟测试和真实游戏验证；没有证据不能声称游戏兼容性已验证。
- 文档改动做相关内容、路径和格式核对，无需因此重编译插件。
- 项目级 skills 若存在，位于 `.claude/skills`，按任务相关性加载。
