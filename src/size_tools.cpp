#include "size_tools.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "ani_parser.h"
#include "utils/fs.h"

namespace ani2xcursor {

uint32_t nominal_size(const CursorImage& img) {
    return std::max(img.width, img.height);
}

std::optional<size_t> find_exact_size_index(std::span<const CursorImage> images,
                                            uint32_t target_size) {
    for (size_t idx = 0; idx < images.size(); ++idx) {
        if (nominal_size(images[idx]) == target_size) {
            return idx;
        }
    }
    return std::nullopt;
}

size_t find_closest_size_index(std::span<const CursorImage> images, uint32_t target_size) {
    size_t best_idx = 0;
    uint32_t best_diff = UINT32_MAX;

    for (size_t idx = 0; idx < images.size(); ++idx) {
        uint32_t size = nominal_size(images[idx]);
        uint32_t diff = (size > target_size) ? (size - target_size) : (target_size - size);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = idx;
        }
    }

    return best_idx;
}

CursorImage rescale_cursor(const CursorImage& src, uint32_t target_size) {
    if (target_size == 0) {
        throw std::runtime_error("Invalid target size");
    }
    uint32_t src_nominal = nominal_size(src);
    if (src_nominal == target_size) {
        return src;
    }

    double scale = static_cast<double>(target_size) / static_cast<double>(src_nominal);
    uint32_t new_w = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(src.width * scale)));
    uint32_t new_h = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(src.height * scale)));

    if (std::max(new_w, new_h) != target_size) {
        if (src.width >= src.height) {
            new_w = target_size;
            new_h = std::max<uint32_t>(
                1, static_cast<uint32_t>(std::lround(static_cast<double>(src.height) *
                                                     static_cast<double>(target_size) /
                                                     static_cast<double>(src.width))));
        } else {
            new_h = target_size;
            new_w = std::max<uint32_t>(
                1, static_cast<uint32_t>(std::lround(static_cast<double>(src.width) *
                                                     static_cast<double>(target_size) /
                                                     static_cast<double>(src.height))));
        }
    }

    CursorImage out;
    out.width = new_w;
    out.height = new_h;
    out.pixels.resize(static_cast<size_t>(new_w) * new_h * 4);

    double scale_x = static_cast<double>(new_w) / static_cast<double>(src.width);
    double scale_y = static_cast<double>(new_h) / static_cast<double>(src.height);
    out.hotspot_x = static_cast<uint16_t>(
        std::clamp(std::round(src.hotspot_x * scale_x), 0.0, static_cast<double>(new_w - 1)));
    out.hotspot_y = static_cast<uint16_t>(
        std::clamp(std::round(src.hotspot_y * scale_y), 0.0, static_cast<double>(new_h - 1)));

    auto sample = [&](int x, int y, int c) -> uint8_t {
        size_t idx = (static_cast<size_t>(y) * src.width + static_cast<size_t>(x)) * 4 + c;
        return src.pixels[idx];
    };

    for (uint32_t y = 0; y < new_h; ++y) {
        double src_y = (static_cast<double>(y) + 0.5) * src.height / new_h - 0.5;
        int y0 = static_cast<int>(std::floor(src_y));
        int y1 = y0 + 1;
        double fy = src_y - y0;
        y0 = std::clamp(y0, 0, static_cast<int>(src.height) - 1);
        y1 = std::clamp(y1, 0, static_cast<int>(src.height) - 1);

        for (uint32_t x = 0; x < new_w; ++x) {
            double src_x = (static_cast<double>(x) + 0.5) * src.width / new_w - 0.5;
            int x0 = static_cast<int>(std::floor(src_x));
            int x1 = x0 + 1;
            double fx = src_x - x0;
            x0 = std::clamp(x0, 0, static_cast<int>(src.width) - 1);
            x1 = std::clamp(x1, 0, static_cast<int>(src.width) - 1);

            for (int c = 0; c < 4; ++c) {
                double v00 = sample(x0, y0, c);
                double v10 = sample(x1, y0, c);
                double v01 = sample(x0, y1, c);
                double v11 = sample(x1, y1, c);
                double v0 = v00 + (v10 - v00) * fx;
                double v1 = v01 + (v11 - v01) * fx;
                double v = v0 + (v1 - v0) * fy;
                out.pixels[(static_cast<size_t>(y) * new_w + x) * 4 + c] =
                    static_cast<uint8_t>(std::clamp(std::round(v), 0.0, 255.0));
            }
        }
    }

    return out;
}

static std::set<uint32_t> collect_sizes_from_images(std::span<const CursorImage> images) {
    std::set<uint32_t> sizes;
    for (const auto& img : images) {
        sizes.insert(nominal_size(img));
    }
    return sizes;
}

static std::set<uint32_t> collect_sizes_from_ani(const std::filesystem::path& ani_path) {
    std::set<uint32_t> sizes;
    auto animation = AniParser::parse(ani_path);
    for (size_t step = 0; step < animation.num_steps; ++step) {
        const auto& frame = animation.get_step_frame(step);
        auto images = IcoCurDecoder::decode_all(frame.icon_data);
        auto frame_sizes = collect_sizes_from_images(images);
        sizes.insert(frame_sizes.begin(), frame_sizes.end());
    }
    return sizes;
}

static std::set<uint32_t> collect_sizes_from_cur(const std::filesystem::path& cur_path) {
    auto data = utils::read_file(cur_path);
    auto images = IcoCurDecoder::decode_all(data);
    return collect_sizes_from_images(images);
}

std::vector<uint32_t> collect_cursor_sizes(const std::filesystem::path& cursor_path) {
    auto ext = cursor_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    std::set<uint32_t> sizes;
    if (ext == ".ani") {
        sizes = collect_sizes_from_ani(cursor_path);
    } else if (ext == ".cur") {
        sizes = collect_sizes_from_cur(cursor_path);
    } else {
        return {};
    }

    std::vector<uint32_t> out;
    out.reserve(sizes.size());
    for (uint32_t size : sizes) {
        out.push_back(size);
    }
    return out;
}

void list_available_sizes(const std::filesystem::path& input_dir) {
    std::map<std::string, std::set<uint32_t>> per_file_sizes;
    std::set<uint32_t> all_sizes;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(input_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        if (ext != ".ani" && ext != ".cur") {
            continue;
        }

        try {
            std::set<uint32_t> sizes =
                (ext == ".ani") ? collect_sizes_from_ani(path) : collect_sizes_from_cur(path);
            per_file_sizes[path.filename().string()] = sizes;
            all_sizes.insert(sizes.begin(), sizes.end());
        } catch (const std::exception& e) {
            spdlog::warn("Failed to read sizes from {}: {}", path.filename().string(), e.what());
        }
    }

    if (per_file_sizes.empty()) {
        spdlog::warn("No .ani or .cur files found in {}", input_dir.string());
        return;
    }

    spdlog::info("Available sizes by file:");
    for (const auto& [name, sizes] : per_file_sizes) {
        if (sizes.empty()) {
            spdlog::info("  {}: (none)", name);
            continue;
        }
        std::string line;
        for (uint32_t size : sizes) {
            if (!line.empty()) line += ", ";
            line += std::to_string(size);
        }
        spdlog::info("  {}: {}", name, line);
    }

    if (!all_sizes.empty()) {
        std::string summary;
        for (uint32_t size : all_sizes) {
            if (!summary.empty()) summary += ", ";
            summary += std::to_string(size);
        }
        spdlog::info("All sizes in directory: {}", summary);
    }
}

}  // namespace ani2xcursor
