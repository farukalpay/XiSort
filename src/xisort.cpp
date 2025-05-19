// PDF alignment: XiSort v36.5 matches “Deterministic Sorting via IEEE-754…”, arXiv 2505.12345 §2–§4
// AUTHOR: FARUK ALPAY
// ORCID: 0009-0009-2207-6528

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#ifdef _OPENMP
#include <omp.h>
#endif

// Configuration for XiSort behavior
struct XiSortConfig {
    bool external;
    bool trace;
    bool parallel;
    std::size_t mem_limit;
    std::size_t buffer_elems;
    XiSortConfig()
        : external(false), trace(false), parallel(false),
          mem_limit(SIZE_MAX), buffer_elems((1ULL << 15)) {}
};

// Static atomic variables for curvature trace
static std::atomic<double> phiTrace;
static std::atomic<long long> curvCount;

// Convert double to 64-bit key implementing IEEE-754 total order
static inline uint64_t double_to_key(double x) {
    union { double d; uint64_t u; } conv;
    conv.d = x;
    uint64_t u = conv.u;
    uint64_t mask = (u >> 63) ? 0xFFFFFFFFFFFFFFFFULL : 0x8000000000000000ULL;
    return u ^ mask;
}

// Structure representing an element with sorting keys
struct XiItem {
    uint64_t key;
    uint64_t tie;
    uint64_t seq;
    double value;
};

// Atomic addition for double (since C++17 doesn't have fetch_add for double)
static inline void atomic_add_double(std::atomic<double>& atom, double val) {
    double curr = atom.load(std::memory_order_relaxed);
    double newVal;
    do {
        newVal = curr + val;
    } while(!atom.compare_exchange_weak(curr, newVal, std::memory_order_relaxed));
}

// Merge function for in-memory mergesort (stable merge)
static void merge_arrays(XiItem *arr, XiItem *aux, std::size_t left, std::size_t mid, std::size_t right, bool trace) {
    // Copy the segment [left, right] into aux
    for(std::size_t k = left; k <= right; ++k) {
        aux[k] = arr[k];
    }
    std::size_t i = left;
    std::size_t j = mid + 1;
    std::size_t k = left;
    // curvature trace local accumulators
    double phiLocal = 0.0;
    long long countLocal = 0;
    int lastSource = 0; // 0 = none, 1 = left, 2 = right
    long long segLen = 0;
    // Merge two sorted halves, track segments for curvature
    while(i <= mid && j <= right) {
        // Compare keys (with tie-breakers for stability)
        if(aux[i].key < aux[j].key || 
           (aux[i].key == aux[j].key && aux[i].tie < aux[j].tie) || 
           (aux[i].key == aux[j].key && aux[i].tie == aux[j].tie && aux[i].seq <= aux[j].seq)) {
            // Taking element from left half
            if(lastSource != 1) {
                if(segLen > 0 && trace) {
                    // finalize previous segment from other side
                    phiLocal += 1.0 / (double)segLen;
                    ++countLocal;
                }
                segLen = 0;
                lastSource = 1;
            }
            arr[k++] = aux[i++];
            ++segLen;
        } else {
            // Taking element from right half
            if(lastSource != 2) {
                if(segLen > 0 && trace) {
                    phiLocal += 1.0 / (double)segLen;
                    ++countLocal;
                }
                segLen = 0;
                lastSource = 2;
            }
            arr[k++] = aux[j++];
            ++segLen;
        }
    }
    // Append remaining elements from left side (if any)
    if(i <= mid) {
        if(lastSource != 1) {
            if(segLen > 0 && trace) {
                phiLocal += 1.0 / (double)segLen;
                ++countLocal;
            }
            segLen = 0;
            lastSource = 1;
        }
        long long remaining = (long long)(mid - i + 1);
        segLen += remaining;
        // Copy the remainder of left half
        while(i <= mid) {
            arr[k++] = aux[i++];
        }
    }
    // Append remaining elements from right side (if any)
    if(j <= right) {
        if(lastSource != 2) {
            if(segLen > 0 && trace) {
                phiLocal += 1.0 / (double)segLen;
                ++countLocal;
            }
            segLen = 0;
            lastSource = 2;
        }
        long long remaining = (long long)(right - j + 1);
        segLen += remaining;
        while(j <= right) {
            arr[k++] = aux[j++];
        }
    }
    // Finalize last segment
    if(segLen > 0 && trace) {
        phiLocal += 1.0 / (double)segLen;
        ++countLocal;
    }
    // Update global trace accumulators atomically
    if(trace) {
        atomic_add_double(phiTrace, phiLocal);
        curvCount.fetch_add(countLocal, std::memory_order_relaxed);
    }
}

// Recursive mergesort (with optional OpenMP parallel tasks)
static void merge_sort_rec(XiItem *arr, XiItem *aux, std::size_t left, std::size_t right, bool parallel, std::size_t taskThreshold, bool trace) {
    if(left >= right) {
        return;
    }
    std::size_t mid = (left + right) >> 1;
    if(parallel && (right - left + 1) >= taskThreshold) {
        // Parallelize the two recursive sorts using OpenMP tasks
        #pragma omp task shared(arr, aux)
        {
            merge_sort_rec(arr, aux, left, mid, parallel, taskThreshold, trace);
        }
        #pragma omp task shared(arr, aux)
        {
            merge_sort_rec(arr, aux, mid + 1, right, parallel, taskThreshold, trace);
        }
        #pragma omp taskwait
    } else {
        // Recurse sequentially
        merge_sort_rec(arr, aux, left, mid, parallel, taskThreshold, trace);
        merge_sort_rec(arr, aux, mid + 1, right, parallel, taskThreshold, trace);
    }
    merge_arrays(arr, aux, left, mid, right, trace);
}

// Merge two run files (external merge)
static void merge_files(const std::string &file1, const std::string &file2, const std::string &outFile, const XiSortConfig &cfg) {
    std::ifstream fin1(file1, std::ios::binary);
    std::ifstream fin2(file2, std::ios::binary);
    std::ofstream fout(outFile, std::ios::binary);
    // Buffers for reading from files
    std::size_t bufSize = cfg.buffer_elems;
    std::vector<XiItem> buffer1(bufSize);
    std::vector<XiItem> buffer2(bufSize);
    std::vector<double> outBuffer;
    outBuffer.reserve(bufSize);
    bool end1 = false, end2 = false;
    std::size_t count1 = 0, count2 = 0;
    // initial fill of both buffers
    if(fin1.good()) {
        // read up to buffer_elems doubles from file1
        std::vector<double> temp(bufSize);
        fin1.read(reinterpret_cast<char*>(temp.data()), bufSize * sizeof(double));
        std::size_t readDoubles = fin1.gcount() / sizeof(double);
        for(std::size_t i = 0; i < readDoubles; ++i) {
            buffer1[i].value = temp[i];
            buffer1[i].key = double_to_key(temp[i]);
            // for external merge, original sequence order is preserved by merge procedure, tie/seq can be zero
            buffer1[i].tie = 0;
            buffer1[i].seq = 0;
        }
        count1 = readDoubles;
        if(readDoubles < bufSize) {
            end1 = true;
        }
    } else {
        end1 = true;
        count1 = 0;
    }
    if(fin2.good()) {
        std::vector<double> temp(bufSize);
        fin2.read(reinterpret_cast<char*>(temp.data()), bufSize * sizeof(double));
        std::size_t readDoubles = fin2.gcount() / sizeof(double);
        for(std::size_t i = 0; i < readDoubles; ++i) {
            buffer2[i].value = temp[i];
            buffer2[i].key = double_to_key(temp[i]);
            buffer2[i].tie = 0;
            buffer2[i].seq = 0;
        }
        count2 = readDoubles;
        if(readDoubles < bufSize) {
            end2 = true;
        }
    } else {
        end2 = true;
        count2 = 0;
    }
    // indices into buffers
    std::size_t idx1 = 0;
    std::size_t idx2 = 0;
    double phiLocal = 0.0;
    long long countLocal = 0;
    int lastSource = 0;
    long long segLen = 0;
    // Merging loop reading from buffers and writing output
    while(true) {
        // If either buffer is empty, break to handle remainder outside
        if(idx1 >= count1 || idx2 >= count2) {
            // If one file finished and the other still has data buffered, break out to flush remainder
            break;
        }
        // Compare current elements from each buffer
        XiItem &a = buffer1[idx1];
        XiItem &b = buffer2[idx2];
        if(a.key < b.key || (a.key == b.key && a.tie < b.tie) || (a.key == b.key && a.tie == b.tie && a.seq <= b.seq)) {
            // from buffer1
            if(lastSource != 1) {
                if(segLen > 0 && cfg.trace) {
                    phiLocal += 1.0 / (double)segLen;
                    ++countLocal;
                }
                segLen = 0;
                lastSource = 1;
            }
            outBuffer.push_back(a.value);
            ++segLen;
            idx1++;
            // if buffer1 is exhausted, refill from file1
            if(idx1 >= count1 && !end1) {
                std::vector<double> temp(bufSize);
                fin1.read(reinterpret_cast<char*>(temp.data()), bufSize * sizeof(double));
                std::size_t readDoubles = fin1.gcount() / sizeof(double);
                for(std::size_t i = 0; i < readDoubles; ++i) {
                    buffer1[i].value = temp[i];
                    buffer1[i].key = double_to_key(temp[i]);
                    buffer1[i].tie = 0;
                    buffer1[i].seq = 0;
                }
                count1 = readDoubles;
                idx1 = 0;
                if(readDoubles < bufSize) {
                    end1 = true;
                }
            }
        } else {
            // from buffer2
            if(lastSource != 2) {
                if(segLen > 0 && cfg.trace) {
                    phiLocal += 1.0 / (double)segLen;
                    ++countLocal;
                }
                segLen = 0;
                lastSource = 2;
            }
            outBuffer.push_back(b.value);
            ++segLen;
            idx2++;
            if(idx2 >= count2 && !end2) {
                std::vector<double> temp(bufSize);
                fin2.read(reinterpret_cast<char*>(temp.data()), bufSize * sizeof(double));
                std::size_t readDoubles = fin2.gcount() / sizeof(double);
                for(std::size_t i = 0; i < readDoubles; ++i) {
                    buffer2[i].value = temp[i];
                    buffer2[i].key = double_to_key(temp[i]);
                    buffer2[i].tie = 0;
                    buffer2[i].seq = 0;
                }
                count2 = readDoubles;
                idx2 = 0;
                if(readDoubles < bufSize) {
                    end2 = true;
                }
            }
        }
        // Flush output buffer if full
        if(outBuffer.size() >= bufSize) {
            fout.write(reinterpret_cast<char*>(outBuffer.data()), outBuffer.size() * sizeof(double));
            outBuffer.clear();
        }
    }
    // Output any remaining elements from buffer1 or buffer2 (whichever still has data)
    if(idx1 < count1 || !end1) {
        // flush any pending segment from other source
        if(lastSource != 1) {
            if(segLen > 0 && cfg.trace) {
                phiLocal += 1.0 / (double)segLen;
                ++countLocal;
            }
            segLen = 0;
            lastSource = 1;
        }
        // Write out remaining in current buffer1
        while(idx1 < count1) {
            outBuffer.push_back(buffer1[idx1++].value);
            ++segLen;
            if(outBuffer.size() >= bufSize) {
                fout.write(reinterpret_cast<char*>(outBuffer.data()), outBuffer.size() * sizeof(double));
                outBuffer.clear();
            }
        }
        // Read and output the rest of file1 beyond current buffer
        while(!end1) {
            std::vector<double> temp(bufSize);
            fin1.read(reinterpret_cast<char*>(temp.data()), bufSize * sizeof(double));
            std::size_t readDoubles = fin1.gcount() / sizeof(double);
            if(readDoubles == 0) {
                end1 = true;
                break;
            }
            for(std::size_t t = 0; t < readDoubles; ++t) {
                outBuffer.push_back(temp[t]);
                ++segLen;
                if(outBuffer.size() >= bufSize) {
                    fout.write(reinterpret_cast<char*>(outBuffer.data()), outBuffer.size() * sizeof(double));
                    outBuffer.clear();
                }
            }
            if(readDoubles < bufSize) {
                end1 = true;
            }
        }
    }
    if(idx2 < count2 || !end2) {
        if(lastSource != 2) {
            if(segLen > 0 && cfg.trace) {
                phiLocal += 1.0 / (double)segLen;
                ++countLocal;
            }
            segLen = 0;
            lastSource = 2;
        }
        while(idx2 < count2) {
            outBuffer.push_back(buffer2[idx2++].value);
            ++segLen;
            if(outBuffer.size() >= bufSize) {
                fout.write(reinterpret_cast<char*>(outBuffer.data()), outBuffer.size() * sizeof(double));
                outBuffer.clear();
            }
        }
        while(!end2) {
            std::vector<double> temp(bufSize);
            fin2.read(reinterpret_cast<char*>(temp.data()), bufSize * sizeof(double));
            std::size_t readDoubles = fin2.gcount() / sizeof(double);
            if(readDoubles == 0) {
                end2 = true;
                break;
            }
            for(std::size_t t = 0; t < readDoubles; ++t) {
                outBuffer.push_back(temp[t]);
                ++segLen;
                if(outBuffer.size() >= bufSize) {
                    fout.write(reinterpret_cast<char*>(outBuffer.data()), outBuffer.size() * sizeof(double));
                    outBuffer.clear();
                }
            }
            if(readDoubles < bufSize) {
                end2 = true;
            }
        }
    }
    // flush any remaining output buffer
    if(!outBuffer.empty()) {
        fout.write(reinterpret_cast<char*>(outBuffer.data()), outBuffer.size() * sizeof(double));
        outBuffer.clear();
    }
    // finalize last segment in curvature trace
    if(segLen > 0 && cfg.trace) {
        phiLocal += 1.0 / (double)segLen;
        ++countLocal;
    }
    // Close files
    fin1.close();
    fin2.close();
    fout.close();
    // update curvature trace
    if(cfg.trace) {
        atomic_add_double(phiTrace, phiLocal);
        curvCount.fetch_add(countLocal, std::memory_order_relaxed);
    }
}

// Main sorting function
void xi_sort(double *data, uint64_t n, const XiSortConfig &cfg) {
    if(n == 0) {
        return;
    }
    // Initialize trace accumulators
    if(cfg.trace) {
        phiTrace.store(0.0, std::memory_order_relaxed);
        curvCount.store(0, std::memory_order_relaxed);
    }
    if(!cfg.external && n * sizeof(double) <= cfg.mem_limit) {
        // In-memory sorting
        // Allocate structures for keys and perform mergesort
        std::size_t N = (std::size_t)n;
        XiItem *arr = new XiItem[N];
        XiItem *aux = new XiItem[N];
        for(std::size_t i = 0; i < N; ++i) {
            arr[i].value = data[i];
            arr[i].key = double_to_key(data[i]);
            arr[i].tie = (uint64_t)i;
            arr[i].seq = (uint64_t)i;
        }
        // Determine task size threshold for parallel mergesort
        std::size_t taskThreshold = 1ULL << 15; // e.g., 32768
        if(cfg.parallel) {
            // Parallel mergesort using OpenMP
            #pragma omp parallel
            {
                #pragma omp single nowait
                {
                    merge_sort_rec(arr, aux, 0, N - 1, true, taskThreshold, cfg.trace);
                }
            }
        } else {
            merge_sort_rec(arr, aux, 0, N - 1, false, taskThreshold, cfg.trace);
        }
        // Copy sorted values back to original array
        for(std::size_t i = 0; i < N; ++i) {
            data[i] = arr[i].value;
        }
        delete [] arr;
        delete [] aux;
    } else {
        // External sorting
        std::vector<std::string> runs;
        runs.reserve((n / (cfg.mem_limit/sizeof(double))) + 1);
        std::size_t N = (std::size_t)n;
        std::size_t maxElems = cfg.mem_limit / sizeof(double);
        if(maxElems < 1) maxElems = 1;
        // Create initial sorted runs from input data
        std::size_t offset = 0;
        int runCount = 0;
        while(offset < N) {
            std::size_t chunkSize = (N - offset < maxElems) ? (N - offset) : maxElems;
            // Allocate chunk array
            XiItem *arr = new XiItem[chunkSize];
            XiItem *aux = new XiItem[chunkSize];
            for(std::size_t i = 0; i < chunkSize; ++i) {
                arr[i].value = data[offset + i];
                arr[i].key = double_to_key(data[offset + i]);
                arr[i].tie = (uint64_t)(offset + i);
                arr[i].seq = (uint64_t)(offset + i);
            }
            // Sort this run (using single-threaded mergesort for simplicity)
            merge_sort_rec(arr, aux, 0, chunkSize - 1, false, 1ULL<<15, cfg.trace);
            // Write this run to file
            char filename[64];
            std::sprintf(filename, "xisort_run_%d.bin", runCount++);
            std::ofstream fout(filename, std::ios::binary);
            for(std::size_t i = 0; i < chunkSize; ++i) {
                double val = arr[i].value;
                fout.write(reinterpret_cast<char*>(&val), sizeof(double));
            }
            fout.close();
            runs.push_back(std::string(filename));
            delete [] arr;
            delete [] aux;
            offset += chunkSize;
        }
        // Iteratively merge runs until one sorted run remains
        while(runs.size() > 1) {
            std::vector<std::string> newRuns;
            newRuns.reserve((runs.size() / 2) + 1);
            for(std::size_t i = 0; i + 1 < runs.size(); i += 2) {
                std::string fileA = runs[i];
                std::string fileB = runs[i+1];
                char outName[64];
                std::sprintf(outName, "xisort_run_%d.bin", runCount++);
                // Merge fileA and fileB into outName
                merge_files(fileA, fileB, outName, cfg);
                // Remove merged input files
                std::remove(fileA.c_str());
                std::remove(fileB.c_str());
                newRuns.push_back(std::string(outName));
            }
            if(runs.size() % 2 == 1) {
                // If odd number of runs, carry the last one to next round
                newRuns.push_back(runs.back());
            }
            runs.swap(newRuns);
        }
        // Now runs[0] is the final sorted file
        if(!runs.empty()) {
            // Load final sorted data back into memory
            std::ifstream fin(runs[0], std::ios::binary);
            std::size_t index = 0;
            const std::size_t bufElems = cfg.buffer_elems;
            std::vector<double> buffer(bufElems);
            while(index < (std::size_t)n) {
                std::size_t toRead = ((std::size_t)n - index < bufElems) ? (std::size_t)n - index : bufElems;
                fin.read(reinterpret_cast<char*>(buffer.data()), toRead * sizeof(double));
                std::size_t got = fin.gcount() / sizeof(double);
                for(std::size_t j = 0; j < got; ++j) {
                    data[index++] = buffer[j];
                }
                if(got < toRead) {
                    break;
                }
            }
            fin.close();
            // Remove final run file
            std::remove(runs[0].c_str());
        }
    }
}
