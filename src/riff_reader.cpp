#include "riff_reader.h"
#include "utils/bytes.h"

#include <spdlog/spdlog.h>

namespace ani2xcursor {

RiffReader::RiffReader(std::span<const uint8_t> data) : data_(data), valid_(false) {
    if (data_.size() < 12) {
        spdlog::error("RIFF: File too small ({} bytes)", data_.size());
        return;
    }
    
    utils::ByteReader reader(data_);
    
    // Read RIFF header
    auto fourcc = reader.read_fourcc();
    if (fourcc != "RIFF") {
        spdlog::error("RIFF: Invalid signature '{}' (expected 'RIFF')", fourcc);
        return;
    }
    
    uint32_t size = reader.read_u32_le();
    auto form_type = reader.read_fourcc();
    
    spdlog::debug("RIFF: Form type '{}', size {}", form_type, size);
    
    // Validate size
    if (size + 8 > data_.size()) {
        if (size != data_.size()) {
            spdlog::warn("RIFF: Declared size {} exceeds file size {} (non-fatal; continuing)",
                         size + 8, data_.size());
        }
        // Continue anyway, some files have incorrect sizes
    }
    
    root_.fourcc = "RIFF";
    root_.size = size;
    root_.data_offset = 12;  // After RIFF header
    root_.form_type = std::string(form_type);
    
    // Data is everything after the form type, up to declared size
    size_t actual_data_size = std::min(static_cast<size_t>(size - 4), data_.size() - 12);
    root_.data = data_.subspan(12, actual_data_size);
    
    valid_ = true;
}

std::optional<RiffChunk> RiffReader::parse_chunk(std::span<const uint8_t> data, 
                                                  size_t& offset) const {
    // Minimum chunk is 8 bytes (fourcc + size)
    if (offset + 8 > data.size()) {
        return std::nullopt;
    }
    
    RiffChunk chunk;
    chunk.fourcc = std::string(reinterpret_cast<const char*>(data.data() + offset), 4);
    chunk.size = utils::read_u32_le(data.data() + offset + 4);
    chunk.data_offset = offset + 8;
    
    spdlog::trace("RIFF: Chunk '{}' at offset {}, size {}", chunk.fourcc, offset, chunk.size);
    
    // For LIST chunks, read form type
    if (chunk.fourcc == "LIST") {
        if (chunk.size < 4 || offset + 12 > data.size()) {
            spdlog::error("RIFF: LIST chunk too small");
            return std::nullopt;
        }
        chunk.form_type = std::string(reinterpret_cast<const char*>(data.data() + offset + 8), 4);
        chunk.data_offset = offset + 12;
        
        // Adjust data span to exclude form type
        size_t data_size = std::min(static_cast<size_t>(chunk.size - 4), 
                                    data.size() - offset - 12);
        chunk.data = data.subspan(offset + 12, data_size);
        
        spdlog::debug("RIFF: LIST '{}' with {} bytes of data", chunk.form_type, data_size);
    } else {
        // Regular chunk
        size_t data_size = std::min(static_cast<size_t>(chunk.size), 
                                    data.size() - offset - 8);
        chunk.data = data.subspan(offset + 8, data_size);
    }
    
    // Advance offset, with word alignment (RIFF chunks are 2-byte aligned)
    size_t chunk_total = 8 + chunk.size;
    if (chunk_total & 1) {
        chunk_total++;  // Pad to word boundary
    }
    offset += chunk_total;
    
    return chunk;
}

void RiffReader::iterate_chunks(std::span<const uint8_t> data, 
                                const ChunkCallback& callback) const {
    size_t offset = 0;
    
    while (offset < data.size()) {
        auto chunk = parse_chunk(data, offset);
        if (!chunk) {
            break;
        }
        
        if (!callback(*chunk)) {
            break;
        }
    }
}

std::optional<RiffChunk> RiffReader::find_chunk(std::span<const uint8_t> data,
                                                 std::string_view fourcc) const {
    std::optional<RiffChunk> result;
    
    iterate_chunks(data, [&](const RiffChunk& chunk) {
        if (chunk.fourcc == fourcc) {
            result = chunk;
            return false;  // Stop iteration
        }
        return true;  // Continue
    });
    
    return result;
}

std::optional<RiffChunk> RiffReader::find_list(std::span<const uint8_t> data,
                                                std::string_view form_type) const {
    std::optional<RiffChunk> result;
    
    iterate_chunks(data, [&](const RiffChunk& chunk) {
        if (chunk.fourcc == "LIST" && chunk.form_type == form_type) {
            result = chunk;
            return false;  // Stop iteration
        }
        return true;  // Continue
    });
    
    return result;
}

} // namespace ani2xcursor
