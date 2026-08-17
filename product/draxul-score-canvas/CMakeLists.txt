# ScoreView's plugin-facing NanoVG pass. The NanoVG core and the platform
# backends come from Draxul (Draxul::PluginSupport::NanoVG in bundled builds,
# the staged copy of libs/draxul-nanovg in standalone extractions); only the
# DraxulPlugin*FrameV2 adapter layer is product-owned.
if(APPLE)
    add_library(draxul-score-canvas STATIC
        src/nanovg_pass_metal.mm
    )
    find_library(SCORE_CANVAS_METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-score-canvas PRIVATE
        ${SCORE_CANVAS_METAL_FRAMEWORK})
    target_compile_options(draxul-score-canvas PRIVATE -fobjc-arc)
else()
    add_library(draxul-score-canvas STATIC
        src/nanovg_pass_vk.cpp
    )
    # The VMA implementation translation unit comes from the shared Vulkan
    # resources target, never from a product copy.
    target_link_libraries(draxul-score-canvas PRIVATE
        Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
        ${DRAXUL_SCOREVIEW_VULKAN_RESOURCES_TARGET})
endif()

target_include_directories(draxul-score-canvas
    PUBLIC include
    PRIVATE src)
target_link_libraries(draxul-score-canvas
    PUBLIC Draxul::PluginSDK ${DRAXUL_SCOREVIEW_NANOVG_TARGET})
target_link_libraries(draxul-score-canvas PRIVATE draxul-score-runtime-support)
