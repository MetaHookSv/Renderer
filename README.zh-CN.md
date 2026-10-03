# MetaRenderer

[English README](README.md)

MetaRenderer 是 MetaHookSv 的图形渲染插件，用现代 OpenGL 渲染管线替换原版 GoldSrc 渲染器，
显著提升渲染性能，并带来 HDR、延迟着色、动态光照与阴影、水面着色器、模型卡通渲染等画质特性。

* 需要支持 OpenGL 4.4 Core Profile 的 GPU。
* 与 ReShade 不兼容。

## 兼容性

|        Engine               |      |
|        ----                 | ---- |
| GoldSrc_blob   (3248~4554)  | √    |
| GoldSrc_legacy (4554~6153)  | √    |
| GoldSrc_new    (8684 ~)     | √    |
| SvEngine       (8832 ~)     | √    |
| GoldSrc_HL25   (>= 9884)    | √    |

## 快速开始

从 [GitHub Release](https://github.com/MetaHookSv/Renderer/releases) 下载
`Renderer-windows-x86.7z`（推送 `v*` 标签时构建）。

将解压出的 `svencoop/` 合并到对应 mod 目录，在 MetaHook 的
`metahook/configs/plugins.lst` 中启用 Renderer。最后，别忘了从MetaHook启动游戏。

## 文档

- [构建说明：构建、依赖与 CI 细节](docs/zh-CN/build-instruction.md)
- [安装说明：安装目录布局与启用插件](docs/zh-CN/installation.md)
- [功能说明：兼容性、GPU 需求、特性与控制台参数](docs/zh-CN/features.md)

## 许可证

项目采用 [MIT License](LICENSE)；各依赖保留自己的许可证。
