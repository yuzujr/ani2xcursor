#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ani2xcursor {

// RIFF chunk header
struct RiffChunk {
    std::string fourcc;             // 4-character code
    uint32_t size;                  // Size of data (not including header)
    size_t data_offset;             // Offset of data in source buffer
    std::span<const uint8_t> data;  // Chunk data

    // For LIST/RIFF chunks, this is the form type (e.g., "ACON", "fram")
    std::string form_type;

    [[nodiscard]] bool is_list() const {
        return fourcc == "RIFF" || fourcc == "LIST";
    }
};

// RIFF file reader with chunk iteration
class RiffReader {
public:
    explicit RiffReader(std::span<const uint8_t> data);

    // Get the root chunk (RIFF)
    [[nodiscard]] const RiffChunk& root() const {
        return root_;
    }

    // Check if this is a valid RIFF file with expected form type
    [[nodiscard]] bool is_valid() const {
        return valid_;
    }
    [[nodiscard]] std::string_view form_type() const {
        return root_.form_type;
    }

    // Iterate over all chunks at a given level (callback returns false to stop)
    using ChunkCallback = std::function<bool(const RiffChunk&)>;
    void iterate_chunks(std::span<const uint8_t> data, const ChunkCallback& callback) const;

    // Find first chunk with given fourcc in data
    [[nodiscard]] std::optional<RiffChunk> find_chunk(std::span<const uint8_t> data,
                                                      std::string_view fourcc) const;

    // Find LIST chunk with given form type
    [[nodiscard]] std::optional<RiffChunk> find_list(std::span<const uint8_t> data,
                                                     std::string_view form_type) const;

    // Parse a single chunk at given offset, returns the chunk and advances offset
    [[nodiscard]] std::optional<RiffChunk> parse_chunk(std::span<const uint8_t> data,
                                                       size_t& offset) const;

private:
    std::span<const uint8_t> data_;
    RiffChunk root_;
    bool valid_;
};

}  // namespace ani2xcursor
