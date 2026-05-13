#include "nvme_passthru.h"
#include <vector>
#include <unordered_map>
#include <random>
#include <cstring>
#include <string>
#include <iostream>
#include <chrono>

using namespace Embedded;

static inline uint8_t pattern_for(uint64_t lba, uint64_t gen) {
    // Simple deterministic pattern
    return static_cast<uint8_t>((lba * 37 + gen * 13) & 0xFF);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " /dev/nvmeXnY <seq|rand> [ops=10000] [span_lbas=1048576] [nsid=1]\n";
        return 1;
    }
    const char* dev = argv[1];
    std::string mode = argv[2];
    if (mode != "seq" && mode != "rand") {
        std::cerr << "[ERROR] Invalid mode '" << mode << "'. Use 'seq' or 'rand'.\n";
        return 1;
    }
    uint32_t ops   = (argc > 3) ? std::stoul(argv[3]) : 10000;
    uint64_t span  = (argc > 4) ? std::stoull(argv[4]) : 1048576ull;  // 4GB span
    int nsid       = (argc > 5) ? std::stoi(argv[5]) : 1;

    Proj2 nvme;
    if (nvme.Open(dev, nsid) != 0) {
        std::cerr << "Cannot open " << dev << "\n";
        return 2;
    }

    std::mt19937_64 rng(123456789ULL);
    std::uniform_int_distribution<uint64_t> dist(0, span/4 - 1);

    std::vector<uint8_t> wbuf(16384), rbuf(16384);
    std::unordered_map<uint64_t, uint8_t> latest;
    latest.reserve(ops);
        
    int ret = nvme.ShowFtlInfo();
    if (ret) {
        std::cerr << "[ERROR] FTL information is invisible " << " ret=" << ret << "\n";
        return 2;
    }

    auto t_start = std::chrono::high_resolution_clock::now();

    // (1) Writes
    for (uint32_t i = 0; i < ops; i++) {
        uint64_t lba = (mode == "seq") ? (i*4) : (dist(rng))*4;
        uint8_t p = pattern_for(lba, i);
        std::memset(wbuf.data(), p, wbuf.size());

        int ret = nvme.WriteBlocks(lba, 4, wbuf.data(), /*FUA*/false);
        if (ret) {
            std::cerr << "[ERROR] Write failed @ LBA " << lba << " ret=" << ret << "\n";
            return 3;
        }
        latest[lba] = p;
    }
    //std::cout << "[HOST] finished writes\n";

    // (2) Cache thrashing 
    for (uint32_t i = 0; i < 16384; i++) {
        uint64_t lba = span + i*4;
        std::memset(wbuf.data(), static_cast<int>(i & 0xFF), wbuf.size());
        nvme.WriteBlocks(lba, 4, wbuf.data(), false);
    }
    //std::cout << "[HOST] finished thrash\n";

    // (3) Verify
    size_t ok = 0, fail = 0;
    //std::cout << "[HOST] start verify, latest size=" << latest.size() << "\n";
    for (const auto& kv : latest) {
        uint64_t lba = kv.first;
        uint8_t exp = kv.second;
        std::memset(rbuf.data(), 0, rbuf.size());
        int ret = nvme.ReadBlocks(lba, 4, rbuf.data());
        if (ret) {
            std::cerr << "[ERROR] Read failed @ LBA " << lba << " ret=" << ret << "\n";
            return 4;
        }
        bool good = true;
        for (auto c : rbuf) { if (c != exp) { good = false; break; } }
        if (good) ++ok; else ++fail;
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::cout << "-----------------------------------------------\n";
    std::cout << " Cosmos+ OpenSSD FTL Benchmark (" << mode << ")\n";
    std::cout << "-----------------------------------------------\n";
    std::cout << " mode=" << mode << " ops=" << ops << " span=" << span << " (16KB units)\n";
    std::cout << " result: OK=" << ok << " FAIL=" << fail << "\n";
    std::cout << " elapsed: " << ms << " ms  (" 
              << (ops * 2.0 / (ms / 1000.0)) << " IOPS est. for R/W)\n";
    std::cout << "-----------------------------------------------\n";
    
    ret = nvme.ShowFtlInfo();
    if (ret) {
        std::cerr << "[ERROR] FTL information is invisible " << " ret=" << ret << "\n";
        return 2;
    }

    return (fail == 0) ? 0 : 5;
}

