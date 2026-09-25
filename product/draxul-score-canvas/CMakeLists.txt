# ScoreView owns the drawing callback; Draxul's shared support leaf owns SDK
# frame conversion and the platform NanoVG context lifecycle.
add_library(draxul-score-canvas INTERFACE)
target_link_libraries(draxul-score-canvas INTERFACE
    ${DRAXUL_SCOREVIEW_NANOVG_PASS_TARGET})
