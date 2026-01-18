#include "cli.h"
#include "converter.h"
#include "inf_parser.h"
#include "manual_mapping.h"
#include "path_utils.h"
#include "preview_generator.h"
#include "size_tools.h"
#include "theme_installer.h"
#include "xcursor_writer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

void setup_logging(bool verbose) {
    auto logger = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(logger);
    spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);
    spdlog::set_pattern("[%^%l%$] %v");
}

std::optional<ani2xcursor::MappingLoadResult> handle_manual_mapping_request(
    const ani2xcursor::Args& args,
    const fs::path& mapping_path,
    const fs::path& mapping_dir,
    bool mapping_present) {
    if (!args.manual_mapping) {
        return std::nullopt;
    }

    if (mapping_present) {
        try {
            auto mapping = ani2xcursor::load_mapping_toml(mapping_path);
            for (const auto& warning : mapping.warnings) {
                spdlog::warn("mapping.toml: {}", warning);
            }
            spdlog::info("Manual mapping requested; using existing mapping.toml.");
            return mapping;
        } catch (const std::exception& e) {
            spdlog::warn("Manual mapping requested but mapping.toml failed to parse: {}",
                         e.what());
        }
    }

    spdlog::info("Manual mapping requested; generating previews and mapping.toml.");
    auto preview_dir = mapping_dir / "previews";
    auto preview_result = ani2xcursor::generate_previews(
        args.input_dir, preview_dir, args.size_filter, args.specific_sizes);
    ani2xcursor::write_mapping_toml_template(
        mapping_path, args.input_dir, preview_result.guesses);
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
    return std::nullopt;
}

std::string resolve_theme_name(const ani2xcursor::Args& args,
                               const ani2xcursor::MappingLoadResult& mapping) {
    std::error_code name_ec;
    auto abs_input = fs::absolute(args.input_dir, name_ec);
    fs::path name_source = name_ec ? args.input_dir : abs_input;
    if (!mapping.theme_name.empty()) {
        return mapping.theme_name;
    }
    if (!name_source.filename().string().empty()) {
        return name_source.filename().string();
    }
    if (!name_source.parent_path().filename().string().empty()) {
        return name_source.parent_path().filename().string();
    }
    return "cursor_theme";
}

bool build_mappings_from_manual(const ani2xcursor::Args& args,
                                const ani2xcursor::MappingLoadResult& mapping,
                                std::vector<ani2xcursor::CursorMapping>& out) {
    bool missing_required = false;
    for (const auto& role : ani2xcursor::known_roles()) {
        auto it = mapping.role_to_path.find(role);
        if (it == mapping.role_to_path.end() || it->second.empty()) {
            spdlog::warn("mapping.toml: role '{}' is not mapped", role);
            if (!ani2xcursor::is_optional_role(role) && !args.skip_broken) {
                missing_required = true;
            }
            continue;
        }
        out.push_back({role, it->second});
    }
    if (missing_required) {
        spdlog::error("Missing required roles in mapping.toml (use --skip-broken to continue)");
    }
    return !missing_required;
}

std::optional<fs::path> find_inf_path(const fs::path& input_dir) {
    auto inf_path = input_dir / "Install.inf";
    if (fs::exists(inf_path)) {
        return inf_path;
    }
    inf_path = input_dir / "install.inf";
    if (fs::exists(inf_path)) {
        return inf_path;
    }
    return std::nullopt;
}

int generate_mapping_for_missing_inf(const ani2xcursor::Args& args,
                                     const fs::path& mapping_path,
                                     const fs::path& mapping_dir) {
    spdlog::warn("Install.inf not found and mapping.toml not present.");
    auto preview_dir = mapping_dir / "previews";
    auto preview_result = ani2xcursor::generate_previews(
        args.input_dir, preview_dir, args.size_filter, args.specific_sizes);
    ani2xcursor::write_mapping_toml_template(
        mapping_path, args.input_dir, preview_result.guesses);
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

} // namespace

int main(int argc, char* argv[]) {
    try {
        auto args = ani2xcursor::parse_args(argc, argv);

        if (args.help) {
            ani2xcursor::print_usage(argv[0]);
            return 0;
        }

        if (args.input_dir.empty()) {
            std::cerr << "Error: input directory required\n\n";
            ani2xcursor::print_usage(argv[0]);
            return 1;
        }

        setup_logging(args.verbose);

        if (!fs::exists(args.input_dir)) {
            spdlog::error("Input directory does not exist: {}", args.input_dir.string());
            return 1;
        }

        if (args.list_sizes) {
            ani2xcursor::list_available_sizes(args.input_dir);
            return 0;
        }

        auto mapping_dir = args.input_dir / "ani2xcursor";
        auto mapping_path = mapping_dir / "mapping.toml";
        bool mapping_present = fs::exists(mapping_path);

        // Load manual mapping if requested or available
        std::optional<ani2xcursor::MappingLoadResult> loaded_mapping;
        if (args.manual_mapping) {
            loaded_mapping = handle_manual_mapping_request(
                args, mapping_path, mapping_dir, mapping_present);
            if (!loaded_mapping) {
                // generated manual mapping; exit now
                return 0;
            }
        } else if (mapping_present) {
            try {
                loaded_mapping = ani2xcursor::load_mapping_toml(mapping_path);
                for (const auto& warning : loaded_mapping->warnings) {
                    spdlog::warn("mapping.toml: {}", warning);
                }
            } catch (const std::exception& e) {
                spdlog::error("Failed to parse mapping.toml: {}", e.what());
                spdlog::warn("Falling back to Install.inf because mapping.toml could not be parsed");
            }
        }

        std::optional<ani2xcursor::InfResult> inf_data;
        std::string theme_name;
        std::vector<ani2xcursor::CursorMapping> mappings;

        // Determine mappings source
        if (loaded_mapping) {
            // Use manual mapping
            theme_name = resolve_theme_name(args, *loaded_mapping);
            if (!build_mappings_from_manual(args, *loaded_mapping, mappings)) {
                return 1;
            }
        } else {
            // Fallback to Install.inf
            auto inf_path = find_inf_path(args.input_dir);
            if (!inf_path) {
                if (mapping_present) {
                    spdlog::error("Install.inf not found and mapping.toml failed to parse");
                    return 1;
                }
                return generate_mapping_for_missing_inf(args, mapping_path, mapping_dir);
            }

            inf_data = ani2xcursor::InfParser::parse(*inf_path);
            theme_name = inf_data->theme_name;
            mappings = inf_data->mappings;
        }

        spdlog::info("Theme: {} ({} cursors)", theme_name, mappings.size());

        // Create output directory structure
        auto theme_dir = args.output_dir / theme_name;
        auto cursors_dir = theme_dir / "cursors";

        std::filesystem::create_directories(cursors_dir);

        // Process each cursor
        int success_count = 0;
        int error_count = 0;

        bool using_manual = loaded_mapping.has_value();

        for (const auto& mapping : mappings) {
            std::filesystem::path cursor_path;
            std::string display_name;

            // Determine cursor file path
            if (using_manual) {
                std::string rel = ani2xcursor::normalize_relative_path(mapping.value);
                cursor_path = args.input_dir / rel;
                display_name = rel;

                if (!std::filesystem::exists(cursor_path)) {
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
                auto ani_path_opt = ani2xcursor::find_file_icase(args.input_dir, filename);

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
                if (ani2xcursor::is_cur_file(cursor_path)) {
                    frames_delays = ani2xcursor::process_cur_file(
                        cursor_path, args.size_filter, args.specific_sizes);
                } else if (ani2xcursor::is_ani_file(cursor_path)) {
                    frames_delays = ani2xcursor::process_ani_file(
                        cursor_path, args.size_filter, args.specific_sizes);
                } else {
                    throw std::runtime_error("Unsupported cursor file type");
                }
                auto& frames = frames_delays.first;
                auto& delays = frames_delays.second;

                // Get X11 cursor name and aliases
                auto names = ani2xcursor::XcursorWriter::get_cursor_names(mapping.role);

                // Write Xcursor file
                auto output_cursor_path = cursors_dir / names.primary;
                ani2xcursor::XcursorWriter::write_cursor(frames, delays, output_cursor_path);

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
