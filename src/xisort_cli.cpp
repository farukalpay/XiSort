// xisort_cli.cpp  — Command-line wrapper for XiSort v36.5
// AUTHOR: Faruk Alpay  •  ORCID: 0009-0009-2207-6528
// -----------------------------------------------------------------------------
// Build (GCC 14 with OpenMP):
//   g++-14 -std=c++17 -O3 -fopenmp xisort_cli.cpp -o xisort
// -----------------------------------------------------------------------------
// This file purposefully AVOIDS <algorithm>; we replace std::min with a tiny
// helper `xmin()` defined below so that the only standard headers pulled in are
// the absolute minimum needed for a portable CLI.
// -----------------------------------------------------------------------------

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <string>
#include <vector>

#include "xisort.cpp"   // core sorter + XiSortConfig + double_to_key

using Clock = std::chrono::steady_clock;

// ─── tiny helper ─────────────────────────────────────────
template <typename T>
static inline T xmin(T a, T b) noexcept { return (a < b ? a : b); }

// ─── error & timing helpers ──────────────────────────────────────────────────
static void die(const std::string &msg) {
    std::cerr << "[xisort] " << msg << "\n";
    std::exit(EXIT_FAILURE);
}
static inline double ms_since(const Clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ─── external merge‑sort primitives ──────────────────────────────────────────
struct Run {
    std::ifstream file;
    std::vector<double> buffer;
    std::size_t idx{0};
    bool eof{false};
};
struct HeapItem {
    double value;
    std::size_t run_id;
    bool operator>(const HeapItem &o) const {
        return double_to_key(value) > double_to_key(o.value);
    }
};

static void external_sort(const std::string &in_path,
                          const std::string &out_path,
                          std::size_t mem_limit_bytes,
                          bool parallel)
{
    const std::size_t ALIGN = 8; // sizeof(double)
    const std::uint64_t total_bytes = std::filesystem::file_size(in_path);
    if (total_bytes % ALIGN) die("input file size not multiple of 8 bytes");
    const std::uint64_t total_elems = total_bytes / ALIGN;
    if (!total_elems) die("input file is empty");

    // ── Phase 1: split into sorted runs ────────────────────────────────────
    const std::size_t max_elems_RAM = mem_limit_bytes / sizeof(double);
    if (!max_elems_RAM) die("mem‑limit too small (< 8 bytes)");

    std::ifstream fin(in_path, std::ios::binary);
    if (!fin) die("cannot open input file");

    std::vector<std::string> run_paths;
    std::vector<double> buf(max_elems_RAM);
    std::uint64_t remaining = total_elems;
    std::size_t   run_idx   = 0;

    auto t1 = Clock::now();
    while (remaining) {
        std::size_t chunk = xmin<std::uint64_t>(remaining, max_elems_RAM);
        fin.read(reinterpret_cast<char*>(buf.data()), chunk * sizeof(double));
        if (fin.gcount() != static_cast<std::streamsize>(chunk*sizeof(double)))
            die("I/O error while reading");

        XiSortConfig cfg; cfg.parallel = parallel; cfg.trace = false;
        xi_sort(buf.data(), chunk, cfg);

        std::string run_path = "xisort_run_" + std::to_string(run_idx++) + ".bin";
        std::ofstream fout(run_path, std::ios::binary);
        fout.write(reinterpret_cast<char*>(buf.data()), chunk*sizeof(double));
        fout.close();
        run_paths.push_back(run_path);
        remaining -= chunk;
    }
    fin.close();
    std::cerr << "[xisort] phase‑1 produced " << run_paths.size()
              << " runs in " << ms_since(t1)/1000.0 << " s\n";

    // ── Phase 2: k‑way merge ──────────────────────────────────────────────
    auto t2 = Clock::now();
    const std::size_t RUN_BUF = 4096; // doubles kept from each run in RAM

    std::vector<Run> runs(run_paths.size());
    for (std::size_t i = 0; i < run_paths.size(); ++i) {
        runs[i].file.open(run_paths[i], std::ios::binary);
        runs[i].buffer.resize(RUN_BUF);
        runs[i].file.read(reinterpret_cast<char*>(runs[i].buffer.data()), RUN_BUF*sizeof(double));
        std::size_t got = runs[i].file.gcount() / sizeof(double);
        runs[i].buffer.resize(got);
        runs[i].eof = (got == 0);
    }

    std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<>> heap;
    for (std::size_t i = 0; i < runs.size(); ++i)
        if (!runs[i].eof) heap.push({runs[i].buffer[0], i});

    std::ofstream fout(out_path, std::ios::binary);
    if (!fout) die("cannot open output file");

    std::vector<double> out_buf(RUN_BUF);
    std::size_t out_idx = 0;

    while (!heap.empty()) {
        HeapItem it = heap.top(); heap.pop();
        out_buf[out_idx++] = it.value;
        Run &r = runs[it.run_id];
        if (++r.idx == r.buffer.size()) {
            r.file.read(reinterpret_cast<char*>(r.buffer.data()), RUN_BUF*sizeof(double));
            std::size_t got = r.file.gcount() / sizeof(double);
            r.buffer.resize(got);
            r.idx = 0;
            r.eof = (got == 0);
        }
        if (!r.eof) heap.push({r.buffer[r.idx], it.run_id});
        if (out_idx == out_buf.size()) {
            fout.write(reinterpret_cast<char*>(out_buf.data()), out_idx*sizeof(double));
            out_idx = 0;
        }
    }
    if (out_idx)
        fout.write(reinterpret_cast<char*>(out_buf.data()), out_idx*sizeof(double));
    fout.close();
    std::cerr << "[xisort] phase‑2 merged in " << ms_since(t2)/1000.0 << " s\n";

    for (const auto &p : run_paths) std::filesystem::remove(p);
}

// ─── main ────────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "Usage: ./xisort [options] <input.bin> <output.bin>\n"
                     "Options:\n"
                     "  --external            external merge‑sort mode\n"
                     "  --parallel            enable OpenMP parallelism\n"
                     "  --mem-limit=<bytes>   RAM budget (external mode)\n"
                     "  --trace               verbose trace\n";
        return EXIT_FAILURE;
    }

    bool external = false, parallel = false, trace = false;
    std::size_t mem_limit = 1ULL<<30; // 1 GiB default
    std::vector<std::string> pos;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--external") external = true;
        else if (arg == "--parallel") parallel = true;
        else if (arg == "--trace") trace = true;
        else if (arg.rfind("--mem-limit=", 0) == 0)
            mem_limit = std::stoull(arg.substr(12));
        else pos.push_back(arg);
    }
    if (pos.size() != 2) die("need <input> and <output> paths");

    const std::string in_path = pos[0];
    const std::string out_path = pos[1];

    auto t_start = Clock::now();

    if (external)
        external_sort(in_path, out_path, mem_limit, parallel);
    else {
        std::uint64_t bytes = std::filesystem::file_size(in_path);
        if (bytes % 8) die("input file size not multiple of 8 bytes");
        std::size_t n = bytes / 8;
        std::vector<double> data(n);
        {
            std::ifstream fin(in_path, std::ios::binary);
            fin.read(reinterpret_cast<char*>(data.data()), bytes);
        }
        XiSortConfig cfg; cfg.parallel = parallel; cfg.trace = trace;
        xi_sort(data.data(), n, cfg);
        {
            std::ofstream fout(out_path, std::ios::binary);
            fout.write(reinterpret_cast<char*>(data.data()), bytes);
        }
    }

    std::cerr << "[xisort] total " << ms_since(t_start)/1000.0 << " s" << std::endl;
    return EXIT_SUCCESS;
}
