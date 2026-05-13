#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Embedded {

    enum nvme_opcode {
        NVME_CMD_FLUSH                 = 0x00,
        NVME_CMD_WRITE                 = 0x01,
        NVME_CMD_READ                  = 0x02,
        NVME_CMD_FTL_MAP               = 0x03,
    };

    class Proj2 {
    public:
        Proj2() : fd_(-1), nsid_(1) {}
        int Open(const std::string &dev, int nsid = 1);
        int WriteBlocks(uint64_t slba, uint16_t nlb, const void* buf, bool fua);
        int ReadBlocks (uint64_t slba, uint16_t nlb, void* buf);
        int Flush();
        int ShowFtlInfo();

    private:
        int fd_;
        int nsid_;
        int io_passthru(uint8_t opcode,
                        uint64_t slba, uint16_t nlb,
                        void* buf, uint32_t bytes,
                        bool fua);
    };
}

