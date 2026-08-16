# The ImGui host interface and scancode table now come from the shared
# Draxul ImGui support stack (draxul-imgui-core via the plugin ImGui target);
# only ScoreView's remaining shadow headers and logging live here.
add_library(draxul-score-runtime-support STATIC
    src/log.cpp
)

target_include_directories(draxul-score-runtime-support PUBLIC include)
target_link_libraries(draxul-score-runtime-support PUBLIC
    glm::glm
    imgui
    ${DRAXUL_SCOREVIEW_SDL_TARGET}
)
