# AGENTS.md

This file provides guidance and important rules working with code in this repository.

## When coding / building plan

- Use a progressive disclosure approach for agent coding in this repository: start from high-level information in the Basic Memory knowledge base first, and only locate/read specific files or symbols when necessary, instead of expanding a large amount of context at once.

#### Basic Memory knowledge base (project-scoped, `memory/`)

- Notes live in `memory/` (markdown with YAML frontmatter: `title`/`type`/`permalink`), tracked in git.
- This repository contains the standalone Renderer plugin, extracted from MetaHookSv `Plugins/Renderer` and `Build/svencoop/renderer`. Its notes were migrated from MetaHookSv and adapted to the CMake workspace; see `memory/project_overview.md` for scope and provenance.
- Basic Memory is registered as MCP server `basic-memory`, pinned to the `renderer` project (project-level `.mcp.json`, mirrored by `.codex/config.toml`). The `metahooksv` project belongs to the source repository.
- Prefer Basic Memory MCP tools (`search_notes` / `read_note` / `write_note` / `edit_note`) only when their project resolves to this repository's `memory/` directory. Verify the project binding before writing; when no matching project is available, read and edit the local markdown files directly.
- Notes use the `renderer/` permalink prefix to distinguish them from the source repository.
- Historical records are not current evidence: `Renderer.md` retains verification logs and `Plugins/Renderer/` paths from the source repository, while current source paths are `src/<file>`. Each entry in `build_and_verification.md` states its own applicability; do not extend an old result to a new change.

#### High-level information in this repository (read corresponding notes first)

- Project overview, dependency boundaries and entry points: `project_overview`
- Rendering architecture and historical experience: `Renderer`
- Build commands, dependency pinning, gamedata sync, verification status: `build_and_verification`
- Engine-private symbol inventory: `PrivateSymbols.md`
- Coding conventions: `CodeStyles`

#### When notes are insufficient: source entry points (query and read on demand)

- Build: `CMakeLists.txt`, `cmake/Sources.cmake` (explicit compile list), `cmake/Dependencies.cmake` (source-path resolution and FetchContent fallback), `cmake/VCLTL.cmake`, `scripts/build-Renderer-x86-{Debug,Release}.bat`
- Plugin sources: `src/`; lifecycle entry `src/plugins.cpp`, hooks `src/exportfuncs.cpp` and `src/gl_hooks.cpp`, internal state `src/gl_local.h`, main render loop `src/gl_rmain.cpp`
- Public API / interface: `include/Interface/IMetaRenderer.h`, provided and installed by this repository and taking precedence over MetaHook's historical copy
- Runtime assets: `assets/svencoop/renderer/` (shaders, textures, configuration, localization), installed alongside the DLL
- gamedata: `scripts/manifests/renderer.json` (same schema as MetaHook), `scripts/sync-gamedata.py`, `scripts/validate-gamedata.py`; the build-time sync prunes the upstream catalog into the nested `metahook/gamedata/renderer/` directory, which the host launcher merges
- Tests: the four existing suites under `tests/`, run by CTest with `RENDERER_BUILD_TESTS=ON`
- Docs: `README.md` / `README.zh-CN.md`, prose pages under `docs/en/` and `docs/zh-CN/`
- External sources, all read-only inputs: `METAHOOK_SOURCE_PATH` (public API, SourceSDK, VGUI), `VGUI2EXTENSION_SOURCE_PATH` (public interface headers only; the plugin is not built here), `FREEIMAGE_SOURCE_PATH`, `GLEW_SOURCE_PATH`. `SDL2_INCLUDE_DIRS` and `CAPSTONE_INCLUDE_DIRS` resolve with host-first defaults, `SDL3_INCLUDE_DIRS` is optional. Empty paths fall back to fixed-commit FetchContent; only `thirdparty/ScopeExit` and `thirdparty/tinyobjloader` are submodules.
- Build output: `build/x86/<configuration>/`; install output: `install/x86/<configuration>/`. Neither is tracked, and nothing is deployed to the game automatically.

#### Progressive disclosure key points

- Read notes first, then locate a single file/symbol; do not read the whole repository at once.
- Prefer correctly scoped Basic Memory MCP tools for knowledge retrieval; otherwise use the local notes before reading source.
- Prefer Context7 for external dependency/library usage (query on demand).

## Repository rules

- Preserve the MetaHook API, plugin exports, calling conventions and original rendering behavior. Match the naming, indentation and comment style of the files you touch.
- Resolve engine and client private symbols through the host `ResolveGameSymbol`/gamedata contract. Do not add scan fallbacks for symbols gamedata already provides.
- Consume the `RENDERER_*_INCLUDE_DIRS` paths produced by the dependency preparation functions; do not bypass the normalized results by using the raw cache inputs.
- When gamedata usage changes, update `scripts/manifests/renderer.json` in the same change.
- Do not modify external sources or third-party sources. SDL building and packaging belong to MetaHook; this repository only reads the include directories.
- Regression tests keep assertions enabled even in Release (`/UNDEBUG`); documentation and configuration text are not assertion targets.
- Verification distinguishes build/simulated tests from a real game run. Claims about game or OpenGL visual compatibility must not be made without evidence. Documentation changes need content, path and format checks, not a plugin rebuild.

## Explore SKILLs

- Project-level skills, when present, live in `.claude/skills` no matter what harness tool is being used.
