[Back to README](../../README.md) | [中文](../zh-CN/getting-started.md)

# Getting started

This page covers the build, install and dependency details of the Renderer plugin.
For engine/GPU compatibility, features and console variables, see [Features](features.md).

Renderer is the Windows x86 CMake project for the MetaHook Renderer (internal name MetaRenderer) plugin.

Renderer's own public interface lives in `include/Interface/IMetaRenderer.h`.

## Requirements

- Windows with Visual Studio 2022 C++ desktop workload and the Windows SDK
- CMake 3.21+
- Git
- Network access on first configure: CMake fetches MetaHook via FetchContent, initializes the other required submodules, and downloads, verifies and extracts VC-LTL 5.3.1
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

`METAHOOK_SOURCE_PATH` points to the **MetaHook repository root** containing
`include/metahook.h`, `include/SourceSDK/` and `include/vgui_controls/`.

When unset, `FetchContent_Declare` / `FetchContent_MakeAvailable` fetches the MetaHook
source, placed by default under the current build directory in `_deps/renderer_metahook-src/`.

With a valid explicit path, MetaHook's FetchContent fetch is skipped entirely.

`FREEIMAGE_SOURCE_PATH` points to the **FreeImage source root** containing `CMakeLists.txt`.

`GLEW_SOURCE_PATH` points to the **glew source root** containing `CMakeLists.txt`,
must support the current build options and provide a `libglew_static` target.

```bat
scripts\build-Renderer-x86-Debug.bat "-DMETAHOOK_SOURCE_PATH=D:/MetaHook" "-DFREEIMAGE_SOURCE_PATH=D:/FreeImage_clone" "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Debug/include"
scripts\build-Renderer-x86-Release.bat "-DMETAHOOK_SOURCE_PATH=D:/MetaHook" "-DFREEIMAGE_SOURCE_PATH=D:/FreeImage_clone" "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

The same names can be set as environment variables on first configure. Explicit `-D`
arguments take precedence over an existing CMake cache; environment variables only seed
the cache. `-DMETAHOOK_SOURCE_PATH=` switches back to FetchContent;
`-DFREEIMAGE_SOURCE_PATH=` switches back to the FreeImage submodule; `-DGLEW_SOURCE_PATH=`
switches back to the GLEW submodule. External sources are inputs only — build output stays
in Renderer's build directory.

SDL2 and SDL3 are built and installed by the MetaHook project. Run the matching MetaHook
build script first, then pass the `include` directory of its install tree (which must
contain `SDL2/SDL_video.h`) via `SDL2_INCLUDE_DIRS`.

This parameter is required; Renderer does not download, build, link or install SDL, and
no longer keeps an SDL submodule.

`SDL3_INCLUDE_DIRS` is optional and, when set, should point to an include directory
containing `SDL3/SDL.h`, for example
`-DSDL3_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include`.

Renderer does not currently use SDL3 headers directly.

Both parameters accept semicolon-separated directory lists and same-named environment
variables on first configure; invalid paths fail at configure time.

Capstone only needs its headers and can reuse MetaHook's existing source or install tree:

```bat
scripts\build-Renderer-x86-Release.bat "-DCAPSTONE_INCLUDE_DIRS=D:/MetaHook/thirdparty/capstone_fork/include" "-DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include"
```

A directory containing `capstone.h` directly, or a parent include directory containing
`capstone/capstone.h`, is accepted; separate multiple directories with semicolons and
quote the whole `-D` argument. Setting it skips initialization of Renderer's own Capstone
submodule; `-DCAPSTONE_INCLUDE_DIRS=` restores the default. The same-named environment
variable is honored on first configure.

When no Capstone parameter is given, the default submodule is used and no external
directory is required. CMake normalizes the Capstone and SDL input paths
for compilation and checks the effective directories and required headers; SDL2 is
mandatory and SDL3 is checked only when provided. Headers should match the Capstone
version used by the host MetaHook, since callbacks pass its structures. Renderer never
builds or links the Capstone library, so `CAPSTONE_LIBRARY_DIRS` need not be passed; if a
shared invocation passes it, configure explicitly reports that it is ignored.

Calling CMake directly:

```bat
cmake -S . -B build/x86/Release -G "Visual Studio 17 2022" -A Win32 -DCMAKE_INSTALL_PREFIX=install/x86/Release -DMETAHOOK_SOURCE_PATH=D:/MetaHook -DFREEIMAGE_SOURCE_PATH=D:/FreeImage_clone -DSDL2_INCLUDE_DIRS=D:/MetaHook/install/x86/Release/include
cmake --build build/x86/Release --config Release --target install --parallel
```

## Install and run

`install/x86/<Debug|Release>/`:

```text
svencoop/
  metahook/plugins/Renderer.dll
  metahook/plugins/Renderer.pdb
  metahook/dlls/FreeImage/FreeImage.dll   (Debug: FreeImaged.dll)
  metahook/gamedata/renderer/            (Renderer's own gamedata catalog)
  renderer/                           (shaders, textures, config and localization resources)
```

Nothing is copied into the local game directory automatically. Merge it into the target
mod directory and enable Renderer in MetaHook's `metahook/configs/plugins.lst`.

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

| Submodule | Pinned commit | Purpose |
| --- | --- | --- |
| hzqst/FreeImage_clone | `c68700b9fe699dbbf99f88a611065f101cba1a41` | FreeImage DLL and its built-in image format library |
| hzqst/glew-cmake | `56ed32d4a929f993f0e6b7f905af9be4d38fda04` | GLEW static library |
| hzqst/capstone | `e81e390f621ee59d14f70e16fe065dd00f78ee71` | Disassembly data type headers |
| SergiusTheBest/ScopeExit | `bd345da594a4675d04de663d93d00cb81b6678b2` | ScopeExit headers |
| hzqst/tinyobjloader | `cab4ad7254cbf7eaaafdb73d272f99e92f166df8` | OBJ reading source |

Above submodules keep the source repository's URLs and SHAs; URLs are in `.gitmodules`.
MetaHook uses FetchContent and has been removed from the submodule list.
No `git submodule update --recursive` is needed; Renderer initializes only the
dependencies it uses directly.
Capstone and SDL are not linked as libraries into Renderer; the host interface and
existing runtime loading logic are unchanged.

VC-LTL comes from the official `Chuyu-Team/VC-LTL5` v5.3.1 `VC-LTL-Binary.7z`, SHA-256:
`7a18799ed3aa84a225610a5447a56bc534c5c98ccb8dec05caba0e3f633431ad`.

The cache lives in `thirdparty/cache/`; `RENDERER_DEPENDENCY_CACHE_DIR` can set the cache
directory for the first configure. The bundled helper is applied uniformly to Renderer,
GLEW, FreeImage and their static dependencies.

Renderer uses C++20, Debug `/MTd` and Release `/MT`, keeping the original VGUI macros,
Release LTCG, `/OPT:REF`, `/OPT:ICF` and LargeAddressAware. Both configurations emit a
Renderer PDB. The explicit compilation list is in `cmake/Sources.cmake`.

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
