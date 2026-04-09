#include "nvme_passthru.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/nvme_ioctl.h>
#include <fcntl.h>
#include <cstring>
#include <cstdint>
#include <stdlib.h>
#include <vector>
#include <iostream>
#include <cstdio>
#include <inttypes.h>




using namespace std;

const unsigned int PAGE_SIZE = 4096;
const unsigned int MAX_BUFLEN = 16*1024*1024; /* Maximum transfer size (can be adjusted if needed) */
const unsigned int NSID = 1; /* NSID can be checked using 'sudo nvme list' */

const unsigned int MAX_TRANSFER = 4* 1024; // 4KB.
const unsigned int SECTOR_SIZE = 512;
const unsigned int TIMEOUT_MS = 5000; // some big thing...

int Embedded::Proj1::Open(const std::string &dev) {
    int err;
    err = open(dev.c_str(), O_RDWR); // not RDonly?
    if (err < 0)
        return -1;
    fd_ = err;

    struct stat nvme_stat;
    err = fstat(fd_, &nvme_stat);
    if (err < 0)
        return -1;
    if (!S_ISCHR(nvme_stat.st_mode) && !S_ISBLK(nvme_stat.st_mode))
        return -1;

    return 0;
}

int Embedded::Proj1::ImageWrite(const std::vector<uint8_t> &buf) {
    if (buf.empty()) return -EINVAL;
    if (buf.size() > MAX_BUFLEN) {
        cerr << "[ERROR] Image size exceeds the maximum transfer size limit." << endl;
        return -EINVAL;
    }

    /* ------------------------------------------------------------------
     * TODO: Implement this function.
     * 
     * Return 0 on success, or a negative error code on failure.
     * ------------------------------------------------------------------ */

    size_t total = buf.size();

    size_t offset = 0;
    __u64 slba = 0;
    

    
    // 
    while(offset < total) {
        size_t chunk = min((size_t)MAX_TRANSFER, total - offset);
        size_t aligned = (chunk + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
        __u32 nlb = (__u32) (aligned / SECTOR_SIZE - 1);

        void *tmp = nullptr;
        if(posix_memalign(&tmp, PAGE_SIZE, aligned) != 0) return - ENOMEM;
        memset(tmp, 0, aligned);
        memcpy (tmp, buf.data() + offset, chunk);

        __u32 slba_low = (__u32)(slba & 0xFFFFFFFF);
        __u32 slba_high = (__u32)(slba >> 32);

        int ret = nvme_passthru(
            NVME_CMD_WRITE, // write opcode
            NSID,
            slba_low, // cwd 10
            slba_high, // cwd 11
            nlb, // cdw 12
            tmp, // *buf
            (__u32)aligned // length
        );

        if(ret < 0) {
            free(tmp);
            return ret;
        }

        
        free(tmp);

        slba += aligned / SECTOR_SIZE;
        offset += aligned;

    }
    return 0; 
}

int Embedded::Proj1::ImageRead(std::vector<uint8_t> &buf, size_t size) {
    if (size == 0) return -EINVAL;
    if (size > MAX_BUFLEN) {
        cerr << "[ERROR] Requested read size exceeds the maximum transfer size limit." << endl;
        return -EINVAL;
    }


    /* ------------------------------------------------------------------
     * TODO: Implement this function.
     * 
     * Return 0 on success, or a negative error code on failure.
     * ------------------------------------------------------------------ */

    buf.resize(size);

    size_t offset = 0;
    __u64 slba = 0;
    

    /* ------------------------------------------------------------------
     * How to align chunk?
     * 
     * Think about saving 4000byte data. you have to align it to 
     * PAGE SIZE = 4096. 96 byte will remain as empty. 
     * So we have to align at "올림", use (N + PAGE SIZE -1) * PAGE SIZE.
     * 
     * Think about sending "chunk = 454650"
     *  Align = (454650 + 4095) / 4096 * 4096 = 454656 (올림)
     *  SLBA = 0.
     *  NLB = 454656 / SECTOR SIZE  -1 = 887
     *  => send done
     * 
     *  SLBA = 888
     *  ...
     * ------------------------------------------------------------------ */
    
    // 자르기.
    while(offset < size) {
        size_t chunk = min((size_t)MAX_TRANSFER, size - offset);
        size_t aligned = (chunk + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;

        // nlb: Number of Logical Block (0 based.)
        __u32 nlb = (__u32)(aligned / SECTOR_SIZE - 1);

        void *tmp = nullptr;
        if(posix_memalign(&tmp, PAGE_SIZE, aligned) != 0) return - ENOMEM;
        memset(tmp, 0, aligned);

        // slba : starting logical block address


        __u32 slba_low = (__u32)(slba & 0xFFFFFFFF);
        __u32 slba_high = (__u32)(slba >> 32);

        int ret = nvme_passthru(
            NVME_CMD_READ, // read opcode
            NSID,
            slba_low, // cwd 10
            slba_high, // cwd 11
            nlb,
            tmp,
            (__u32)aligned
        );

        if(ret < 0) {
            free(tmp);
            return ret;
        }

        memcpy (buf.data() + offset, tmp, chunk);
        free(tmp);

        slba += aligned / SECTOR_SIZE;
        offset += aligned;

    }
    return 0; 
}

int Embedded::Proj1::Hello() {
    /* ------------------------------------------------------------------
     * TODO: Implement this function.
     * 
     * Return 0 on success, or a negative error code on failure.
     * ------------------------------------------------------------------ */

    return -1; // placeholder
}

// IOCTL call -> 
int Embedded::Proj1::nvme_passthru(
    __u8 opcode,
    __u32 nsid,
    __u32 cdw10,
    __u32 cdw11,
    __u32 cdw12,
    void *buf,
    __u32 length
)
{
    struct nvme_user_io io;
    memset(&io, 0, sizeof(io));

    io.opcode   = opcode;
    io.addr     = (__u64)(uintptr_t)buf;
    io.slba     = ((__u64)cdw11 << 32) | cdw10;  // slba 재조합
    io.nblocks  = (__u16)cdw12;                   // nlb

    int ret = ioctl(fd_, NVME_IOCTL_SUBMIT_IO, &io); // why use user IO

    if (ret != 0) {
        cerr << "[ioctl error] ret=" << ret
             << " errno=" << errno
             << " (" << strerror(errno) << ")" << endl;
        return -errno;
    }
    return 0;
/* ------------------------------------------------------------------
     * TODO: Implement this function.
     * This function should serve as the low-level interface for issuing
     * passthru NVMe commands. Make sure to include appropriate arguments
     * (e.g., opcode, namespace ID, command dwords, buffer pointer, length,
     * and result field) so that higher-level methods (ImageWrite, ImageRead,
     * Hello) can be implemented using this helper.
     *
     * Hint: refer to the Linux nvme_ioctl.h header and the struct nvme_passthru_cmd definition.
     * - Link: https://elixir.bootlin.com/linux/v5.15/source/include/uapi/linux/nvme_ioctl.h
     * ------------------------------------------------------------------ */
}

