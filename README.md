# MetaRenderer

[中文文档](README.zh-CN.md)

MetaRenderer is a graphics enhancement plugin for MetaHookSv. It replaces the original
GoldSrc renderer with a modern OpenGL pipeline, greatly improving rendering performance
and adding HDR, deferred shading, dynamic lights and shadows, a water shader, studio
model celshade and more.

* A GPU supporting the OpenGL 4.4 Core Profile is required.
* It is not compatible with ReShade.

## Compatibility

|        Engine               |      |
|        ----                 | ---- |
| GoldSrc_blob   (3248~4554)  | √    |
| GoldSrc_legacy (4554~6153)  | √    |
| GoldSrc_new    (8684 ~)     | √    |
| SvEngine       (8832 ~)     | √    |
| GoldSrc_HL25   (>= 9884)    | √    |

## Quick start

Download `Renderer-windows-x86.7z` from
[GitHub Releases](https://github.com/MetaHookSv/Renderer/releases) (built on `v*` tag pushes).

Merge the extracted `svencoop/` into the target mod directory and enable Renderer in
MetaHook's `metahook/configs/plugins.lst`. Don't forget to launch game from MetaHook.

## Documentation

- [Build instruction: build, dependency and CI details](docs/en/build-instruction.md)
- [Installation: install layout and enabling the plugin](docs/en/installation.md)
- [Features: compatibility, GPU requirements, features and console variables](docs/en/features.md)

## License

Licensed under the [MIT License](LICENSE); each dependency keeps its own license.
