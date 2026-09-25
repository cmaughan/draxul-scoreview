#pragma once

// NanoVG replay is private to the ScoreView presentation target. The public
// analysis_overlay.h intentionally exposes only the renderer-free geometry
// model and builder.

#include <draxul/scoreview/analysis_overlay.h>

struct NVGcontext;

namespace draxul::scoreview
{

struct ScoreTextFonts;

void draw_analysis_overlay(NVGcontext* vg, const AnalysisOverlay& overlay,
    const AnalysisOverlay::Page& page, glm::vec2 origin, float scale, float pixel_scale,
    const ScoreTextFonts& fonts, bool unique_active = false);

void draw_unique_wash(NVGcontext* vg, const AnalysisOverlay::Page& page, glm::vec2 origin,
    float scale, float pixel_scale, const ScoreTextFonts& fonts);

void draw_analysis_banner(NVGcontext* vg, const AnalysisOverlay& overlay, float pixel_scale,
    const ScoreTextFonts& fonts);

} // namespace draxul::scoreview
