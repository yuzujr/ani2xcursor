#pragma once

#include "size_filter.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ani2xcursor {

enum class OutputFormat {
    Xcursor,
    Source,
};

struct Args {
    std::filesystem::path input_dir;
    std::filesystem::path output_dir;
    bool install = false;
    bool verbose = false;
    bool skip_broken = false;
    bool manual_mapping = false;
    bool list_sizes = false;
    bool help = false;
    SizeFilter size_filter = SizeFilter::All;
    std::vector<uint32_t> specific_sizes;
    OutputFormat format = OutputFormat::Xcursor;
};

void print_usage(const char* program);
Args parse_args(int argc, char* argv[]);

} // namespace ani2xcursor
