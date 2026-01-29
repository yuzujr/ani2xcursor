#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "riff_reader.h"

namespace ani2xcursor {

namespace fs = std::filesystem;

// A single frame of an animated cursor
struct AniFrame {
    std::vector<uint8_t> icon_data;  // Raw ICO/CUR data for this frame
    uint32_t delay_ms;               // Delay in milliseconds
    uint16_t hotspot_x;
    uint16_t hotspot_y;
    uint32_t width;
    uint32_t height;
};

// Parsed animated cursor
struct Animation {
    std::vector<AniFrame> frames;
    std::vector<uint32_t> sequence;  // Frame playback order (indices into frames)
    uint32_t num_frames;             // From anih header
    uint32_t num_steps;              // From anih header
    uint32_t display_rate;           // Default rate from anih (jiffies = 1/60 sec)
    uint32_t flags;                  // From anih (bit 0: contains sequence, bit 1: contains icon)

    // Get actual frame data for a step (resolves sequence if present)
    [[nodiscard]] const AniFrame& get_step_frame(size_t step) const;

    // Get delay for a step in milliseconds
    [[nodiscard]] uint32_t get_step_delay_ms(size_t step) const;

    // Total animation duration
    [[nodiscard]] uint32_t total_duration_ms() const;
};

// ANI file parser
class AniParser {
public:
    // Parse ANI file from path
    [[nodiscard]] static Animation parse(const fs::path& path);

    // Parse ANI from memory
    [[nodiscard]] static Animation parse(std::span<const uint8_t> data);

private:
    static Animation parse_impl(std::span<const uint8_t> data);

    // Parse anih (ANI Header) chunk
    static void parse_anih(const RiffChunk& chunk, Animation& anim);

    // Parse rate chunk (per-frame delays)
    static std::vector<uint32_t> parse_rate(const RiffChunk& chunk, uint32_t num_steps);

    // Parse seq chunk (playback sequence)
    static std::vector<uint32_t> parse_seq(const RiffChunk& chunk, uint32_t num_steps);

    // Parse icon frames from LIST fram chunk
    static std::vector<AniFrame> parse_frames(const RiffReader& reader, const RiffChunk& fram_list,
                                              uint32_t num_frames);

    // Convert jiffies (1/60 sec) to milliseconds
    static constexpr uint32_t jiffies_to_ms(uint32_t jiffies) {
        // 1 jiffy = 1/60 second = 16.667 ms
        return (jiffies * 1000 + 30) / 60;  // Round to nearest
    }

    // Default delay if not specified (10 jiffies = ~167ms)
    static constexpr uint32_t DEFAULT_JIFFIES = 10;
};

}  // namespace ani2xcursor
