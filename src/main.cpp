#include "inf_parser.h"
#include "ani_parser.h"
#include "ico_cur_decoder.h"
#include "manual_mapping.h"
#include "preview_generator.h"
#include "size_filter.h"
#include "size_selection.h"
#include "theme_installer.h"
#include "utils/fs.h"
#include "xcursor_writer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::optional<fs::path> find_file_icase(const fs::path& dir, const std::string& filename) {
    auto exact_path = dir / filename;
    if (fs::exists(exact_path)) {
        return exact_path;
    }
    
    std::string lower_target = filename;
    std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        
        std::string entry_name = entry.path().filename().string();
        std::string lower_entry = entry_name;
        std::transform(lower_entry.begin(), lower_entry.end(), lower_entry.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        
        if (lower_entry == lower_target) {
            return entry.path();
        }
    }
    
    return std::nullopt;
}

struct Args {
    fs::path input_dir;
    fs::path output_dir;
    bool install = false;
    bool verbose = false;
    bool skip_broken = false;
    bool manual_mapping = false;
    bool list_sizes = false;
    bool help = false;
    SizeFilter size_filter = SizeFilter::All;
    std::vector<uint32_t> specific_sizes;
};

uint32_t nominal_size(const ani2xcursor::CursorImage& img) {
    return std::max(img.width, img.height);
}

std::optional<size_t> find_exact_size_index(std::span<const ani2xcursor::CursorImage> images,
                                            uint32_t target_size) {
    for (size_t idx = 0; idx < images.size(); ++idx) {
        if (nominal_size(images[idx]) == target_size) {
            return idx;
        }
    }
    return std::nullopt;
}

size_t find_closest_size_index(std::span<const ani2xcursor::CursorImage> images,
                               uint32_t target_size) {
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

ani2xcursor::CursorImage rescale_cursor(const ani2xcursor::CursorImage& src, uint32_t target_size) {
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

    ani2xcursor::CursorImage out;
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

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " <input_dir> [options]\n\n"
              << "Convert Windows Animated Cursors (.ani) to Linux Xcursor theme.\n\n"
              << "Arguments:\n"
              << "  <input_dir>       Directory containing Install.inf and .ani files\n\n"
              << "Options:\n"
              << "  --out, -o <dir>       Output directory (default: ./out)\n"
              << "  --install, -i         Install theme to $XDG_DATA_HOME/icons\n"
              << "  --verbose, -v         Enable verbose logging\n"
              << "  --skip-broken         Continue on conversion errors\n"
              << "  --manual-mapping      Generate previews + mapping.toml then exit\n"
              << "  --list-sizes          Show available sizes in cursor files then exit\n"
              << "  --sizes <mode>        Size selection mode:\n"
              << "                          all    - Export all sizes (default)\n"
              << "                          max    - Export only largest size\n"
              << "                          24,32  - Ensure sizes (reuse if present, rescale if missing)\n"
              << "  --help, -h            Show this help message\n";
}

Args parse_args(int argc, char* argv[]) {
    Args args;
    args.output_dir = "out";
    
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            args.help = true;
            return args;
        } else if (arg == "--verbose" || arg == "-v") {
            args.verbose = true;
        } else if (arg == "--install" || arg == "-i") {
            args.install = true;
        } else if (arg == "--skip-broken") {
            args.skip_broken = true;
        } else if (arg == "--manual-mapping") {
            args.manual_mapping = true;
        } else if (arg == "--list-sizes") {
            args.list_sizes = true;
        } else if ((arg == "--out" || arg == "-o") && i + 1 < argc) {
            args.output_dir = argv[++i];
        } else if (arg == "--sizes" && i + 1 < argc) {
            std::string sizes_arg = argv[++i];
            if (sizes_arg == "all") {
                args.size_filter = SizeFilter::All;
            } else if (sizes_arg == "max") {
                args.size_filter = SizeFilter::Max;
            } else {
                // Parse comma-separated list of sizes
                args.size_filter = SizeFilter::Specific;
                size_t pos = 0;
                while (pos < sizes_arg.length()) {
                    size_t comma = sizes_arg.find(',', pos);
                    std::string size_str = sizes_arg.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                    try {
                        uint32_t size = std::stoul(size_str);
                        if (size > 0 && size <= 256) {
                            args.specific_sizes.push_back(size);
                        } else {
                            throw std::runtime_error("Size must be between 1 and 256");
                        }
                    } catch (const std::exception& e) {
                        throw std::runtime_error("Invalid size value: " + size_str);
                    }
                    if (comma == std::string::npos) break;
                    pos = comma + 1;
                }
                if (args.specific_sizes.empty()) {
                    throw std::runtime_error("No valid sizes specified");
                }
            }
        } else if (!arg.starts_with("-") && args.input_dir.empty()) {
            args.input_dir = arg;
        } else {
            throw std::runtime_error(std::string("Unknown argument: ") + std::string(arg));
        }
    }
    
    return args;
}

std::set<uint32_t> collect_sizes_from_images(std::span<const ani2xcursor::CursorImage> images) {
    std::set<uint32_t> sizes;
    for (const auto& img : images) {
        sizes.insert(nominal_size(img));
    }
    return sizes;
}

std::set<uint32_t> collect_sizes_from_ani(const fs::path& ani_path) {
    std::set<uint32_t> sizes;
    auto animation = ani2xcursor::AniParser::parse(ani_path);
    for (size_t step = 0; step < animation.num_steps; ++step) {
        const auto& frame = animation.get_step_frame(step);
        auto images = ani2xcursor::IcoCurDecoder::decode_all(frame.icon_data);
        auto frame_sizes = collect_sizes_from_images(images);
        sizes.insert(frame_sizes.begin(), frame_sizes.end());
    }
    return sizes;
}

std::set<uint32_t> collect_sizes_from_cur(const fs::path& cur_path) {
    auto data = ani2xcursor::utils::read_file(cur_path);
    auto images = ani2xcursor::IcoCurDecoder::decode_all(data);
    return collect_sizes_from_images(images);
}

void list_available_sizes(const fs::path& input_dir) {
    std::map<std::string, std::set<uint32_t>> per_file_sizes;
    std::set<uint32_t> all_sizes;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(input_dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext != ".ani" && ext != ".cur") {
            continue;
        }

        try {
            std::set<uint32_t> sizes = (ext == ".ani") ?
                collect_sizes_from_ani(path) : collect_sizes_from_cur(path);
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

// Process a single .ani file and return decoded frames with multi-size support
std::pair<std::vector<ani2xcursor::CursorImage>, std::vector<uint32_t>>
process_ani_file(const fs::path& ani_path, SizeFilter filter, const std::vector<uint32_t>& specific_sizes) {
    spdlog::info("Processing: {}", ani_path.filename().string());
    
    auto animation = ani2xcursor::AniParser::parse(ani_path);
    
    std::vector<std::vector<ani2xcursor::CursorImage>> frames_by_step;
    std::vector<uint32_t> step_delays;
    
    for (size_t step = 0; step < animation.num_steps; ++step) {
        const auto& frame = animation.get_step_frame(step);
        uint32_t delay = frame.delay_ms;
        
        auto images = ani2xcursor::IcoCurDecoder::decode_all(frame.icon_data);
        
        if (images.empty()) {
            throw std::runtime_error("No images decoded from frame " + std::to_string(step));
        }
        
        spdlog::debug("Frame {}: {} sizes", step, images.size());
        
        frames_by_step.push_back(std::move(images));
        step_delays.push_back(delay);
    }
    
    size_t num_sizes = frames_by_step[0].size();
    for (size_t i = 1; i < frames_by_step.size(); ++i) {
        if (frames_by_step[i].size() != num_sizes) {
            spdlog::warn("Inconsistent sizes, using first size only");
            num_sizes = 1;
            break;
        }
    }

    std::vector<ani2xcursor::CursorImage> decoded_frames;
    std::vector<uint32_t> delays;
    std::map<uint32_t, size_t> size_frame_counts;

    std::span<const ani2xcursor::CursorImage> size_span(frames_by_step[0].data(), num_sizes);

    if (filter == SizeFilter::Specific) {
        std::vector<uint32_t> targets;
        targets.reserve(specific_sizes.size());
        for (uint32_t size : specific_sizes) {
            if (std::find(targets.begin(), targets.end(), size) == targets.end()) {
                targets.push_back(size);
            }
        }

        for (uint32_t target_size : targets) {
            auto exact_idx = find_exact_size_index(size_span, target_size);
            bool needs_rescale = !exact_idx.has_value();
            size_t source_idx = exact_idx.value_or(find_closest_size_index(size_span, target_size));
            uint32_t source_size = nominal_size(frames_by_step[0][source_idx]);

            if (needs_rescale) {
                spdlog::info("Rescaling {}x{} -> {}x{}", source_size, source_size,
                             target_size, target_size);
            }

            for (size_t step = 0; step < frames_by_step.size(); ++step) {
                const auto& img = frames_by_step[step][source_idx];
                if (needs_rescale) {
                    decoded_frames.push_back(rescale_cursor(img, target_size));
                } else {
                    decoded_frames.push_back(img);
                }
                delays.push_back(step_delays[step]);
            }

            size_frame_counts[target_size] = frames_by_step.size();
        }
    } else {
        auto size_indices_to_export = ani2xcursor::select_size_indices(size_span, filter, specific_sizes);

        if (size_indices_to_export.empty()) {
            throw std::runtime_error("No sizes selected for export");
        }
        
        for (size_t size_idx : size_indices_to_export) {
            uint32_t nominal = 0;
            
            for (size_t step = 0; step < frames_by_step.size(); ++step) {
                const auto& img = frames_by_step[step][size_idx];
                nominal = nominal_size(img);
                
                decoded_frames.push_back(img);
                delays.push_back(step_delays[step]);
            }
            
            size_frame_counts[nominal] = frames_by_step.size();
        }
    }
    
    if (spdlog::get_level() <= spdlog::level::info) {
        spdlog::info("Exported {} sizes:", size_frame_counts.size());
        for (const auto& [size, count] : size_frame_counts) {
            spdlog::info("  {}x{}: {} frames", size, size, count);
        }
    }
    
    if (decoded_frames.empty()) {
        throw std::runtime_error("No frames decoded from " + ani_path.string());
    }
    
    return {std::move(decoded_frames), std::move(delays)};
}

std::pair<std::vector<ani2xcursor::CursorImage>, std::vector<uint32_t>>
process_cur_file(const fs::path& cur_path, SizeFilter filter, const std::vector<uint32_t>& specific_sizes) {
    spdlog::info("Processing: {}", cur_path.filename().string());

    auto data = ani2xcursor::utils::read_file(cur_path);
    auto images = ani2xcursor::IcoCurDecoder::decode_all(data);

    std::vector<ani2xcursor::CursorImage> decoded_images;
    std::vector<uint32_t> delays;
    std::map<uint32_t, size_t> size_counts;
    std::span<const ani2xcursor::CursorImage> image_span(images.data(), images.size());

    if (filter == SizeFilter::Specific) {
        std::vector<uint32_t> targets;
        targets.reserve(specific_sizes.size());
        for (uint32_t size : specific_sizes) {
            if (std::find(targets.begin(), targets.end(), size) == targets.end()) {
                targets.push_back(size);
            }
        }

        for (uint32_t target_size : targets) {
            auto exact_idx = find_exact_size_index(image_span, target_size);
            bool needs_rescale = !exact_idx.has_value();
            size_t source_idx = exact_idx.value_or(find_closest_size_index(image_span, target_size));
            uint32_t source_size = nominal_size(images[source_idx]);

            if (needs_rescale) {
                spdlog::info("Rescaling {}x{} -> {}x{}", source_size, source_size,
                             target_size, target_size);
                decoded_images.push_back(rescale_cursor(images[source_idx], target_size));
            } else {
                decoded_images.push_back(images[source_idx]);
            }
            delays.push_back(0);
            size_counts[target_size]++;
        }
    } else {
        auto size_indices = ani2xcursor::select_size_indices(image_span, filter, specific_sizes);
        if (size_indices.empty()) {
            throw std::runtime_error("No sizes selected for export");
        }

        decoded_images.reserve(size_indices.size());
        delays.reserve(size_indices.size());

        for (size_t idx : size_indices) {
            const auto& img = images[idx];
            decoded_images.push_back(img);
            delays.push_back(0);
            size_counts[nominal_size(img)]++;
        }
    }

    if (spdlog::get_level() <= spdlog::level::info) {
        spdlog::info("Exported {} sizes:", size_counts.size());
        for (const auto& [size, count] : size_counts) {
            spdlog::info("  {}x{}: {} frames", size, size, count);
        }
    }

    return {std::move(decoded_images), std::move(delays)};
}

bool is_ani_file(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".ani";
}

bool is_cur_file(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".cur";
}

std::string normalize_relative_path(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

int main(int argc, char* argv[]) {
    try {
        // Parse command line arguments
        Args args = parse_args(argc, argv);
        
        if (args.help) {
            print_usage(argv[0]);
            return 0;
        }
        
        if (args.input_dir.empty()) {
            std::cerr << "Error: input directory required\n\n";
            print_usage(argv[0]);
            return 1;
        }
        
        // Setup logging
        auto logger = spdlog::stdout_color_mt("console");
        spdlog::set_default_logger(logger);
        spdlog::set_level(args.verbose ? spdlog::level::debug : spdlog::level::info);
        spdlog::set_pattern("[%^%l%$] %v");
        
        // Validate input directory
        if (!fs::exists(args.input_dir)) {
            spdlog::error("Input directory does not exist: {}", args.input_dir.string());
            return 1;
        }

        if (args.list_sizes) {
            list_available_sizes(args.input_dir);
            return 0;
        }
        
        auto mapping_dir = args.input_dir / "ani2xcursor";
        auto mapping_path = mapping_dir / "mapping.toml";
        bool mapping_present = fs::exists(mapping_path);

        std::optional<ani2xcursor::MappingLoadResult> manual_mapping;

        if (args.manual_mapping) {
            if (mapping_present) {
                try {
                    manual_mapping = ani2xcursor::load_mapping_toml(mapping_path);
                    for (const auto& warning : manual_mapping->warnings) {
                        spdlog::warn("mapping.toml: {}", warning);
                    }
                    spdlog::info("Manual mapping requested; using existing mapping.toml.");
                } catch (const std::exception& e) {
                    spdlog::warn("Manual mapping requested but mapping.toml failed to parse: {}", e.what());
                }
            }

            if (!manual_mapping) {
                spdlog::info("Manual mapping requested; generating previews and mapping.toml.");
                auto preview_dir = mapping_dir / "previews";
                auto preview_result = ani2xcursor::generate_previews(
                    args.input_dir, preview_dir, args.size_filter, args.specific_sizes);
                ani2xcursor::write_mapping_toml_template(mapping_path, args.input_dir, preview_result.guesses);

                std::error_code abs_ec;
                auto abs_mapping = fs::absolute(mapping_path, abs_ec);
                auto abs_previews = fs::absolute(preview_dir, abs_ec);
                if (abs_ec) {
                    abs_mapping = mapping_path;
                    abs_previews = preview_dir;
                }
                spdlog::info("Generated: {} and {}", abs_mapping.string(),
                             (abs_previews / "*.png").string());
                spdlog::info("Edit mapping.toml and re-run the same command.");
                return 0;
            }
        }

        if (!manual_mapping && mapping_present) {
            try {
                manual_mapping = ani2xcursor::load_mapping_toml(mapping_path);
                for (const auto& warning : manual_mapping->warnings) {
                    spdlog::warn("mapping.toml: {}", warning);
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to parse mapping.toml: {}", e.what());
            }
        }

        std::optional<ani2xcursor::InfResult> inf_data;
        std::string theme_name;
        std::vector<ani2xcursor::CursorMapping> mappings;

        if (manual_mapping) {
            std::error_code name_ec;
            auto abs_input = fs::absolute(args.input_dir, name_ec);
            fs::path name_source = name_ec ? args.input_dir : abs_input;
            if (manual_mapping->theme_name.empty()) {
                theme_name = name_source.filename().string();
            } else {
                theme_name = manual_mapping->theme_name;
            }
            if (theme_name.empty()) {
                theme_name = name_source.parent_path().filename().string();
            }
            if (theme_name.empty()) {
                theme_name = "cursor_theme";
            }

            bool missing_required = false;
            for (const auto& role : ani2xcursor::known_roles()) {
                auto it = manual_mapping->role_to_path.find(role);
                if (it == manual_mapping->role_to_path.end() || it->second.empty()) {
                    spdlog::warn("mapping.toml: role '{}' is not mapped", role);
                    if (!ani2xcursor::is_optional_role(role) && !args.skip_broken) {
                        missing_required = true;
                    }
                    continue;
                }
                mappings.push_back({role, it->second});
            }

            if (missing_required) {
                spdlog::error("Missing required roles in mapping.toml (use --skip-broken to continue)");
                return 1;
            }
        } else {
            auto inf_path = args.input_dir / "Install.inf";
            if (!fs::exists(inf_path)) {
                // Try lowercase
                inf_path = args.input_dir / "install.inf";
            }

            if (!fs::exists(inf_path)) {
                if (mapping_present) {
                    spdlog::error("Install.inf not found and mapping.toml failed to parse");
                    return 1;
                }

                spdlog::warn("Install.inf not found and mapping.toml not present.");
                auto preview_dir = mapping_dir / "previews";
                auto preview_result = ani2xcursor::generate_previews(
                    args.input_dir, preview_dir, args.size_filter, args.specific_sizes);
                ani2xcursor::write_mapping_toml_template(mapping_path, args.input_dir, preview_result.guesses);
                std::error_code abs_ec;
                auto abs_mapping = fs::absolute(mapping_path, abs_ec);
                auto abs_previews = fs::absolute(preview_dir, abs_ec);
                if (abs_ec) {
                    abs_mapping = mapping_path;
                    abs_previews = preview_dir;
                }
                spdlog::warn("Generated: {} and {}", abs_mapping.string(),
                             (abs_previews / "*.png").string());
                spdlog::warn("Edit mapping.toml and re-run the same command.");
                return 2;
            }

            if (mapping_present) {
                spdlog::warn("Falling back to Install.inf because mapping.toml could not be parsed");
            }

            inf_data = ani2xcursor::InfParser::parse(inf_path);
            theme_name = inf_data->theme_name;
            mappings = inf_data->mappings;
        }

        spdlog::info("Theme: {} ({} cursors)", theme_name, mappings.size());

        // Create output directory structure
        auto theme_dir = args.output_dir / theme_name;
        auto cursors_dir = theme_dir / "cursors";
        
        fs::create_directories(cursors_dir);
        
        // Process each cursor
        int success_count = 0;
        int error_count = 0;
        
        bool using_manual = manual_mapping.has_value();

        for (const auto& mapping : mappings) {
            fs::path cursor_path;
            std::string display_name;

            if (using_manual) {
                std::string rel = normalize_relative_path(mapping.value);
                cursor_path = args.input_dir / rel;
                display_name = rel;

                if (!fs::exists(cursor_path)) {
                    spdlog::error("Cursor file not found: {}", display_name);
                    if (!args.skip_broken) {
                        return 1;
                    }
                    ++error_count;
                    continue;
                }
            } else {
                // Extract just the filename from the (possibly full Windows) path
                auto filename = ani2xcursor::InfResult::extract_filename(mapping.value);
                display_name = filename;

                // Find file with case-insensitive matching (Windows compatibility)
                auto ani_path_opt = find_file_icase(args.input_dir, filename);

                if (!ani_path_opt) {
                    spdlog::error("Cursor file not found: {}", filename);
                    if (!args.skip_broken) {
                        return 1;
                    }
                    ++error_count;
                    continue;
                }

                cursor_path = *ani_path_opt;
            }

            try {
                std::pair<std::vector<ani2xcursor::CursorImage>, std::vector<uint32_t>> frames_delays;
                if (is_cur_file(cursor_path)) {
                    frames_delays = process_cur_file(cursor_path, args.size_filter, args.specific_sizes);
                } else if (is_ani_file(cursor_path)) {
                    frames_delays = process_ani_file(cursor_path, args.size_filter, args.specific_sizes);
                } else {
                    throw std::runtime_error("Unsupported cursor file type");
                }
                auto& frames = frames_delays.first;
                auto& delays = frames_delays.second;
                
                // Get X11 cursor name and aliases
                auto names = ani2xcursor::XcursorWriter::get_cursor_names(mapping.role);
                
                // Write Xcursor file
                auto cursor_path = cursors_dir / names.primary;
                ani2xcursor::XcursorWriter::write_cursor(frames, delays, cursor_path);
                
                // Create aliases
                ani2xcursor::XcursorWriter::create_aliases(cursors_dir, names.primary, 
                                                            names.aliases);
                
                spdlog::debug("Converted '{}' -> {}", mapping.role, names.primary);
                ++success_count;
                
            } catch (const std::exception& e) {
                spdlog::error("Failed to convert {}: {}", display_name, e.what());
                if (!args.skip_broken) {
                    return 1;
                }
                ++error_count;
            }
        }
        
        if (success_count == 0) {
            spdlog::error("No cursors were converted successfully");
            return 1;
        }
        
        // Write index.theme
        ani2xcursor::XcursorWriter::write_index_theme(theme_dir, theme_name);
        
        spdlog::info("Conversion complete: {} cursors converted, {} errors", 
                     success_count, error_count);
        
        // Install if requested
        if (args.install) {
            ani2xcursor::ThemeInstaller::install(theme_dir);
        } else {
            spdlog::info("Theme created at: {}", theme_dir.string());
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return 1;
    }
}
