#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ani2xcursor::utils {

namespace fs = std::filesystem;

// Read entire file into memory
inline std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    
    auto size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("Failed to get file size: " + path.string());
    }
    
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Failed to read file: " + path.string());
    }
    
    return data;
}

// Read file as string
inline std::string read_file_string(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

// Write binary data to file
inline void write_file(const fs::path& path, std::span<const uint8_t> data) {
    // Ensure parent directory exists
    if (auto parent = path.parent_path(); !parent.empty()) {
        fs::create_directories(parent);
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to create file: " + path.string());
    }
    
    if (!data.empty() && !file.write(reinterpret_cast<const char*>(data.data()), 
                                      static_cast<std::streamsize>(data.size()))) {
        throw std::runtime_error("Failed to write file: " + path.string());
    }
}

// Write string to file
inline void write_file_string(const fs::path& path, std::string_view content) {
    // Ensure parent directory exists
    if (auto parent = path.parent_path(); !parent.empty()) {
        fs::create_directories(parent);
    }
    
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to create file: " + path.string());
    }
    
    if (!file.write(content.data(), static_cast<std::streamsize>(content.size()))) {
        throw std::runtime_error("Failed to write file: " + path.string());
    }
}

// Get XDG_DATA_HOME path
inline fs::path get_xdg_data_home() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return fs::path(xdg);
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return fs::path(home) / ".local" / "share";
    }
    throw std::runtime_error("Cannot determine XDG_DATA_HOME: neither XDG_DATA_HOME nor HOME is set");
}

} // namespace ani2xcursor::utils
