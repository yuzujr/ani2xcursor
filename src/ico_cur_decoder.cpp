#include "ico_cur_decoder.h"
#include "utils/bytes.h"

#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ani2xcursor {

CursorImage IcoCurDecoder::decode(std::span<const uint8_t> data) {
    auto images = decode_all(data);
    if (images.empty()) {
        throw std::runtime_error("ICO/CUR: No images found");
    }
    
    // Return the first (best) image
    return std::move(images[0]);
}

std::vector<CursorImage> IcoCurDecoder::decode_all(std::span<const uint8_t> data) {
    if (data.size() < 6) {
        throw std::runtime_error("ICO/CUR: File too small");
    }
    
    auto header = parse_header(data);
    
    if (header.reserved != 0) {
        throw std::runtime_error("ICO/CUR: Invalid header (reserved != 0)");
    }
    
    if (header.type != 1 && header.type != 2) {
        throw std::runtime_error("ICO/CUR: Invalid type (expected 1=ICO or 2=CUR)");
    }
    
    bool is_cursor = (header.type == 2);
    spdlog::debug("ICO/CUR: Type={}, {} images", is_cursor ? "CUR" : "ICO", header.count);
    
    if (header.count == 0) {
        throw std::runtime_error("ICO/CUR: No images in file");
    }
    
    // Parse directory entries
    std::vector<DirEntry> entries;
    entries.reserve(header.count);
    
    size_t min_file_size = 6 + header.count * 16;
    if (data.size() < min_file_size) {
        throw std::runtime_error("ICO/CUR: File too small for directory");
    }
    
    for (uint16_t i = 0; i < header.count; ++i) {
        entries.push_back(parse_dir_entry(data, i));
    }
    
    // Sort by quality (largest, highest bpp first) and decode
    size_t best = select_best_image(entries);
    
    std::vector<CursorImage> results;
    
    // Decode best image first
    const auto& entry = entries[best];
    
    spdlog::debug("ICO/CUR: Best image #{}: {}x{}, offset={}, size={}",
                  best, entry.width == 0 ? 256 : entry.width,
                  entry.height == 0 ? 256 : entry.height,
                  entry.offset, entry.size);
    
    if (entry.offset + entry.size > data.size()) {
        throw std::runtime_error("ICO/CUR: Image data extends beyond file");
    }
    
    auto img_data = data.subspan(entry.offset, entry.size);
    
    if (is_png(img_data)) {
        spdlog::debug("ICO/CUR: Image is PNG format");
        uint16_t hx = is_cursor ? entry.planes_or_hotspot_x : 0;
        uint16_t hy = is_cursor ? entry.bpp_or_hotspot_y : 0;
        results.push_back(decode_png(img_data, hx, hy));
    } else {
        spdlog::debug("ICO/CUR: Image is BMP/DIB format");
        results.push_back(decode_bmp(img_data, entry, is_cursor));
    }
    
    return results;
}

IcoCurDecoder::FileHeader IcoCurDecoder::parse_header(std::span<const uint8_t> data) {
    utils::ByteReader reader(data);
    FileHeader h;
    h.reserved = reader.read_u16_le();
    h.type = reader.read_u16_le();
    h.count = reader.read_u16_le();
    return h;
}

IcoCurDecoder::DirEntry IcoCurDecoder::parse_dir_entry(std::span<const uint8_t> data, 
                                                        size_t index) {
    size_t offset = 6 + index * 16;
    utils::ByteReader reader(data);
    reader.seek(offset);
    
    DirEntry e;
    e.width = reader.read_u8();
    e.height = reader.read_u8();
    e.color_count = reader.read_u8();
    e.reserved = reader.read_u8();
    e.planes_or_hotspot_x = reader.read_u16_le();
    e.bpp_or_hotspot_y = reader.read_u16_le();
    e.size = reader.read_u32_le();
    e.offset = reader.read_u32_le();
    
    return e;
}

bool IcoCurDecoder::is_png(std::span<const uint8_t> data) {
    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    static const uint8_t png_sig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return data.size() >= 8 && std::memcmp(data.data(), png_sig, 8) == 0;
}

CursorImage IcoCurDecoder::decode_png(std::span<const uint8_t> data,
                                       uint16_t hotspot_x, uint16_t hotspot_y) {
    int w, h, channels;
    uint8_t* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()),
                                            &w, &h, &channels, 4);
    
    if (!pixels) {
        throw std::runtime_error(std::string("ICO/CUR: Failed to decode PNG: ") + 
                                 stbi_failure_reason());
    }
    
    CursorImage img;
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.hotspot_x = hotspot_x;
    img.hotspot_y = hotspot_y;
    
    size_t pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    img.pixels.assign(pixels, pixels + pixel_count);
    
    stbi_image_free(pixels);
    
    spdlog::debug("ICO/CUR: Decoded PNG {}x{}, hotspot ({}, {})",
                  img.width, img.height, img.hotspot_x, img.hotspot_y);
    
    return img;
}

CursorImage IcoCurDecoder::decode_bmp(std::span<const uint8_t> data,
                                       const DirEntry& entry,
                                       bool is_cursor) {
    // BMP/DIB in ICO format starts with BITMAPINFOHEADER (40 bytes)
    // without the standard BMP file header
    
    if (data.size() < 40) {
        throw std::runtime_error("ICO/CUR: BMP data too small");
    }
    
    utils::ByteReader reader(data);
    
    // BITMAPINFOHEADER
    uint32_t header_size = reader.read_u32_le();
    int32_t bmp_width = reader.read_i32_le();
    int32_t bmp_height = reader.read_i32_le();  // Negative = top-down
    uint16_t planes = reader.read_u16_le();
    uint16_t bpp = reader.read_u16_le();
    uint32_t compression = reader.read_u32_le();
    [[maybe_unused]] uint32_t image_size = reader.read_u32_le();
    reader.skip(16);  // Skip resolution and color counts
    
    spdlog::debug("ICO/CUR BMP: header_size={}, {}x{}, planes={}, bpp={}, compression={}",
                  header_size, bmp_width, bmp_height, planes, bpp, compression);
    
    // Height in ICO is doubled (includes mask)
    bool top_down = bmp_height < 0;
    uint32_t actual_height = static_cast<uint32_t>(std::abs(bmp_height)) / 2;
    uint32_t actual_width = static_cast<uint32_t>(bmp_width);
    
    // Use entry dimensions if BMP header has 0
    if (actual_width == 0) actual_width = entry.width == 0 ? 256 : entry.width;
    if (actual_height == 0) actual_height = entry.height == 0 ? 256 : entry.height;
    
    CursorImage img;
    img.width = actual_width;
    img.height = actual_height;
    
    // Hotspot from directory entry (for CUR files)
    img.hotspot_x = is_cursor ? entry.planes_or_hotspot_x : 0;
    img.hotspot_y = is_cursor ? entry.bpp_or_hotspot_y : 0;
    
    img.pixels.resize(static_cast<size_t>(img.width) * img.height * 4);
    
    if (compression != 0) {
        throw std::runtime_error("ICO/CUR: Compressed BMP not supported");
    }
    
    // Calculate row stride (BMP rows are 4-byte aligned)
    uint32_t row_stride = ((actual_width * bpp + 31) / 32) * 4;
    uint32_t mask_stride = ((actual_width + 31) / 32) * 4;
    
    size_t pixel_offset = header_size;
    size_t color_table_size = 0;
    
    // Handle color palette
    std::vector<uint32_t> palette;
    if (bpp <= 8) {
        uint32_t num_colors = 1u << bpp;
        color_table_size = num_colors * 4;
        palette.reserve(num_colors);
        
        reader.seek(header_size);
        for (uint32_t i = 0; i < num_colors && reader.remaining() >= 4; ++i) {
            uint8_t b = reader.read_u8();
            uint8_t g = reader.read_u8();
            uint8_t r = reader.read_u8();
            reader.skip(1);  // Reserved
            palette.push_back((r << 0) | (g << 8) | (b << 16) | (0xFF << 24));
        }
    }
    
    pixel_offset = header_size + color_table_size;
    size_t mask_offset = pixel_offset + static_cast<size_t>(row_stride) * actual_height;
    
    // Check if we have enough data
    size_t required = mask_offset + static_cast<size_t>(mask_stride) * actual_height;
    if (data.size() < required) {
        spdlog::warn("ICO/CUR: BMP data truncated ({} < {}), may have artifacts", 
                     data.size(), required);
    }
    
    // Decode pixels based on bit depth
    for (uint32_t y = 0; y < actual_height; ++y) {
        // BMP is bottom-up by default (unless height is negative)
        uint32_t src_y = top_down ? y : (actual_height - 1 - y);
        size_t src_row = pixel_offset + static_cast<size_t>(src_y) * row_stride;
        size_t dst_row = static_cast<size_t>(y) * img.width * 4;
        
        for (uint32_t x = 0; x < actual_width; ++x) {
            uint32_t pixel = 0;
            
            if (src_row + (x * bpp) / 8 >= data.size()) {
                continue;  // Out of bounds
            }
            
            switch (bpp) {
                case 1: {
                    size_t byte_idx = src_row + x / 8;
                    uint8_t bit_mask = 0x80 >> (x % 8);
                    uint8_t idx = (data[byte_idx] & bit_mask) ? 1 : 0;
                    pixel = (idx < palette.size()) ? palette[idx] : 0;
                    break;
                }
                case 4: {
                    size_t byte_idx = src_row + x / 2;
                    uint8_t nibble = (x % 2 == 0) ? (data[byte_idx] >> 4) : (data[byte_idx] & 0x0F);
                    pixel = (nibble < palette.size()) ? palette[nibble] : 0;
                    break;
                }
                case 8: {
                    uint8_t idx = data[src_row + x];
                    pixel = (idx < palette.size()) ? palette[idx] : 0;
                    break;
                }
                case 24: {
                    size_t byte_idx = src_row + static_cast<size_t>(x) * 3;
                    if (byte_idx + 2 < data.size()) {
                        uint8_t b = data[byte_idx];
                        uint8_t g = data[byte_idx + 1];
                        uint8_t r = data[byte_idx + 2];
                        pixel = (r << 0) | (g << 8) | (b << 16) | (0xFF << 24);
                    }
                    break;
                }
                case 32: {
                    size_t byte_idx = src_row + static_cast<size_t>(x) * 4;
                    if (byte_idx + 3 < data.size()) {
                        uint8_t b = data[byte_idx];
                        uint8_t g = data[byte_idx + 1];
                        uint8_t r = data[byte_idx + 2];
                        uint8_t a = data[byte_idx + 3];
                        pixel = (r << 0) | (g << 8) | (b << 16) | (a << 24);
                    }
                    break;
                }
                default:
                    spdlog::warn("ICO/CUR: Unsupported BMP bit depth {}", bpp);
                    break;
            }
            
            // Write pixel as RGBA
            size_t dst_idx = dst_row + static_cast<size_t>(x) * 4;
            img.pixels[dst_idx + 0] = (pixel >> 0) & 0xFF;   // R
            img.pixels[dst_idx + 1] = (pixel >> 8) & 0xFF;   // G
            img.pixels[dst_idx + 2] = (pixel >> 16) & 0xFF;  // B
            img.pixels[dst_idx + 3] = (pixel >> 24) & 0xFF;  // A
        }
    }
    
    // Apply AND mask (transparency) for non-32bpp images
    // For 32bpp, alpha channel is already set
    if (bpp < 32 && mask_offset + mask_stride <= data.size()) {
        for (uint32_t y = 0; y < actual_height; ++y) {
            uint32_t src_y = top_down ? y : (actual_height - 1 - y);
            size_t mask_row = mask_offset + static_cast<size_t>(src_y) * mask_stride;
            size_t dst_row = static_cast<size_t>(y) * img.width * 4;
            
            for (uint32_t x = 0; x < actual_width; ++x) {
                size_t byte_idx = mask_row + x / 8;
                if (byte_idx >= data.size()) continue;
                
                uint8_t bit_mask = 0x80 >> (x % 8);
                bool transparent = (data[byte_idx] & bit_mask) != 0;
                
                if (transparent) {
                    size_t dst_idx = dst_row + static_cast<size_t>(x) * 4;
                    img.pixels[dst_idx + 3] = 0;  // Set alpha to 0
                }
            }
        }
    }
    
    spdlog::debug("ICO/CUR: Decoded BMP {}x{} {}bpp, hotspot ({}, {})",
                  img.width, img.height, bpp, img.hotspot_x, img.hotspot_y);
    
    return img;
}

size_t IcoCurDecoder::select_best_image(const std::vector<DirEntry>& entries) {
    if (entries.empty()) return 0;
    
    size_t best = 0;
    uint32_t best_score = 0;
    
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        uint32_t w = e.width == 0 ? 256 : e.width;
        uint32_t h = e.height == 0 ? 256 : e.height;
        
        // Score = area * bpp (prefer larger images with more colors)
        uint32_t bpp = e.bpp_or_hotspot_y;  // For ICO this is bpp
        if (bpp == 0) bpp = 32;  // Assume 32bpp if not specified
        
        uint32_t score = w * h * bpp;
        
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    
    return best;
}

} // namespace ani2xcursor
