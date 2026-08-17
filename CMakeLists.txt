cmake_minimum_required(VERSION 3.25)

set(DRAXUL_SCOREVIEW_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DRAXUL_SCOREVIEW_STANDALONE ON)
    project(draxul-scoreview-plugin VERSION 0.1.0 LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    find_program(DRAXUL_SCOREVIEW_COMPILER_CACHE NAMES ccache sccache)
    if(DRAXUL_SCOREVIEW_COMPILER_CACHE)
        set(CMAKE_C_COMPILER_LAUNCHER
            "${DRAXUL_SCOREVIEW_COMPILER_CACHE}" CACHE FILEPATH "" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${DRAXUL_SCOREVIEW_COMPILER_CACHE}" CACHE FILEPATH "" FORCE)
    endif()
    if(APPLE)
        enable_language(OBJCXX)
        set(CMAKE_OBJCXX_STANDARD 20)
        set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
        if(DRAXUL_SCOREVIEW_COMPILER_CACHE)
            set(CMAKE_OBJCXX_COMPILER_LAUNCHER
                "${DRAXUL_SCOREVIEW_COMPILER_CACHE}" CACHE FILEPATH "" FORCE)
        endif()
    endif()
endif()

if(NOT TARGET Draxul::PluginSDK)
    find_package(DraxulPluginSDK CONFIG REQUIRED)
endif()

include(cmake/Dependencies.cmake)
if(DRAXUL_SCOREVIEW_STANDALONE)
    if(NOT TARGET Draxul::PluginImGui)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../support/imgui/CMakeLists.txt")
            message(FATAL_ERROR
                "ScoreView standalone builds require the copied plugins/support/imgui tree")
        endif()
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../support/imgui"
            "${CMAKE_CURRENT_BINARY_DIR}/plugin-imgui")
    endif()
    set(DRAXUL_SCOREVIEW_IMGUI_TARGET Draxul::PluginImGui)

    # The shared foundation types ship with the installed plugin SDK as plain
    # archives (draxul-types plus the draxul-performance archive its
    # PERF_MEASURE spans reference); the headers ride the SDK include
    # directory that Draxul::PluginSDK already exports.
    if(NOT TARGET draxul-types)
        find_library(DRAXUL_SCOREVIEW_TYPES_LIBRARY NAMES draxul-types REQUIRED)
        find_library(DRAXUL_SCOREVIEW_PERFORMANCE_LIBRARY
            NAMES draxul-performance REQUIRED)
        add_library(draxul-performance STATIC IMPORTED GLOBAL)
        set_target_properties(draxul-performance PROPERTIES
            IMPORTED_LOCATION "${DRAXUL_SCOREVIEW_PERFORMANCE_LIBRARY}")
        add_library(draxul-types STATIC IMPORTED GLOBAL)
        set_target_properties(draxul-types PROPERTIES
            IMPORTED_LOCATION "${DRAXUL_SCOREVIEW_TYPES_LIBRARY}")
        target_link_libraries(draxul-types INTERFACE
            Draxul::PluginSDK glm::glm draxul-performance)
    endif()
    set(DRAXUL_SCOREVIEW_TYPES_TARGET draxul-types)

    # The NanoVG core + Vulkan/Metal backends: a standalone extraction carries
    # a copy of libs/draxul-nanovg beside this tree and builds only its
    # backend half (the pass half needs draxul-renderer).
    if(NOT TARGET draxul-nanovg-backend)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../support/nanovg/CMakeLists.txt")
            message(FATAL_ERROR
                "ScoreView standalone builds require the copied libs/draxul-nanovg "
                "tree beside this directory as support/nanovg.")
        endif()
        set(DRAXUL_NANOVG_BACKEND_ONLY ON)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../support/nanovg"
            "${CMAKE_CURRENT_BINARY_DIR}/nanovg-backend")
    endif()
    set(DRAXUL_SCOREVIEW_NANOVG_TARGET draxul-nanovg-backend)

    if(NOT APPLE)
        # Core's shared VMA implementation TU
        # (Draxul::PluginSupport::VulkanResources) is not installed with the
        # SDK, so a standalone extraction compiles its own copy of the
        # two-line implementation unit.
        set(DRAXUL_SCOREVIEW_VMA_IMPL
            "${CMAKE_CURRENT_BINARY_DIR}/scoreview_vma_impl.cpp")
        file(WRITE "${DRAXUL_SCOREVIEW_VMA_IMPL}"
            "#define VMA_IMPLEMENTATION\n#include <vk_mem_alloc.h>\n")
        add_library(draxul-scoreview-vma-impl STATIC
            "${DRAXUL_SCOREVIEW_VMA_IMPL}")
        target_link_libraries(draxul-scoreview-vma-impl PRIVATE
            Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
        set(DRAXUL_SCOREVIEW_VULKAN_RESOURCES_TARGET draxul-scoreview-vma-impl)
    endif()
else()
    set(DRAXUL_SCOREVIEW_IMGUI_TARGET Draxul::PluginSupport::ImGui)
    set(DRAXUL_SCOREVIEW_TYPES_TARGET Draxul::PluginSupport::Types)
    set(DRAXUL_SCOREVIEW_NANOVG_TARGET Draxul::PluginSupport::NanoVG)
    if(NOT APPLE)
        set(DRAXUL_SCOREVIEW_VULKAN_RESOURCES_TARGET
            Draxul::PluginSupport::VulkanResources)
    endif()
endif()

# SDL linkage differs by build flavor. Standalone builds and Windows embed the
# static SDL3 archive. Bundled macOS builds must not: the host executable
# already carries a full static SDL3, and embedding a second copy in the dylib
# registers duplicate SDL Objective-C classes (SDL3Window, SDL3_cocoametalview,
# ...) with the process-global ObjC runtime. There the product libraries link
# headers only and the module resolves SDL_* from the host executable at
# dlopen via -undefined dynamic_lookup.
if(APPLE AND NOT DRAXUL_SCOREVIEW_STANDALONE)
    set(DRAXUL_SCOREVIEW_SDL_TARGET SDL3::Headers)
else()
    set(DRAXUL_SCOREVIEW_SDL_TARGET SDL3::SDL3)
endif()

# ScoreView owns its complete product stack. Draxul consumes only the native
# module through the installed C ABI; no app, library, or module target is
# linked across the DLL boundary.
add_subdirectory(product)

add_library(draxul-scoreview-plugin MODULE src/scoreview_plugin.cpp)
target_include_directories(draxul-scoreview-plugin PRIVATE src)
target_link_libraries(draxul-scoreview-plugin PRIVATE
    Draxul::PluginSDK
    draxul-scoreview-runtime
    draxul-score-canvas
    draxul-score-runtime-support
    ${DRAXUL_SCOREVIEW_IMGUI_TARGET}
    nlohmann_json::nlohmann_json
    imgui)
# The adapter shell header (draxul/plugin_adapter.h) is header-only. Bundled
# builds take its include directory from the support target; the standalone
# extraction finds the same header in the installed SDK include tree via
# Draxul::PluginSDK.
if(NOT DRAXUL_SCOREVIEW_STANDALONE)
    target_link_libraries(draxul-scoreview-plugin PRIVATE
        Draxul::PluginSupport::Adapter)
endif()
if(APPLE)
    target_link_libraries(draxul-scoreview-plugin PRIVATE
        "-framework Metal" "-framework Foundation")
    draxul_plugin_imgui_attach_metal_backend(draxul-scoreview-plugin ScoreView)
    if(NOT DRAXUL_SCOREVIEW_STANDALONE)
        target_link_options(draxul-scoreview-plugin PRIVATE
            "LINKER:-undefined,dynamic_lookup")
    endif()
else()
    target_link_libraries(draxul-scoreview-plugin PRIVATE Vulkan::Vulkan)
    # The NanoVG GLSL sources live beside the shared backend (bundled builds:
    # libs/draxul-nanovg/shaders; standalone: the staged support/nanovg copy).
    get_target_property(SCOREVIEW_NANOVG_SHADER_DIR
        ${DRAXUL_SCOREVIEW_NANOVG_TARGET} DRAXUL_NANOVG_SHADER_DIR)
    if(NOT SCOREVIEW_NANOVG_SHADER_DIR)
        message(FATAL_ERROR
            "The NanoVG backend target does not expose DRAXUL_NANOVG_SHADER_DIR")
    endif()
    set(SCOREVIEW_PLUGIN_SHADER_OUTPUTS)
    foreach(SCOREVIEW_SHADER_NAME nanovg.vert nanovg.frag)
        set(SCOREVIEW_SHADER_SOURCE
            "${SCOREVIEW_NANOVG_SHADER_DIR}/${SCOREVIEW_SHADER_NAME}")
        set(SCOREVIEW_SHADER_OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/${SCOREVIEW_SHADER_NAME}.spv")
        add_custom_command(OUTPUT "${SCOREVIEW_SHADER_OUTPUT}"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE}
                "${SCOREVIEW_SHADER_SOURCE}" -o "${SCOREVIEW_SHADER_OUTPUT}"
            DEPENDS "${SCOREVIEW_SHADER_SOURCE}"
            VERBATIM)
        list(APPEND SCOREVIEW_PLUGIN_SHADER_OUTPUTS
            "${SCOREVIEW_SHADER_OUTPUT}")
    endforeach()
    add_custom_target(compile_scoreview_plugin_vulkan
        DEPENDS ${SCOREVIEW_PLUGIN_SHADER_OUTPUTS})
    add_dependencies(draxul-scoreview-plugin compile_scoreview_plugin_vulkan)
endif()

set_target_properties(draxul-scoreview-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "draxul-scoreview")
if(APPLE)
    # CMake gives MODULE libraries a .so suffix on macOS by default, while
    # Draxul's macOS plugin contract and this plugin's manifest use .dylib.
    # Bundled builds get this from draxul_register_bundled_plugin; standalone
    # extractions must set it themselves so the module matches the manifest.
    set_target_properties(draxul-scoreview-plugin PROPERTIES SUFFIX ".dylib")
    if(DRAXUL_SCOREVIEW_STANDALONE)
        # Export only the C ABI entry point, exactly as
        # draxul_register_bundled_plugin does for bundled modules. Without
        # this, ImGui's inline functions are coalescible weak externals and
        # dyld unifies them with the host executable's at load — the module's
        # ImGui then reads the host's ImGui context and crashes (the
        # SetCurrentFont "font && font->IsLoaded()" assertion).
        target_link_options(draxul-scoreview-plugin PRIVATE
            "LINKER:-exported_symbol,_draxul_plugin_query_v2")
    endif()
endif()

if(NOT DRAXUL_SCOREVIEW_STANDALONE)
    set(_scoreview_product_targets
        draxul-score-runtime-support
        draxul-notation
        draxul-score-learn
        draxul-score-input
        draxul-score-audio
        draxul-score-canvas
        draxul-scoreview
        draxul-scoreview-runtime
        draxul-scoreview-plugin)
    foreach(_target IN LISTS _scoreview_product_targets)
        get_target_property(_scoreview_target_type ${_target} TYPE)
        if(_scoreview_target_type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        if(COMMAND draxul_apply_sanitizers)
            draxul_apply_sanitizers(${_target})
            draxul_apply_coverage(${_target})
            draxul_apply_msvc_parallel_pdb_fix(${_target})
        endif()
    endforeach()

    set(_scoreview_shader_payload)
    file(GLOB _scoreview_test_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/notation_*_tests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/scoreview_*_tests.cpp")
    foreach(_shader IN LISTS SCOREVIEW_PLUGIN_SHADER_OUTPUTS)
        get_filename_component(_shader_name "${_shader}" NAME)
        list(APPEND _scoreview_shader_payload
            "${_shader}=>shaders/${_shader_name}")
    endforeach()
    draxul_register_bundled_plugin(
        ID dev.draxul.scoreview
        TARGET draxul-scoreview-plugin
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml"
        FEATURE DRAXUL_ENABLE_SCOREVIEW
        DEPENDENCY_MODE STRICT
        PRODUCT_TARGETS ${_scoreview_product_targets}
        TEST_SOURCES ${_scoreview_test_sources}
        TEST_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests.cmake"
        COPY_FILES
            ${_scoreview_shader_payload}
            "$<TARGET_FILE:verovio>=>$<TARGET_FILE_NAME:verovio>"
            "${verovio_SOURCE_DIR}/fonts/Leipzig/Leipzig.ttf=>verovio-data/Leipzig.ttf"
        COPY_DIRECTORIES
            "${verovio_SOURCE_DIR}/data=>verovio-data"
            "${ydp_grand_piano_SOURCE_DIR}=>soundfonts"
        DEPENDS verovio ${SCOREVIEW_PLUGIN_SHADER_OUTPUTS})
endif()
