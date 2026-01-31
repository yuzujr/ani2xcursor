#include "ani_parser.h"

#include <libintl.h>
#include <spdlog/spdlog.h>

#include <stdexcept>

#include "utils/bytes.h"
#include "utils/fs.h"

namespace ani2xcursor {

const AniFrame& Animation::get_step_frame(size_t step) const {
    if (step >= num_steps) {
        throw std::out_of_range(_("Animation step index out of range"));
    }

    size_t frame_idx = step;
    if (!sequence.empty()) {
        frame_idx = sequence[step];
    }

    if (frame_idx >= frames.size()) {
        throw std::out_of_range(_("Animation frame index out of range"));
    }

    return frames[frame_idx];
}

uint32_t Animation::get_step_delay_ms(size_t step) const {
    if (step >= num_steps) {
        return 0;
    }

    // If we have per-frame delays in the frame data, use those
    const auto& frame = get_step_frame(step);
    return frame.delay_ms;
}

uint32_t Animation::total_duration_ms() const {
    uint32_t total = 0;
    for (size_t i = 0; i < num_steps; ++i) {
        total += get_step_delay_ms(i);
    }
    return total;
}

Animation AniParser::parse(const fs::path& path) {
    spdlog::debug("Parsing ANI file: {}", path.string());
    auto data = utils::read_file(path);
    return parse(data);
}

Animation AniParser::parse(std::span<const uint8_t> data) {
    return parse_impl(data);
}

Animation AniParser::parse_impl(std::span<const uint8_t> data) {
    RiffReader reader(data);

    if (!reader.is_valid()) {
        throw std::runtime_error(_("Invalid RIFF file"));
    }

    if (reader.form_type() != "ACON") {
        throw std::runtime_error(_("Not an ANI file: expected RIFF ACON, got RIFF ") +
                                 std::string(reader.form_type()));
    }

    Animation anim{};
    anim.display_rate = DEFAULT_JIFFIES;

    // Parse all chunks in the root
    std::optional<RiffChunk> anih_chunk;
    std::optional<RiffChunk> rate_chunk;
    std::optional<RiffChunk> seq_chunk;
    std::optional<RiffChunk> fram_list;

    reader.iterate_chunks(reader.root().data, [&](const RiffChunk& chunk) {
        spdlog::debug("ANI: Found chunk '{}'{}", chunk.fourcc,
                      chunk.is_list() ? " (LIST '" + chunk.form_type + "')" : "");

        if (chunk.fourcc == "anih") {
            anih_chunk = chunk;
        } else if (chunk.fourcc == "rate") {
            rate_chunk = chunk;
        } else if (chunk.fourcc == "seq ") {
            seq_chunk = chunk;
        } else if (chunk.fourcc == "LIST" && chunk.form_type == "fram") {
            fram_list = chunk;
        }
        // Ignore INFO and other chunks
        return true;
    });

    // anih is required
    if (!anih_chunk) {
        throw std::runtime_error(_("ANI file missing required 'anih' chunk"));
    }
    parse_anih(*anih_chunk, anim);

    spdlog::info(
        spdlog::fmt_lib::runtime(_("ANI: {} frames, {} steps, default rate {} jiffies ({}ms)")),
        anim.num_frames, anim.num_steps, anim.display_rate, jiffies_to_ms(anim.display_rate));

    // Parse optional rate chunk
    std::vector<uint32_t> rates;
    if (rate_chunk) {
        rates = parse_rate(*rate_chunk, anim.num_steps);
        spdlog::debug("ANI: Found 'rate' chunk with {} entries", rates.size());
    }

    // Parse optional sequence chunk
    if (seq_chunk) {
        anim.sequence = parse_seq(*seq_chunk, anim.num_steps);
        spdlog::debug("ANI: Found 'seq ' chunk with {} entries", anim.sequence.size());
    }

    // Find and parse frames
    if (!fram_list) {
        throw std::runtime_error(_("ANI file missing required LIST 'fram' chunk"));
    }

    anim.frames = parse_frames(reader, *fram_list, anim.num_frames);

    if (anim.frames.empty()) {
        throw std::runtime_error(_("ANI file contains no frames"));
    }

    // Apply delays to frames
    uint32_t default_delay = jiffies_to_ms(anim.display_rate);
    for (size_t i = 0; i < anim.frames.size(); ++i) {
        if (i < rates.size()) {
            anim.frames[i].delay_ms = jiffies_to_ms(rates[i]);
        } else {
            anim.frames[i].delay_ms = default_delay;
        }
    }

    // If we have a sequence and step-specific rates, apply them
    // (rates are per-step, not per-frame when sequence exists)
    if (!anim.sequence.empty() && !rates.empty()) {
        // Store per-step delays - we'll use them when generating output
        for (size_t step = 0; step < anim.num_steps && step < rates.size(); ++step) {
            size_t frame_idx = anim.sequence[step];
            if (frame_idx < anim.frames.size()) {
                // Note: This overwrites if same frame used multiple times
                // That's OK - we query delay via get_step_delay_ms which handles this
                anim.frames[frame_idx].delay_ms = jiffies_to_ms(rates[step]);
            }
        }
    }

    spdlog::info(spdlog::fmt_lib::runtime(_("ANI: Parsed {} frames successfully")),
                 anim.frames.size());

    return anim;
}

void AniParser::parse_anih(const RiffChunk& chunk, Animation& anim) {
    // anih structure (36 bytes):
    // DWORD cbSize       - size of structure (36)
    // DWORD nFrames      - number of images
    // DWORD nSteps       - number of steps/frames in animation
    // DWORD iWidth       - reserved (0)
    // DWORD iHeight      - reserved (0)
    // DWORD iBitCount    - reserved (0)
    // DWORD nPlanes      - reserved (0)
    // DWORD iDispRate    - default display rate (jiffies)
    // DWORD bfAttributes - flags (bit 0=seq present, bit 1=icon data is raw)

    if (chunk.data.size() < 36) {
        throw std::runtime_error(_("ANI 'anih' chunk too small"));
    }

    utils::ByteReader reader(chunk.data);

    uint32_t cb_size = reader.read_u32_le();
    anim.num_frames = reader.read_u32_le();
    anim.num_steps = reader.read_u32_le();

    reader.skip(16);  // Skip reserved fields

    anim.display_rate = reader.read_u32_le();
    anim.flags = reader.read_u32_le();

    // Validate
    if (anim.num_frames == 0) {
        throw std::runtime_error(_("ANI 'anih' reports 0 frames"));
    }
    if (anim.num_steps == 0) {
        anim.num_steps = anim.num_frames;
    }
    if (anim.display_rate == 0) {
        anim.display_rate = DEFAULT_JIFFIES;
        spdlog::debug("ANI: Using default display rate {} jiffies", DEFAULT_JIFFIES);
    }

    spdlog::debug("ANI anih: cbSize={}, frames={}, steps={}, rate={}, flags={:#x}", cb_size,
                  anim.num_frames, anim.num_steps, anim.display_rate, anim.flags);
}

std::vector<uint32_t> AniParser::parse_rate(const RiffChunk& chunk, uint32_t num_steps) {
    std::vector<uint32_t> rates;

    size_t num_entries = chunk.data.size() / 4;
    if (num_entries < num_steps) {
        spdlog::warn(spdlog::fmt_lib::runtime(_("ANI 'rate' chunk has {} entries, expected {}")),
                     num_entries, num_steps);
    }

    utils::ByteReader reader(chunk.data);
    for (size_t i = 0; i < num_entries && i < num_steps; ++i) {
        rates.push_back(reader.read_u32_le());
    }

    return rates;
}

std::vector<uint32_t> AniParser::parse_seq(const RiffChunk& chunk, uint32_t num_steps) {
    std::vector<uint32_t> seq;

    size_t num_entries = chunk.data.size() / 4;
    if (num_entries < num_steps) {
        spdlog::warn(spdlog::fmt_lib::runtime(_("ANI 'seq ' chunk has {} entries, expected {}")),
                     num_entries, num_steps);
    }

    utils::ByteReader reader(chunk.data);
    for (size_t i = 0; i < num_entries && i < num_steps; ++i) {
        seq.push_back(reader.read_u32_le());
    }

    return seq;
}

std::vector<AniFrame> AniParser::parse_frames(const RiffReader& reader, const RiffChunk& fram_list,
                                              uint32_t num_frames) {
    std::vector<AniFrame> frames;
    frames.reserve(num_frames);

    reader.iterate_chunks(fram_list.data, [&](const RiffChunk& chunk) {
        if (chunk.fourcc == "icon") {
            AniFrame frame;
            frame.icon_data.assign(chunk.data.begin(), chunk.data.end());
            frame.delay_ms = 0;  // Will be set later
            frame.hotspot_x = 0;
            frame.hotspot_y = 0;
            frame.width = 0;
            frame.height = 0;

            frames.push_back(std::move(frame));
            spdlog::debug("ANI: Found icon frame {} ({} bytes)", frames.size(), chunk.data.size());
        }
        return true;
    });

    if (frames.size() != num_frames) {
        spdlog::warn(spdlog::fmt_lib::runtime(_("ANI: Expected {} frames, found {}")), num_frames,
                     frames.size());
    }

    return frames;
}

}  // namespace ani2xcursor
