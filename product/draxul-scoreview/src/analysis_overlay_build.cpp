#include <draxul/scoreview/analysis_overlay.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace draxul
{
namespace scoreview
{

namespace
{

// Bar index for a qstamp: the last bar whose start is at or before q.
int bar_at(const std::vector<double>& starts, double q)
{
    if (starts.empty())
        return -1;
    const auto after = std::upper_bound(starts.begin(), starts.end(), q + 1e-9);
    return static_cast<int>(std::distance(starts.begin(), after)) - 1;
}

struct BoxRef
{
    int page = -1;
    int index = -1; // into pages[page].bars
};

} // namespace

AnalysisOverlay build_analysis_overlay(const std::vector<ScoreDrawList>& pages,
    const Timemap& timemap, const PieceProfile& profile,
    const std::vector<double>& bar_starts_q,
    const std::function<int(const std::string&)>& midi_pitch)
{
    AnalysisOverlay overlay;
    overlay.pages.resize(pages.size());
    if (pages.empty() || bar_starts_q.empty())
        return overlay;

    // Bar geometry, best source first:
    //
    // 1. The interpreter's per-measure boxes — every op inside the measure
    //    group contributes, so min.x/max.x ARE the barlines and the y band
    //    covers staff lines plus directions above/below. Encounter order is
    //    bar order for a single-part score, which is the overlay's audience
    //    (the composer itself is single-part-only).
    // 2. Fallback (no measures recorded): boxes from note glyphs mapped
    //    through the timemap — annotations then hug the notes rather than
    //    the bars, which is why this is only the fallback.
    bool have_measures = false;
    for (const ScoreDrawList& page : pages)
        have_measures |= !page.measures.empty();
    if (have_measures)
    {
        int bar = 0;
        for (size_t p = 0; p < pages.size(); ++p)
        {
            for (const MeasureBox& measure : pages[p].measures)
            {
                if (!measure.valid)
                {
                    ++bar; // an empty engraved measure still advances the count
                    continue;
                }
                AnalysisOverlay::BarBox box;
                box.bar = bar++;
                box.min = measure.min;
                box.max = measure.max;
                overlay.pages[p].bars.push_back(box);
            }
        }
    }
    else
    {
        // note element id -> source bar, straight off the analysis axis.
        std::unordered_map<std::string, int> id_bar;
        for (const TimemapEntry& entry : timemap.entries)
        {
            const int bar = bar_at(bar_starts_q, entry.qstamp);
            if (bar < 0)
                continue;
            for (const std::string& id : entry.note_on)
                id_bar.emplace(id, bar);
        }
        for (size_t p = 0; p < pages.size(); ++p)
        {
            std::map<int, AnalysisOverlay::BarBox> boxes;
            for (const GlyphInstance& glyph : pages[p].glyphs)
            {
                const auto found = id_bar.find(glyph.element_id);
                if (found == id_bar.end())
                    continue;
                const glm::vec2 at{ glyph.xform.e, glyph.xform.f };
                auto [it, fresh] = boxes.try_emplace(found->second);
                AnalysisOverlay::BarBox& box = it->second;
                if (fresh)
                {
                    box.bar = found->second;
                    box.min = box.max = at;
                }
                else
                {
                    box.min = glm::min(box.min, at);
                    box.max = glm::max(box.max, at);
                }
            }
            overlay.pages[p].bars.reserve(boxes.size());
            for (auto& [bar, box] : boxes)
                overlay.pages[p].bars.push_back(box);
        }
    }
    // Row indices via the x-reset rule (boxes are in bar order; systems run
    // left-to-right, so a bar starting left of its predecessor opens the
    // next row).
    for (AnalysisOverlay::Page& page : overlay.pages)
    {
        int row = 0;
        for (size_t i = 0; i < page.bars.size(); ++i)
        {
            if (i > 0 && page.bars[i].min.x < page.bars[i - 1].min.x - 1e-3f)
                ++row;
            page.bars[i].row = row;
        }
    }

    // Staff height proxy: median bar-box height. Note-point boxes hug the
    // staff, so this lands near the real staff extent — every overlay size
    // derives from it rather than from guessed canvas conventions.
    std::vector<float> heights;
    for (const AnalysisOverlay::Page& page : overlay.pages)
    {
        for (const AnalysisOverlay::BarBox& box : page.bars)
        {
            const float h = box.max.y - box.min.y;
            if (h > 0.0f)
                heights.push_back(h);
        }
    }
    if (!heights.empty())
    {
        std::nth_element(heights.begin(), heights.begin() + static_cast<long>(heights.size() / 2),
            heights.end());
        overlay.staff_height = heights[heights.size() / 2];
    }
    if (overlay.staff_height <= 0.0f)
        overlay.staff_height = pages.front().canvas_size.y * 0.03f;
    const float staff_h = overlay.staff_height;

    // Bar -> box lookup, and "first engraved bar at or after B" for bars
    // whose content is all rests (no note geometry of their own).
    std::map<int, BoxRef> by_bar;
    for (size_t p = 0; p < overlay.pages.size(); ++p)
    {
        for (size_t i = 0; i < overlay.pages[p].bars.size(); ++i)
            by_bar.try_emplace(overlay.pages[p].bars[i].bar,
                BoxRef{ static_cast<int>(p), static_cast<int>(i) });
    }
    const auto box_at_or_after = [&](int bar) -> BoxRef {
        const auto found = by_bar.lower_bound(bar);
        return found != by_bar.end() ? found->second : BoxRef{};
    };
    const auto box_of = [&](BoxRef ref) -> const AnalysisOverlay::BarBox& {
        return overlay.pages[static_cast<size_t>(ref.page)]
            .bars[static_cast<size_t>(ref.index)];
    };

    // Section openings get the heavier tick.
    std::vector<char> section_open(bar_starts_q.size(), 0);
    for (const PieceProfile::Section& section : profile.sections)
    {
        if (section.start_bar >= 0 && section.start_bar < static_cast<int>(section_open.size()))
            section_open[static_cast<size_t>(section.start_bar)] = 1;
    }

    // Page-header band: title/composer text lives OUTSIDE the measures, above
    // the first system. First-row annotation lanes must not climb into it.
    std::vector<float> header_bottom(pages.size(), 0.0f);
    for (size_t p = 0; p < pages.size(); ++p)
    {
        float first_row_top = pages[p].canvas_size.y;
        for (const AnalysisOverlay::BarBox& box : overlay.pages[p].bars)
        {
            if (box.row == 0)
                first_row_top = std::min(first_row_top, box.min.y);
        }
        for (const DrawText& text : pages[p].texts)
        {
            if (text.pos.y < first_row_top)
                header_bottom[p] = std::max(header_bottom[p], text.pos.y);
        }
    }

    // Full vertical extent of each system row (all bars on the row, not just
    // one phrase's) — span lines, labels and washes all anchor to it.
    std::vector<std::vector<glm::vec2>> row_extents(overlay.pages.size());
    for (size_t p = 0; p < overlay.pages.size(); ++p)
    {
        for (const AnalysisOverlay::BarBox& box : overlay.pages[p].bars)
        {
            auto& rows = row_extents[p];
            if (static_cast<size_t>(box.row) >= rows.size())
                rows.resize(static_cast<size_t>(box.row) + 1, { box.min.y, box.max.y });
            rows[static_cast<size_t>(box.row)].x
                = std::min(rows[static_cast<size_t>(box.row)].x, box.min.y);
            rows[static_cast<size_t>(box.row)].y
                = std::max(rows[static_cast<size_t>(box.row)].y, box.max.y);
        }
    }

    // Phrase spans: each phrase's bar boxes, one segment per system row (the
    // row index carries the grouping — no geometry heuristics).
    for (size_t pi = 0; pi < profile.phrases.size(); ++pi)
    {
        const PieceProfile::Phrase& phrase = profile.phrases[pi];
        bool first_segment = true;
        for (size_t p = 0; p < overlay.pages.size(); ++p)
        {
            const auto& bars = overlay.pages[p].bars;
            AnalysisOverlay::PhraseSpan span;
            int current_row = -1;
            bool open = false;
            const auto flush = [&]() {
                if (!open)
                    return;
                const glm::vec2 extent = row_extents[p][static_cast<size_t>(current_row)];
                // Lane 1 of the annotation stack, in the gap above the row.
                // Measure boxes span the full system band (dir text and all),
                // so the offset is a small fraction — and hard-clamped below
                // whatever sits above: the previous system, or the page
                // header on the first row. The lanes must live in the gap,
                // never inside a neighbouring system.
                const float above = current_row > 0
                    ? row_extents[p][static_cast<size_t>(current_row) - 1].y
                    : header_bottom[p];
                span.y = std::max(extent.x - 0.10f * staff_h, above + 0.04f * staff_h);
                span.row_y0 = extent.x;
                span.row_y1 = extent.y;
                overlay.pages[p].phrases.push_back(span);
                first_segment = false;
                open = false;
            };
            for (const AnalysisOverlay::BarBox& box : bars)
            {
                if (box.bar < phrase.start_bar || box.bar >= phrase.end_bar)
                    continue;
                if (!open || box.row != current_row)
                {
                    flush();
                    span = AnalysisOverlay::PhraseSpan{};
                    span.phrase_id = static_cast<int>(pi);
                    span.x0 = box.min.x;
                    span.confidence = static_cast<float>(phrase.confidence);
                    span.repeat_of = phrase.repeat_of;
                    span.transposed = phrase.transposed;
                    span.opens_phrase = first_segment;
                    span.opens_section = first_segment
                        && phrase.start_bar < static_cast<int>(section_open.size())
                        && section_open[static_cast<size_t>(phrase.start_bar)] != 0;
                    current_row = box.row;
                    open = true;
                }
                span.x1 = std::max(span.x1, box.max.x);
            }
            flush();
        }
    }

    // A bar box only spans its own notes — a bar of low notes has a top far
    // below the system's. Labels must clear the SYSTEM, so anchor them to
    // the full extent of the bar's row.
    const auto row_extent = [&](BoxRef ref) {
        const AnalysisOverlay::BarBox& box = box_of(ref);
        return row_extents[static_cast<size_t>(ref.page)][static_cast<size_t>(box.row)];
    };

    // Key labels: one per detected key region (the whole piece when the key
    // never moves), anchored above the region's first engraved bar's row.
    const auto add_key_label = [&](int bar, const PieceProfile::KeyEstimate& key) {
        const BoxRef ref = box_at_or_after(std::max(0, bar));
        if (ref.page < 0)
            return;
        const AnalysisOverlay::BarBox& box = box_of(ref);
        char text[96];
        std::snprintf(text, sizeof(text), "%s (%.2f)", key_name(key.tonic_pc, key.minor).c_str(),
            key.confidence);
        AnalysisOverlay::Label label;
        // Lane 2: above the span lane, with the same in-the-gap clamps (the
        // previous system's bottom, or the page header on the first row).
        const float above = box.row > 0
            ? row_extents[static_cast<size_t>(ref.page)][static_cast<size_t>(box.row) - 1].y
            : header_bottom[static_cast<size_t>(ref.page)];
        label.pos = { box.min.x,
            std::max(row_extent(ref).x - 0.20f * staff_h, above + 0.10f * staff_h) };
        label.text = text;
        label.emphasis = true;
        overlay.pages[static_cast<size_t>(ref.page)].labels.push_back(std::move(label));
    };
    if (profile.key_sections.empty())
    {
        add_key_label(0, profile.global_key);
    }
    else
    {
        for (const PieceProfile::KeySection& section : profile.key_sections)
            add_key_label(bar_at(bar_starts_q, section.start_q + 1e-6), section.key);
    }

    // Motif coloring: reconstruct the analysis's melody (top sounding pitch
    // per onset) from the timemap, then paint every note of every motif
    // occurrence in its motif's color — the spelling palette's seven
    // naturals, cycled by rank, same color for the same motif everywhere. A
    // note claimed by two motifs keeps the stronger (lower-rank) color.
    const size_t motif_count = midi_pitch ? profile.motifs.size() : 0;
    std::unordered_map<std::string, int> note_color; // element id -> palette
    if (motif_count > 0)
    {
        // THE melody, via the same sustain-aware skyline the miner used —
        // occurrence positions index that melody, so any other extraction
        // here would color the wrong notes.
        std::vector<std::vector<std::string>> onset_ids;
        const std::vector<AnalysisOnset> onsets
            = analysis_onsets_from_timemap(timemap, midi_pitch, &onset_ids);
        const std::vector<MelodyNote> skyline = skyline_melody(onsets);
        struct ColorNote
        {
            double q = 0.0;
            std::string id;
        };
        std::vector<ColorNote> melody;
        melody.reserve(skyline.size());
        for (const MelodyNote& note : skyline)
            melody.push_back({ note.qstamp, onset_ids[note.onset][note.voice] });
        std::vector<double> melody_q;
        melody_q.reserve(melody.size());
        for (const ColorNote& note : melody)
            melody_q.push_back(note.q);

        for (size_t m = 0; m < motif_count; ++m)
        {
            const PieceProfile::Motif& motif = profile.motifs[m];
            // Rank -> one of the seven natural (white-key) palette colors.
            const int palette = static_cast<int>(m % 7) * 3 + 1;
            const std::vector<double>& occurrences
                = motif.occurrences_q.empty() ? std::vector<double>{ motif.first_q }
                                              : motif.occurrences_q;
            for (const double q : occurrences)
            {
                const auto found = std::lower_bound(melody_q.begin(), melody_q.end(), q - 1e-6);
                if (found == melody_q.end() || std::abs(*found - q) > 1e-6)
                    continue;
                const size_t start = static_cast<size_t>(
                    std::distance(melody_q.begin(), found));
                // intervals.size() steps span intervals.size()+1 notes.
                for (size_t i = start;
                    i < melody.size() && i <= start + motif.intervals.size(); ++i)
                    note_color.try_emplace(melody[i].id, palette);
            }
            AnalysisOverlay::MotifLegend legend;
            legend.palette = palette;
            legend.count = motif.count;
            legend.shape = "[";
            for (size_t i = 0; i < motif.intervals.size(); ++i)
            {
                if (i > 0)
                    legend.shape += " ";
                legend.shape
                    += (motif.intervals[i] >= 0 ? "+" : "") + std::to_string(motif.intervals[i]);
            }
            legend.shape += "]";
            overlay.motif_legend.push_back(std::move(legend));
        }

        overlay.highlights.resize(pages.size());
        for (size_t p = 0; p < pages.size(); ++p)
        {
            overlay.highlights[p].build(pages[p]);
            for (const auto& [id, palette] : note_color)
                overlay.highlights[p].set_guidance(id, palette);
        }
    }

    size_t unique_phrases = 0;
    for (const PieceProfile::Phrase& phrase : profile.phrases)
        unique_phrases += phrase.repeat_of < 0 ? 1 : 0;
    char banner[256];
    std::snprintf(banner, sizeof(banner),
        "analysis · %s (%.2f) · %zu phrases (%zu unique) · %zu sections · structure %.2f · "
        "%zu chords · %zu motifs · %zu figures",
        key_name(profile.global_key.tonic_pc, profile.global_key.minor).c_str(),
        profile.global_key.confidence, profile.phrases.size(), unique_phrases,
        profile.sections.size(), profile.structure_confidence, profile.chords.size(),
        profile.motifs.size(), profile.figures.size());
    overlay.banner = banner;
    return overlay;
}

} // namespace scoreview
} // namespace draxul
