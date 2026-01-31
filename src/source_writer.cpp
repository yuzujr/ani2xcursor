#include "source_writer.h"

#include <spdlog/spdlog.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "size_tools.h"
#include "utils/fs.h"

namespace ani2xcursor {

namespace {

struct SizeGroup {
    uint32_t size = 0;
    std::vector<size_t> indices;
};

size_t digit_count(size_t value) {
    size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

std::string format_frame_name(const std::string& base, size_t index, size_t total) {
    if (total <= 1) {
        return base;
    }
    size_t width = std::max<size_t>(2, digit_count(total));
    std::ostringstream name;
    name << base << "-" << std::setw(static_cast<int>(width)) << std::setfill('0') << (index + 1);
    return name.str();
}

void write_png(const fs::path& path, const CursorImage& image) {
    if (image.width == 0 || image.height == 0) {
        throw std::runtime_error(_("Invalid image size for PNG output"));
    }
    if (image.pixels.size() != static_cast<size_t>(image.width) * image.height * 4) {
        throw std::runtime_error(_("Invalid pixel buffer size for PNG output"));
    }

    if (auto parent = path.parent_path(); !parent.empty()) {
        fs::create_directories(parent);
    }

    int stride = static_cast<int>(image.width) * 4;
    int ok = stbi_write_png(path.c_str(), static_cast<int>(image.width),
                            static_cast<int>(image.height), 4, image.pixels.data(), stride);
    if (ok == 0) {
        throw std::runtime_error(_("Failed to write PNG: ") + path.string());
    }
}

std::string base64_encode(std::span<const uint8_t> data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (data.empty()) {
        return {};
    }

    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                     (static_cast<uint32_t>(data[i + 2]));
        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        out.push_back(kTable[(v >> 6) & 0x3F]);
        out.push_back(kTable[v & 0x3F]);
        i += 3;
    }

    size_t rem = data.size() - i;
    if (rem == 1) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t v =
            (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(kTable[(v >> 18) & 0x3F]);
        out.push_back(kTable[(v >> 12) & 0x3F]);
        out.push_back(kTable[(v >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}

std::vector<SizeGroup> group_by_size(const std::vector<CursorImage>& frames) {
    std::vector<SizeGroup> groups;
    std::unordered_map<uint32_t, size_t> index_by_size;

    for (size_t i = 0; i < frames.size(); ++i) {
        uint32_t size = nominal_size(frames[i]);
        auto it = index_by_size.find(size);
        if (it == index_by_size.end()) {
            index_by_size[size] = groups.size();
            groups.push_back(SizeGroup{size, {}});
            it = index_by_size.find(size);
        }
        groups[it->second].indices.push_back(i);
    }

    return groups;
}

bool is_animated(const std::vector<SizeGroup>& groups) {
    for (const auto& group : groups) {
        if (group.indices.size() > 1) {
            return true;
        }
    }
    return false;
}

}  // namespace

void SourceWriter::write_cursor(const fs::path& src_dir, const std::string& primary_name,
                                const std::vector<CursorImage>& frames,
                                const std::vector<uint32_t>& delays_ms) {
    if (frames.empty()) {
        throw std::runtime_error(_("No frames to write for cursor: ") + primary_name);
    }
    if (frames.size() != delays_ms.size()) {
        throw std::runtime_error(_("Frame/delay count mismatch for cursor: ") + primary_name);
    }

    auto groups = group_by_size(frames);
    if (groups.empty()) {
        throw std::runtime_error(_("No size groups found for cursor: ") + primary_name);
    }

    bool animated = is_animated(groups);

    fs::path config_dir = src_dir / "config";
    fs::path svg_dir = src_dir / "svg";
    fs::path png_root = src_dir / "png";

    std::ostringstream config;

    for (const auto& group : groups) {
        fs::path png_dir = png_root / std::to_string(group.size);
        size_t frame_count = group.indices.size();

        for (size_t frame_idx = 0; frame_idx < frame_count; ++frame_idx) {
            size_t img_index = group.indices[frame_idx];
            const auto& img = frames[img_index];
            std::string frame_name = format_frame_name(primary_name, frame_idx, frame_count);

            fs::path png_path = png_dir / (frame_name + ".png");
            write_png(png_path, img);

            std::string rel_path = "png/" + std::to_string(group.size) + "/" + frame_name + ".png";

            config << group.size << " " << img.hotspot_x << " " << img.hotspot_y << " " << rel_path;
            if (animated) {
                config << " " << delays_ms[img_index];
            }
            config << "\n";
        }
    }

    fs::path config_path = config_dir / (primary_name + ".cursor");
    utils::write_file_string(config_path, config.str());

    const SizeGroup* svg_group = &groups[0];
    for (const auto& group : groups) {
        if (group.size > svg_group->size) {
            svg_group = &group;
        }
    }

    fs::create_directories(svg_dir);

    size_t svg_frame_count = svg_group->indices.size();
    for (size_t frame_idx = 0; frame_idx < svg_frame_count; ++frame_idx) {
        size_t img_index = svg_group->indices[frame_idx];
        const auto& img = frames[img_index];
        std::string frame_name = format_frame_name(primary_name, frame_idx, svg_frame_count);

        fs::path png_path = png_root / std::to_string(svg_group->size) / (frame_name + ".png");
        auto png_data = utils::read_file(png_path);
        std::string encoded = base64_encode(png_data);

        std::ostringstream svg;
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << img.width << "\" height=\""
            << img.height << "\" viewBox=\"0 0 " << img.width << " " << img.height << "\">"
            << "<image width=\"" << img.width << "\" height=\"" << img.height
            << "\" href=\"data:image/png;base64," << encoded << "\" />"
            << "</svg>\n";

        fs::path svg_path = svg_dir / (frame_name + ".svg");
        utils::write_file_string(svg_path, svg.str());
    }

    spdlog::debug("Wrote source cursor '{}'", primary_name);
}

void SourceWriter::write_cursor_list(const fs::path& src_dir,
                                     const std::vector<CursorListEntry>& entries) {
    std::ostringstream out;
    for (const auto& entry : entries) {
        out << entry.alias << " " << entry.target << "\n";
    }

    fs::path list_path = src_dir / "cursorList";
    utils::write_file_string(list_path, out.str());
}

}  // namespace ani2xcursor
