#include "inf_parser.h"
#include "ani_parser.h"
#include "ico_cur_decoder.h"
#include "xcursor_writer.h"
#include "theme_installer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
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

enum class SizeFilter { All, Max, Specific };

struct Args {
    fs::path input_dir;
    fs::path output_dir;
    bool install = false;
    bool verbose = false;
    bool skip_broken = false;
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
    
    std::vector<size_t> size_indices_to_export;
    
    if (filter == SizeFilter::All) {
        for (size_t i = 0; i < num_sizes; ++i) {
            size_indices_to_export.push_back(i);
        }
    } else if (filter == SizeFilter::Max) {
        if (num_sizes > 0) {
            size_indices_to_export.push_back(0);
        }
    } else if (filter == SizeFilter::Specific) {
        for (uint32_t target_size : specific_sizes) {
            size_t best_idx = 0;
            uint32_t best_diff = UINT32_MAX;
            
            for (size_t idx = 0; idx < num_sizes && idx < frames_by_step[0].size(); ++idx) {
                const auto& img = frames_by_step[0][idx];
                uint32_t nominal_size = std::max(img.width, img.height);
                uint32_t diff = (nominal_size > target_size) ? 
                               (nominal_size - target_size) : (target_size - nominal_size);
                
                if (diff < best_diff) {
                    best_diff = diff;
                    best_idx = idx;
                }
            }
            
            if (std::find(size_indices_to_export.begin(), size_indices_to_export.end(), best_idx) 
                == size_indices_to_export.end()) {
                size_indices_to_export.push_back(best_idx);
            }
        }
    }
    
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
        
        auto inf_path = args.input_dir / "Install.inf";
        if (!fs::exists(inf_path)) {
            // Try lowercase
            inf_path = args.input_dir / "install.inf";
            if (!fs::exists(inf_path)) {
                spdlog::error("Install.inf not found in {}", args.input_dir.string());
                return 1;
            }
        }
        
        auto inf_data = ani2xcursor::InfParser::parse(inf_path);
        
        spdlog::info("Theme: {} ({} cursors)", inf_data.theme_name, inf_data.mappings.size());
        
        // Create output directory structure
        auto theme_dir = args.output_dir / inf_data.theme_name;
        auto cursors_dir = theme_dir / "cursors";
        
        fs::create_directories(cursors_dir);
        
        // Process each cursor
        int success_count = 0;
        int error_count = 0;
        
        for (const auto& mapping : inf_data.mappings) {
            // Extract just the filename from the (possibly full Windows) path
            auto filename = ani2xcursor::InfResult::extract_filename(mapping.value);
            
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
            
            auto ani_path = *ani_path_opt;
            
            try {
                // Process ANI file with size filter
                auto [frames, delays] = process_ani_file(ani_path, args.size_filter, args.specific_sizes);
                
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
                spdlog::error("Failed to convert {}: {}", filename, e.what());
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
        ani2xcursor::XcursorWriter::write_index_theme(theme_dir, inf_data.theme_name);
        
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
