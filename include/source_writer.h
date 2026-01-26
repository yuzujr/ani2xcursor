#pragma once

#include "ico_cur_decoder.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ani2xcursor {

namespace fs = std::filesystem;

struct CursorListEntry {
    std::string alias;
    std::string target;
};

class SourceWriter {
public:
    static void write_cursor(const fs::path& src_dir,
                             const std::string& primary_name,
                             const std::vector<CursorImage>& frames,
                             const std::vector<uint32_t>& delays_ms);

    static void write_cursor_list(const fs::path& src_dir,
                                  const std::vector<CursorListEntry>& entries);
};

} // namespace ani2xcursor
