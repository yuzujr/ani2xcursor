-- ani2xcursor: Convert Windows Animated Cursors to Linux Xcursor themes
-- Build system: xmake

set_project("ani2xcursor")
set_version("1.0.0")

-- C++20 standard
set_languages("c++20")

-- Enable warnings
set_warnings("all", "extra")

-- Lock dependencies for reproducible builds
set_policy("package.requires_lock", true)

-- Dependencies
add_requires("spdlog v1.16.0", {configs = {fmt_external = false}})
add_requires("stb 2025.03.14")
add_requires("libxcursor 1.2.3")

-- Main target
target("ani2xcursor")
    set_kind("binary")
    
    -- Source files
    add_files("src/*.cpp")
    
    -- Header paths
    add_includedirs("src")
    
    -- Link dependencies
    add_packages("spdlog", "stb", "libxcursor")
    
    -- Linux specific
    if is_plat("linux") then
        add_syslinks("pthread")
    end

-- Optional: test target
target("ani2xcursor_test")
    set_kind("binary")
    set_default(false)
    
    add_files("tests/test_inf_parser.cpp")
    add_files("src/inf_parser.cpp")
    add_files("src/riff_reader.cpp")
    add_files("src/ani_parser.cpp")
    add_files("src/ico_cur_decoder.cpp")
    
    add_includedirs("src")
    add_packages("spdlog", "stb")
