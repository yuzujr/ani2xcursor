#include <libintl.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <clocale>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "cli.h"
#include "converter.h"
#include "inf_parser.h"
#include "manifest.h"
#include "path_utils.h"
#include "preview_generator.h"
#include "size_tools.h"
#include "source_writer.h"
#include "spdlog/fmt/bundled/base.h"
#include "theme_installer.h"
#include "xcursor_writer.h"

#ifndef _
#define _(String) String
#endif

namespace {

namespace fs = std::filesystem;

void setup_logging(bool verbose) {
    auto logger = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(logger);
    spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);
    spdlog::set_pattern("[%^%l%$] %v");
}

std::optional<ani2xcursor::ManifestLoadResult> handle_manifest_request(
    const ani2xcursor::Args& args, const fs::path& manifest_path, const fs::path& manifest_dir,
    bool manifest_present) {
    if (!args.manifest) {
        return std::nullopt;
    }

    if (manifest_present) {
        std::string label = manifest_path.filename().string();
        try {
            auto manifest = ani2xcursor::load_manifest_toml(manifest_path);
            for (const auto& warning : manifest.warnings) {
                spdlog::warn(spdlog::fmt_lib::runtime(_("{}: {}")), label, warning);
            }
            spdlog::info(spdlog::fmt_lib::runtime(_("Manifest requested; using existing {}.")),
                         label);
            return manifest;
        } catch (const std::exception& e) {
            spdlog::warn(
                spdlog::fmt_lib::runtime(_("Manifest requested but {} failed to parse: {}")), label,
                e.what());
        }
    }

    spdlog::info(_("Manifest requested; generating previews and manifest.toml."));
    auto preview_dir = manifest_dir / "previews";
    auto preview_result = ani2xcursor::generate_previews(args.input_dir, preview_dir,
                                                         args.size_filter, args.specific_sizes);
    ani2xcursor::write_manifest_toml_template(manifest_path, args.input_dir,
                                              preview_result.guesses);
    std::error_code abs_ec;
    auto abs_manifest = fs::absolute(manifest_path, abs_ec);
    auto abs_previews = fs::absolute(preview_dir, abs_ec);
    if (abs_ec) {
        abs_manifest = manifest_path;
        abs_previews = preview_dir;
    }
    spdlog::info(spdlog::fmt_lib::runtime(_("Generated: {} and {}")), abs_manifest.string(),
                 (abs_previews / "*.png").string());
    spdlog::info(_("Edit manifest.toml and re-run the command."));
    return std::nullopt;
}

std::string resolve_theme_name(const ani2xcursor::Args& args,
                               const ani2xcursor::ManifestLoadResult& manifest) {
    std::error_code name_ec;
    auto abs_input = fs::weakly_canonical(args.input_dir, name_ec);
    fs::path name_source = name_ec ? args.input_dir : abs_input;
    if (!manifest.theme_name.empty()) {
        return manifest.theme_name;
    }
    auto filename = name_source.filename().string();
    if (!filename.empty() && filename != "." && filename != "..") {
        return filename;
    }
    auto parent_name = name_source.parent_path().filename().string();
    if (!parent_name.empty() && parent_name != "." && parent_name != "..") {
        return parent_name;
    }
    return "cursor_theme";
}

bool build_mappings_from_manifest(const ani2xcursor::Args& args,
                                  const ani2xcursor::ManifestLoadResult& manifest,
                                  std::vector<ani2xcursor::CursorMapping>& out) {
    bool missing_required = false;
    for (const auto& role : ani2xcursor::known_roles()) {
        auto it = manifest.role_to_path.find(role);
        if (it == manifest.role_to_path.end() || it->second.empty()) {
            spdlog::warn(spdlog::fmt_lib::runtime(_("manifest.toml: role '{}' is not mapped")),
                         role);
            if (!ani2xcursor::is_optional_role(role) && !args.skip_broken) {
                missing_required = true;
            }
            continue;
        }
        out.push_back({role, it->second});
    }
    if (missing_required) {
        spdlog::error(_("Missing required roles in manifest.toml (use --skip-broken to continue)"));
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

int generate_manifest_for_missing_inf(const ani2xcursor::Args& args, const fs::path& manifest_path,
                                      const fs::path& manifest_dir) {
    spdlog::warn(_("Install.inf not found and manifest.toml not present."));
    auto preview_dir = manifest_dir / "previews";
    auto preview_result = ani2xcursor::generate_previews(args.input_dir, preview_dir,
                                                         args.size_filter, args.specific_sizes);
    ani2xcursor::write_manifest_toml_template(manifest_path, args.input_dir,
                                              preview_result.guesses);
    std::error_code abs_ec;
    auto abs_manifest = fs::absolute(manifest_path, abs_ec);
    auto abs_previews = fs::absolute(preview_dir, abs_ec);
    if (abs_ec) {
        abs_manifest = manifest_path;
        abs_previews = preview_dir;
    }
    spdlog::warn(spdlog::fmt_lib::runtime(_("Generated: {} and {}")), abs_manifest.string(),
                 (abs_previews / "*.png").string());
    spdlog::warn(spdlog::fmt_lib::runtime(_("Edit manifest.toml and re-run the command.")));
    return 2;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Initialize localization
    std::string locale_dir;
    if (std::filesystem::exists("build/locale")) {
        locale_dir = "build/locale";
    } else {
#ifdef ANI2XCURSOR_LOCALEDIR
        locale_dir = ANI2XCURSOR_LOCALEDIR;
#else
        locale_dir = "/usr/share/locale";
#endif
    }

    std::setlocale(LC_ALL, "");
    bindtextdomain("ani2xcursor", locale_dir.c_str());
    textdomain("ani2xcursor");

    try {
        auto args = ani2xcursor::parse_args(argc, argv);

        if (args.help) {
            ani2xcursor::print_usage(argv[0]);
            return 0;
        }

        if (args.input_dir.empty()) {
            std::cerr << _("Error: input directory required\n\n");
            ani2xcursor::print_usage(argv[0]);
            return 1;
        }

        setup_logging(args.verbose);

        if (!fs::exists(args.input_dir)) {
            spdlog::error(spdlog::fmt_lib::runtime(_("Input directory does not exist: {}")),
                          args.input_dir.string());
            return 1;
        }

        if (args.list_sizes) {
            ani2xcursor::list_available_sizes(args.input_dir);
            return 0;
        }

        auto manifest_dir = args.input_dir / "ani2xcursor";
        auto manifest_path = manifest_dir / "manifest.toml";
        bool manifest_present = fs::exists(manifest_path);

        // Load manifest if requested or available
        std::optional<ani2xcursor::ManifestLoadResult> loaded_manifest;
        std::optional<std::string> manifest_failed_label;
        if (args.manifest) {
            loaded_manifest =
                handle_manifest_request(args, manifest_path, manifest_dir, manifest_present);
            if (!loaded_manifest) {
                // generated manifest; exit now
                return 0;
            }
        } else if (manifest_present) {
            std::string label = manifest_path.filename().string();
            try {
                loaded_manifest = ani2xcursor::load_manifest_toml(manifest_path);
                for (const auto& warning : loaded_manifest->warnings) {
                    spdlog::warn(spdlog::fmt_lib::runtime(_("{}: {}")), label, warning);
                }
            } catch (const std::exception& e) {
                spdlog::error(spdlog::fmt_lib::runtime(_("Failed to parse {}: {}")), label,
                              e.what());
                spdlog::warn(spdlog::fmt_lib::runtime(
                                 _("Falling back to Install.inf because {} could not be parsed")),
                             label);
                manifest_failed_label = label;
            }
        }

        std::optional<ani2xcursor::InfResult> inf_data;
        std::string theme_name;
        std::vector<ani2xcursor::CursorMapping> mappings;

        // Determine mappings source
        if (loaded_manifest) {
            // Use manifest
            theme_name = resolve_theme_name(args, *loaded_manifest);
            if (!build_mappings_from_manifest(args, *loaded_manifest, mappings)) {
                return 1;
            }
        } else {
            // Fallback to Install.inf
            auto inf_path = find_inf_path(args.input_dir);
            if (!inf_path) {
                if (manifest_failed_label) {
                    spdlog::error(
                        spdlog::fmt_lib::runtime(_("Install.inf not found and {} failed to parse")),
                        *manifest_failed_label);
                    return 1;
                }
                return generate_manifest_for_missing_inf(args, manifest_path, manifest_dir);
            }

            inf_data = ani2xcursor::InfParser::parse(*inf_path);
            theme_name = inf_data->theme_name;
            mappings = inf_data->mappings;
        }

        spdlog::info(spdlog::fmt_lib::runtime(_("Theme: {} ({} cursors)")), theme_name,
                     mappings.size());

        // Create output directory structure
        auto theme_dir = args.output_dir / theme_name;
        auto src_dir = theme_dir / "src";
        auto xcursor_dir = theme_dir / "xcursor";
        auto cursors_dir = xcursor_dir / "cursors";

        if (args.format == ani2xcursor::OutputFormat::Xcursor) {
            std::filesystem::create_directories(cursors_dir);
        } else {
            std::filesystem::create_directories(src_dir);
        }

        // Process each cursor
        int success_count = 0;
        int error_count = 0;

        bool using_manifest = loaded_manifest.has_value();
        std::vector<ani2xcursor::CursorListEntry> cursor_list_entries;
        std::unordered_set<std::string> cursor_list_seen;

        for (const auto& mapping : mappings) {
            std::filesystem::path cursor_path;
            std::string display_name;
            auto size_filter = args.size_filter;
            std::vector<uint32_t> specific_sizes = args.specific_sizes;

            // Determine cursor file path
            if (using_manifest) {
                std::string rel = ani2xcursor::normalize_relative_path(mapping.value);
                cursor_path = args.input_dir / rel;
                display_name = rel;

                size_filter = SizeFilter::All;
                specific_sizes.clear();
                if (loaded_manifest) {
                    auto it = loaded_manifest->role_to_sizes.find(mapping.role);
                    if (it != loaded_manifest->role_to_sizes.end() && !it->second.empty()) {
                        size_filter = SizeFilter::Specific;
                        specific_sizes = it->second;
                    }
                }

                if (!std::filesystem::exists(cursor_path)) {
                    spdlog::error(spdlog::fmt_lib::runtime(_("Cursor file not found: {}")),
                                  display_name);
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
                    spdlog::error(spdlog::fmt_lib::runtime(_("Cursor file not found: {}")),
                                  filename);
                    if (!args.skip_broken) {
                        return 1;
                    }
                    ++error_count;
                    continue;
                }

                cursor_path = *ani_path_opt;
            }

            try {
                std::pair<std::vector<ani2xcursor::CursorImage>, std::vector<uint32_t>>
                    frames_delays;
                if (ani2xcursor::is_cur_file(cursor_path)) {
                    frames_delays =
                        ani2xcursor::process_cur_file(cursor_path, size_filter, specific_sizes);
                } else if (ani2xcursor::is_ani_file(cursor_path)) {
                    frames_delays =
                        ani2xcursor::process_ani_file(cursor_path, size_filter, specific_sizes);
                } else {
                    throw std::runtime_error(_("Unsupported cursor file type"));
                }
                auto& frames = frames_delays.first;
                auto& delays = frames_delays.second;

                // Get X11 cursor name and aliases
                auto names = ani2xcursor::XcursorWriter::get_cursor_names(mapping.role);

                if (args.format == ani2xcursor::OutputFormat::Xcursor) {
                    // Write Xcursor file
                    auto output_cursor_path = cursors_dir / names.primary;
                    ani2xcursor::XcursorWriter::write_cursor(frames, delays, output_cursor_path);

                    // Create aliases
                    ani2xcursor::XcursorWriter::create_aliases(cursors_dir, names.primary,
                                                               names.aliases);
                } else {
                    ani2xcursor::SourceWriter::write_cursor(src_dir, names.primary, frames, delays);

                    for (const auto& alias : names.aliases) {
                        if (alias == names.primary) {
                            continue;
                        }
                        if (cursor_list_seen.insert(alias).second) {
                            cursor_list_entries.push_back({alias, names.primary});
                        }
                    }
                }

                spdlog::debug("Converted '{}' -> {}", mapping.role, names.primary);
                ++success_count;

            } catch (const std::exception& e) {
                spdlog::error(spdlog::fmt_lib::runtime(_("Failed to convert {}: {}")), display_name,
                              e.what());
                if (!args.skip_broken) {
                    return 1;
                }
                ++error_count;
            }
        }

        if (success_count == 0) {
            spdlog::error(_("No cursors were converted successfully"));
            return 1;
        }

        if (args.format == ani2xcursor::OutputFormat::Xcursor) {
            // Write index.theme
            ani2xcursor::XcursorWriter::write_index_theme(xcursor_dir, theme_name);
        } else {
            ani2xcursor::SourceWriter::write_cursor_list(src_dir, cursor_list_entries);
        }

        spdlog::info(
            spdlog::fmt_lib::runtime(_("Conversion complete: {} cursors converted, {} errors")),
            success_count, error_count);

        // Install if requested
        if (args.install) {
            if (args.format == ani2xcursor::OutputFormat::Xcursor) {
                ani2xcursor::ThemeInstaller::install(xcursor_dir, theme_name);
            } else {
                spdlog::warn(_("--install ignored for source output format"));
            }
        } else {
            if (args.format == ani2xcursor::OutputFormat::Xcursor) {
                spdlog::info(spdlog::fmt_lib::runtime(_("Theme created at: {}")),
                             xcursor_dir.string());
            } else {
                spdlog::info(spdlog::fmt_lib::runtime(_("Source files created at: {}")),
                             src_dir.string());
            }
        }

        return 0;
    } catch (const std::exception& e) {
        spdlog::error(spdlog::fmt_lib::runtime(_("Error: {}")), e.what());
        return 1;
    }
}
