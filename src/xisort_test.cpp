// AUTHOR: FARUK ALPAY
// ORCID: 0009-0009-2207-6528
/*  xisort_test.cpp  – End-to-end validation for XiSort v36.5
    Builds on xisort.cpp (must be in same directory)
*/

/*
How to run?
g++ -std=c++17 -O3 -fopenmp xisort_test.cpp -o xisort_tests \
    -DXISORT_CURVATURE_TRACE=1

./xisort_tests          # full set
./xisort_tests --small  # fast/minimal source code
*/

#include <cmath>
#include <random>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <filesystem>
#include "xisort.cpp"          

// ---------- helpers -------------------------------------------------
static inline bool is_sorted_total(const std::vector<double>& v)
{
    for (std::size_t i = 1; i < v.size(); ++i)
        if (double_to_key(v[i-1]) > double_to_key(v[i]))
            return false;
    return true;
}

static void print_sample(const std::vector<double>& v, std::size_t k=5)
{
    std::cout.setf(std::ios::scientific);
    std::cout << "first " << k << ": ";
    for (std::size_t i=0;i<k && i<v.size();++i)   std::cout << v[i] << " ";
    std::cout << "\nlast  " << k << ": ";
    for (std::size_t i=k;i>0 && i<=v.size();--i)  std::cout << v[v.size()-i] << " ";
    std::cout << "\n";
    std::cout.unsetf(std::ios::scientific);
}

static double elapsed_ms(const std::chrono::steady_clock::time_point& t0)
{
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
}

// ---------- constants / flags ---------------------------------------
constexpr std::size_t   INMEM_COUNT_BIG   = 100'000'000;     //  ~0.8 GB
constexpr std::size_t   INMEM_COUNT_SMALL = 10'000'000;      //  ~80 MB
constexpr std::uint64_t EXTERNAL_SIZE_GB  = 100;             // 100 GB file
constexpr std::size_t   BUFFER_ELEMS      = 1ULL<<15;        // 32 768 doubles

// ---------- main tests ----------------------------------------------
int main(int argc,char**argv)
{
    bool small = (argc>=2 && std::string(argv[1])=="--small");

    std::cout << "===== XiSort validation suite =====\n";

    // ---------------------------------------------------------------
    // Test-0  (special IEEE values)
    // ---------------------------------------------------------------
    {
        std::cout << "\n[Test-0] special IEEE-754 values\n";
        std::vector<double> v = {
            5.0, -0.0, 0.0,
            std::numeric_limits<double>::quiet_NaN(),
            -5.0, std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()
        };
        XiSortConfig cfg;
        cfg.trace = true;
        xi_sort(v.data(), static_cast<uint64_t>(v.size()), cfg);
        print_sample(v, v.size());
        std::cout << (is_sorted_total(v) ? "status: OK\n" : "status: FAIL\n");
    }

    // ---------------------------------------------------------------
    // Test-1  (duplicate-heavy array)
    // ---------------------------------------------------------------
    {
        std::cout << "\n[Test-1] duplicate-heavy distribution\n";
        const std::size_t N = small ? 1'000'000 : 10'000'000;
        std::vector<double> v(N);
        // generate discrete distribution: 70% same value, 30% random small set
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<int> bucket(0,9);
        for (auto&x: v) {
            int b = bucket(rng);
            x = (b==0) ? 0.123456789 : static_cast<double>(b);
        }
        XiSortConfig cfg;
        cfg.trace = false;
        auto t0 = std::chrono::steady_clock::now();
        xi_sort(v.data(), static_cast<uint64_t>(v.size()), cfg);
        std::cout << "time: " << elapsed_ms(t0) << " ms\n";
        std::cout << (is_sorted_total(v) ? "status: OK\n" : "status: FAIL\n");
    }

    // ---------------------------------------------------------------
    // Test-2  (in-memory 100 M std-normal)
    // ---------------------------------------------------------------
    {
        std::cout << "\n[Test-2] in-memory large sort\n";
        const std::size_t N = small ? INMEM_COUNT_SMALL : INMEM_COUNT_BIG;
        std::vector<double> v(N);
        std::mt19937_64 rng(1);
        std::normal_distribution<double> gauss(0.0,1.0);
        for(auto& x:v) x = gauss(rng);
        XiSortConfig cfg;
        cfg.trace = false;
        cfg.parallel = true;
        auto t0 = std::chrono::steady_clock::now();
        xi_sort(v.data(), static_cast<uint64_t>(v.size()), cfg);
        std::cout << "time: " << elapsed_ms(t0)/1000.0 << " s\n";
        std::cout << (is_sorted_total(v) ? "status: OK\n" : "status: FAIL\n");
        print_sample(v);
    }

    // ---------------------------------------------------------------
    // Test-3  (external 100 GB)
    // ---------------------------------------------------------------
    {
        std::cout << "\n[Test-3] external " << EXTERNAL_SIZE_GB
                  << " GB dataset (may take hours)…\n";
        std::uint64_t elems = (EXTERNAL_SIZE_GB*1024ULL*1024ULL*1024ULL)/sizeof(double);
        if (small) {
            elems = 1'000'000;  // quick smoke-test
            std::cout << "  (--small enabled → only " << elems << " elems)\n";
        }
        // allocate buffer to stream-generate file
        const std::string file = "xisort_ext_input.bin";
        {
            std::ofstream fout(file, std::ios::binary);
            std::mt19937_64 rng(777);
            std::uniform_real_distribution<double> dist(-1.0,1.0);
            std::vector<double> buf(BUFFER_ELEMS);
            std::uint64_t written = 0;
            while (written < elems) {
                std::size_t chunk = std::min<std::uint64_t>(BUFFER_ELEMS, elems - written);
                for (std::size_t i=0;i<chunk;++i) buf[i]=dist(rng);
                fout.write(reinterpret_cast<char*>(buf.data()), chunk*sizeof(double));
                written += chunk;
            }
        }
        // mmap or load? We'll mmap by reading in place into RAM (requires large mem),
        // instead we'll chunk-load into vector for demo <- just smoke-test path
        std::vector<double> v(elems);
        {
            std::ifstream fin(file, std::ios::binary);
            fin.read(reinterpret_cast<char*>(v.data()), elems*sizeof(double));
        }
        XiSortConfig cfg;
        cfg.external = true;
        cfg.parallel = true;
        cfg.trace = false;
        cfg.mem_limit = 256ULL*1024*1024;  // 256 MB run size
        cfg.buffer_elems = BUFFER_ELEMS;
        auto t0 = std::chrono::steady_clock::now();
        xi_sort(v.data(), elems, cfg);
        std::cout << "time: " << elapsed_ms(t0)/1000.0 << " s (external path)\n";
        std::cout << (is_sorted_total(v) ? "status: OK\n" : "status: FAIL\n");
        // cleanup
        std::filesystem::remove(file);
    }

    std::cout << "\nAll tests completed.\n";
    #if XISORT_CURVATURE_TRACE
    std::cout << "Φ_total (accumulated) = " << phiTrace.load()
              << "  (#segments=" << curvCount.load() << ")\n";
    #endif
    return 0;
}
