// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: unpacks a fake-signed (backup) PS4 package into a game folder, with shadPS4's package
// extractor (v0.7.0, tools/pkg_extract). Retail packages need the console's keys and fail.
//   pkg-extract <package.pkg> <output folder>
//   pkg-extract --list <package.pkg> <output folder>   the files it would write (folders only)
// The output folder is the game folder itself (e.g. CUSA03173); for an update, extract to
// <title>-UPDATE and copy it over the base game.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include "pkg.h"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool list = argc == 4 && std::string(argv[1]) == "--list";
    if (list) {
        ++argv;
        --argc;
    }
    if (argc != 3) {
        std::fprintf(stderr, "usage: pkg-extract [--list] <package.pkg> <output folder>\n");
        return 2;
    }
    const std::filesystem::path package = std::filesystem::u8path(argv[1]);
    const std::filesystem::path output = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    PKG pkg;
    std::string failure;
    if (!pkg.Open(package, failure)) {
        std::fprintf(stderr, "pkg-extract: %s: not a PS4 package%s%s\n", argv[1],
                     failure.empty() ? "" : ": ", failure.c_str());
        return 1;
    }
    const auto header = pkg.GetPkgHeader();
    std::printf("Package: %s, title %.*s, %.1f GiB, flags: %s\n", argv[1],
                int(pkg.GetTitleID().size()), pkg.GetTitleID().data(),
                double(pkg.GetPkgSize()) / (1u << 30), pkg.GetPkgFlags().empty() ? "-" : pkg.GetPkgFlags().c_str());
    if (PKG::isFlagSet(header.pkg_content_flags, PKGContentFlag::DELTA_PATCH) &&
        !PKG::isFlagSet(header.pkg_content_flags, PKGContentFlag::CUMULATIVE_PATCH)) {
        std::printf("Note: a delta patch holds only changed parts of files; copying it over the base "
                    "game does not apply it.\n");
    }
    std::filesystem::create_directories(output);
    if (!pkg.Extract(package, output, failure)) {
        std::fprintf(stderr, "pkg-extract: %s\n", failure.empty() ? "extraction failed" : failure.c_str());
        return 1;
    }
    const int files = int(pkg.GetNumberOfFiles());
    if (list) {
        for (int i = 0; i < files; ++i) {
            if (const auto path = pkg.FilePath(i); !path.empty()) {
                std::printf("%s\n", path.lexically_relative(output).generic_string().c_str());
            }
        }
        return 0;
    }
    std::atomic<int> next{0}, done{0};
    const unsigned workers = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::printf("Extracting %d entries into %s with %u threads\n", files, output.string().c_str(), workers);
    std::vector<std::thread> threads;
    for (unsigned w = 0; w < workers; ++w) {
        threads.emplace_back([&] {
            for (int i; (i = next.fetch_add(1)) < files;) {
                pkg.ExtractFiles(i);
                const int n = done.fetch_add(1) + 1;
                if (n % 500 == 0 || n == files) {
                    std::printf("  %d / %d\n", n, files);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    std::printf("Done: %s\n", output.string().c_str());
    return 0;
}
