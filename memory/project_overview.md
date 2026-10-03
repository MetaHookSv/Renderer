---
title: project_overview
type: note
permalink: renderer/project-overview
---

# Renderer 独立工程

## 范围与来源

本仓库迁入 MetaHookSv `11a852774b1725d02735aeb348c32a7bf454507c` 的
`Plugins/Renderer` 和 `Build/svencoop/renderer`，提供 Windows x86 Debug/Release CMake 构建。
渲染源码、已有测试源码、运行资源保留原样；不迁入其他插件、宿主实现或预编译依赖。

## 架构与入口

- `src/plugins.cpp`：MetaHook 插件生命周期、宿主 API、FreeImage 版本检查。
- `src/exportfuncs.cpp`、`src/gl_hooks.cpp`：客户端/引擎 hook 和私有符号接入。
- `src/gl_local.h`：内部状态和跨模块声明。
- `src/gl_rmain.cpp`：主渲染流程；`gl_studio.cpp`、`gl_wsurf.cpp` 等实现各类渲染。
- `src/gl_scclient.cpp`：Sven 客户端 Core Profile 与 portal 兼容逻辑。
- `src/gl_draw.cpp`：纹理读取和 FreeImage 调用。
- `assets/svencoop/renderer`：随 DLL 安装的 shaders、textures、配置及本地化资源。

## 依赖边界

Renderer 自身公共接口为 `include/Interface/IMetaRenderer.h`，从 MetaHook 原样迁入，
优先使用本仓库副本并随 install 发布；MetaHook 来源副本保留不变。

MetaHook 提供公共 API、SourceSDK、VGUI 代码，仅消费其源码，不构建宿主可执行文件。
指定 `METAHOOK_SOURCE_PATH` 时直接使用外部仓库根目录；否则通过 FetchContent 获取固定版本，
不使用 MetaHook submodule，也不初始化宿主的递归依赖。
GLEW 静态链接，FreeImage 动态链接并随插件安装。SDL/Capstone 只消费头文件。
SDL2/SDL3 由 MetaHook 构建、安装；Renderer 的 `SDL2_INCLUDE_DIRS` 必需，
`SDL3_INCLUDE_DIRS` 可选，均仅提供外部 include 目录，不存在 SDL submodule 或构建目标。
VGUI2Extension 的公共接口头文件由独立仓库提供：`VGUI2EXTENSION_SOURCE_PATH` 指向仓库根目录，
空值通过 FetchContent 获取固定提交。仅消费 `include/Interface` 和 `include/Interface/VGUI`，
优先于 MetaHook 的历史副本，不构建 VGUI2Extension；运行时插件仍可选。
gamedata 的查询/解析 API 由宿主 MetaHook 提供；Renderer 自带 manifest 与同步脚本，构建时产出裁剪后的嵌套 catalog `metahook/gamedata/renderer/`，宿主加载时与自身 catalog 取并集。详见 [构建及验证](build_and_verification.md)。

私有符号解析必须遵循宿主 gamedata 契约，不能通过兼容性判断恢复已删除的扫描 fallback。
迁移不改变导出、hook 调用约定、OpenGL 行为或已有资源格式。

## 知识入口

- [Renderer 架构和历史经验](Renderer.md)
- [构建及验证](build_and_verification.md)
- [使用和依赖说明](../README.md)

Basic Memory 尚未注册到本仓库；源项目 `metahooksv` 只用于查阅来源，不是写入目标。
