#pragma once

#include <draxul/scoreview/keyboard_layout.h>

#include <vector>

struct NVGcontext;

namespace draxul::scoreview
{

struct KeyboardLit
{
    int midi = -1;
    float alpha = 1.0f;
    int palette = 0;
};

void draw_piano_keyboard(NVGcontext* vg, float x, float y, float w, float h,
    const std::vector<KeyboardLit>& lit, float overall_alpha, float pixel_scale);

} // namespace draxul::scoreview
