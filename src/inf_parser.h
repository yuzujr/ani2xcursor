#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ani2xcursor {

namespace fs = std::filesystem;

// ============================================================================
// Data Structures
// ============================================================================

// Cursor role to file path mapping
struct CursorMapping {
    std::string role;   // Internal role name (e.g., "pointer", "help", "working")
    std::string value;  // Expanded path/filename (may contain %10% for Windows dir)
};

// Parsed INF data with full installation intent
struct InfResult {
    std::string theme_name;                   // From SCHEME_NAME or reg writes
    std::string cursor_dir;                   // CUR_DIR expanded (relative path)
    std::vector<std::string> files_to_copy;   // From CopyFiles sections
    std::vector<CursorMapping> mappings;      // Final role->path mappings
    std::vector<std::string> warnings;        // Parse warnings (non-fatal issues)
    
    // Get filename/path for a role, returns nullopt if not found
    [[nodiscard]] std::optional<std::string> get_value(std::string_view role) const;
    
    // Compatibility alias for existing code
    [[nodiscard]] std::optional<std::string> get_filename(std::string_view role) const {
        return get_value(role);
    }
    
    // Extract just the filename from a full path value
    // Handles Windows paths with %10% prefixes, backslashes, etc.
    [[nodiscard]] static std::string extract_filename(std::string_view path);
};

// ============================================================================
// Role Mapping Tables (Table-driven, maintainable)
// ============================================================================

// Windows registry cursor value names -> internal role names
// Used by Wreg parsing (AddReg with individual cursor entries)
struct WinCursorKeyMapping {
    const char* win_key;    // Windows registry value name (case-insensitive)
    const char* role;       // Internal role name
};

// Standard Windows cursor registry keys to internal roles
// Reference: https://docs.microsoft.com/en-us/windows/win32/menurc/about-cursors
inline constexpr WinCursorKeyMapping kWinCursorKeyTable[] = {
    {"Arrow",       "pointer"},
    {"Help",        "help"},
    {"AppStarting", "working"},
    {"Wait",        "busy"},
    {"Crosshair",   "precision"},
    {"IBeam",       "text"},
    {"NWPen",       "hand"},        // Handwriting cursor
    {"No",          "unavailable"},
    {"SizeNS",      "vert"},        // Vertical resize
    {"SizeWE",      "horz"},        // Horizontal resize  
    {"SizeNWSE",    "dgn1"},        // Diagonal resize NW-SE
    {"SizeNESW",    "dgn2"},        // Diagonal resize NE-SW
    {"SizeAll",     "move"},        // Move/drag all directions
    {"UpArrow",     "alternate"},   // Alternate select
    {"Hand",        "link"},        // Hand/link cursor
    {"Person",      "person"},      // Person select (Windows 10+)
    {"Pin",         "pin"},         // Pin cursor (Windows 10+)
    // Additional aliases that some themes use
    {"precisionhair", "precision"}, // Alias for crosshair
};

// Scheme slot order - position in comma-separated scheme string
// This is the fixed Windows cursor scheme order
// Reference: HKCU\Control Panel\Cursors\Schemes value format
inline constexpr const char* kSchemeSlots[] = {
    "pointer",      // 0  - Arrow (Normal Select)
    "help",         // 1  - Help
    "working",      // 2  - AppStarting (Working in Background)
    "busy",         // 3  - Wait (Busy)
    "precision",    // 4  - Crosshair (Precision Select)
    "text",         // 5  - IBeam (Text Select)
    "hand",         // 6  - NWPen (Handwriting)
    "unavailable",  // 7  - No (Unavailable)
    "vert",         // 8  - SizeNS (Vertical Resize)
    "horz",         // 9  - SizeWE (Horizontal Resize)
    "dgn1",         // 10 - SizeNWSE (Diagonal Resize 1)
    "dgn2",         // 11 - SizeNESW (Diagonal Resize 2)
    "move",         // 12 - SizeAll (Move)
    "alternate",    // 13 - UpArrow (Alternate Select)
    "link",         // 14 - Hand (Link Select)
    "pin",          // 15 - Pin (Windows 10+)
    "person",       // 16 - Person (Windows 10+)
};

inline constexpr size_t kSchemeSlotCount = sizeof(kSchemeSlots) / sizeof(kSchemeSlots[0]);

// ============================================================================
// Parser Classes
// ============================================================================

// Parsed registry line data
struct RegEntry {
    std::string root;        // HKCU, HKLM, etc.
    std::string subkey;      // Registry path
    std::string value_name;  // Value name (may be empty for default)
    std::string flags;       // Optional flags (e.g., "0x00020000")
    std::string data;        // Value data
    bool valid = false;      // Whether parsing succeeded
};

// Parse a single registry line
class RegLineParser {
public:
    // Parse a registry entry line (comma-separated fields)
    // Format: ROOT,"SubKey","ValueName",Flags,"Data"
    // or: ROOT,"SubKey","ValueName",,"Data"
    [[nodiscard]] static RegEntry parse(std::string_view line);
    
private:
    // Parse a potentially quoted field from a comma-separated line
    // Returns the field value and advances pos past the field and comma
    [[nodiscard]] static std::string parse_field(std::string_view line, size_t& pos);
};

// Main INF parser
class InfParser {
public:
    // Parse INF file from path
    [[nodiscard]] static InfResult parse(const fs::path& path);
    
    // Parse INF content from string
    [[nodiscard]] static InfResult parse_string(std::string_view content);

private:
    InfParser() = default;
    
    // Main parsing entry point
    void parse_impl(std::string_view content);
    
    // Section parsing
    void parse_strings_section(std::string_view content);
    void parse_default_install_section(std::string_view content);
    void parse_copy_files_section(const std::string& section_name);
    void parse_add_reg_section(const std::string& section_name);
    
    // Registry entry processing
    void process_cursor_reg_entry(const RegEntry& entry);
    void process_scheme_reg_entry(const RegEntry& entry);
    
    // Variable expansion with nested %VAR% support
    [[nodiscard]] std::string expand_vars(std::string_view input) const;
    
    // Parse scheme string (comma-separated cursor paths)
    void parse_scheme_string(std::string_view scheme_data);
    
    // Helper: map Windows cursor key to internal role
    [[nodiscard]] static std::optional<std::string> win_key_to_role(std::string_view win_key);
    
    // Helper: extract key=value from a line
    [[nodiscard]] static std::optional<std::pair<std::string, std::string>> 
        parse_key_value(std::string_view line);
    
    // Add a mapping (handles priority: Wreg > Scheme)
    void add_mapping(const std::string& role, const std::string& value, bool high_priority);
    
    // Add a warning
    void add_warning(const std::string& msg);
    
    // Data members
    std::map<std::string, std::string, std::less<>> variables_;   // [Strings] variables
    std::map<std::string, std::string, std::less<>> sections_;    // Section name -> content
    std::map<std::string, std::string> role_mappings_;            // role -> expanded path
    std::map<std::string, bool> role_from_wreg_;                  // track if from Wreg (high priority)
    
    InfResult result_;
};

} // namespace ani2xcursor
