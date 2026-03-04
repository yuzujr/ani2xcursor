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
    set_rundir("$(projectdir)")
    add_files("src/*.cpp")
    add_includedirs("include")
    add_packages("spdlog", "stb", "libxcursor")
    
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

