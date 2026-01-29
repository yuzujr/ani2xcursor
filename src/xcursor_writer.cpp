#include "xcursor_writer.h"

#include <X11/Xcursor/Xcursor.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <stdexcept>

#include "utils/fs.h"

namespace ani2xcursor {

std::map<std::string, XcursorWriter::CursorNames> XcursorWriter::cursor_mappings_;
bool XcursorWriter::mappings_initialized_ = false;

void XcursorWriter::init_cursor_mappings() {
    if (mappings_initialized_) return;

    // Windows role -> X11 cursor "primary name" + aliases
    //
    // Notes:
    // - Prefer *standard X11 cursorfont names* (X11/cursorfont.h without the XC_
    // prefix) as primary names
    //   so themes/tooling that expect standard names can resolve them reliably.
    // - Non-standard/common extra names (like "pointer") are kept as aliases.
    //
    // Canonical X11 cursorfont primaries we use here:
    // left_ptr, help, left_ptr_watch, watch, crosshair, xterm,
    // pencil, not-allowed (non-standard but widely used), sb_v_double_arrow,
    // sb_h_double_arrow, bd_double_arrow, fd_double_arrow, fleur, center_ptr,
    // hand2

    // Arrow / default pointer
    cursor_mappings_["pointer"] = {"left_ptr",
                                   {"default", "arrow", "top_left_arrow",
                                    // some themes use these
                                    "left_ptr"}};

    // Help
    cursor_mappings_["help"] = {
        "help", {"question_arrow", "whats_this", "d9ce0ab605698f320427677b458ad60b"}};

    // AppStarting / working-in-background (Windows)
    cursor_mappings_["working"] = {
        "left_ptr_watch",
        {"progress", "half-busy",
         // common hashed aliases seen in popular themes
         "00000000000000020006000e7e9ffc3f", "3ecb610c1bf2410f44200f48c40d3599",
         "08e8e1c95fe2fc01f976f1e063a24ccd",
         // some themes use these literal names
         "left_ptr_watch"}};

    // Wait / busy (Windows)
    cursor_mappings_["busy"] = {"watch",
                                {"wait", "clock", "0426c94ea35c87780ff01dc239897213", "watch"}};

    // Precision / crosshair
    cursor_mappings_["precision"] = {
        "crosshair",
        {
            "cross", "cross_reverse", "tcross", "diamond_cross", "crosshair"
            // (Optional) add hashed crosshair aliases here if you encounter them
            // in the wild
        }};

    // Text / IBeam
    cursor_mappings_["text"] = {"xterm", {"ibeam", "text", "xterm"}};

    // Handwriting / pen
    cursor_mappings_["hand"] = {"pencil", {"handwriting", "pencil"}};

    // Unavailable / No
    cursor_mappings_["unavailable"] = {
        "not-allowed",
        {"no-drop", "crossed_circle", "forbidden", "03b6e0fcb3499374a867c041f52298f0", "circle",
         "not-allowed"}};

    // Vertical resize
    cursor_mappings_["vert"] = {"sb_v_double_arrow",
                                {"ns-resize", "size_ver", "v_double_arrow", "row-resize",
                                 "n-resize", "s-resize", "00008160000006810000408080010102",
                                 "split_v", "top_side", "bottom_side", "sb_v_double_arrow"}};

    // Horizontal resize
    cursor_mappings_["horz"] = {"sb_h_double_arrow",
                                {"ew-resize", "size_hor", "h_double_arrow", "col-resize",
                                 "e-resize", "w-resize", "028006030e0e7ebffc7f7070c0600140",
                                 "split_h", "left_side", "right_side", "sb_h_double_arrow"}};

    // Diagonal resize (NW-SE)
    cursor_mappings_["dgn1"] = {"bd_double_arrow",
                                {"nwse-resize", "size_fdiag", "fd_double_arrow", "nw-resize",
                                 "se-resize", "c7088f0f3e6c8088236ef8e1e3e70000", "top_left_corner",
                                 "bottom_right_corner", "bd_double_arrow"}};

    // Diagonal resize (NE-SW)
    cursor_mappings_["dgn2"] = {
        "fd_double_arrow",
        {"nesw-resize", "size_bdiag", "ne-resize", "sw-resize", "fcf1c3c7cd4491d801f1e1c78f100000",
         "top_right_corner", "bottom_left_corner", "fd_double_arrow"}};

    // Move / size all
    cursor_mappings_["move"] = {
        "fleur",
        {"move", "size_all", "all-scroll", "grabbing", "4498f0e0c1937ffe01fd06f973665830",
         "9081237383d90e509aa00f00170e968f", "fleur"}};

    // Alternate / up arrow / center_ptr
    cursor_mappings_["alternate"] = {"center_ptr", {"up-arrow", "up_arrow", "center_ptr"}};

    // Link / hand cursor
    cursor_mappings_["link"] = {"hand2",
                                {"hand", "hand1", "hand2",
                                 "pointer",  // non-standard but appears in some themes
                                 "pointing_hand", "openhand", "e29285e634086352946a0e7090d73106",
                                 "9d800788f1b08800ae810202380a0822"}};

    // Optional newer roles (Windows 10/11 packs sometimes include these)
    cursor_mappings_["person"] = {"person", {}};
    cursor_mappings_["pin"] = {"pin", {}};

    mappings_initialized_ = true;
}

void XcursorWriter::write_cursor(const std::vector<CursorImage>& images,
                                 const std::vector<uint32_t>& delays_ms,
                                 const fs::path& output_path) {
    if (images.empty()) {
        throw std::runtime_error("No images to write");
    }

    // Multi-size support: images should be grouped by nominal size
    // i.e., [all frames of size1, all frames of size2, ...]
    // Xcursor format stores all images with their size field, and the loader
    // selects appropriate size based on display DPI/scale
    //
    // For animated cursors: frames of the same size form an animation sequence
    // Important: XcursorImagesLoadImages() uses the 'size' field (max(w,h))
    // to group images for size selection, and 'delay' for animation timing.

    // Log size distribution for debugging
    std::map<uint32_t, size_t> size_counts;
    for (const auto& img : images) {
        uint32_t nominal_size = std::max(img.width, img.height);
        size_counts[nominal_size]++;
    }

    // Create directory if needed
    fs::create_directories(output_path.parent_path());

    // Create XcursorImages structure
    XcursorImages* xcur_images = XcursorImagesCreate(static_cast<int>(images.size()));
    if (!xcur_images) {
        throw std::runtime_error("Failed to create XcursorImages");
    }

    // Clean up on exit
    auto cleanup = [&]() {
        XcursorImagesDestroy(xcur_images);
    };

    try {
        for (size_t i = 0; i < images.size(); ++i) {
            const auto& img = images[i];

            XcursorImage* xcur_img =
                XcursorImageCreate(static_cast<int>(img.width), static_cast<int>(img.height));

            if (!xcur_img) {
                throw std::runtime_error("Failed to create XcursorImage");
            }

            // XcursorImageCreate sets xcur_img->size = max(width, height)
            // This is the "nominal size" used by the loader for size selection

            xcur_img->xhot = img.hotspot_x;
            xcur_img->yhot = img.hotspot_y;
            xcur_img->delay = (i < delays_ms.size()) ? delays_ms[i] : 100;

            // Convert RGBA to Xcursor format (ARGB, but actually BGRA in memory on
            // LE) Xcursor expects premultiplied ARGB in native byte order On
            // little-endian: bytes are B, G, R, A
            size_t pixel_count = static_cast<size_t>(img.width) * img.height;

            for (size_t p = 0; p < pixel_count; ++p) {
                uint8_t r = img.pixels[p * 4 + 0];
                uint8_t g = img.pixels[p * 4 + 1];
                uint8_t b = img.pixels[p * 4 + 2];
                uint8_t a = img.pixels[p * 4 + 3];

                // Xcursor uses ARGB format (32-bit value: AARRGGBB)
                xcur_img->pixels[p] =
                    (static_cast<XcursorPixel>(a) << 24) | (static_cast<XcursorPixel>(r) << 16) |
                    (static_cast<XcursorPixel>(g) << 8) | (static_cast<XcursorPixel>(b) << 0);
            }

            xcur_images->images[i] = xcur_img;
        }

        xcur_images->nimage = static_cast<int>(images.size());

        // Write to file using filename-based API
        if (!XcursorFilenameSaveImages(output_path.c_str(), xcur_images)) {
            throw std::runtime_error("Failed to write Xcursor file: " + output_path.string());
        }

        spdlog::debug("Wrote {}", output_path.filename().string());

    } catch (...) {
        cleanup();
        throw;
    }

    cleanup();
}

void XcursorWriter::write_cursor(const CursorImage& image, const fs::path& output_path) {
    write_cursor(std::vector<CursorImage>{image}, std::vector<uint32_t>{0}, output_path);
}

void XcursorWriter::write_index_theme(const fs::path& theme_dir, const std::string& theme_name) {
    auto index_path = theme_dir / "index.theme";

    std::string content = "[Icon Theme]\n";
    content += "Name=" + theme_name + "\n";
    content += "Comment=Cursor theme converted from Windows ANI by ani2xcursor\n";
    content += "Inherits=default\n";

    utils::write_file_string(index_path, content);
    spdlog::debug("Wrote index.theme");
}

XcursorWriter::CursorNames XcursorWriter::get_cursor_names(const std::string& win_role) {
    init_cursor_mappings();

    auto it = cursor_mappings_.find(win_role);
    if (it != cursor_mappings_.end()) {
        return it->second;
    }

    // Unknown role - use as-is
    return {win_role, {}};
}

void XcursorWriter::create_aliases(const fs::path& cursors_dir, const std::string& primary_name,
                                   const std::vector<std::string>& aliases) {
    auto primary_path = cursors_dir / primary_name;

    if (!fs::exists(primary_path)) {
        spdlog::warn("Cannot create aliases: primary cursor '{}' does not exist", primary_name);
        return;
    }

    for (const auto& alias : aliases) {
        auto alias_path = cursors_dir / alias;

        // Skip if alias already exists
        if (fs::exists(alias_path)) {
            spdlog::debug("Alias '{}' already exists, skipping", alias);
            continue;
        }

        std::error_code ec;
        fs::create_symlink(primary_name, alias_path, ec);

        if (ec) {
            spdlog::warn("Failed to create symlink {} -> {}: {}", alias, primary_name,
                         ec.message());
        }
    }
}

}  // namespace ani2xcursor
