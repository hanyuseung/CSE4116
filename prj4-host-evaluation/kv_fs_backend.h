#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#define ENOSUCHKEY 0x7C1

#ifndef KV_FSYNC_PERIOD
#define KV_FSYNC_PERIOD 1000   // default policy: call fsync per single PUT
#endif

const unsigned int KEY_SIZE   = 4;
const unsigned int PAGE_SIZE  = 4096;
const unsigned int MAX_BUFLEN = 16384;

namespace Embedded {

    struct IndexEntry {
        uint64_t offset;  // value log address
        uint32_t length;  // value length
    };

    class FsBackend {
    public:
        FsBackend();
        ~FsBackend();

        int Open(const std::string& root_dir);

        int KeyValuePut(const std::string& key, const std::string& value);
        int KeyValueGet(const std::string& key, std::string& value);

    private:
        int values_fd_;  // values.log
        int index_fd_;   // index.log

        std::unordered_map<uint32_t, IndexEntry> index_;
        uint64_t put_count_;

        int load_index(); 
    };
}

