// Basic test for INF parser
#include "../src/inf_parser.h"

#include <cassert>
#include <iostream>
#include <spdlog/spdlog.h>

void test_inf_parser() {
    std::cout << "Testing INF parser...\n";
    
    // Test basic INF parsing
    const char* test_inf = R"(
[Version]
signature="$CHICAGO$"

[Strings]
CUR_DIR      = "Cursors\test_cursor"
SCHEME_NAME  = "TestCursor"
pointer      = "Normal.ani"
help         = "Help.ani"
busy         = "Busy.ani"
)";
    
    auto result = ani2xcursor::InfParser::parse_string(test_inf);
    
    assert(result.theme_name == "TestCursor");
    assert(result.cursor_dir == "Cursors\\test_cursor");
    assert(result.mappings.size() == 3);
    
    auto pointer = result.get_filename("pointer");
    assert(pointer.has_value());
    assert(*pointer == "Normal.ani");
    
    auto help = result.get_filename("help");
    assert(help.has_value());
    assert(*help == "Help.ani");
    
    auto missing = result.get_filename("nonexistent");
    assert(!missing.has_value());
    
    std::cout << "INF parser tests passed!\n";
}

int main() {
    spdlog::set_level(spdlog::level::debug);
    
    try {
        test_inf_parser();
        std::cout << "\nAll tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
