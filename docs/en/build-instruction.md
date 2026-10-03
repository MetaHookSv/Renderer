[Back to README](../../README.md) | [中文](../zh-CN/build-instruction.md)

# Build instruction

This page covers the build, dependency and CI details of the Renderer plugin.
For the install layout and enabling the plugin, see [Installation](installation.md);
for engine/GPU compatibility, features and console variables, see [Features](features.md).

Renderer is the Windows x86 CMake project for the MetaHook Renderer (internal name MetaRenderer) plugin.

Renderer's own public interface lives in `include/Interface/IMetaRenderer.h`.

## Requirements

- Windows with Visual Studio 2022 C++ desktop workload and the Windows SDK
- CMake 3.21+
- Git
- Network access on first configure: CMake fetches MetaHook, FreeImage and GLEW via FetchContent (Capstone too when the host does not provide it), initializes the other required submodules, and downloads, verifies and extracts VC-LTL 5.3.1
- A MetaHook build for the target configuration, providing the `SDL2_INCLUDE_DIRS` directory (see below)

## Build

```bat
scripts\build-Renderer-x86-Debug.bat "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Debug/include"
scripts\build-Renderer-x86-Release.bat "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

Both entry points use `Visual Studio 17 2022 -A Win32` and perform configure, build and
install. They can be invoked from outside the project; when `SolutionDir` is unset the
project is located from the script path. Initialization, download, configure, compile or
install failure returns a non-zero exit code. Build directories are `build/x86/Debug`
and `build/x86/Release`. This project provides plain Debug and Release, no AVX2 entry point.

## Specifying source paths manually

MetaHook, FreeImage and GLEW are downloaded automatically at fixed versions. To reuse
local sources, pass any of these optional parameters:

| Parameter | Local source directory |
| --- | --- |
| `METAHOOK_SOURCE_PATH` | MetaHook repository root with `include/metahook.h` and the HLSDK, SourceSDK and VGUI sources |
| `FREEIMAGE_SOURCE_PATH` | FreeImage_clone root with `CMakeLists.txt` and `Source/FreeImage.h` |
| `GLEW_SOURCE_PATH` | glew-cmake root with `CMakeLists.txt` and `include/GL/glew.h`, providing `libglew_static` |

Build MetaHook for the desired configuration first, then set the required
`SDL2_INCLUDE_DIRS` to its installed `include` directory containing `SDL2/SDL_video.h`.
Renderer only uses SDL headers; MetaHook handles its build and installation.

For example, using local sources for a Release build:

```bat
scripts\build-Renderer-x86-Release.bat ^
  "-DMETAHOOK_SOURCE_PATH=D:/MetaHook" ^
  "-DFREEIMAGE_SOURCE_PATH=D:/FreeImage_clone" ^
  "-DGLEW_SOURCE_PATH=D:/glew-cmake" ^
  "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

Optional header paths:

- `CAPSTONE_INCLUDE_DIRS`: a directory containing `capstone.h` or `capstone/capstone.h`.
  Headers must match the host MetaHook's Capstone version. By default, Renderer uses
  MetaHook's copy or downloads a fixed version if unavailable. No Capstone library path is needed.
- `SDL3_INCLUDE_DIRS`: a directory containing `SDL3/SDL.h`; normally unnecessary.

All paths above also accept same-named environment variables on first configure.
Use `-DNAME=value` to change a cached value; `-DNAME=` restores automatic fetching for
the source paths or default lookup for Capstone. Header paths accept semicolon-separated
directory lists; quote the entire `-D` argument.

## gamedata

Renderer declares the engine/client private symbols it resolves (including
per-gameVersion conditional groups and module ownership) through
`scripts/manifests/renderer.json`, using the same schema as MetaHook.

At build time `RENDERER_SYNC_GAMEDATA` (default `ON`) invokes `scripts/sync-gamedata.py`
to trim the upstream catalog to just those symbols and publish it to
`metahook/gamedata/renderer/`, then `scripts/validate-gamedata.py` validates it against
the manifest.

The raw snapshot is cached persistently under `build/x86/<config>/gamedata-sync/` and
supports offline builds.

With `OFF`, only existing data is installed and nothing is downloaded. This nested
directory is merged by the host launcher's catalog loader.

## Dependencies and build conventions

- CMake prepares the dependencies automatically; no manual recursive submodule update is needed.
  Dependency versions are recorded in [cmake/Dependencies.cmake](../../cmake/Dependencies.cmake)
  and the repository's submodule revisions.
- VC-LTL 5.3.1 is downloaded and verified automatically. Its cache defaults to
  `thirdparty/cache/`; set `-DRENDERER_DEPENDENCY_CACHE_DIR=<path>` on first configure to change it.
- For offline builds, prepare all dependencies and the gamedata cache with an online build first.
  Local source paths alone do not cover every download.
- External source directories are left unchanged. Build output stays under `build/`,
  and the default install location is `install/x86/<configuration>/`.
  Both Debug and Release include a Renderer PDB for debugging; deployment to the game is manual
  (see [Installation](installation.md)).

## Regression tests

The four existing test groups were migrated with the source and run through an optional
CTest target:

```bat
scripts\build-Renderer-x86-Release.bat -DRENDERER_BUILD_TESTS=ON "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
ctest --test-dir build/x86/Release -C Release --output-on-failure
```

## License

See [LICENSE](../../LICENSE); each dependency keeps its own license. The main project
ships no prebuilt third-party artifacts.
