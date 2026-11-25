#include "diff/diff.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <glib.h>

namespace {

using namespace filescope::diff;
using namespace std::literals;

void test_classify() {
    for (const auto bytes : {"\x89PNG\r\n\x1a\n"sv, "\xff\xd8\xff"sv, "GIF87a"sv, "GIF89a"sv,
                             "RIFF\0\0\0\0WEBP"sv, "BM"sv}) {
        g_assert_true(classify(bytes) == ContentKind::IMAGE);
    }
    for (const auto bytes : {""sv, "hello\n"sv, "# 문서\n- item\n"sv, "RIFF"sv}) {
        g_assert_true(classify(bytes) == ContentKind::TEXT);
    }
    for (const auto bytes :
         {"a\0b"sv, "\xc3\x28"sv, "PK\x03\x04"sv, "PK\x05\x06"sv, "PK\x07\x08"sv}) {
        g_assert_true(classify(bytes) == ContentKind::BINARY);
    }
}

std::string fixture(const char *name, const char *suffix) {
    const auto path = std::string(FILESCOPE_DIFF_GOLDEN_DIR) + '/' + name + suffix;
    gchar *data = nullptr;
    gsize size = 0;
    g_assert_true(g_file_get_contents(path.c_str(), &data, &size, nullptr));
    const std::unique_ptr<gchar, decltype(&g_free)> bytes(data, g_free);
    return {bytes.get(), size};
}

std::size_t line_count(std::string_view bytes) {
    return static_cast<std::size_t>(std::count(bytes.begin(), bytes.end(), '\n')) +
           (!bytes.empty() && bytes.back() != '\n');
}

std::string unified(std::string_view before, std::string_view after) {
    if (classify(before) == ContentKind::BINARY || classify(after) == ContentKind::BINARY) {
        return "BINARY\n";
    }
    const auto result = diff_lines(before, after);
    g_assert_true(result.ok());
    const auto &diff = result.value();
    const auto old_end = line_count(before);
    const auto new_end = line_count(after);
    std::string out;
    for (const auto &hunk : diff.hunks) {
        out += "@@ -" + std::to_string(hunk.old_start) + ',' + std::to_string(hunk.old_count) +
               " +" + std::to_string(hunk.new_start) + ',' + std::to_string(hunk.new_count) +
               " @@\n";
        for (const auto &line : hunk.lines) {
            out += line.op == LineOp::ADD ? '+' : (line.op == LineOp::DELETE ? '-' : ' ');
            out += line.text;
            out += '\n';
            if ((diff.old_missing_newline && line.old_number == old_end) ||
                (diff.new_missing_newline && line.new_number == new_end)) {
                out += "\\ No newline at end of file\n";
            }
        }
    }
    return out;
}

void test_golden(gconstpointer data) {
    const auto *name = static_cast<const char *>(data);
    const auto before = fixture(name, ".before");
    const auto after = fixture(name, ".after");
    const auto expected = fixture(name, ".expected");
    const auto actual = unified(before, after);
    g_assert_cmpstr(actual.c_str(), ==, expected.c_str());
}

using Tokens = std::vector<std::string>;

std::string join(const Tokens &tokens) {
    std::string bytes;
    for (const auto &token : tokens) {
        bytes += token;
    }
    return bytes;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- LCS length is symmetric.
std::size_t lcs(const Tokens &before, const Tokens &after) {
    std::vector<std::size_t> row(after.size() + 1);
    for (const auto &old_line : before) {
        std::size_t diagonal = 0;
        for (std::size_t j = 1; j < row.size(); ++j) {
            const auto above = row[j];
            row[j] = old_line == after[j - 1] ? diagonal + 1 : std::max(row[j], row[j - 1]);
            diagonal = above;
        }
    }
    return row.back();
}

Tokens apply(const Tokens &before, const TextDiff &diff, std::size_t new_size) {
    Tokens rebuilt;
    std::size_t old_index = 0;
    for (const auto &hunk : diff.hunks) {
        const auto old_start = hunk.old_start - (hunk.old_count != 0);
        while (old_index < old_start) {
            rebuilt.push_back(before.at(old_index++));
        }
        const auto new_start = hunk.new_start - (hunk.new_count != 0);
        g_assert_cmpuint(old_index, ==, old_start);
        g_assert_cmpuint(rebuilt.size(), ==, new_start);
        for (const auto &line : hunk.lines) {
            if (line.op != LineOp::ADD) {
                g_assert_cmpuint(line.old_number, ==, old_index + 1);
                const auto text =
                    line.text +
                    ((diff.old_missing_newline && old_index + 1 == before.size()) ? "" : "\n");
                g_assert_true(before.at(old_index++) == text);
            } else {
                g_assert_cmpuint(line.old_number, ==, 0);
            }
            if (line.op != LineOp::DELETE) {
                g_assert_cmpuint(line.new_number, ==, rebuilt.size() + 1);
                rebuilt.push_back(
                    line.text +
                    ((diff.new_missing_newline && rebuilt.size() + 1 == new_size) ? "" : "\n"));
            } else {
                g_assert_cmpuint(line.new_number, ==, 0);
            }
        }
        g_assert_cmpuint(old_index - old_start, ==, hunk.old_count);
        g_assert_cmpuint(rebuilt.size() - new_start, ==, hunk.new_count);
    }
    while (old_index < before.size()) {
        rebuilt.push_back(before.at(old_index++));
    }
    return rebuilt;
}

void test_minimal_and_applicable() {
    std::mt19937 rng(7823);
    const auto generate = [&rng]() {
        const std::array<std::string, 4> choices = {"\n", "a\n", "b\n", "c\r\n"};
        Tokens tokens(std::uniform_int_distribution<std::size_t>(0, 15)(rng));
        for (auto &token : tokens) {
            token = choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(rng)];
        }
        if (!tokens.empty() && tokens.back().size() > 1 && std::bernoulli_distribution{}(rng)) {
            tokens.back().pop_back();
        }
        return tokens;
    };

    for (unsigned i = 0; i < 500; ++i) {
        const auto before = generate();
        const auto after = generate();
        TextOptions options;
        options.context_lines = i % 4;
        const auto result = diff_lines(join(before), join(after), options);
        g_assert_true(result.ok());
        const auto &diff = result.value();
        g_assert_false(diff.too_complex);
        g_assert_true(apply(before, diff, after.size()) == after);
        g_assert_cmpuint(diff.added + diff.deleted, ==,
                         before.size() + after.size() - 2 * lcs(before, after));
    }
}

void test_newline_only() {
    const auto result = diff_lines("last\n", "last");
    g_assert_true(result.ok());
    const auto &diff = result.value();
    g_assert_false(diff.old_missing_newline);
    g_assert_true(diff.new_missing_newline);
    g_assert_cmpuint(diff.added, ==, 1);
    g_assert_cmpuint(diff.deleted, ==, 1);
    g_assert_true(apply({"last\n"}, diff, 1) == Tokens{"last"});
}

std::string repeated_line(std::string_view line, unsigned count) {
    std::string bytes;
    for (unsigned i = 0; i < count; ++i) {
        bytes += line;
    }
    return bytes;
}

void test_context() {
    for (unsigned gap : {5, 6, 7, 8}) {
        const auto common = repeated_line("same\n", gap);
        const auto result = diff_lines("first\n" + common + "last\n", "one\n" + common + "two\n");
        g_assert_true(result.ok());
        g_assert_cmpuint(result.value().hunks.size(), ==, gap <= 6 ? 1 : 2);
    }
}

void test_limits() {
    const auto at_limit = repeated_line("a\n", 50000);
    const auto equal = diff_lines(at_limit, at_limit);
    g_assert_true(equal.ok());
    g_assert_true(equal.value().hunks.empty());
    const auto beyond_limit = at_limit + "a\n";
    for (const auto &result : {diff_lines(beyond_limit, ""), diff_lines("", beyond_limit)}) {
        g_assert_false(result.ok());
        g_assert_true(result.error().code == filescope::core::ErrorCode::TOO_LARGE);
    }

    TextOptions options;
    options.max_edit_distance = 2;
    const auto exact = diff_lines("a\n", "b\n", options);
    g_assert_true(exact.ok());
    g_assert_false(exact.value().too_complex);

    const auto before = repeated_line("old\n", 10000);
    const auto after = repeated_line("new\n", 10000);
    const auto started = std::chrono::steady_clock::now();
    const auto result = diff_lines(before, after);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    g_test_message("10000 disjoint lines per side, D limit 5000: %lld ms",
                   static_cast<long long>(elapsed.count()));
    g_assert_true(result.ok());
    g_assert_true(result.value().too_complex);
    g_assert_cmpuint(result.value().added, ==, 10000);
    g_assert_cmpuint(result.value().deleted, ==, 10000);
    g_assert_true(result.value().hunks.empty());
}

struct Image {
    Image(std::uint32_t width, std::uint32_t height)
        : pixels(static_cast<std::size_t>(width * 4 + 4) * height),
          view{width, height, width * 4 + 4, pixels.data()} {}

    std::uint8_t &at(std::uint32_t x, std::uint32_t y) {
        return pixels[y * view.stride + static_cast<std::size_t>(x) * 4];
    }

    std::vector<std::uint8_t> pixels;
    ImageView view;
};

void assert_rect(const Rect &actual, const Rect &expected) {
    g_assert_cmpuint(actual.x, ==, expected.x);
    g_assert_cmpuint(actual.y, ==, expected.y);
    g_assert_cmpuint(actual.width, ==, expected.width);
    g_assert_cmpuint(actual.height, ==, expected.height);
}

void test_image_threshold_and_neighbors() {
    Image before(64, 64);
    Image after(64, 64);
    for (auto channel : {0, 24, 30}) {
        after.at(20, 20) = static_cast<std::uint8_t>(channel);
        const auto result = compare_images(before.view, after.view);
        g_assert_true(result.ok());
        const auto &diff = result.value();
        const auto count = channel == 30 ? 1U : 0U;
        g_assert_true(diff.same_size);
        g_assert_cmpuint(diff.changed_pixels, ==, count);
        g_assert_cmpuint(diff.region_count, ==, count);
        g_assert_cmpuint(diff.regions.size(), ==, count);
        if (count == 1) {
            assert_rect(diff.regions.front(), {16, 16, 16, 16});
        }
    }
    after.at(33, 20) = 30;
    after.at(2, 2) = 30;
    const auto result = compare_images(before.view, after.view);
    g_assert_true(result.ok());
    const auto &diff = result.value();
    g_assert_cmpuint(diff.changed_pixels, ==, 3);
    g_assert_cmpuint(diff.region_count, ==, 2);
    assert_rect(diff.regions.front(), {16, 16, 32, 16});
    assert_rect(diff.regions.back(), {0, 0, 16, 16});
}

void test_image_region_limit() {
    Image before(64, 208);
    Image after(64, 208);
    for (std::uint32_t i = 0; i < 9; ++i) {
        after.at((i % 2) * 32, (i / 2) * 32) = 30;
    }
    after.at(0, 160) = 30;
    after.at(16, 160) = 30;
    const auto result = compare_images(before.view, after.view);
    g_assert_true(result.ok());
    const auto &diff = result.value();
    g_assert_cmpuint(diff.region_count, ==, 10);
    g_assert_cmpuint(diff.regions.size(), ==, 8);
    g_assert_cmpuint(diff.changed_pixels, ==, 11);
    assert_rect(diff.regions.front(), {0, 160, 32, 16});
    for (std::size_t i = 1; i < diff.regions.size(); ++i) {
        g_assert_cmpuint(diff.regions[i].width, ==, 16);
        g_assert_cmpuint(diff.regions[i].height, ==, 16);
    }
}

void test_image_resize_and_errors() {
    Image before(64, 64);
    Image after(64, 80);
    const auto result = compare_images(before.view, after.view);
    g_assert_true(result.ok());
    const auto &diff = result.value();
    g_assert_false(diff.same_size);
    g_assert_cmpuint(diff.changed_pixels, ==, 64ULL * 16);
    g_assert_cmpuint(diff.region_count, ==, 1);
    assert_rect(diff.regions.front(), {0, 64, 64, 16});

    Image clipped(17, 17);
    clipped.at(16, 16) = 30;
    const auto smaller = compare_images(before.view, clipped.view);
    g_assert_true(smaller.ok());
    g_assert_false(smaller.value().same_size);
    g_assert_cmpuint(smaller.value().changed_pixels, ==, 1);
    g_assert_cmpuint(smaller.value().region_count, ==, 1);
    assert_rect(smaller.value().regions.front(), {16, 16, 1, 1});

    auto bad_stride = before.view;
    bad_stride.stride = 255;
    ImageOptions zero_cell;
    zero_cell.cell = 0;
    for (const auto &invalid :
         {compare_images({}, after.view), compare_images(before.view, bad_stride),
          compare_images(before.view, after.view, zero_cell)}) {
        g_assert_false(invalid.ok());
        g_assert_true(invalid.error().code == filescope::core::ErrorCode::FAILED);
    }
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    g_test_add_func("/diff/classify", test_classify);
    g_test_add_data_func("/diff/golden/empty", "empty", test_golden);
    g_test_add_data_func("/diff/golden/one-empty", "one-empty", test_golden);
    g_test_add_data_func("/diff/golden/no-final-newline", "no-final-newline", test_golden);
    g_test_add_data_func("/diff/golden/crlf", "crlf", test_golden);
    g_test_add_data_func("/diff/golden/repeated", "repeated", test_golden);
    g_test_add_data_func("/diff/golden/markdown", "markdown", test_golden);
    g_test_add_data_func("/diff/golden/invalid-utf8", "invalid-utf8", test_golden);
    g_test_add_func("/diff/text/minimal-and-applicable", test_minimal_and_applicable);
    g_test_add_func("/diff/text/newline-only", test_newline_only);
    g_test_add_func("/diff/text/context", test_context);
    g_test_add_func("/diff/text/limits", test_limits);
    g_test_add_func("/diff/image/threshold-and-neighbors", test_image_threshold_and_neighbors);
    g_test_add_func("/diff/image/region-limit", test_image_region_limit);
    g_test_add_func("/diff/image/resize-and-errors", test_image_resize_and_errors);
    return g_test_run();
}
