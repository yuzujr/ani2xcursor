#include "cli.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ani2xcursor {

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " <input_dir> [options]\n\n"
              << "Convert Windows Animated Cursors (.ani) to Linux Xcursor theme.\n\n"
              << "Arguments:\n"
              << "  <input_dir>       Directory containing Install.inf and .ani files\n\n"
              << "Options:\n"
              << "  --out, -o <dir>       Output directory (default: ./out)\n"
              << "  --format <mode>       Output format: xcursor (default) or source\n"
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
        } else if (arg == "--format" && i + 1 < argc) {
            std::string fmt = argv[++i];
            if (fmt == "xcursor") {
                args.format = OutputFormat::Xcursor;
            } else if (fmt == "source") {
                args.format = OutputFormat::Source;
            } else {
                throw std::runtime_error("Invalid format: " + fmt);
            }
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
                    std::string size_str = sizes_arg.substr(
                        pos, comma == std::string::npos ? std::string::npos : comma - pos);
                    try {
                        uint32_t size = std::stoul(size_str);
                        if (size > 0 && size <= 256) {
                            args.specific_sizes.push_back(size);
                        } else {
                            throw std::runtime_error("Size must be between 1 and 256");
                        }
                    } catch (const std::exception&) {
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

} // namespace ani2xcursor
