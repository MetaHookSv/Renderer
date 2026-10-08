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
| GoldSrc_CoF    (5936)       | √    |

## Quick start

Download `Renderer-windows-x86.7z` from
[GitHub Releases](https://github.com/MetaHookSv/Renderer/releases) (built on `v*` tag pushes).

Merge the extracted `svencoop/` into the target mod directory and enable Renderer in
MetaHook's `metahook/configs/plugins.lst`. Don't forget to launch game from MetaHook.

## Documentation

- [Build instruction](docs/en/build-instruction.md)
- [Installation](docs/en/installation.md)
- [Features](docs/en/features.md)
- [F5 debugging (optional)](docs/en/debugging.md)

## License

Licensed under the [MIT License](LICENSE); each dependency keeps its own license.

## C/C++ formatting

Formatting uses [MetaHookSv/FormatValidation](https://github.com/MetaHookSv/FormatValidation)
and clang-format **23.1.3**, with the DiligentCore style (4 spaces, preserved include
order). Install the formatter for the Python interpreter used by CMake:

```sh
python -m pip install clang-format==23.1.3
cmake -S . -B build/format "-DFORMAT_VALIDATION_ONLY=ON"
cmake --build build/format --target format-check
cmake --build build/format --target format
```

The format-only configuration needs CMake 3.21+, Git, Python 3.9+ (CI uses 3.12),
and a build generator; `-G Ninja` works without Visual Studio. It prepares no native
SDK or game dependencies. Formatting targets are explicit and are not part of a
normal DLL build. With a Visual Studio generator, add `--config Debug` or
`--config Release` when building a formatting target.

The aggregate provides `FORMAT_VALIDATION_SOURCE_PATH=thirdparty/FormatValidation`.
Standalone components accept that CMake variable or its environment counterpart;
if empty, FetchContent downloads the fixed tooling commit. Quote relative paths,
for example `"-DFORMAT_VALIDATION_SOURCE_PATH=../../thirdparty/FormatValidation"`.
Configuration generates the ignored root `.clang-format` for editors; change the
shared style rather than that generated copy. An optional
`FORMAT_VALIDATION_CLANG_FORMAT_EXECUTABLE` selects an explicit formatter, whose
version must still match the pin.

Checks cover owned C/C++ files in `src/`, `include/`, and `tests/`, including
non-ignored new files. Repository-relative exclusions live in `.clang-format-ignore`.
Third-party sources and build artifacts are excluded. The `clang-format` workflow
checks the full scope on pushes, pull requests, and manual runs.
