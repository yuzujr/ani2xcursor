// Comprehensive tests for refactored INF parser
// Tests cover: Scheme.Reg only, Wreg override, variable expansion, warnings
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "cli.h"
#include "inf_parser.h"
#include "path_utils.h"
#include "xcursor_writer.h"

// ============================================================================
// Test 1: Scheme.Reg only (no Wreg) - mapping by position
// ============================================================================
void test_scheme_reg_only() {
    std::cout << "Test: Scheme.Reg only (position-based mapping)...\n";

    const char* inf_content = R"(
[Version]
signature="$CHICAGO$"

[DefaultInstall]
CopyFiles = Scheme.Cur
AddReg    = Scheme.Reg

[Scheme.Reg]
HKCU,"Control Panel\Cursors\Schemes","%SCHEME_NAME%",,"%10%\%CUR_DIR%\%pointer%,%10%\%CUR_DIR%\%help%,%10%\%CUR_DIR%\%working%"

[Scheme.Cur]
Normal.ani
Help.ani
Working.ani

[Strings]
CUR_DIR      = "Cursors\TestTheme"
SCHEME_NAME  = "TestTheme"
pointer      = "Normal.ani"
help         = "Help.ani"
working      = "Working.ani"
)";

    auto result = ani2xcursor::InfParser::parse_string(inf_content);

    assert(result.theme_name == "TestTheme");

    // Should have 3 mappings from scheme slots
    assert(result.mappings.size() == 3);

    // Check expanded paths
    auto pointer = result.get_value("pointer");
    assert(pointer.has_value());
    assert(pointer->find("Normal.ani") != std::string::npos);

    auto help = result.get_value("help");
    assert(help.has_value());
    assert(help->find("Help.ani") != std::string::npos);

    auto working = result.get_value("working");
    assert(working.has_value());
    assert(working->find("Working.ani") != std::string::npos);

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 2: Wreg overrides Scheme.Reg
// ============================================================================
void test_wreg_override() {
    std::cout << "Test: Wreg overrides Scheme.Reg...\n";

    const char* inf_content = R"(
[Version]
signature="$CHICAGO$"

[DefaultInstall]
AddReg = Scheme.Reg, Wreg

[Scheme.Reg]
HKCU,"Control Panel\Cursors\Schemes","%SCHEME_NAME%",,"%10%\%CUR_DIR%\SchemePointer.ani,%10%\%CUR_DIR%\SchemeHelp.ani"

[Wreg]
HKCU,"Control Panel\Cursors",Arrow,0x00020000,"%10%\%CUR_DIR%\WregPointer.ani"

[Strings]
CUR_DIR      = "Cursors\Test"
SCHEME_NAME  = "TestOverride"
)";

    auto result = ani2xcursor::InfParser::parse_string(inf_content);

    // pointer should come from Wreg (higher priority)
    auto pointer = result.get_value("pointer");
    assert(pointer.has_value());
    assert(pointer->find("WregPointer.ani") != std::string::npos);

    // help should come from Scheme (no Wreg entry)
    auto help = result.get_value("help");
    assert(help.has_value());
    assert(help->find("SchemeHelp.ani") != std::string::npos);

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 3: Full Wreg parsing with all standard keys
// ============================================================================
void test_full_wreg() {
    std::cout << "Test: Full Wreg with all standard cursor keys...\n";

    const char* inf_content = R"(
[Version]
signature="$CHICAGO$"

[DefaultInstall]
AddReg = Wreg

[Wreg]
HKCU,"Control Panel\Cursors",,0x00020000,"%SCHEME_NAME%"
HKCU,"Control Panel\Cursors",Arrow,0x00020000,"%10%\%CUR_DIR%\Normal.ani"
HKCU,"Control Panel\Cursors",Help,0x00020000,"%10%\%CUR_DIR%\Help.ani"
HKCU,"Control Panel\Cursors",AppStarting,0x00020000,"%10%\%CUR_DIR%\Working.ani"
HKCU,"Control Panel\Cursors",Wait,0x00020000,"%10%\%CUR_DIR%\Busy.ani"
HKCU,"Control Panel\Cursors",Crosshair,0x00020000,"%10%\%CUR_DIR%\Precision.ani"
HKCU,"Control Panel\Cursors",IBeam,0x00020000,"%10%\%CUR_DIR%\Text.ani"
HKCU,"Control Panel\Cursors",NWPen,0x00020000,"%10%\%CUR_DIR%\Handwriting.ani"
HKCU,"Control Panel\Cursors",No,0x00020000,"%10%\%CUR_DIR%\Unavailable.ani"
HKCU,"Control Panel\Cursors",SizeNS,0x00020000,"%10%\%CUR_DIR%\Vertical.ani"
HKCU,"Control Panel\Cursors",SizeWE,0x00020000,"%10%\%CUR_DIR%\Horizontal.ani"
HKCU,"Control Panel\Cursors",SizeNWSE,0x00020000,"%10%\%CUR_DIR%\Diagonal1.ani"
HKCU,"Control Panel\Cursors",SizeNESW,0x00020000,"%10%\%CUR_DIR%\Diagonal2.ani"
HKCU,"Control Panel\Cursors",SizeAll,0x00020000,"%10%\%CUR_DIR%\Move.ani"
HKCU,"Control Panel\Cursors",UpArrow,0x00020000,"%10%\%CUR_DIR%\Alternate.ani"
HKCU,"Control Panel\Cursors",Hand,0x00020000,"%10%\%CUR_DIR%\Link.ani"
HKCU,"Control Panel\Cursors",Pin,0x00020000,"%10%\%CUR_DIR%\Pin.ani"
HKCU,"Control Panel\Cursors",Person,0x00020000,"%10%\%CUR_DIR%\Person.ani"

[Strings]
CUR_DIR      = "Cursors\FullWreg"
SCHEME_NAME  = "FullWregTheme"
)";

    auto result = ani2xcursor::InfParser::parse_string(inf_content);

    assert(result.theme_name == "FullWregTheme");

    // Check all standard mappings
    struct ExpectedMapping {
        const char* role;
        const char* expected_file;
    };

    ExpectedMapping expected[] = {
        {"pointer", "Normal.ani"},      {"help", "Help.ani"},
        {"working", "Working.ani"},     {"busy", "Busy.ani"},
        {"precision", "Precision.ani"}, {"text", "Text.ani"},
        {"hand", "Handwriting.ani"},    {"unavailable", "Unavailable.ani"},
        {"vert", "Vertical.ani"},       {"horz", "Horizontal.ani"},
        {"dgn1", "Diagonal1.ani"},      {"dgn2", "Diagonal2.ani"},
        {"move", "Move.ani"},           {"alternate", "Alternate.ani"},
        {"link", "Link.ani"},           {"pin", "Pin.ani"},
        {"person", "Person.ani"},
    };

    for (const auto& exp : expected) {
        auto val = result.get_value(exp.role);
        assert(val.has_value());
        assert(val->find(exp.expected_file) != std::string::npos);
    }

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 4: Variable expansion with nested references
// ============================================================================
void test_variable_expansion() {
    std::cout << "Test: Variable expansion with nested references...\n";

    const char* inf_content = R"(
[Version]
signature="$CHICAGO$"

[DefaultInstall]
AddReg = Wreg

[Wreg]
HKCU,"Control Panel\Cursors",Arrow,0x00020000,"%10%\%FULL_PATH%"

[Strings]
CUR_DIR      = "Cursors\Nested"
FILENAME     = "Normal.ani"
FULL_PATH    = "%CUR_DIR%\%FILENAME%"
SCHEME_NAME  = "NestedVars"
)";

    auto result = ani2xcursor::InfParser::parse_string(inf_content);

    auto pointer = result.get_value("pointer");
    assert(pointer.has_value());
    // Should be fully expanded to contain the actual filename
    assert(pointer->find("Normal.ani") != std::string::npos);
    assert(pointer->find("Cursors\\Nested") != std::string::npos);
    // %10% should be preserved
    assert(pointer->find("%10%") != std::string::npos);

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 5: Missing variable generates warning but doesn't fail
// ============================================================================
void test_missing_variable_warning() {
    std::cout << "Test: Missing variable generates warning...\n";

    const char* inf_content = R"(
[Version]
signature="$CHICAGO$"

[DefaultInstall]
AddReg = Wreg

[Wreg]
HKCU,"Control Panel\Cursors",Arrow,0x00020000,"%10%\%MISSING_VAR%\file.ani"

[Strings]
SCHEME_NAME  = "MissingVarTest"
)";

    auto result = ani2xcursor::InfParser::parse_string(inf_content);

    // Should have a warning about missing variable
    bool found_warning = false;
    for (const auto& w : result.warnings) {
        if (w.find("missing_var") != std::string::npos ||
            w.find("MISSING_VAR") != std::string::npos) {
            found_warning = true;
            break;
        }
    }
    assert(found_warning);

    // Path should still contain the unresolved variable
    auto pointer = result.get_value("pointer");
    assert(pointer.has_value());
    assert(pointer->find("%MISSING_VAR%") != std::string::npos);

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 6: Case-insensitive variable names
// ============================================================================
void test_case_insensitive_vars() {
    std::cout << "Test: Case-insensitive variable names...\n";

    const char* inf_content = R"(
[Version]
signature="$CHICAGO$"

[DefaultInstall]
AddReg = Wreg

[Wreg]
HKCU,"Control Panel\Cursors",Arrow,0x00020000,"%10%\%CUR_DIR%\%PoInTeR%"

[Strings]
cur_dir      = "Cursors\CaseTest"
POINTER      = "Normal.ani"
SCHEME_NAME  = "CaseInsensitive"
)";

    auto result = ani2xcursor::InfParser::parse_string(inf_content);

    auto pointer = result.get_value("pointer");
    assert(pointer.has_value());
    // Mixed case should still resolve
    assert(pointer->find("Normal.ani") != std::string::npos);
    assert(pointer->find("Cursors\\CaseTest") != std::string::npos);

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 7: RegLineParser edge cases
// ============================================================================
void test_reg_line_parser() {
    std::cout << "Test: RegLineParser edge cases...\n";

    // Standard line with flags
    auto entry1 = ani2xcursor::RegLineParser::parse(
        R"(HKCU,"Control Panel\Cursors",Arrow,0x00020000,"%10%\path\file.ani")");
    assert(entry1.valid);
    assert(entry1.root == "HKCU");
    assert(entry1.subkey == "Control Panel\\Cursors");
    assert(entry1.value_name == "Arrow");
    assert(entry1.flags == "0x00020000");
    assert(entry1.data == "%10%\\path\\file.ani");

    // Line with empty flags field
    auto entry2 = ani2xcursor::RegLineParser::parse(
        R"(HKCU,"Control Panel\Cursors\Schemes","%NAME%",,"data")");
    assert(entry2.valid);
    assert(entry2.value_name == "%NAME%");
    assert(entry2.flags.empty());
    assert(entry2.data == "data");

    // Line with default value (empty value_name)
    auto entry3 = ani2xcursor::RegLineParser::parse(
        R"(HKCU,"Control Panel\Cursors",,0x00020000,"%SCHEME_NAME%")");
    assert(entry3.valid);
    assert(entry3.value_name.empty());

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 8: Theme name from various sources
// ============================================================================
void test_theme_name_sources() {
    std::cout << "Test: Theme name from various sources...\n";

    // From [Strings] SCHEME_NAME
    const char* inf1 = R"(
[Strings]
SCHEME_NAME = "FromStrings"
)";
    auto result1 = ani2xcursor::InfParser::parse_string(inf1);
    assert(result1.theme_name == "FromStrings");

    // From Wreg default value when SCHEME_NAME missing
    const char* inf2 = R"(
[DefaultInstall]
AddReg = Wreg

[Wreg]
HKCU,"Control Panel\Cursors",,0x00020000,"FromWregDefault"

[Strings]
)";
    auto result2 = ani2xcursor::InfParser::parse_string(inf2);
    assert(result2.theme_name == "FromWregDefault");

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 9: CLI version flag parsing
// ============================================================================
void test_version_flag() {
    std::cout << "Test: CLI version flag parsing...\n";

    {
        char arg0[] = "ani2xcursor";
        char arg1[] = "--version";
        char* argv[] = {arg0, arg1};
        auto args = ani2xcursor::parse_args(2, argv);
        assert(args.version);
        assert(!args.help);
        assert(args.input_dir.empty());
    }

    {
        char arg0[] = "ani2xcursor";
        char arg1[] = "-V";
        char* argv[] = {arg0, arg1};
        auto args = ani2xcursor::parse_args(2, argv);
        assert(args.version);
        assert(!args.help);
        assert(args.input_dir.empty());
    }

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 10: Diagonal cursor aliases stay distinct
// ============================================================================
void test_xcursor_diagonal_aliases() {
    std::cout << "Test: Xcursor diagonal aliases stay distinct...\n";

    const auto dgn1 = ani2xcursor::XcursorWriter::get_cursor_names("dgn1");
    const auto dgn2 = ani2xcursor::XcursorWriter::get_cursor_names("dgn2");

    assert(dgn1.primary == "bd_double_arrow");
    assert(dgn2.primary == "fd_double_arrow");
    assert(std::find(dgn1.aliases.begin(), dgn1.aliases.end(), "fd_double_arrow") ==
           dgn1.aliases.end());
    assert(std::find(dgn1.aliases.begin(), dgn1.aliases.end(), "bd_double_arrow") !=
           dgn1.aliases.end());
    assert(std::find(dgn2.aliases.begin(), dgn2.aliases.end(), "fd_double_arrow") !=
           dgn2.aliases.end());

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 11: dgn1 aliases must not pre-create the dgn2 primary path
// ============================================================================
void test_xcursor_diagonal_primary_path_collision() {
    std::cout << "Test: Xcursor diagonal aliases do not shadow the second primary...\n";

    namespace fs = std::filesystem;

    const auto temp_dir = fs::temp_directory_path() / "ani2xcursor_test_diagonal_aliases";
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir);

    try {
        std::ofstream(temp_dir / "bd_double_arrow") << "dgn1";

        const auto dgn1 = ani2xcursor::XcursorWriter::get_cursor_names("dgn1");
        ani2xcursor::XcursorWriter::create_aliases(temp_dir, dgn1.primary, dgn1.aliases);

        const auto dgn2_primary = temp_dir / "fd_double_arrow";
        assert(!fs::exists(dgn2_primary));

        std::ofstream(dgn2_primary) << "dgn2";
        assert(fs::exists(dgn2_primary));
        assert(!fs::is_symlink(dgn2_primary));

        fs::remove_all(temp_dir);
    } catch (...) {
        fs::remove_all(temp_dir, ec);
        throw;
    }

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 12: find_inf_file supports localized filenames
// ============================================================================
void test_find_inf_file_localized_name() {
    std::cout << "Test: find_inf_file accepts localized .inf names...\n";

    namespace fs = std::filesystem;

    const auto temp_dir = fs::temp_directory_path() / "ani2xcursor_test_find_inf_localized";
    const auto localized_name = fs::path(std::u8string(u8"\u5b89\u88c5.inf"));

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir);

    try {
        std::ofstream(temp_dir / localized_name) << R"(
[DefaultInstall]
AddReg = Wreg

[Wreg]
HKCU,"Control Panel\Cursors",Arrow,0x00020000,"%10%\Theme\Arrow.ani"
)";

        const auto found = ani2xcursor::find_inf_file(temp_dir);
        assert(found.has_value());
        assert(found->filename() == localized_name);

        fs::remove_all(temp_dir, ec);
    } catch (...) {
        fs::remove_all(temp_dir, ec);
        throw;
    }

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 13: find_inf_file prefers installer-like INF when several exist
// ============================================================================
void test_find_inf_file_prefers_installer_like_candidate() {
    std::cout << "Test: find_inf_file prefers installer-like .inf candidates...\n";

    namespace fs = std::filesystem;

    const auto temp_dir = fs::temp_directory_path() / "ani2xcursor_test_find_inf_scoring";
    const auto installer_name = fs::path(std::u8string(u8"\u53f3\u952e\u5b89\u88c5.inf"));

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir);

    try {
        std::ofstream(temp_dir / "readme.inf") << "[Version]\nsignature=\"$CHICAGO$\"\n";
        std::ofstream(temp_dir / installer_name) << R"(
[DefaultInstall]
CopyFiles = Scheme.Cur
AddReg = Scheme.Reg, Wreg

[Wreg]
HKCU,"Control Panel\Cursors",Arrow,0x00020000,"%10%\Theme\Arrow.ani"
)";

        const auto found = ani2xcursor::find_inf_file(temp_dir);
        assert(found.has_value());
        assert(found->filename() == installer_name);

        fs::remove_all(temp_dir, ec);
    } catch (...) {
        fs::remove_all(temp_dir, ec);
        throw;
    }

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Test 14: find_inf_file still prefers Install.inf when present
// ============================================================================
void test_find_inf_file_prefers_install_inf() {
    std::cout << "Test: find_inf_file still prefers Install.inf...\n";

    namespace fs = std::filesystem;

    const auto temp_dir = fs::temp_directory_path() / "ani2xcursor_test_find_inf_install";
    const auto localized_name = fs::path(std::u8string(u8"\u53f3\u952e\u5b89\u88c5.inf"));

    std::error_code ec;
    fs::remove_all(temp_dir, ec);
    fs::create_directories(temp_dir);

    try {
        std::ofstream(temp_dir / "INSTALL.INF") << "[DefaultInstall]\nAddReg = Wreg\n";
        std::ofstream(temp_dir / localized_name) << "[DefaultInstall]\nAddReg = Wreg\n";

        const auto found = ani2xcursor::find_inf_file(temp_dir);
        assert(found.has_value());
        assert(found->filename() == fs::path("INSTALL.INF"));

        fs::remove_all(temp_dir, ec);
    } catch (...) {
        fs::remove_all(temp_dir, ec);
        throw;
    }

    std::cout << "  PASSED!\n";
}

// ============================================================================
// Main
// ============================================================================
int main() {
    spdlog::set_level(spdlog::level::warn);  // Reduce noise during tests

    std::cout << "\n=== INF Parser Tests ===\n\n";

    try {
        test_scheme_reg_only();
        test_wreg_override();
        test_full_wreg();
        test_variable_expansion();
        test_missing_variable_warning();
        test_case_insensitive_vars();
        test_reg_line_parser();
        test_theme_name_sources();
        test_version_flag();
        test_xcursor_diagonal_aliases();
        test_xcursor_diagonal_primary_path_collision();
        test_find_inf_file_localized_name();
        test_find_inf_file_prefers_installer_like_candidate();
        test_find_inf_file_prefers_install_inf();

        std::cout << "\n=== All tests passed! ===\n\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << "\n";
        return 1;
    }
}
