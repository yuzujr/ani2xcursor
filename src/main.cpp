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
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <optional>
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
    bool help = false;
    SizeFilter size_filter = SizeFilter::All;
    std::vector<uint32_t> specific_sizes;
};

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
              << "  --sizes <mode>        Size selection mode:\n"
              << "                          all    - Export all sizes (default)\n"
              << "                          max    - Export only largest size\n"
              << "                          24,32  - Export specific sizes (comma-separated)\n"
              << "  --help, -h            Show this help message\n\n"
              << "Examples:\n"
              << "  " << program << " ~/Downloads/MyCursor -o ./themes -i\n"
              << "  " << program << " ~/Downloads/MyCursor --sizes max\n"
              << "  " << program << " ~/Downloads/MyCursor --sizes 32,48\n";
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

    std::span<const ani2xcursor::CursorImage> size_span(frames_by_step[0].data(), num_sizes);
    auto size_indices_to_export = ani2xcursor::select_size_indices(size_span, filter, specific_sizes);

    if (size_indices_to_export.empty()) {
        throw std::runtime_error("No sizes selected for export");
    }
    
    std::vector<ani2xcursor::CursorImage> decoded_frames;
    std::vector<uint32_t> delays;
    std::map<uint32_t, size_t> size_frame_counts;
    
    for (size_t size_idx : size_indices_to_export) {
        uint32_t nominal_size = 0;
        
        for (size_t step = 0; step < frames_by_step.size(); ++step) {
            const auto& img = frames_by_step[step][size_idx];
            nominal_size = std::max(img.width, img.height);
            
            decoded_frames.push_back(img);
            delays.push_back(step_delays[step]);
        }
        
        size_frame_counts[nominal_size] = frames_by_step.size();
    }
    
    if (spdlog::get_level() <= spdlog::level::info) {
        spdlog::info("Exported {} sizes:", size_indices_to_export.size());
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

    std::span<const ani2xcursor::CursorImage> image_span(images.data(), images.size());
    auto size_indices = ani2xcursor::select_size_indices(image_span, filter, specific_sizes);
    if (size_indices.empty()) {
        throw std::runtime_error("No sizes selected for export");
    }

    std::vector<ani2xcursor::CursorImage> decoded_images;
    std::vector<uint32_t> delays;
    decoded_images.reserve(size_indices.size());
    delays.reserve(size_indices.size());

    std::map<uint32_t, size_t> size_counts;

    for (size_t idx : size_indices) {
        const auto& img = images[idx];
        decoded_images.push_back(img);
        delays.push_back(0);
        size_counts[std::max(img.width, img.height)]++;
    }

    if (spdlog::get_level() <= spdlog::level::info) {
        spdlog::info("Exported {} sizes:", size_indices.size());
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
        
        auto mapping_dir = args.input_dir / "ani2xcursor";
        auto mapping_path = mapping_dir / "mapping.toml";
        bool mapping_present = fs::exists(mapping_path);

        if (args.manual_mapping) {
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

        std::optional<ani2xcursor::MappingLoadResult> manual_mapping;
        if (mapping_present) {
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
