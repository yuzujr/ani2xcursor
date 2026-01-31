#include "preview_generator.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>
#include <string>

#include "ani_parser.h"
#include "ico_cur_decoder.h"
#include "size_selection.h"
#include "spdlog/fmt/bundled/base.h"
#include "utils/fs.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_easy_font.h>
#include <stb_image_write.h>

namespace ani2xcursor {

namespace {

struct RgbaImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return out;
}

std::vector<std::string> tokenize(std::string_view s) {
    std::vector<std::string> tokens;
    std::string current;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

std::string guess_role_from_name(std::string_view name) {
    std::string lower = to_lower(name);
    auto tokens = tokenize(lower);
    auto has_sub = [&](std::string_view sub) {
        return lower.find(sub) != std::string::npos;
    };

    if (has_sub("normal") || has_sub("arrow") || has_sub("left_ptr")) return "pointer";
    if (has_sub("help") || has_sub("question")) return "help";
    if (has_sub("work") || has_sub("progress") || has_sub("starting")) return "working";
    if (has_sub("wait") || has_sub("busy") || has_sub("watch")) return "busy";
    if (has_sub("precision") || has_sub("cross")) return "precision";
    if (has_sub("text") || has_sub("font")) return "text";
    if (has_sub("hand") || has_sub("pen")) return "hand";
    if (has_sub("unavail") || has_sub("not")) return "unavailable";
    if (has_sub("vert")) return "vert";
    if (has_sub("hori") || has_sub("horz")) return "horz";
    if ((has_sub("dgn") && has_sub("1")) || (has_sub("diag") && has_sub("1"))) return "dgn1";
    if ((has_sub("dgn") && has_sub("2")) || (has_sub("diag") && has_sub("2"))) return "dgn2";
    if (has_sub("move")) return "move";
    if (has_sub("alt")) return "alternate";
    if (has_sub("link")) return "link";
    if (has_sub("person")) return "person";
    if (has_sub("pin") || has_sub("location")) return "pin";

    return "";
}

std::string make_preview_name(const fs::path& rel_path) {
    std::string rel = rel_path.generic_string();
    std::string out;
    out.reserve(rel.size() + 4);
    for (char c : rel) {
        if (c == '/' || c == '\\') {
            out += "__";
        } else {
            out += c;
        }
    }
    out += ".png";
    return out;
}

RgbaImage make_checkerboard(uint32_t width, uint32_t height) {
    RgbaImage img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<size_t>(width) * height * 4);

    const uint8_t c1[4] = {236, 236, 236, 255};
    const uint8_t c2[4] = {200, 200, 200, 255};
    const uint32_t cell = 8;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            bool alt = ((x / cell) + (y / cell)) % 2 == 1;
            const uint8_t* c = alt ? c1 : c2;
            size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            img.pixels[idx + 0] = c[0];
            img.pixels[idx + 1] = c[1];
            img.pixels[idx + 2] = c[2];
            img.pixels[idx + 3] = c[3];
        }
    }

    return img;
}

void alpha_blit(const CursorImage& src, RgbaImage& dst, uint32_t dst_x, uint32_t dst_y) {
    for (uint32_t y = 0; y < src.height; ++y) {
        uint32_t dy = dst_y + y;
        if (dy >= dst.height) {
            continue;
        }
        for (uint32_t x = 0; x < src.width; ++x) {
            uint32_t dx = dst_x + x;
            if (dx >= dst.width) {
                continue;
            }
            size_t src_idx = (static_cast<size_t>(y) * src.width + x) * 4;
            size_t dst_idx = (static_cast<size_t>(dy) * dst.width + dx) * 4;

            uint8_t a = src.pixels[src_idx + 3];
            if (a == 0) {
                continue;
            }

            uint8_t inv = static_cast<uint8_t>(255 - a);

            dst.pixels[dst_idx + 0] = static_cast<uint8_t>(
                (src.pixels[src_idx + 0] * a + dst.pixels[dst_idx + 0] * inv) / 255);
            dst.pixels[dst_idx + 1] = static_cast<uint8_t>(
                (src.pixels[src_idx + 1] * a + dst.pixels[dst_idx + 1] * inv) / 255);
            dst.pixels[dst_idx + 2] = static_cast<uint8_t>(
                (src.pixels[src_idx + 2] * a + dst.pixels[dst_idx + 2] * inv) / 255);
            dst.pixels[dst_idx + 3] = 255;
        }
    }
}

RgbaImage compose_preview(const std::vector<CursorImage>& frames) {
    if (frames.empty()) {
        throw std::runtime_error(_("No frames available for preview"));
    }

    uint32_t cell = 0;
    for (const auto& frame : frames) {
        cell = std::max(cell, std::max(frame.width, frame.height));
    }
    if (cell == 0) {
        cell = 1;
    }

    const uint32_t margin = 4;
    const uint32_t spacing = 4;
    uint32_t width = margin * 2 + cell * static_cast<uint32_t>(frames.size()) +
                     spacing * static_cast<uint32_t>(frames.size() - 1);
    uint32_t height = margin * 2 + cell;

    RgbaImage out = make_checkerboard(width, height);

    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        uint32_t base_x = margin + static_cast<uint32_t>(i) * (cell + spacing);
        uint32_t x = base_x + (cell - frame.width) / 2;
        uint32_t y = margin + (cell - frame.height) / 2;
        alpha_blit(frame, out, x, y);
    }

    return out;
}

float read_f32(const char* data) {
    float f = 0.0f;
    std::memcpy(&f, data, sizeof(float));
    return f;
}

void draw_text(RgbaImage& img, int x, int y, const std::string& text,
               const std::array<uint8_t, 4>& color) {
    int vbuf_size = std::max(1024, static_cast<int>(text.size()) * 270);
    std::vector<char> buffer(static_cast<size_t>(vbuf_size));

    unsigned char col[4] = {color[0], color[1], color[2], color[3]};
    int num_quads =
        stb_easy_font_print(static_cast<float>(x), static_cast<float>(y),
                            const_cast<char*>(text.c_str()), col, buffer.data(), vbuf_size);

    const int stride = 16;
    for (int q = 0; q < num_quads; ++q) {
        float min_x = std::numeric_limits<float>::infinity();
        float max_x = -std::numeric_limits<float>::infinity();
        float min_y = std::numeric_limits<float>::infinity();
        float max_y = -std::numeric_limits<float>::infinity();

        const char* quad = buffer.data() + q * 4 * stride;
        const uint8_t* quad_color = reinterpret_cast<const uint8_t*>(quad + 12);

        for (int v = 0; v < 4; ++v) {
            const char* vert = quad + v * stride;
            float vx = read_f32(vert + 0);
            float vy = read_f32(vert + 4);
            min_x = std::min(min_x, vx);
            max_x = std::max(max_x, vx);
            min_y = std::min(min_y, vy);
            max_y = std::max(max_y, vy);
        }

        int x0 = static_cast<int>(std::floor(min_x));
        int x1 = static_cast<int>(std::ceil(max_x));
        int y0 = static_cast<int>(std::floor(min_y));
        int y1 = static_cast<int>(std::ceil(max_y));

        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(static_cast<int>(img.width), x1);
        y1 = std::min(static_cast<int>(img.height), y1);

        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                size_t idx = (static_cast<size_t>(py) * img.width + px) * 4;
                img.pixels[idx + 0] = quad_color[0];
                img.pixels[idx + 1] = quad_color[1];
                img.pixels[idx + 2] = quad_color[2];
                img.pixels[idx + 3] = quad_color[3];
            }
        }
    }
}

RgbaImage make_placeholder(const std::string& filename) {
    std::string line1 = filename;
    std::string line2 = _("decode failed");

    int width1 = stb_easy_font_width(const_cast<char*>(line1.c_str()));
    int width2 = stb_easy_font_width(const_cast<char*>(line2.c_str()));
    int text_width = std::max(width1, width2);
    int line_height = stb_easy_font_height(const_cast<char*>("A"));

    const int padding = 10;
    uint32_t width = static_cast<uint32_t>(std::max(120, text_width + padding * 2));
    uint32_t height = static_cast<uint32_t>(padding * 2 + line_height * 2 + 4);

    RgbaImage img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<size_t>(width) * height * 4);

    const uint8_t bg[4] = {245, 245, 245, 255};
    for (size_t i = 0; i < img.pixels.size(); i += 4) {
        img.pixels[i + 0] = bg[0];
        img.pixels[i + 1] = bg[1];
        img.pixels[i + 2] = bg[2];
        img.pixels[i + 3] = bg[3];
    }

    draw_text(img, padding, padding, line1, {40, 40, 40, 255});
    draw_text(img, padding, padding + line_height + 4, line2, {200, 40, 40, 255});

    return img;
}

bool write_png(const fs::path& path, const RgbaImage& image) {
    fs::create_directories(path.parent_path());
    int stride = static_cast<int>(image.width) * 4;
    return stbi_write_png(path.c_str(), static_cast<int>(image.width),
                          static_cast<int>(image.height), 4, image.pixels.data(), stride) != 0;
}

size_t choose_closest_index(std::span<const CursorImage> images, uint32_t target_size) {
    size_t best_idx = 0;
    uint32_t best_diff = UINT32_MAX;

    for (size_t idx = 0; idx < images.size(); ++idx) {
        uint32_t nominal = std::max(images[idx].width, images[idx].height);
        uint32_t diff = (nominal > target_size) ? (nominal - target_size) : (target_size - nominal);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = idx;
        }
    }

    return best_idx;
}

bool write_preview_for_ani(const fs::path& path, const fs::path& preview_path, SizeFilter filter,
                           const std::vector<uint32_t>& specific_sizes) {
    try {
        auto animation = AniParser::parse(path);
        if (animation.num_steps == 0) {
            throw std::runtime_error(_("ANI: No frames"));
        }

        size_t first_step = 0;
        size_t mid_step = animation.num_steps / 2;
        size_t last_step = animation.num_steps - 1;

        const auto& first_frame = animation.get_step_frame(first_step);
        auto first_images = IcoCurDecoder::decode_all(first_frame.icon_data);
        std::span<const CursorImage> first_span(first_images.data(), first_images.size());
        size_t preview_idx = choose_preview_index(first_span, filter, specific_sizes);
        uint32_t target_size =
            std::max(first_images[preview_idx].width, first_images[preview_idx].height);

        std::vector<CursorImage> frames;
        frames.reserve(3);

        for (size_t step : {first_step, mid_step, last_step}) {
            const auto& frame = animation.get_step_frame(step);
            auto images = IcoCurDecoder::decode_all(frame.icon_data);
            std::span<const CursorImage> frame_span(images.data(), images.size());
            size_t idx = choose_closest_index(frame_span, target_size);
            frames.push_back(std::move(images[idx]));
        }

        auto preview = compose_preview(frames);
        if (!write_png(preview_path, preview)) {
            throw std::runtime_error(_("Failed to write preview PNG"));
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::warn(spdlog::fmt_lib::runtime(_("Preview decode failed for {}: {}")),
                     path.filename().string(), e.what());
        auto placeholder = make_placeholder(path.filename().string());
        if (!write_png(preview_path, placeholder)) {
            throw std::runtime_error(_("Failed to write placeholder PNG"));
        }
        return false;
    }
}

bool write_preview_for_cur(const fs::path& path, const fs::path& preview_path, SizeFilter filter,
                           const std::vector<uint32_t>& specific_sizes) {
    try {
        auto data = utils::read_file(path);
        auto images = IcoCurDecoder::decode_all(data);
        std::span<const CursorImage> image_span(images.data(), images.size());
        size_t preview_idx = choose_preview_index(image_span, filter, specific_sizes);
        std::vector<CursorImage> frames;
        frames.push_back(std::move(images[preview_idx]));
        auto preview = compose_preview(frames);
        if (!write_png(preview_path, preview)) {
            throw std::runtime_error(_("Failed to write preview PNG"));
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::warn(spdlog::fmt_lib::runtime(_("Preview decode failed for {}: {}")),
                     path.filename().string(), e.what());
        auto placeholder = make_placeholder(path.filename().string());
        if (!write_png(preview_path, placeholder)) {
            throw std::runtime_error(_("Failed to write placeholder PNG"));
        }
        return false;
    }
}

bool is_cursor_file(const fs::path& path) {
    auto ext = to_lower(path.extension().string());
    return ext == ".ani" || ext == ".cur";
}

bool is_ani_file(const fs::path& path) {
    return to_lower(path.extension().string()) == ".ani";
}

}  // namespace

PreviewGenerationResult generate_previews(const fs::path& input_dir, const fs::path& preview_dir,
                                          SizeFilter filter,
                                          const std::vector<uint32_t>& specific_sizes) {
    PreviewGenerationResult result;
    std::vector<fs::path> cursor_files;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(input_dir, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            continue;
        }
        if (!it->is_regular_file()) {
            continue;
        }
        const auto& path = it->path();
        if (path.string().find("ani2xcursor") != std::string::npos) {
            continue;
        }
        if (!is_cursor_file(path)) {
            continue;
        }
        cursor_files.push_back(path);
    }

    std::sort(cursor_files.begin(), cursor_files.end(), [&](const fs::path& a, const fs::path& b) {
        std::error_code rel_ec;
        auto rel_a = fs::relative(a, input_dir, rel_ec);
        auto rel_b = fs::relative(b, input_dir, rel_ec);
        return rel_a.generic_string() < rel_b.generic_string();
    });

    for (const auto& path : cursor_files) {
        std::error_code rel_ec;
        auto rel_path = fs::relative(path, input_dir, rel_ec);
        if (rel_ec) {
            rel_path = path.filename();
        }

        auto preview_name = make_preview_name(rel_path);
        auto preview_path = preview_dir / preview_name;

        bool ok = false;
        if (is_ani_file(path)) {
            ok = write_preview_for_ani(path, preview_path, filter, specific_sizes);
        } else {
            ok = write_preview_for_cur(path, preview_path, filter, specific_sizes);
        }

        if (ok) {
            result.generated += 1;
        } else {
            result.generated += 1;
            result.failed += 1;
        }

        std::string guess = guess_role_from_name(rel_path.stem().string());
        if (!guess.empty() && result.guesses.find(guess) == result.guesses.end()) {
            result.guesses[guess] = rel_path.generic_string();
        }
    }

    return result;
}

}  // namespace ani2xcursor
