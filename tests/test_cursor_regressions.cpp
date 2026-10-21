#include "cli.h"
#include "ico_cur_decoder.h"
#include "size_tools.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void append_u16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(value & 0xff);
    bytes.push_back(value >> 8);
}

void append_u32(std::vector<uint8_t>& bytes, uint32_t value) {
    append_u16(bytes, value & 0xffff);
    append_u16(bytes, value >> 16);
}

// A 1x1 CUR: one padded pixel row and one padded transparency mask row.
std::vector<uint8_t> make_cursor(uint16_t bpp, uint32_t used_colors,
                                 uint32_t palette_colors, bool transparent = false) {
    std::vector<uint8_t> bytes;
    append_u16(bytes, 0);
    append_u16(bytes, 2);
    append_u16(bytes, 1);
    bytes.insert(bytes.end(), {1, 1, 0, 0});
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u32(bytes, 48 + palette_colors * 4);
    append_u32(bytes, 22);
    append_u32(bytes, 40);
    append_u32(bytes, 1);
    append_u32(bytes, 2);
    append_u16(bytes, 1);
    append_u16(bytes, bpp);
    append_u32(bytes, 0);
    append_u32(bytes, 4);
    append_u32(bytes, 0);
    append_u32(bytes, 0);
    append_u32(bytes, used_colors);
    append_u32(bytes, 0);
    for (uint32_t i = 0; i < palette_colors; ++i) {
        bytes.insert(bytes.end(), {uint8_t(i ? 30 : 0), uint8_t(i ? 20 : 0),
                                  uint8_t(i ? 10 : 0), 0});
    }
    uint8_t pixel = bpp == 1 ? 0x80 : bpp == 4 ? 0x10 : 1;
    if (bpp == 32) {
        bytes.insert(bytes.end(), {30, 20, 10, 255});
    } else {
        bytes.insert(bytes.end(), {pixel, 0, 0, 0});
    }
    bytes.insert(bytes.end(), {uint8_t(transparent ? 0x80 : 0), 0, 0, 0});
    return bytes;
}

ani2xcursor::Args parse_sizes(std::initializer_list<const char*> options) {
    std::vector<const char*> argv = {"ani2xcursor"};
    argv.insert(argv.end(), options);
    return ani2xcursor::parse_args(static_cast<int>(argv.size()),
                                  const_cast<char**>(argv.data()));
}

}  // namespace

void test_cursor_regressions() {
    std::cout << "Test: BMP palette lengths, small images, and transparency...\n";
    for (uint16_t bpp : {1, 4, 8}) {
        // Explicit reduced palettes and the biClrUsed=0 default both work.
        for (uint32_t used : {0u, 2u}) {
            auto img = ani2xcursor::IcoCurDecoder::decode(
                make_cursor(bpp, used, used ? used : (1u << bpp)));
            require(img.width == 1 && img.height == 1, "Incorrect decoded dimensions");
            require(img.pixels == std::vector<uint8_t>({10, 20, 30, 255}),
                    "Incorrect palette pixel or transparency");
        }
    }
    auto transparent = ani2xcursor::IcoCurDecoder::decode(make_cursor(8, 2, 2, true));
    require(transparent.pixels[3] == 0, "AND mask did not clear alpha");

    // The entire DIB is 48 bytes: reading another 16 after its header fails.
    auto direct = ani2xcursor::IcoCurDecoder::decode(make_cursor(32, 0, 0));
    require(direct.pixels == std::vector<uint8_t>({10, 20, 30, 255}),
            "Small true-color cursor failed");

    for (const auto& invalid : {make_cursor(4, 17, 17), make_cursor(8, 100, 2)}) {
        bool rejected = false;
        try {
            ani2xcursor::IcoCurDecoder::decode(invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Invalid palette was accepted");
    }

    std::cout << "Test: standard size preset and repeated size options...\n";
    const std::vector<uint32_t> standard = {24, 30, 32, 36, 40, 48, 64};
    auto preset = parse_sizes({"--size", "standard"});
    require(preset.size_filter == SizeFilter::Specific && preset.specific_sizes == standard,
            "Incorrect standard size preset");
    auto numeric = parse_sizes({"--size", "standard", "--size", "32,48"});
    require(numeric.specific_sizes == std::vector<uint32_t>({32, 48}),
            "Repeated sizes accumulated previous values");
    auto all = parse_sizes({"--size", "standard", "--size", "all"});
    require(all.size_filter == SizeFilter::All && all.specific_sizes.empty(),
            "All mode retained old sizes");
    auto last_preset = parse_sizes({"-s", "16", "-s", "standard"});
    require(last_preset.specific_sizes == standard, "Preset retained old numeric sizes");

    std::cout << "Test: resampling selection, pixel art colors, and hotspots...\n";
    using ani2xcursor::Resampling;
    using ani2xcursor::rescale_cursor;
    require(parse_sizes({"--resample", "nearest"}).resampling == Resampling::Nearest,
            "CLI did not select nearest neighbor");
    require(parse_sizes({"--resample", "auto"}).resampling == Resampling::Auto,
            "CLI did not select automatic resampling");
    require(parse_sizes({"--resample", "bilinear"}).resampling == Resampling::Bilinear,
            "CLI did not select bilinear resampling");
    require(parse_sizes({}).resampling == Resampling::Bilinear,
            "Default resampling changed");
    bool invalid_mode = false;
    try {
        parse_sizes({"--resample", "unknown"});
    } catch (const std::exception&) {
        invalid_mode = true;
    }
    require(invalid_mode, "Unknown resampling mode was accepted");

    ani2xcursor::CursorImage checker{};
    checker.width = checker.height = 2;
    checker.hotspot_x = checker.hotspot_y = 1;
    checker.pixels = {0, 0, 0, 255, 255, 255, 255, 255,
                     255, 255, 255, 255, 0, 0, 0, 255};
    auto nearest = rescale_cursor(checker, 4, Resampling::Nearest);
    for (uint32_t y = 0; y < 4; ++y) {
        for (uint32_t x = 0; x < 4; ++x) {
            uint8_t expected = ((x / 2) == (y / 2)) ? 0 : 255;
            size_t p = (y * 4 + x) * 4;
            require(nearest.pixels[p] == expected && nearest.pixels[p + 1] == expected &&
                        nearest.pixels[p + 2] == expected && nearest.pixels[p + 3] == 255,
                    "Nearest neighbor changed pixel art colors or block boundaries");
        }
    }
    require(nearest.hotspot_x == 2 && nearest.hotspot_y == 2, "Enlarged hotspot is wrong");
    auto bilinear = rescale_cursor(checker, 4, Resampling::Bilinear);
    require(bilinear.pixels[4] > 0 && bilinear.pixels[4] < 255,
            "Bilinear interpolation did not blend the edge");
    require(rescale_cursor(checker, 4).pixels == bilinear.pixels,
            "Default no longer matches bilinear");
    require(rescale_cursor(checker, 4, Resampling::Auto).pixels == nearest.pixels,
            "Auto enlargement did not preserve pixel art");
    require(rescale_cursor(checker, 1, Resampling::Auto).pixels ==
                std::vector<uint8_t>({128, 128, 128, 255}),
            "Auto reduction did not blend checkerboard pixels");
    require(rescale_cursor(checker, 1, Resampling::Nearest).pixels ==
                std::vector<uint8_t>({0, 0, 0, 255}),
            "Nearest reduction did not preserve the source color");
    for (Resampling mode : {Resampling::Bilinear, Resampling::Nearest, Resampling::Auto}) {
        require(rescale_cursor(checker, 2, mode).pixels == checker.pixels,
                "Same-size resampling modified pixels");
        ani2xcursor::CursorImage large{};
        large.width = large.height = 64;
        large.hotspot_x = large.hotspot_y = 3;
        large.pixels.assign(64 * 64 * 4, 255);
        auto reduced = rescale_cursor(large, 30, mode);
        require(reduced.hotspot_x == 1 && reduced.hotspot_y == 1,
                "Hotspot was rounded through an intermediate size");
    }
    std::cout << "  PASSED!\n";
}
