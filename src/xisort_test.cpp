// AUTHOR: FARUK ALPAY
// ORCID: 0009-0009-2207-6528
/*  xisort_test.cpp  – End-to-end validation for XiSort v36.5
    Builds on xisort.cpp (must be in same directory)
*/

/*
How to run?
g++ -std=c++17 -O3 -fopenmp xisort_cli.cpp -o xisort
(needs xisort.cpp and xisort_cli.cpp in same dir)

./xisort_tests           ← test harness (runs 0-3)
./xisort_tests --small   ← CLI sorter invoked by Test-3
*/

#include <cmath>
#include <random>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>
#include "xisort.cpp"                 // ← Sorter implementation

// ─── helpers ─────────────────────────────────────────────────────────
static inline bool is_sorted_total(const std::vector<double>& v)
{
    for (std::size_t i = 1; i < v.size(); ++i)
        if (double_to_key(v[i-1]) > double_to_key(v[i]))
            return false;
    return true;
}

static void print_sample(const std::vector<double>& v, std::size_t k = 5)
{
    std::cout.setf(std::ios::scientific);
    std::cout << "first " << k << ": ";
    for (std::size_t i = 0; i < k && i < v.size(); ++i)
        std::cout << v[i] << ' ';
    std::cout << "\nlast  " << k << ": ";
    for (std::size_t i = k; i > 0 && i <= v.size(); --i)
        std::cout << v[v.size() - i] << ' ';
    std::cout << '\n';
    std::cout.unsetf(std::ios::scientific);
}

static double elapsed_ms(const std::chrono::steady_clock::time_point& t0)
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

// ─── constants ───────────────────────────────────────────────────────
constexpr std::size_t   INMEM_COUNT_BIG   = 100'000'000;     // ~0.8 GB
constexpr std::size_t   INMEM_COUNT_SMALL = 10'000'000;      // ~80 MB
constexpr std::uint64_t EXTERNAL_SIZE_GB  = 100;             // 100 GB file
constexpr std::size_t   BUFFER_ELEMS      = 1ULL << 15;      // 32 768 doubles

// ─── main ────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    bool small = (argc >= 2 && std::string(argv[1]) == "--small");

    std::cout << "===== XiSort validation suite =====\n";

    // ── Test-0 : special IEEE values ────────────────────────────────
    {
        std::cout << "\n[Test-0] special IEEE-754 values\n";
        std::vector<double> v = {
            5.0, -0.0, 0.0,
            std::numeric_limits<double>::quiet_NaN(),
            -5.0,  std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()
        };
        XiSortConfig cfg;   cfg.trace = true;
        xi_sort(v.data(), static_cast<uint64_t>(v.size()), cfg);
        print_sample(v, v.size());
        std::cout << (is_sorted_total(v) ? "status: OK\n" : "status: FAIL\n");
    }

    // ── Test-1 : duplicate-heavy vector ─────────────────────────────
    {
        std::cout << "\n[Test-1] duplicate-heavy distribution\n";
        const std::size_t N = small ? 1'000'000 : 10'000'000;
        std::vector<double> v(N);
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<int> bucket(0,9);
        for (auto& x : v) {
            int b = bucket(rng);
            x = (b == 0) ? 0.123456789 : static_cast<double>(b);
        }
        XiSortConfig cfg;   cfg.trace = false;
        auto t0 = std::chrono::steady_clock::now();
        xi_sort(v.data(), static_cast<uint64_t>(v.size()), cfg);
        std::cout << "time: " << elapsed_ms(t0) << " ms\n";
        std::cout << (is_sorted_total(v) ? "status: OK\n" : "status: FAIL\n");
    }

    // ── Test-2 : in-memory 100 M normal variates ───────────────────
    {
        std::cout << "\n[Test-2] in-memory large sort\n";
        const std::size_t N = small ? INMEM_COUNT_SMALL : INMEM_COUNT_BIG;
        std::vector<double> v(N);
        std::mt19937_64 rng(1);
        std::normal_distribution<double> gauss(0.0, 1.0);
        for (auto& x : v) x = gauss(rng);
        XiSortConfig cfg;   cfg.parallel = true;   cfg.trace = false;
        auto t0 = std::chrono::steady_clock::now();
        xi_sort(v.data(), static_cast<uint64_t>(v.size()), cfg);
        std::cout << "time: " << elapsed_ms(t0)/1000.0 << " s\n";
        std::cout << (is_sorted_total(v) ? "status: OK\n" : "status: FAIL\n");
        print_sample(v);
    }

    // ── Test-3 : external 100 GB file sort (disk) ───────────────────
    if (!small) {
        std::cout << "\n[Test-3] external " << EXTERNAL_SIZE_GB
                  << " GB dataset (disk-only pipeline)…\n";

        const std::string file_in  = "xisort_ext_input.bin";
        const std::string file_out = "xisort_ext_sorted.bin";

        /* A. generate dataset if missing */
        const uint64_t elems =
            (EXTERNAL_SIZE_GB * 1024ULL * 1024ULL * 1024ULL) / sizeof(double);
        if (!std::filesystem::exists(file_in)) {
            std::cout << "  Generating input file… (one-time)\n";
            std::ofstream fout(file_in, std::ios::binary);
            std::mt19937_64 rng(777);
            std::uniform_real_distribution<double> dist(-1.0, 1.0);
            std::vector<double> buf(BUFFER_ELEMS);
            uint64_t written = 0;
            while (written < elems) {
                std::size_t chunk =
                    std::min<uint64_t>(BUFFER_ELEMS, elems - written);
                for (std::size_t i = 0; i < chunk; ++i) buf[i] = dist(rng);
                fout.write(reinterpret_cast<char*>(buf.data()),
                           chunk * sizeof(double));
                written += chunk;
            }
            fout.close();
        }

        /* B. call external CLI sorter ------------------------------ */
        auto t0 = std::chrono::steady_clock::now();
        std::string cmd =
            "./xisort --external --parallel --mem-limit=1073741824 "
            + file_in + " " + file_out;
        int ret = std::system(cmd.c_str());
        double secs = elapsed_ms(t0) / 1000.0;

        if (ret != 0) {
            std::cout << "XiSort CLI failed (code " << ret << ")\nstatus: FAIL\n";
        } else {
            std::cout << "time: " << secs << " s (external path)\nstatus: OK\n";
        }

        /* C. Spot-check first/last 1 000 doubles -------------------- */
        std::ifstream fin(file_out, std::ios::binary);
        std::vector<double> head(1000), tail(1000);
        fin.read(reinterpret_cast<char*>(head.data()),
                 head.size() * sizeof(double));
        fin.seekg(-static_cast<std::streamoff>(tail.size()*sizeof(double)),
                  std::ios::end);
        fin.read(reinterpret_cast<char*>(tail.data()),
                 tail.size() * sizeof(double));
        fin.close();

        auto slice_sorted = [](const std::vector<double>& s) {
            for (std::size_t i = 1; i < s.size(); ++i)
                if (double_to_key(s[i-1]) > double_to_key(s[i]))
                    return false;
            return true;
        };

        bool ok = slice_sorted(head) && slice_sorted(tail);
        std::cout << (ok ? "Spot-check passed" : "Spot-check failed") << '\n';
    }

    return 0;
}
