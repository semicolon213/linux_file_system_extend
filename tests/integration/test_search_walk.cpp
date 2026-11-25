#include "search/search.hpp"

#include <chrono>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include <giomm/init.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "test_support.hpp"

namespace {

using filescope::core::ErrorCode;
using filescope::search::Hit;
using filescope::search::Query;
using filescope::search::Searcher;
using filescope::search::SearchId;
using filescope::search::Summary;
using filescope::test::TestEnvironment;
using filescope::test::wait_until;
using Clock = std::chrono::steady_clock;

void create_file(const std::string &path) {
    const int fd = g_open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(close(fd), ==, 0);
}

void create_directory(const std::string &path) {
    g_assert_cmpint(g_mkdir(path.c_str(), 0700), ==, 0);
}

struct Collected {
    Summary summary;
    std::map<std::string, filescope::core::Entry> entries;
};

Collected collect(Query query) {
    Collected collected;
    bool done = false;
    Searcher searcher;
    const auto id = searcher.start(std::move(query));
    g_assert_true(id.ok());
    searcher.signal_hits().connect([&](SearchId found_id, const std::vector<Hit> &batch) {
        g_assert_false(done);
        g_assert_cmpuint(found_id, ==, id.value());
        for (const auto &hit : batch) {
            g_assert_true(collected.entries.emplace(hit.path, hit.entry).second);
        }
    });
    searcher.signal_done().connect([&](auto finished, const auto &result) {
        g_assert_false(done);
        g_assert_cmpuint(finished, ==, id.value());
        g_assert_true(result.ok());
        collected.summary = result.value();
        done = true;
    });
    g_assert_true(wait_until([&] { return done; }, 3000));
    g_assert_cmpuint(collected.summary.hits, ==, collected.entries.size());
    return collected;
}

void test_filters_and_links() {
    const TestEnvironment environment;
    const auto root = environment.root() + "/a";
    create_directory(root);
    create_directory(root + "/b");
    create_directory(root + "/b/c");
    create_directory(root + "/.hidden");
    create_file(root + "/Notes.md");
    create_file(root + "/b/notes.txt");
    create_file(root + "/b/c/other.md");
    create_file(root + "/.hidden/notes.md");
    create_file(root + "/x.md.bak");
    const auto invalid_name = std::string("raw-") + static_cast<char>(0xff);
    create_file(root + "/" + invalid_name);
    create_file(root + "/Straße");
    g_assert_cmpint(symlink(root.c_str(), (root + "/b/c/loop").c_str()), ==, 0);

    Query query;
    query.root = root + "/";
    query.text = "notes";
    auto result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 2);
    g_assert_cmpuint(result.summary.dirs_scanned, ==, 3);
    g_assert_cmpuint(result.entries.count(root + "/Notes.md"), ==, 1);
    g_assert_cmpuint(result.entries.count(root + "/b/notes.txt"), ==, 1);

    query.show_hidden = true;
    result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 3);
    g_assert_false(result.entries.at(root + "/.hidden/notes.md").is_hidden);
    g_assert_cmpuint(result.summary.dirs_scanned, ==, 4);

    query.recursive = false;
    result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 1);
    g_assert_cmpuint(result.entries.count(root + "/Notes.md"), ==, 1);
    g_assert_cmpuint(result.summary.dirs_scanned, ==, 1);

    query.show_hidden = false;
    query.recursive = true;
    query.text.clear();
    query.extensions = {"MD"};
    result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 2);
    g_assert_cmpuint(result.entries.count(root + "/Notes.md"), ==, 1);
    g_assert_cmpuint(result.entries.count(root + "/b/c/other.md"), ==, 1);

    query.extensions.clear();
    query.text = "STRASSE";
    result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 1);
    g_assert_cmpuint(result.entries.count(root + "/Straße"), ==, 1);

    query.text = "raw-";
    result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 1);
    const auto &entry = result.entries.at(root + "/" + invalid_name);
    g_assert_true(entry.name == invalid_name);
    struct stat status {};
    g_assert_cmpint(lstat((root + "/" + invalid_name).c_str(), &status), ==, 0);
    g_assert_true(entry.type == filescope::core::FileType::REGULAR);
    g_assert_cmpuint(entry.ino, ==, status.st_ino);
    g_assert_cmpuint(entry.dev, ==, status.st_dev);
    g_assert_cmpuint(entry.mode, ==, status.st_mode);

    query.text = "loop";
    result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 1);
    g_assert_true(result.entries.at(root + "/b/c/loop").type == filescope::core::FileType::SYMLINK);
    g_assert_cmpuint(result.summary.dirs_scanned, ==, 3);
}

void test_denied() {
    if (geteuid() == 0) {
        g_test_skip("root can read chmod 000 directories");
        return;
    }
    const TestEnvironment environment;
    const auto &root = environment.root();
    const auto denied = root + "/denied";
    create_directory(denied);
    create_file(denied + "/match");
    create_file(root + "/match");
    g_assert_cmpint(g_chmod(denied.c_str(), 0), ==, 0);
    Query query;
    query.root = root;
    query.text = "match";
    const auto result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 1);
    g_assert_cmpuint(result.entries.count(root + "/match"), ==, 1);
    g_assert_cmpuint(result.summary.denied.size(), ==, 1);
    g_assert_true(result.summary.denied.front() == denied);
    g_assert_false(result.summary.denied_more);

    query.root = denied;
    const auto inaccessible_root = collect(query);
    g_assert_cmpuint(inaccessible_root.summary.dirs_scanned, ==, 0);
    g_assert_true(inaccessible_root.summary.denied == std::vector<std::string>{denied});
    g_assert_cmpint(g_chmod(denied.c_str(), 0700), ==, 0);

    std::set<std::string> blocked;
    for (int i = 0; i < 101; ++i) {
        const auto path = root + "/blocked-" + std::to_string(i);
        create_directory(path);
        g_assert_cmpint(g_chmod(path.c_str(), 0), ==, 0);
        blocked.insert(path);
    }
    query.root = root;
    const auto many = collect(query);
    g_assert_cmpuint(many.summary.hits, ==, 2);
    g_assert_cmpuint(many.summary.denied.size(), ==, 100);
    g_assert_true(many.summary.denied_more);
    for (const auto &path : many.summary.denied) {
        g_assert_cmpuint(blocked.count(path), ==, 1);
    }
    for (const auto &path : blocked) {
        g_assert_cmpint(g_chmod(path.c_str(), 0700), ==, 0);
    }
}

void test_truncation() {
    const TestEnvironment environment;
    for (int i = 0; i < 100; ++i) {
        create_file(environment.root() + "/file-" + std::to_string(i));
    }
    Query query;
    query.root = environment.root();
    query.max_results = 10;
    const auto result = collect(query);
    g_assert_cmpuint(result.summary.hits, ==, 10);
    g_assert_true(result.summary.truncated);
}

void test_delivery_and_lifetime() {
    const TestEnvironment environment;
    const auto &root = environment.root();
    const auto main_thread = std::this_thread::get_id();
    for (int dir = 0; dir < 10; ++dir) {
        const auto path = root + "/dir-" + std::to_string(dir);
        create_directory(path);
        for (int i = 0; i < 1000; ++i) {
            create_file(path + "/item-" + std::to_string(i));
        }
    }
    create_file(root + "/replacement");
    Query query;
    query.root = root;
    query.text = "item-";
    query.max_results = 10001;
    auto searcher = std::make_unique<Searcher>();
    SearchId cancel_id = 0;
    SearchId superseded = 0;
    SearchId replacement = 0;
    SearchId paced = 0;
    SearchId destroyed = 0;
    std::map<SearchId, unsigned> completions;
    std::map<SearchId, std::size_t> hit_counts;
    std::vector<Clock::time_point> paced_batches;
    searcher->signal_hits().connect([&](SearchId id, const std::vector<Hit> &batch) {
        g_assert_true(std::this_thread::get_id() == main_thread);
        g_assert_cmpuint(completions.count(id), ==, 0);
        g_assert_false(batch.empty());
        g_assert_cmpuint(batch.size(), <=, 500);
        hit_counts[id] += batch.size();
        if (id == cancel_id) {
            g_assert_cmpuint(hit_counts[id], ==, batch.size());
            g_assert_true(searcher->cancel(id));
            g_assert_false(searcher->cancel(id));
        } else if (id == superseded) {
            g_assert_cmpuint(hit_counts[id], ==, batch.size());
            const auto invalid = searcher->start(Query{});
            g_assert_false(invalid.ok());
            Query next;
            next.root = root;
            next.text = "replacement";
            const auto added = searcher->start(next);
            g_assert_true(added.ok());
            replacement = added.value();
        } else if (id == replacement) {
            g_assert_cmpuint(completions[superseded], ==, 1);
            g_assert_cmpuint(batch.size(), ==, 1);
            g_assert_true(batch.front().path == root + "/replacement");
        } else if (id == paced) {
            paced_batches.push_back(Clock::now());
        } else {
            g_assert_cmpuint(id, ==, destroyed);
        }
    });
    searcher->signal_done().connect([&](SearchId id, const auto &result) {
        g_assert_true(std::this_thread::get_id() == main_thread);
        g_assert_cmpuint(++completions[id], ==, 1);
        if (id == cancel_id || id == superseded) {
            g_assert_false(result.ok());
            g_assert_true(result.error().code == ErrorCode::CANCELLED);
        } else {
            g_assert_true(result.ok());
            g_assert_cmpuint(id, !=, destroyed);
            g_assert_cmpuint(result.value().hits, ==, hit_counts[id]);
            g_assert_false(result.value().truncated);
        }
        g_assert_false(searcher->cancel(id));
    });

    const auto start = [&] {
        const auto result = searcher->start(query);
        g_assert_true(result.ok());
        return result.value();
    };
    cancel_id = start();
    g_assert_true(wait_until([&] { return completions.count(cancel_id) != 0; }, 3000));
    superseded = start();
    g_assert_true(
        wait_until([&] { return replacement != 0 && completions.count(replacement); }, 3000));
    g_assert_cmpuint(hit_counts[replacement], ==, 1);

    const auto began = Clock::now();
    paced = start();
    g_assert_true(wait_until([&] { return completions.count(paced) != 0; }, 8000));
    const auto elapsed = std::chrono::duration<double>(Clock::now() - began).count();
    g_assert_cmpuint(hit_counts[paced], ==, 10000);
    g_assert_cmpuint(paced_batches.size(), >=, 20);
    for (std::size_t i = 1; i < paced_batches.size(); ++i) {
        const auto gap = paced_batches[i] - paced_batches[i - 1];
        g_assert_true(gap >= std::chrono::milliseconds(100));
    }
    g_test_message("10,000 files in %.3f s (%zu batches)", elapsed, paced_batches.size());

    destroyed = start();
    g_assert_true(wait_until([&] { return hit_counts.count(destroyed) != 0; }, 3000));
    const auto delivered = hit_counts[destroyed];
    searcher.reset();
    Query empty;
    empty.root = root;
    empty.text = "a-name-that-is-not-present";
    g_assert_cmpuint(collect(empty).summary.hits, ==, 0);
    g_assert_cmpuint(hit_counts[destroyed], ==, delivered);
    g_assert_cmpuint(completions.count(destroyed), ==, 0);
}

} // namespace

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, nullptr);
    Gio::init();
    g_test_add_func("/search/walk/filters-and-links", test_filters_and_links);
    g_test_add_func("/search/walk/denied", test_denied);
    g_test_add_func("/search/walk/truncation", test_truncation);
    g_test_add_func("/search/walk/delivery-and-lifetime", test_delivery_and_lifetime);
    return g_test_run();
}
