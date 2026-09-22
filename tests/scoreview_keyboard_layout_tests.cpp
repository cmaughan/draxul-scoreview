// Renderer-free guidance keyboard geometry and spelling palette contracts.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/keyboard_layout.h>

#include <algorithm>
#include <cstdlib>

using namespace draxul::scoreview;

TEST_CASE("keyboard layout maps the complete 88-key range", "[scoreview][keyboard]")
{
    CHECK(kKeyboardLowMidi == 21); // A0
    CHECK(kKeyboardHighMidi == 108); // C8
    CHECK(kKeyboardHighMidi - kKeyboardLowMidi + 1 == 88);
    CHECK(kKeyboardWhiteKeys == 52);

    CHECK(keyboard_white_index(kKeyboardLowMidi - 1) == -1);
    CHECK(keyboard_white_index(kKeyboardHighMidi + 1) == -1);
    CHECK(keyboard_white_index(21) == 0);
    CHECK(keyboard_white_index(108) == 51);
    CHECK(keyboard_white_index(22) == -1); // A#0 is black
    CHECK(keyboard_is_black(61));
    CHECK_FALSE(keyboard_is_black(60));

    int whites = 0;
    for (int midi = kKeyboardLowMidi; midi <= kKeyboardHighMidi; ++midi)
        whites += keyboard_is_black(midi) ? 0 : 1;
    CHECK(whites == kKeyboardWhiteKeys);
}

TEST_CASE("keyboard key centers preserve white-key order and black-key boundaries",
    "[scoreview][keyboard]")
{
    constexpr float x = 17.0f;
    constexpr float width = 520.0f;
    constexpr float white_width = width / static_cast<float>(kKeyboardWhiteKeys);

    CHECK(keyboard_key_center_x(21, x, width)
        == Catch::Approx(x + 0.5f * white_width));
    CHECK(keyboard_key_center_x(108, x, width)
        == Catch::Approx(x + 51.5f * white_width));

    // Middle C sits left of C#4, which sits exactly on the C/D boundary.
    const float c4 = keyboard_key_center_x(60, x, width);
    const float cs4 = keyboard_key_center_x(61, x, width);
    const float d4 = keyboard_key_center_x(62, x, width);
    CHECK(c4 < cs4);
    CHECK(cs4 < d4);
    CHECK(cs4 == Catch::Approx((c4 + d4) * 0.5f).margin(0.01f));
}

TEST_CASE("default spelling follows the seven natural parent letters",
    "[scoreview][keyboard]")
{
    const int expected_by_pitch_class[12] = {
        0, 0, // C, C#
        1, 1, // D, D#
        2, // E
        3, 3, // F, F#
        4, 4, // G, G#
        5, 5, // A, A#
        6, // B
    };
    for (int pitch_class = 0; pitch_class < 12; ++pitch_class)
    {
        CHECK(guidance_default_letter(60 + pitch_class)
            == expected_by_pitch_class[pitch_class]);
        CHECK(guidance_default_letter(-12 + pitch_class)
            == expected_by_pitch_class[pitch_class]);
    }
}

TEST_CASE("guidance palette selects spelling colors rather than pitch classes",
    "[scoreview][keyboard]")
{
    const auto differ = [](const unsigned char* a, const unsigned char* b) {
        return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
    };
    const auto same = [](const unsigned char* a, const unsigned char* b) {
        return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
    };

    // Accidentals use their notated parent letter: C# is red, while the
    // enharmonic Db is orange.
    const int c_sharp = guidance_palette_index(61, /*letter C=*/0);
    const int d_flat = guidance_palette_index(61, /*letter D=*/1);
    const int c_natural = guidance_palette_index(60, 0);
    const int d_natural = guidance_palette_index(62, 1);
    CHECK(c_sharp != d_flat);
    CHECK(same(kGuidancePalette[c_sharp], kGuidancePalette[c_natural]));
    CHECK(same(kGuidancePalette[d_flat], kGuidancePalette[d_natural]));
    CHECK(differ(kGuidancePalette[c_sharp], kGuidancePalette[d_flat]) > 80);

    CHECK(guidance_palette_index(61, 0) == guidance_palette_index(85, 0));
    CHECK(guidance_palette_index(61) == c_sharp);
    CHECK(guidance_palette_index(60) == c_natural);

    constexpr unsigned char expected_natural_rgb[7][3] = {
        { 222, 49, 43 },
        { 240, 126, 32 },
        { 233, 190, 25 },
        { 106, 178, 54 },
        { 26, 175, 165 },
        { 52, 96, 200 },
        { 198, 66, 160 },
    };
    const int naturals[7] = {
        guidance_palette_index(60, 0),
        guidance_palette_index(62, 1),
        guidance_palette_index(64, 2),
        guidance_palette_index(65, 3),
        guidance_palette_index(67, 4),
        guidance_palette_index(69, 5),
        guidance_palette_index(71, 6),
    };
    for (int letter = 0; letter < 7; ++letter)
    {
        CHECK(kGuidancePalette[naturals[letter]][0] == expected_natural_rgb[letter][0]);
        CHECK(kGuidancePalette[naturals[letter]][1] == expected_natural_rgb[letter][1]);
        CHECK(kGuidancePalette[naturals[letter]][2] == expected_natural_rgb[letter][2]);
    }

    int min_natural = 1000;
    for (int i = 0; i < 7; ++i)
    {
        for (int j = i + 1; j < 7; ++j)
            min_natural = std::min(min_natural,
                differ(kGuidancePalette[naturals[i]], kGuidancePalette[naturals[j]]));
    }
    CHECK(min_natural > 55);
    CHECK(differ(kGuidancePalette[naturals[0]], kGuidancePalette[naturals[3]]) > 200);

    for (int midi = kKeyboardLowMidi; midi <= kKeyboardHighMidi; ++midi)
    {
        const int index = guidance_palette_index(midi);
        REQUIRE(index >= 0);
        REQUIRE(index < kGuidancePaletteSize);
    }
}
