#include "nvme_passthru.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace Embedded;

static inline uint32_t div_round_up(uint32_t x, uint32_t y){ return (x + y - 1) / y; }

int Proj2::Open(const std::string &dev, int nsid) {
    nsid_ = nsid;
    int fd = open(dev.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st{};
    if (fstat(fd, &st) < 0) { close(fd); return -1; }
    if (!S_ISCHR(st.st_mode) && !S_ISBLK(st.st_mode)) { close(fd); return -1; }
    fd_ = fd;
    return 0;
}

int Proj2::io_passthru(uint8_t opcode,
                       uint64_t slba, uint16_t nlb,
                       void* buf, uint32_t bytes,
                       bool fua)
{
    struct nvme_passthru_cmd cmd{};
    cmd.opcode = opcode;
    cmd.nsid   = nsid_;
    cmd.cdw10  = static_cast<uint32_t>(slba & 0xFFFFFFFFULL);      // SLBA[31:0]
    cmd.cdw11  = static_cast<uint32_t>(slba >> 32);                 // SLBA[63:32]
    cmd.cdw12  = (nlb ? (uint32_t)(nlb - 1) : 0);                   // NLB(0-based)
    if (fua) cmd.cdw13 = (1u << 14);                                // FUA bit
    cmd.addr   = reinterpret_cast<uint64_t>(buf);
    cmd.data_len = bytes;
    cmd.timeout_ms = 0; 

    if (opcode == NVME_CMD_FLUSH) {
        cmd.addr = 0;
        cmd.data_len = 0;
        cmd.cdw10 = cmd.cdw11 = cmd.cdw12 = cmd.cdw13 = 0;
    }

    int ret = ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd);
    return (ret < 0) ? -errno : 0;
}

int Proj2::WriteBlocks(uint64_t slba, uint16_t nlb, const void* pbuf, bool fua) {
    if (nlb == 0) return -EINVAL;
    void* buf = const_cast<void*>(pbuf);
    uint32_t bytes = nlb * 4096u;
    return io_passthru(NVME_CMD_WRITE, slba, nlb, buf, bytes, fua);
}

int Proj2::ReadBlocks(uint64_t slba, uint16_t nlb, void* buf) {
    if (nlb == 0) return -EINVAL;
    uint32_t bytes = nlb * 4096u;
    return io_passthru(NVME_CMD_READ, slba, nlb, buf, bytes, false);
}

int Proj2::Flush() {
    return io_passthru(NVME_CMD_FLUSH, 0, 0, nullptr, 0, false);
}

int Proj2::ShowFtlInfo() {
    return io_passthru(NVME_CMD_FTL_MAP, 0, 0, nullptr, 0, false);
}

