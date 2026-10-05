# GLFW is test-only and shared across components through GLFW_SOURCE_PATH: the
# aggregator injects the repository-level thirdparty/glfw submodule, while a
# standalone Renderer build falls back to the same fork at its pinned commit.
if(NOT GLFW_SOURCE_PATH)
    include(FetchContent)
    FetchContent_Declare(renderer_test_glfw
        GIT_REPOSITORY https://github.com/MetaHookSv/glfw
        GIT_TAG 92dcf4ce74f2e2554a98fea09be7c705c17daa5a
        GIT_SUBMODULES "" GIT_SUBMODULES_RECURSE FALSE
        SOURCE_SUBDIR _renderer_glfw_source_only)
    FetchContent_MakeAvailable(renderer_test_glfw)
    set(GLFW_SOURCE_PATH "${renderer_test_glfw_SOURCE_DIR}")
endif()
if(NOT EXISTS "${GLFW_SOURCE_PATH}/include/GLFW/glfw3.h")
    message(FATAL_ERROR "GLFW_SOURCE_PATH must provide include/GLFW/glfw3.h: ${GLFW_SOURCE_PATH}")
endif()
# Configure the shared source tree without its docs, examples, tests or install
# rules; EXCLUDE_FROM_ALL keeps GLFW out of the plugin build unless a test uses it.
# A sibling component's tests may have added the target already, so guard on it.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
if(NOT TARGET glfw)
    add_subdirectory("${GLFW_SOURCE_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)
endif()

foreach(test studio_model_validation_tests studio_model_load_tests ringbuffer_tests sc_client_tests)
    add_executable(${test} "${CMAKE_CURRENT_SOURCE_DIR}/${test}.cpp")
    target_link_libraries(${test} PRIVATE renderer_settings)
    # These tests include production translation units and discard unused handlers.
    # Assertions must remain enabled in Release as in the original test runner.
    target_compile_options(${test} PRIVATE /UNDEBUG /Gy $<$<CONFIG:Release>:/O1>)
    target_link_options(${test} PRIVATE /OPT:REF /INCREMENTAL:NO)
    add_test(NAME ${test} COMMAND ${test})
endforeach()

# Unlike the CPU regression suites above nothing here is patched out: the probe
# opens a real GL context through GLFW, so it links the vendored GLEW and GLFW
# rather than only reading their headers.
add_executable(gl_parallel_shader_compile_tests gl_parallel_shader_compile_tests.cpp)
target_compile_definitions(gl_parallel_shader_compile_tests PRIVATE GLEW_STATIC GLFW_INCLUDE_NONE)
target_link_libraries(gl_parallel_shader_compile_tests PRIVATE renderer_settings libglew_static glfw)
add_test(NAME gl_parallel_shader_compile_tests COMMAND gl_parallel_shader_compile_tests)
# The probe needs a driver-backed GL context. Runners that cannot provide one
# (WGL reports no OpenGL support, e.g. headless CI) exit with this code so CTest
# marks the test skipped instead of failing the build.
set_tests_properties(gl_parallel_shader_compile_tests PROPERTIES SKIP_RETURN_CODE 2)
