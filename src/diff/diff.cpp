#include "diff/diff.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

#include <glib.h>

namespace filescope::diff {
namespace {

bool starts_with(std::string_view bytes, std::string_view signature) noexcept {
    return bytes.substr(0, signature.size()) == signature;
}

using Lines = std::vector<std::string_view>;

bool split_lines(std::string_view bytes, std::uint32_t limit, Lines &lines) {
    while (!bytes.empty()) {
        if (lines.size() == limit) {
            return false;
        }
        const auto newline = bytes.find('\n');
        const auto length = newline == std::string_view::npos ? bytes.size() : newline + 1;
        // Keep the delimiter while matching so a final newline change produces an edit.
        lines.push_back(bytes.substr(0, length));
        bytes.remove_prefix(length);
    }
    return true;
}

using Offset = std::int64_t;

std::vector<std::size_t> line_ids(const Lines &lines,
                                  std::unordered_map<std::string_view, std::size_t> &ids) {
    std::vector<std::size_t> result;
    result.reserve(lines.size());
    for (const auto line : lines) {
        result.push_back(ids.try_emplace(line, ids.size()).first->second);
    }
    return result;
}

bool takes_add(const std::vector<Offset> &previous, std::size_t index) {
    return index == 0 || (index < previous.size() && previous[index - 1] < previous[index]);
}

std::vector<LineOp> backtrack(const std::vector<std::vector<Offset>> &trace, Offset x, Offset y) {
    std::vector<LineOp> ops;
    ops.reserve(static_cast<std::size_t>(x + y));
    for (auto depth = trace.size(); depth > 0; --depth) {
        const auto &previous = trace[depth - 1];
        const auto index = static_cast<std::size_t>((x - y + static_cast<Offset>(depth)) / 2);
        const bool add = takes_add(previous, index);
        const Offset previous_x = previous[add ? index : index - 1];
        const Offset previous_y = previous_x - (x - y) - (add ? 1 : -1);
        while (x > previous_x && y > previous_y) {
            ops.push_back(LineOp::CONTEXT);
            --x;
            --y;
        }
        ops.push_back(add ? LineOp::ADD : LineOp::DELETE);
        x = previous_x;
        y = previous_y;
    }
    ops.resize(ops.size() + static_cast<std::size_t>(x), LineOp::CONTEXT);
    std::reverse(ops.begin(), ops.end());
    return ops;
}

std::optional<std::vector<LineOp>> myers(const Lines &before, const Lines &after,
                                         std::uint32_t limit) {
    const auto old_size = static_cast<Offset>(before.size());
    const auto new_size = static_cast<Offset>(after.size());
    if (std::abs(old_size - new_size) > limit) {
        return std::nullopt;
    }

    // Resolve line equality once, before the same lines are revisited on different diagonals.
    std::unordered_map<std::string_view, std::size_t> ids;
    const auto old_ids = line_ids(before, ids);
    const auto new_ids = line_ids(after, ids);

    // At depth d only diagonals -d, -d+2, ..., d are reachable.
    std::vector<std::vector<Offset>> trace;
    for (Offset depth = 0; depth <= limit; ++depth) {
        std::vector<Offset> row(static_cast<std::size_t>(depth + 1));
        for (std::size_t index = 0; index < row.size(); ++index) {
            Offset x = 0;
            if (depth != 0) {
                const auto &previous = trace.back();
                x = takes_add(previous, index) ? previous[index] : previous[index - 1] + 1;
            }
            const Offset diagonal = 2 * static_cast<Offset>(index) - depth;
            Offset y = x - diagonal;
            while (x < old_size && y < new_size &&
                   old_ids[static_cast<std::size_t>(x)] == new_ids[static_cast<std::size_t>(y)]) {
                ++x;
                ++y;
            }
            if (x >= old_size && y >= new_size) {
                return backtrack(trace, old_size, new_size);
            }
            row[index] = x;
        }
        trace.push_back(std::move(row));
    }
    return std::nullopt;
}

std::string line_text(std::string_view line) {
    if (line.back() == '\n') {
        line.remove_suffix(1);
    }
    return std::string(line);
}

void make_hunks(TextDiff &result, const std::vector<LineOp> &ops, const Lines &before,
                const Lines &after, std::size_t context) {
    std::uint32_t old_consumed = 0;
    std::uint32_t new_consumed = 0;
    std::size_t cursor = 0;
    while (cursor < ops.size()) {
        const auto first_edit =
            std::find_if(ops.begin() + static_cast<std::ptrdiff_t>(cursor), ops.end(),
                         [](LineOp op) { return op != LineOp::CONTEXT; });
        if (first_edit == ops.end()) {
            break;
        }
        auto last_edit = static_cast<std::size_t>(first_edit - ops.begin());
        const auto skipped = last_edit - cursor - std::min(context, last_edit - cursor);
        old_consumed += static_cast<std::uint32_t>(skipped);
        new_consumed += static_cast<std::uint32_t>(skipped);
        cursor += skipped;

        auto end = last_edit + 1;
        for (; end < ops.size(); ++end) {
            if (ops[end] != LineOp::CONTEXT) {
                last_edit = end;
            } else if (static_cast<std::uint64_t>(end - last_edit) > 2ULL * context) {
                break;
            }
        }
        end = last_edit + 1 + std::min(context, ops.size() - last_edit - 1);

        Hunk hunk;
        const auto old_start = old_consumed;
        const auto new_start = new_consumed;
        for (; cursor < end; ++cursor) {
            const auto op = ops[cursor];
            const auto text = op == LineOp::ADD ? after[new_consumed] : before[old_consumed];
            const auto old_number = op == LineOp::ADD ? 0 : ++old_consumed;
            const auto new_number = op == LineOp::DELETE ? 0 : ++new_consumed;
            hunk.lines.push_back({op, old_number, new_number, line_text(text)});
            result.added += op == LineOp::ADD;
            result.deleted += op == LineOp::DELETE;
        }
        hunk.old_count = old_consumed - old_start;
        hunk.new_count = new_consumed - new_start;
        hunk.old_start = old_start + (hunk.old_count != 0);
        hunk.new_start = new_start + (hunk.new_count != 0);
        result.hunks.push_back(std::move(hunk));
    }
}

bool valid_view(const ImageView &view) {
    std::size_t row_bytes = 0;
    std::size_t offset = 0;
    return view.pixels != nullptr &&
           !__builtin_mul_overflow(static_cast<std::size_t>(view.width), 4U, &row_bytes) &&
           view.stride >= row_bytes &&
           (view.height == 0 ||
            (!__builtin_mul_overflow(view.stride, view.height - 1, &offset) &&
             !__builtin_add_overflow(offset, row_bytes, &offset) &&
             offset <= static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max())));
}

bool changed_pixel(const std::uint8_t *before, const std::uint8_t *after, int threshold) {
    for (std::size_t channel = 0; channel < 4; ++channel) {
        if (std::abs(before[channel] - after[channel]) > threshold) {
            return true;
        }
    }
    return false;
}

Rect collect_region(std::vector<std::uint8_t> &cells, std::vector<std::size_t> &pending,
                    std::size_t start, std::size_t columns, const ImageView &after,
                    std::uint32_t cell) {
    auto left = start % columns;
    auto right = left;
    auto top = start / columns;
    auto bottom = top;

    const auto enqueue = [&](std::size_t index) {
        if (cells[index] != 0) {
            cells[index] = 0;
            pending.push_back(index);
        }
    };
    enqueue(start);
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        const auto x = index % columns;
        const auto y = index / columns;
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
        if (x > 0) {
            enqueue(index - 1);
        }
        if (x + 1 < columns) {
            enqueue(index + 1);
        }
        if (y > 0) {
            enqueue(index - columns);
        }
        if (index < cells.size() - columns) {
            enqueue(index + columns);
        }
    }

    const auto x = static_cast<std::uint32_t>(left * cell);
    const auto y = static_cast<std::uint32_t>(top * cell);
    const auto width = std::min<std::uint64_t>((right + 1ULL) * cell, after.width) - x;
    const auto height = std::min<std::uint64_t>((bottom + 1ULL) * cell, after.height) - y;
    return {x, y, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

std::uint64_t area(const Rect &rect) {
    return static_cast<std::uint64_t>(rect.width) * rect.height;
}

} // namespace

ContentKind classify(std::string_view bytes) noexcept {
    using namespace std::literals;
    if (starts_with(bytes, "\x89PNG\r\n\x1a\n"sv) || starts_with(bytes, "\xff\xd8\xff"sv) ||
        starts_with(bytes, "GIF87a"sv) || starts_with(bytes, "GIF89a"sv) ||
        starts_with(bytes, "BM"sv) ||
        (bytes.size() >= 12 && starts_with(bytes, "RIFF"sv) && bytes.substr(8, 4) == "WEBP")) {
        return ContentKind::IMAGE;
    }
    if (bytes.empty()) {
        return ContentKind::TEXT;
    }
    if (starts_with(bytes, "PK\x03\x04"sv) || starts_with(bytes, "PK\x05\x06"sv) ||
        starts_with(bytes, "PK\x07\x08"sv)) {
        return ContentKind::BINARY;
    }
    return g_utf8_validate_len(bytes.data(), bytes.size(), nullptr) != 0 ? ContentKind::TEXT
                                                                         : ContentKind::BINARY;
}

core::Result<TextDiff> diff_lines(std::string_view before, std::string_view after,
                                  const TextOptions &options) {
    Lines old_lines;
    Lines new_lines;
    if (!split_lines(before, options.max_lines, old_lines) ||
        !split_lines(after, options.max_lines, new_lines)) {
        return core::make_error(core::ErrorCode::TOO_LARGE, "Diff line limit exceeded");
    }

    TextDiff result;
    result.old_missing_newline = !before.empty() && before.back() != '\n';
    result.new_missing_newline = !after.empty() && after.back() != '\n';
    if (before == after) {
        return result;
    }
    const auto ops = myers(old_lines, new_lines, options.max_edit_distance);
    if (!ops) {
        result.too_complex = true;
        result.added = static_cast<std::uint32_t>(new_lines.size());
        result.deleted = static_cast<std::uint32_t>(old_lines.size());
        return result;
    }

    make_hunks(result, *ops, old_lines, new_lines, options.context_lines);
    return result;
}

core::Result<ImageDiff> compare_images(const ImageView &before, const ImageView &after,
                                       const ImageOptions &options) {
    if (!valid_view(before) || !valid_view(after) || options.cell == 0) {
        return core::make_error(core::ErrorCode::FAILED, "Invalid image view or cell size");
    }

    ImageDiff result;
    result.same_size = before.width == after.width && before.height == after.height;
    if (after.width == 0 || after.height == 0) {
        return result;
    }

    const std::size_t columns = (after.width - 1) / options.cell + 1;
    const std::size_t rows = (after.height - 1) / options.cell + 1;
    std::vector<std::uint8_t> cells(columns * rows);
    for (std::uint32_t y = 0; y < after.height; ++y) {
        for (std::uint32_t x = 0; x < after.width; ++x) {
            const auto channel_offset = static_cast<std::size_t>(x) * 4;
            if (y >= before.height || x >= before.width ||
                changed_pixel(before.pixels + y * before.stride + channel_offset,
                              after.pixels + y * after.stride + channel_offset,
                              options.threshold)) {
                ++result.changed_pixels;
                cells[(y / options.cell) * columns + x / options.cell] = 1;
            }
        }
    }

    std::vector<std::size_t> pending;
    for (std::size_t index = 0; index < cells.size(); ++index) {
        if (cells[index] != 0) {
            if (result.region_count == std::numeric_limits<std::uint32_t>::max()) {
                return core::make_error(core::ErrorCode::FAILED, "Too many changed regions");
            }
            ++result.region_count;
            result.regions.push_back(
                collect_region(cells, pending, index, columns, after, options.cell));
        }
    }
    std::stable_sort(result.regions.begin(), result.regions.end(),
                     [](const Rect &a, const Rect &b) { return area(a) > area(b); });
    if (result.regions.size() > options.max_regions) {
        result.regions.resize(options.max_regions);
    }
    return result;
}

} // namespace filescope::diff
