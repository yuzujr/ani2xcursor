#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ani2xcursor {

// Decoded cursor image
struct CursorImage {
    std::vector<uint8_t> pixels;  // RGBA32 format (width * height * 4 bytes)
    uint32_t width;
    uint32_t height;
    uint16_t hotspot_x;
    uint16_t hotspot_y;
};

// ICO/CUR container decoder
// Handles both PNG and BMP/DIB payloads
class IcoCurDecoder {
public:
    // Decode a single image from ICO/CUR data
    // Takes the "best" image (largest, highest bit depth)
    [[nodiscard]] static CursorImage decode(std::span<const uint8_t> data);
    
    // Decode all images from ICO/CUR data
    [[nodiscard]] static std::vector<CursorImage> decode_all(std::span<const uint8_t> data);

private:
    // ICO/CUR file header
    struct FileHeader {
        uint16_t reserved;    // Must be 0
        uint16_t type;        // 1 = ICO, 2 = CUR
        uint16_t count;       // Number of images
    };
    
    // ICO/CUR directory entry
    struct DirEntry {
        uint8_t width;        // 0 = 256
        uint8_t height;       // 0 = 256
        uint8_t color_count;  // 0 if >= 256 colors
        uint8_t reserved;
        uint16_t planes_or_hotspot_x;   // ICO: color planes, CUR: hotspot X
        uint16_t bpp_or_hotspot_y;      // ICO: bits per pixel, CUR: hotspot Y
        uint32_t size;        // Size of image data
        uint32_t offset;      // Offset to image data
    };
    
    // Parse file header
    static FileHeader parse_header(std::span<const uint8_t> data);
    
    // Parse directory entry
    static DirEntry parse_dir_entry(std::span<const uint8_t> data, size_t index);
    
    // Check if image data is PNG
    static bool is_png(std::span<const uint8_t> data);
    
    // Decode PNG image using stb_image
    static CursorImage decode_png(std::span<const uint8_t> data, 
                                   uint16_t hotspot_x, uint16_t hotspot_y);
    
    // Decode BMP/DIB image
    static CursorImage decode_bmp(std::span<const uint8_t> data,
                                   const DirEntry& entry,
                                   bool is_cursor);
    
    // Select best image from multiple entries
    static size_t select_best_image(const std::vector<DirEntry>& entries);
};

} // namespace ani2xcursor
