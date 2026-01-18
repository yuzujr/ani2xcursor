#pragma once

#include "ico_cur_decoder.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ani2xcursor {

namespace fs = std::filesystem;

// Xcursor theme writer
class XcursorWriter {
public:
    // Write a single animated cursor file
    // frames: decoded cursor images with delays
    // output_path: path to output cursor file (without extension)
    static void write_cursor(const std::vector<CursorImage>& images,
                             const std::vector<uint32_t>& delays_ms,
                             const fs::path& output_path);
    
    // Write a single static cursor
    static void write_cursor(const CursorImage& image, const fs::path& output_path);
    
    // Write the index.theme file
    static void write_index_theme(const fs::path& theme_dir, 
                                   const std::string& theme_name);
    
    // Standard cursor name mappings (Windows role -> Xcursor name)
    // Returns the primary Xcursor name and a list of aliases
    struct CursorNames {
        std::string primary;
        std::vector<std::string> aliases;
    };
    
    [[nodiscard]] static CursorNames get_cursor_names(const std::string& win_role);
    
    // Create all aliases (symlinks) for a cursor
    static void create_aliases(const fs::path& cursors_dir,
                               const std::string& primary_name,
                               const std::vector<std::string>& aliases);

private:
    // Initialize cursor name mapping table
    static void init_cursor_mappings();
    
    static std::map<std::string, CursorNames> cursor_mappings_;
    static bool mappings_initialized_;
};

} // namespace ani2xcursor
