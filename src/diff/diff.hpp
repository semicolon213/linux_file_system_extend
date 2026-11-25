#pragma once

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace filescope::diff {

// NOLINTNEXTLINE(performance-enum-size) -- Keep the documented enum declaration.
enum class ContentKind { TEXT, IMAGE, BINARY };

ContentKind classify(std::string_view bytes) noexcept;

// NOLINTNEXTLINE(performance-enum-size) -- Keep the documented enum declaration.
enum class LineOp { CONTEXT, ADD, DELETE };

struct Line {
    LineOp op = LineOp::CONTEXT;
    std::uint32_t old_number = 0;
    std::uint32_t new_number = 0;
    std::string text;
};

struct Hunk {
    std::uint32_t old_start = 0, old_count = 0, new_start = 0, new_count = 0;
    std::vector<Line> lines;
};

struct TextDiff {
    std::vector<Hunk> hunks;
    std::uint32_t added = 0, deleted = 0;
    bool old_missing_newline = false;
    bool new_missing_newline = false;
    bool too_complex = false;
};

struct TextOptions {
    std::uint32_t context_lines = 3;
    std::uint32_t max_lines = 50000;
    std::uint32_t max_edit_distance = 5000;
};

core::Result<TextDiff> diff_lines(std::string_view before, std::string_view after,
                                  const TextOptions &options = {});

struct ImageView {
    std::uint32_t width = 0, height = 0;
    std::size_t stride = 0;
    const std::uint8_t *pixels = nullptr;
};

struct Rect {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
};

struct ImageDiff {
    bool same_size = true;
    std::vector<Rect> regions;
    std::uint32_t region_count = 0;
    std::uint64_t changed_pixels = 0;
};

struct ImageOptions {
    std::uint32_t cell = 16;
    std::uint8_t threshold = 24;
    std::uint32_t max_regions = 8;
};

core::Result<ImageDiff> compare_images(const ImageView &before, const ImageView &after,
                                       const ImageOptions &options = {});

} // namespace filescope::diff
