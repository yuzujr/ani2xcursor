-- ani2xcursor: Convert Windows Animated Cursors to Linux Xcursor themes
-- Build system: xmake

set_project("ani2xcursor")
set_version("1.0.0")

-- C++20 standard
set_languages("c++20")

-- Enable warnings
set_warnings("all", "extra")

-- Dependency source:
--   default  : xmake downloads pinned versions from the internet.
--   --nix=y  : use nix:: packages (for `nix develop` devShell).
option("nix")
    set_default(false)
    set_showmenu(true)
    set_description("Use nix:: packages (for nix develop shell)")
option_end()

-- Lock dependencies for reproducible builds (skip when nix:: packages are used)
if not has_config("nix") then
    set_policy("package.requires_lock", true)
end

-- Dependencies
if has_config("nix") then
    add_requires("nix::spdlog",        {alias = "spdlog"})
    add_requires("pkgconfig::fmt",     {alias = "fmt"})
    add_requires("pkgconfig::stb",     {alias = "stb"})
    add_requires("nix::libxcursor",   {alias = "libxcursor"})
else
    add_requires("spdlog v1.16.0", {configs = {fmt_external = false}})
    add_requires("stb 2025.03.14")
    add_requires("libxcursor 1.2.3")
end

-- Main target
target("ani2xcursor")
    set_kind("binary")
    set_rundir("$(projectdir)")
    add_files("src/*.cpp")
    add_includedirs("include")
    add_packages("spdlog", "stb", "libxcursor")
    if has_config("nix") then
        add_packages("fmt")
    end

    if is_plat("linux") then
        add_syslinks("pthread")

        -- Compile translation files for local development
        after_build(function (target)
            import("lib.detect.find_tool")
            local msgfmt = find_tool("msgfmt")
            if msgfmt then
                for _, po in ipairs(os.files("locale/*.po")) do
                    local lang = path.basename(po)
                    local mo_dst = path.join(os.projectdir(), "build", "locale", lang, "LC_MESSAGES", "ani2xcursor.mo")
                    os.mkdir(path.directory(mo_dst))
                    os.vrunv(msgfmt.program, {"-o", mo_dst, po})
                end
                print("Compiled translations to build/locale")
            else
                print("Warning: msgfmt not found! Translations not compiled.")
            end
        end)

        -- Compile and install translation files
        after_install(function (target)
            import("lib.detect.find_tool")
            local msgfmt = find_tool("msgfmt")
            if msgfmt then
                for _, po in ipairs(os.files("locale/*.po")) do
                    local lang = path.basename(po)
                    local mo_dst = path.join(target:installdir(), "share", "locale", lang, "LC_MESSAGES", "ani2xcursor.mo")
                    os.mkdir(path.directory(mo_dst))
                    os.vrunv(msgfmt.program, {"-o", mo_dst, po})
                    print("Installed translation: " .. mo_dst)
                end
            else
                print("Warning: msgfmt not found! Translations were not installed.")
            end
        end)
    end

-- Test target
target("ani2xcursor_test")
    set_kind("binary")
    set_default(false)
    
    add_files("tests/test_inf_parser.cpp")
    add_files("src/inf_parser.cpp")
    add_files("src/riff_reader.cpp")
    add_files("src/ani_parser.cpp")
    add_files("src/ico_cur_decoder.cpp")
    
    add_includedirs("include")
    add_packages("spdlog", "stb")
    if has_config("nix") then
        add_packages("fmt")
    end
