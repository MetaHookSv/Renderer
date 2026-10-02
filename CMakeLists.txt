cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(Renderer LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Renderer requires Windows MSVC x86. Configure with -A Win32.")
endif()
get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(multi_config)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported configurations" FORCE)
elseif(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Select Debug or Release.")
endif()

include(cmake/Dependencies.cmake)
renderer_prepare_dependencies()

# Validate the resolved directories, including defaults supplied by dependencies.
function(renderer_require_headers dependency header)
    set(header_found FALSE)
    foreach(directory IN LISTS RENDERER_${dependency}_INCLUDE_DIRS)
        if(NOT IS_DIRECTORY "${directory}")
            message(FATAL_ERROR "${dependency}_INCLUDE_DIRS directory does not exist: ${directory}")
        endif()
        if(EXISTS "${directory}/${header}" AND NOT IS_DIRECTORY "${directory}/${header}")
            set(header_found TRUE)
        endif()
    endforeach()
    if(NOT header_found)
        message(FATAL_ERROR "${dependency}_INCLUDE_DIRS must provide ${header}")
    endif()
endfunction()
renderer_require_headers(CAPSTONE capstone.h)
renderer_require_headers(SDL2 SDL2/SDL_video.h)
if(SDL3_INCLUDE_DIRS)
    renderer_require_headers(SDL3 SDL3/SDL.h)
endif()

include("${VC_LTL_Root}/VC-LTL helper for cmake.cmake")

# Inherit the parent's VC-LTL settings rather than invoking vendor helpers twice.
set(USE_VCLTL OFF CACHE BOOL "Separate vendor VC-LTL integration" FORCE)
set(glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(glew-cmake_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ONLY_LIBS ON CACHE BOOL "" FORCE)
add_subdirectory("${GLEW_SOURCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/thirdparty/GLEW" EXCLUDE_FROM_ALL)
if(NOT TARGET libglew_static)
    message(FATAL_ERROR "GLEW_SOURCE_PATH must provide the libglew_static CMake target.")
endif()
add_subdirectory("${FREEIMAGE_SOURCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/thirdparty/FreeImage" EXCLUDE_FROM_ALL)
if(NOT TARGET FreeImage)
    message(FATAL_ERROR "FREEIMAGE_SOURCE_PATH must provide the FreeImage CMake target.")
endif()

add_library(renderer_settings INTERFACE)
target_compile_features(renderer_settings INTERFACE cxx_std_20)
target_include_directories(renderer_settings INTERFACE
    src
    include/Interface
    "${METAHOOK_SOURCE_PATH}/include"
    "${METAHOOK_SOURCE_PATH}/include/Interface"
    "${METAHOOK_SOURCE_PATH}/include/Interface/VGUI"
    "${METAHOOK_SOURCE_PATH}/include/HLSDK/cl_dll"
    "${METAHOOK_SOURCE_PATH}/include/HLSDK/common"
    "${METAHOOK_SOURCE_PATH}/include/HLSDK/engine"
    "${METAHOOK_SOURCE_PATH}/include/HLSDK/pm_shared"
    "${METAHOOK_SOURCE_PATH}/include/HLSDK/public"
    "${METAHOOK_SOURCE_PATH}/include/VGUI"
    "${METAHOOK_SOURCE_PATH}/include/vgui_controls"
    "${METAHOOK_SOURCE_PATH}/include/SourceSDK"
    "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier0"
    "${METAHOOK_SOURCE_PATH}/include/SourceSDK/tier1"
    "${METAHOOK_SOURCE_PATH}/include/SourceSDK/vstdlib"
    ${RENDERER_CAPSTONE_INCLUDE_DIRS}
    thirdparty/ScopeExit/include
    ${RENDERER_SDL3_INCLUDE_DIRS}
    ${RENDERER_SDL2_INCLUDE_DIRS}
    thirdparty/tinyobjloader
    # Tests consume dependency headers without linking the libraries.
    "$<TARGET_PROPERTY:libglew_static,INTERFACE_INCLUDE_DIRECTORIES>"
    "$<TARGET_PROPERTY:FreeImage,INTERFACE_INCLUDE_DIRECTORIES>"
)
target_compile_definitions(renderer_settings INTERFACE WIN32 _WINDOWS _USRDLL _MBCS
    RENDERER_EXPORTS GLEW_STATIC VGUI_USE_SURFACE2 VGUI_USE_SCHEME2 VGUI_USE_INPUT2
    NO_MALLOC_OVERRIDE NO_TIER0_MINIDUMP NO_VCR)
target_compile_options(renderer_settings INTERFACE /permissive /W3
    /wd4311 /wd4312 /wd4819 /wd4996
    $<$<CONFIG:Release>:/wd4244;/wd4267;/wd4305>)

include(cmake/Sources.cmake)
add_library(Renderer SHARED ${RENDERER_SOURCES})
target_link_libraries(Renderer PRIVATE renderer_settings libglew_static FreeImage opengl32)
target_compile_definitions(Renderer PRIVATE $<$<CONFIG:Debug>:_DEBUG>)
target_compile_options(Renderer PRIVATE /MP /Zi $<$<CONFIG:Debug>:/arch:SSE2>)
target_link_options(Renderer PRIVATE /DEBUG
    $<$<CONFIG:Debug>:/INCREMENTAL>
    $<$<CONFIG:Release>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/LARGEADDRESSAWARE>)
set_target_properties(Renderer PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

install(TARGETS Renderer RUNTIME DESTINATION svencoop/metahook/plugins)
install(FILES "$<TARGET_PDB_FILE:Renderer>" DESTINATION svencoop/metahook/plugins)
install(FILES "$<TARGET_FILE:FreeImage>" DESTINATION svencoop/metahook/dlls/FreeImage)
install(DIRECTORY assets/svencoop/renderer DESTINATION svencoop)
install(FILES include/Interface/IMetaRenderer.h DESTINATION include/Interface)

# Renderer ships its own gamedata catalog, pruned to the symbols it resolves, in
# a nested directory the launcher's catalog loader merges (metahook/gamedata/
# renderer). The manifest lists those symbols; the synchronizer downloads the
# upstream catalog into a persistent cache, prunes it, and publishes the result.
option(RENDERER_SYNC_GAMEDATA "Download and prune Renderer's gamedata catalog before building" ON)
set(RENDERER_GAMEDATA_DIR "${CMAKE_BINARY_DIR}/assets/svencoop/metahook/gamedata/renderer" CACHE PATH "Renderer gamedata catalog directory")
if(RENDERER_SYNC_GAMEDATA)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(RendererGameData
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sync-gamedata.py"
            --manifest "${CMAKE_CURRENT_SOURCE_DIR}/scripts/manifests/renderer.json"
            --target-dir "${RENDERER_GAMEDATA_DIR}" --temp-root "${CMAKE_BINARY_DIR}/gamedata-sync"
        COMMENT "Synchronizing and validating Renderer gamedata"
        VERBATIM
    )
    add_custom_target(RendererGameDataValidate
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate-gamedata.py"
            "${RENDERER_GAMEDATA_DIR}" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/scripts/manifests/renderer.json"
        COMMENT "Validating Renderer gamedata against the manifest"
        VERBATIM
    )
    add_dependencies(RendererGameDataValidate RendererGameData)
    add_dependencies(Renderer RendererGameDataValidate)
endif()
install(DIRECTORY "${RENDERER_GAMEDATA_DIR}/" DESTINATION svencoop/metahook/gamedata/renderer OPTIONAL)

option(RENDERER_BUILD_TESTS "Build the existing Renderer regression tests" OFF)
if(RENDERER_BUILD_TESTS)
    enable_testing()
    foreach(test studio_model_validation_tests studio_model_load_tests ringbuffer_tests sc_client_tests)
        add_executable(${test} "src/tests/${test}.cpp")
        target_link_libraries(${test} PRIVATE renderer_settings)
        # These tests include production translation units and discard unused handlers.
        # Assertions must remain enabled in Release as in the original test runner.
        target_compile_options(${test} PRIVATE /UNDEBUG /Gy $<$<CONFIG:Release>:/O1>)
        target_link_options(${test} PRIVATE /OPT:REF /INCREMENTAL:NO)
        add_test(NAME ${test} COMMAND ${test})
    endforeach()
endif()
