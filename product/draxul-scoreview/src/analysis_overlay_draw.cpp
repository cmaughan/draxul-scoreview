#include "analysis_overlay_draw.h"

#include <draxul/scoreview/keyboard_layout.h>
#include <draxul/scoreview/score_render_nvg.h>

#include <nanovg.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

namespace
{

// The overlay's contract color: green = inferred, never engraved source.
constexpr NVGcolor overlay_green(float alpha = 0.92f)
{
    return NVGcolor{ { { 0.00f, 0.54f, 0.26f, alpha } } };
}

} // namespace

void draw_analysis_overlay(NVGcontext* vg, const AnalysisOverlay& overlay,
    const AnalysisOverlay::Page& page, glm::vec2 origin, float scale, float pixel_scale,
    const ScoreTextFonts& fonts, bool unique_active)
{
    if (overlay.staff_height <= 0.0f)
        return;
    // Geometry (anchors, spans) lives in canvas space and scales with the
    // engraving; text and stroke sizes are fixed pixels — annotations must
    // stay readable UI, not grow with the sheet.
    const float line_w = 2.0f * pixel_scale;
    const float tick_h = 14.0f * pixel_scale;
    const auto to_px = [&](glm::vec2 canvas) {
        return glm::vec2{ origin.x + canvas.x * scale, origin.y + canvas.y * scale };
    };

    // Labels share the narrow inter-system gaps, and key changes coincide
    // with phrase starts by construction — collisions are the norm, not the
    // exception. Every label measures itself and nudges right past anything
    // already placed.
    struct Placed
    {
        float x0, y0, x1, y1;
    };
    std::vector<Placed> placed;
    const auto place_text = [&](float x, float y, int align, const char* text) {
        float bounds[4] = { 0, 0, 0, 0 };
        nvgTextAlign(vg, align);
        nvgTextBounds(vg, x, y, text, nullptr, bounds);
        const float w = bounds[2] - bounds[0];
        for (int tries = 0; tries < 6; ++tries)
        {
            bool hit = false;
            for (const Placed& other : placed)
            {
                if (bounds[0] < other.x1 && bounds[2] > other.x0 && bounds[1] < other.y1
                    && bounds[3] > other.y0)
                {
                    const float shift = other.x1 + 4.0f * pixel_scale - bounds[0];
                    bounds[0] += shift;
                    bounds[2] += shift;
                    x += shift;
                    hit = true;
                    break;
                }
            }
            if (!hit)
                break;
        }
        placed.push_back({ bounds[0], bounds[1], bounds[2], bounds[3] });
        nvgText(vg, x, y, text, nullptr);
        (void)w;
    };

    for (const AnalysisOverlay::PhraseSpan& span : page.phrases)
    {
        const glm::vec2 a = to_px({ span.x0, span.y });
        const glm::vec2 b = to_px({ span.x1, span.y });
        nvgBeginPath(vg);
        nvgMoveTo(vg, a.x, a.y);
        nvgLineTo(vg, b.x, b.y);
        nvgStrokeColor(vg, overlay_green(0.55f));
        nvgStrokeWidth(vg, line_w);
        nvgStroke(vg);
        // The opening tick drops toward the staff from the barline; a
        // section start reads as ONE taller, heavier tick (a second parallel
        // mark just looked like an inconsistency).
        const float tick = span.opens_section ? tick_h * 1.6f : tick_h;
        nvgBeginPath(vg);
        nvgMoveTo(vg, a.x, a.y);
        nvgLineTo(vg, a.x, a.y + tick);
        nvgStrokeColor(vg, overlay_green());
        nvgStrokeWidth(vg,
            span.opens_section ? line_w * 2.0f : (span.opens_phrase ? line_w * 1.5f : line_w));
        nvgStroke(vg);
        // The wash centers its own "P11 = P1" — don't duplicate it here.
        const bool label_here
            = span.opens_phrase && !(unique_active && span.repeat_of >= 0);
        if (label_here && fonts.regular >= 0)
        {
            char text[64];
            if (span.repeat_of >= 0)
                std::snprintf(text, sizeof(text), "P%d %s P%d", span.phrase_id,
                    span.transposed ? "\u2248" : "=", span.repeat_of);
            else if (span.confidence > 0.0f)
                std::snprintf(text, sizeof(text), "P%d \u00b7%.2f", span.phrase_id, span.confidence);
            else
                std::snprintf(text, sizeof(text), "P%d", span.phrase_id);
            nvgFontFaceId(vg, fonts.regular);
            nvgFontSize(vg, 12.0f * pixel_scale);
            nvgFillColor(vg, overlay_green());
            place_text(a.x + 4.0f * pixel_scale, a.y - 2.0f * pixel_scale,
                NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, text);
        }
    }

    if (fonts.regular >= 0)
    {
        for (const AnalysisOverlay::Label& label : page.labels)
        {
            const glm::vec2 at = to_px(label.pos);
            nvgFontFaceId(vg, label.emphasis && fonts.bold >= 0 ? fonts.bold : fonts.regular);
            nvgFontSize(vg, (label.emphasis ? 15.0f : 12.0f) * pixel_scale);
            nvgFillColor(vg, overlay_green());
            // Emphasis (key) labels hang above their anchor, the rest below.
            place_text(at.x, at.y + (label.emphasis ? -4.0f : 4.0f) * pixel_scale,
                NVG_ALIGN_LEFT | (label.emphasis ? NVG_ALIGN_BOTTOM : NVG_ALIGN_TOP),
                label.text.c_str());
        }
    }
}

void draw_unique_wash(NVGcontext* vg, const AnalysisOverlay::Page& page, glm::vec2 origin,
    float scale, float pixel_scale, const ScoreTextFonts& fonts)
{
    // Ghost the restatements: a near-opaque paper wash over each repeated
    // phrase's row band, labelled with the original it repeats. What stays
    // at full strength IS the piece's unique material.
    for (const AnalysisOverlay::PhraseSpan& span : page.phrases)
    {
        if (span.repeat_of < 0)
            continue;
        // Generous vertically (covers stems/ledger lines), tight horizontally
        // (the neighbouring bar on the same row may be NEW material).
        const float pad_y = 6.0f * pixel_scale;
        const float pad_x = 2.0f * pixel_scale;
        const float x = origin.x + span.x0 * scale - pad_x;
        const float y = origin.y + span.row_y0 * scale - pad_y;
        const float w = (span.x1 - span.x0) * scale + 2.0f * pad_x;
        const float h = (span.row_y1 - span.row_y0) * scale + 2.0f * pad_y;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, 4.0f * pixel_scale);
        nvgFillColor(vg, nvgRGBAf(0.99f, 0.98f, 0.96f, 0.86f));
        nvgFill(vg);
        nvgStrokeColor(vg, overlay_green(0.35f));
        nvgStrokeWidth(vg, 1.0f * pixel_scale);
        nvgStroke(vg);
        if (fonts.regular >= 0 && span.opens_phrase)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "P%d %s P%d", span.phrase_id,
                span.transposed ? "\u2248" : "=", span.repeat_of);
            nvgFontFaceId(vg, fonts.bold >= 0 ? fonts.bold : fonts.regular);
            nvgFontSize(vg, 14.0f * pixel_scale);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, overlay_green());
            nvgText(vg, x + w * 0.5f, y + h * 0.5f, text, nullptr);
        }
    }
}

void draw_analysis_banner(NVGcontext* vg, const AnalysisOverlay& overlay, float pixel_scale,
    const ScoreTextFonts& fonts)
{
    if (overlay.banner.empty() || fonts.regular < 0)
        return;
    const float size = 13.0f * pixel_scale;
    const float legend_size = 11.0f * pixel_scale;
    const float pad = 6.0f * pixel_scale;
    nvgFontFaceId(vg, fonts.regular);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontSize(vg, size);
    float bounds[4] = { 0, 0, 0, 0 };
    nvgTextBounds(vg, pad, pad, overlay.banner.c_str(), nullptr, bounds);
    const float legend_y = bounds[3] + 3.0f * pixel_scale;
    const float swatch = 9.0f * pixel_scale;

    // Measure the legend row (swatch + "×6 [+2 ...]" per motif) so the
    // backdrop pill covers both lines.
    float legend_w = 0.0f;
    if (!overlay.motif_legend.empty())
    {
        nvgFontSize(vg, legend_size);
        for (const AnalysisOverlay::MotifLegend& motif : overlay.motif_legend)
        {
            const std::string text = "\u00d7" + std::to_string(motif.count) + " " + motif.shape;
            float lb[4] = { 0, 0, 0, 0 };
            nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, lb);
            legend_w += swatch + 3.0f * pixel_scale + (lb[2] - lb[0]) + 10.0f * pixel_scale;
        }
        bounds[2] = std::max(bounds[2], pad + legend_w);
        bounds[3] = legend_y + swatch + 3.0f * pixel_scale;
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, bounds[0] - pad * 0.6f, bounds[1] - pad * 0.4f,
        (bounds[2] - bounds[0]) + pad * 1.2f, (bounds[3] - bounds[1]) + pad * 0.8f,
        3.0f * pixel_scale);
    nvgFillColor(vg, nvgRGBAf(1.0f, 1.0f, 1.0f, 0.85f));
    nvgFill(vg);
    nvgFillColor(vg, overlay_green());
    nvgFontSize(vg, size);
    nvgText(vg, pad, pad, overlay.banner.c_str(), nullptr);

    // The legend: a colored swatch per motif (the note color on the sheet),
    // then how often it recurs and its interval shape.
    float x = pad;
    nvgFontSize(vg, legend_size);
    for (const AnalysisOverlay::MotifLegend& motif : overlay.motif_legend)
    {
        const unsigned char* rgb
            = kGuidancePalette[std::clamp(motif.palette, 0, kGuidancePaletteSize - 1)];
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, legend_y + 1.0f * pixel_scale, swatch, swatch, 2.0f * pixel_scale);
        nvgFillColor(vg, nvgRGBA(rgb[0], rgb[1], rgb[2], 255));
        nvgFill(vg);
        x += swatch + 3.0f * pixel_scale;
        const std::string text = "\u00d7" + std::to_string(motif.count) + " " + motif.shape;
        float lb[4] = { 0, 0, 0, 0 };
        nvgTextBounds(vg, x, legend_y, text.c_str(), nullptr, lb);
        nvgFillColor(vg, overlay_green());
        nvgText(vg, x, legend_y, text.c_str(), nullptr);
        x = lb[2] + 10.0f * pixel_scale;
    }
}

} // namespace scoreview
} // namespace draxul


