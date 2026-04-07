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

const unsigned int SECTOR_SIZE = 512;
const unsigned int TIMEOUT_MS = 5000; // some big thing...

int Embedded::Proj1::Open(const std::string &dev) {
    int err;
    err = open(dev.c_str(), O_RDONLY);
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
        size_t chunk = min((size_t)MAX_BUFLEN, total - offset);
        size_t aligned = (chunk + SECTOR_SIZE - 1) / SECTOR_SIZE * SECTOR_SIZE;
        __u32 nlb = (__u32) (aligned / SECTOR_SIZE - 1);

        void *tmp = nullptr;
        if(posix_memalign(&tmp, PAGE_SIZE, aligned) != 0) return - ENOMEM;
        memset(tmp, 0, aligned);
        memcpy (tmp, buf.data() + offset, chunk);

        __u32 slba_low = (__u32)(slba & 0xFFFFFFFF);
        __u32 slba_high = (__u32)(slba >> 32);

        int ret = nvme_passthru(
            0x01, // read opcode
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

        
        free(tmp);

        slba += aligned / SECTOR_SIZE;
        offset += chunk;

    }
    return 0; 


    return -1; // placeholder
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
    

    
    // 
    while(offset < size) {
        size_t chunk = min((size_t)MAX_BUFLEN, size - offset);
        size_t aligned = (chunk + SECTOR_SIZE - 1) / SECTOR_SIZE * SECTOR_SIZE;
        __u32 nlb = (__u32) (aligned / SECTOR_SIZE - 1);

        void *tmp = nullptr;
        if(posix_memalign(&tmp, PAGE_SIZE, aligned) != 0) return - ENOMEM;
        memset(tmp, 0, aligned);
        __u32 slba_low = (__u32)(slba & 0xFFFFFFFF);
        __u32 slba_high = (__u32)(slba >> 32);

        int ret = nvme_passthru(
            0x02, // read opcode
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
        offset += chunk;

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
    void *buf, // why not  u64? Use pointer t to validate the address.
    __u32 length
)
{
    

    struct nvme_passthru_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.opcode = opcode;
    cmd.nsid = nsid;
    cmd.cdw10 = cdw10;
    cmd.cdw11 = cdw11;
    cmd.cdw12 = cdw12;
    cmd.addr = (__u64)(uintptr_t)buf;
    cmd.data_len = length;
    cmd.timeout_ms = TIMEOUT_MS;

    int ret = ioctl(fd_, NVME_IOCTL_IO_CMD, &cmd);

    

    return (ret!=0) ? -errno : 0; // error? return some negative error code.


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

