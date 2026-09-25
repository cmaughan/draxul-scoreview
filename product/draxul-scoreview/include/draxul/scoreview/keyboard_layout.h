#pragma once

// Renderer-free guidance-keyboard geometry and spelling palette. Keeping
// these values in the ScoreView core lets analysis and learning code describe
// notes without inheriting NanoVG or a presentation dependency.

namespace draxul::scoreview
{

constexpr int kKeyboardLowMidi = 21;
constexpr int kKeyboardHighMidi = 108;
constexpr int kKeyboardWhiteKeys = 52;

bool keyboard_is_black(int midi);
int keyboard_white_index(int midi);
float keyboard_key_center_x(int midi, float x, float w);

constexpr int kGuidancePaletteSize = 21;
constexpr unsigned char kGuidancePalette[kGuidancePaletteSize][3] = {
    { 222, 49, 43 }, { 222, 49, 43 }, { 222, 49, 43 },
    { 240, 126, 32 }, { 240, 126, 32 }, { 240, 126, 32 },
    { 233, 190, 25 }, { 233, 190, 25 }, { 233, 190, 25 },
    { 106, 178, 54 }, { 106, 178, 54 }, { 106, 178, 54 },
    { 26, 175, 165 }, { 26, 175, 165 }, { 26, 175, 165 },
    { 52, 96, 200 }, { 52, 96, 200 }, { 52, 96, 200 },
    { 198, 66, 160 }, { 198, 66, 160 }, { 198, 66, 160 },
};

inline int guidance_default_letter(int midi)
{
    static constexpr int by_pitch_class[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    return by_pitch_class[((midi % 12) + 12) % 12];
}

inline int guidance_palette_index(int midi, int letter = -1)
{
    if (letter < 0 || letter > 6)
        letter = guidance_default_letter(midi);
    static constexpr int natural_pc[7] = { 0, 2, 4, 5, 7, 9, 11 };
    int alter = (((midi % 12) + 12) % 12) - natural_pc[letter];
    if (alter > 6)
        alter -= 12;
    if (alter < -6)
        alter += 12;
    return letter * 3 + (alter < 0 ? 0 : alter > 0 ? 2 : 1);
}

} // namespace draxul::scoreview
