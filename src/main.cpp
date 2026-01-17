#include "inf_parser.h"
#include "ani_parser.h"
#include "ico_cur_decoder.h"
#include "xcursor_writer.h"
#include "theme_installer.h"
#include "utils/fs.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Command line arguments
struct Args {
    fs::path input_dir;
    fs::path output_dir;
    bool install = false;
    bool verbose = false;
    bool skip_broken = false;
    bool help = false;
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " <input_dir> [options]\n\n"
              << "Convert Windows Animated Cursors (.ani) to Linux Xcursor theme.\n\n"
              << "Arguments:\n"
              << "  <input_dir>       Directory containing Install.inf and .ani files\n\n"
              << "Options:\n"
              << "  --out <dir>       Output directory (default: ./out)\n"
              << "  --install         Install theme to $XDG_DATA_HOME/icons\n"
              << "  --verbose, -v     Enable verbose logging\n"
              << "  --skip-broken     Continue on conversion errors\n"
              << "  --help, -h        Show this help message\n\n"
              << "Example:\n"
              << "  " << program << " ~/Downloads/MyCursor --out ./themes --install\n";
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
        } else if (arg == "--install") {
            args.install = true;
        } else if (arg == "--skip-broken") {
            args.skip_broken = true;
        } else if (arg == "--out" && i + 1 < argc) {
            args.output_dir = argv[++i];
        } else if (!arg.starts_with("-") && args.input_dir.empty()) {
            args.input_dir = arg;
        } else {
            throw std::runtime_error(std::string("Unknown argument: ") + std::string(arg));
        }
    }
    
    return args;
}

// Process a single .ani file and return decoded frames
std::pair<std::vector<ani2xcursor::CursorImage>, std::vector<uint32_t>>
process_ani_file(const fs::path& ani_path) {
    spdlog::info("Processing: {}", ani_path.filename().string());
    
    // Parse ANI file
    auto animation = ani2xcursor::AniParser::parse(ani_path);
    
    std::vector<ani2xcursor::CursorImage> decoded_frames;
    std::vector<uint32_t> delays;
    
    // Decode each frame
    for (size_t step = 0; step < animation.num_steps; ++step) {
        const auto& frame = animation.get_step_frame(step);
        
        // Decode ICO/CUR data
        auto image = ani2xcursor::IcoCurDecoder::decode(frame.icon_data);
        
        // Use hotspot from decoded image
        spdlog::debug("Frame {}: {}x{}, hotspot ({}, {}), delay {}ms",
                      step, image.width, image.height,
                      image.hotspot_x, image.hotspot_y,
                      frame.delay_ms);
        
        decoded_frames.push_back(std::move(image));
        delays.push_back(frame.delay_ms);
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
        
        // Parse INF file
        spdlog::info("Parsing Install.inf...");
        auto inf_data = ani2xcursor::InfParser::parse(inf_path);
        
        spdlog::info("Theme: {}", inf_data.theme_name);
        spdlog::info("Found {} cursor mappings", inf_data.mappings.size());
        
        // Create output directory structure
        auto theme_dir = args.output_dir / inf_data.theme_name;
        auto cursors_dir = theme_dir / "cursors";
        
        fs::create_directories(cursors_dir);
        spdlog::info("Output directory: {}", theme_dir.string());
        
        // Process each cursor
        int success_count = 0;
        int error_count = 0;
        
        for (const auto& mapping : inf_data.mappings) {
            auto ani_path = args.input_dir / mapping.filename;
            
            if (!fs::exists(ani_path)) {
                spdlog::error("Cursor file not found: {}", ani_path.string());
                if (!args.skip_broken) {
                    return 1;
                }
                ++error_count;
                continue;
            }
            
            try {
                // Process ANI file
                auto [frames, delays] = process_ani_file(ani_path);
                
                // Get X11 cursor name and aliases
                auto names = ani2xcursor::XcursorWriter::get_cursor_names(mapping.role);
                
                // Write Xcursor file
                auto cursor_path = cursors_dir / names.primary;
                ani2xcursor::XcursorWriter::write_cursor(frames, delays, cursor_path);
                
                // Create aliases
                ani2xcursor::XcursorWriter::create_aliases(cursors_dir, names.primary, 
                                                            names.aliases);
                
                spdlog::info("Converted '{}' -> {} ({} aliases)", 
                            mapping.role, names.primary, names.aliases.size());
                ++success_count;
                
            } catch (const std::exception& e) {
                spdlog::error("Failed to convert {}: {}", mapping.filename, e.what());
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
            spdlog::info("Use --install to install to ~/.local/share/icons");
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return 1;
    }
}
