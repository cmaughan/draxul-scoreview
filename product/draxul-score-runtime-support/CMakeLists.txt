# ScoreView's shadow copies of Draxul's log/events/input/host headers are gone
# (they redefined draxul::LogCategory and draxul::KeyEvent under the same
# include paths — an ODR hazard). This target now only forwards the shared
# Draxul foundation types plus the UI dependencies every product library uses.
add_library(draxul-score-runtime-support INTERFACE)

target_link_libraries(draxul-score-runtime-support INTERFACE
    ${DRAXUL_SCOREVIEW_TYPES_TARGET}
    glm::glm
    imgui
    ${DRAXUL_SCOREVIEW_SDL_TARGET}
)

# The plugin-runtime vocabulary header (<draxul/plugin_runtime.h>). Bundled
# builds reach it through the allowlisted runtime-support target; standalone
# extractions get it from the installed SDK include directory.
if(NOT DRAXUL_SCOREVIEW_STANDALONE)
    target_link_libraries(draxul-score-runtime-support INTERFACE
        Draxul::PluginSupport::Runtime)
endif()
