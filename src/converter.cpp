#include "converter.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>

#include "ani_parser.h"
#include "size_selection.h"
#include "size_tools.h"
#include "utils/fs.h"

namespace ani2xcursor {

std::pair<std::vector<CursorImage>, std::vector<uint32_t>> process_ani_file(
    const std::filesystem::path& ani_path, SizeFilter filter,
    const std::vector<uint32_t>& specific_sizes) {
    spdlog::info("Processing: {}", ani_path.filename().string());

    auto animation = AniParser::parse(ani_path);

    std::vector<std::vector<CursorImage>> frames_by_step;
    std::vector<uint32_t> step_delays;

    for (size_t step = 0; step < animation.num_steps; ++step) {
        const auto& frame = animation.get_step_frame(step);
        uint32_t delay = frame.delay_ms;

        auto images = IcoCurDecoder::decode_all(frame.icon_data);

        if (images.empty()) {
            throw std::runtime_error("No images decoded from frame " + std::to_string(step));
        }

        spdlog::debug("Frame {}: {} sizes", step, images.size());

        frames_by_step.push_back(std::move(images));
        step_delays.push_back(delay);
    }

    size_t num_sizes = frames_by_step[0].size();
    for (size_t i = 1; i < frames_by_step.size(); ++i) {
        if (frames_by_step[i].size() != num_sizes) {
            spdlog::warn("Inconsistent sizes, using first size only");
            num_sizes = 1;
            break;
        }
    }

    std::vector<CursorImage> decoded_frames;
    std::vector<uint32_t> delays;
    std::map<uint32_t, size_t> size_frame_counts;

    std::span<const CursorImage> size_span(frames_by_step[0].data(), num_sizes);

    if (filter == SizeFilter::Specific) {
        std::vector<uint32_t> targets;
        targets.reserve(specific_sizes.size());
        for (uint32_t size : specific_sizes) {
            if (std::find(targets.begin(), targets.end(), size) == targets.end()) {
                targets.push_back(size);
            }
        }

        for (uint32_t target_size : targets) {
            auto exact_idx = find_exact_size_index(size_span, target_size);
            bool needs_rescale = !exact_idx.has_value();
            size_t source_idx = exact_idx.value_or(find_closest_size_index(size_span, target_size));
            uint32_t source_size = nominal_size(frames_by_step[0][source_idx]);

            if (needs_rescale) {
                spdlog::info("Rescaling {}x{} -> {}x{}", source_size, source_size, target_size,
                             target_size);
            }

            for (size_t step = 0; step < frames_by_step.size(); ++step) {
                const auto& img = frames_by_step[step][source_idx];
                if (needs_rescale) {
                    decoded_frames.push_back(rescale_cursor(img, target_size));
                } else {
                    decoded_frames.push_back(img);
                }
                delays.push_back(step_delays[step]);
            }

            size_frame_counts[target_size] = frames_by_step.size();
        }
    } else {
        auto size_indices_to_export = select_size_indices(size_span, filter, specific_sizes);

        if (size_indices_to_export.empty()) {
            throw std::runtime_error("No sizes selected for export");
        }

        for (size_t size_idx : size_indices_to_export) {
            uint32_t nominal = 0;

            for (size_t step = 0; step < frames_by_step.size(); ++step) {
                const auto& img = frames_by_step[step][size_idx];
                nominal = nominal_size(img);

                decoded_frames.push_back(img);
                delays.push_back(step_delays[step]);
            }

            size_frame_counts[nominal] = frames_by_step.size();
        }
    }

    if (spdlog::get_level() <= spdlog::level::info) {
        spdlog::info("Exported {} sizes:", size_frame_counts.size());
        for (const auto& [size, count] : size_frame_counts) {
            spdlog::info("  {}x{}: {} frames", size, size, count);
        }
    }

    if (decoded_frames.empty()) {
        throw std::runtime_error("No frames decoded from " + ani_path.string());
    }

    return {std::move(decoded_frames), std::move(delays)};
}

std::pair<std::vector<CursorImage>, std::vector<uint32_t>> process_cur_file(
    const std::filesystem::path& cur_path, SizeFilter filter,
    const std::vector<uint32_t>& specific_sizes) {
    spdlog::info("Processing: {}", cur_path.filename().string());

    auto data = utils::read_file(cur_path);
    auto images = IcoCurDecoder::decode_all(data);

    std::vector<CursorImage> decoded_images;
    std::vector<uint32_t> delays;
    std::map<uint32_t, size_t> size_counts;
    std::span<const CursorImage> image_span(images.data(), images.size());

    if (filter == SizeFilter::Specific) {
        std::vector<uint32_t> targets;
        targets.reserve(specific_sizes.size());
        for (uint32_t size : specific_sizes) {
            if (std::find(targets.begin(), targets.end(), size) == targets.end()) {
                targets.push_back(size);
            }
        }

        for (uint32_t target_size : targets) {
            auto exact_idx = find_exact_size_index(image_span, target_size);
            bool needs_rescale = !exact_idx.has_value();
            size_t source_idx =
                exact_idx.value_or(find_closest_size_index(image_span, target_size));
            uint32_t source_size = nominal_size(images[source_idx]);

            if (needs_rescale) {
                spdlog::info("Rescaling {}x{} -> {}x{}", source_size, source_size, target_size,
                             target_size);
                decoded_images.push_back(rescale_cursor(images[source_idx], target_size));
            } else {
                decoded_images.push_back(images[source_idx]);
            }
            delays.push_back(0);
            size_counts[target_size]++;
        }
    } else {
        auto size_indices = select_size_indices(image_span, filter, specific_sizes);
        if (size_indices.empty()) {
            throw std::runtime_error("No sizes selected for export");
        }

        decoded_images.reserve(size_indices.size());
        delays.reserve(size_indices.size());

        for (size_t idx : size_indices) {
            const auto& img = images[idx];
            decoded_images.push_back(img);
            delays.push_back(0);
            size_counts[nominal_size(img)]++;
        }
    }

    if (spdlog::get_level() <= spdlog::level::info) {
        spdlog::info("Exported {} sizes:", size_counts.size());
        for (const auto& [size, count] : size_counts) {
            spdlog::info("  {}x{}: {} frames", size, size, count);
        }
    }

    return {std::move(decoded_images), std::move(delays)};
}

}  // namespace ani2xcursor
