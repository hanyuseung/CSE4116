#include "kv_fs_backend.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Embedded;

FsBackend::FsBackend()
    : values_fd_(-1), index_fd_(-1), put_count_(0) {}

FsBackend::~FsBackend() {
    if (values_fd_ >= 0) ::close(values_fd_);
    if (index_fd_  >= 0) ::close(index_fd_);
}

static int ensure_dir(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        return -1;
    }
    if (::mkdir(path.c_str(), 0755) < 0) {
        return -1;
    }
    return 0;
}

int FsBackend::load_index() {
    if (index_fd_ < 0) return -1;

    if (::lseek(index_fd_, 0, SEEK_SET) < 0) {
        return -1;
    }

    index_.clear();

    while (true) {
        uint32_t key;
        uint64_t offset;
        uint32_t length;

        ssize_t r = ::read(index_fd_, &key, sizeof(key));
        if (r == 0) {
            // EOF
            break;
        }
        if (r != (ssize_t)sizeof(key)) {
            return -1;
        }

        r = ::read(index_fd_, &offset, sizeof(offset));
        if (r != (ssize_t)sizeof(offset)) {
            return -1;
        }

        r = ::read(index_fd_, &length, sizeof(length));
        if (r != (ssize_t)sizeof(length)) {
            return -1;
        }

        IndexEntry e{};
        e.offset = offset;
        e.length = length;
        index_[key] = e;
    }

    if (::lseek(index_fd_, 0, SEEK_END) < 0) {
        return -1;
    }

    return 0;
}

int FsBackend::Open(const std::string& root_dir) {
    if (ensure_dir(root_dir) != 0) {
        std::perror("ensure_dir(root_dir)");
        return -1;
    }

    std::string values_path = root_dir + "/values.log";
    std::string index_path  = root_dir + "/index.log";

    values_fd_ = ::open(values_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (values_fd_ < 0) {
        std::perror("open(values.log)");
        return -1;
    }

    index_fd_ = ::open(index_path.c_str(), O_CREAT | O_RDWR, 0644);
    if (index_fd_ < 0) {
        std::perror("open(index.log)");
        ::close(values_fd_);
        values_fd_ = -1;
        return -1;
    }

    if (load_index() != 0) {
        std::fprintf(stderr, "[WARN] Failed to load index.log, starting with empty index\n");
        index_.clear();
        if (::lseek(index_fd_, 0, SEEK_END) < 0) {
            return -1;
        }
    }

    return 0;
}

int FsBackend::KeyValuePut(const std::string& key, const std::string& value) {
    if (key.size() != KEY_SIZE) {
        return -EINVAL;
    }
    if (values_fd_ < 0 || index_fd_ < 0) {
        return -EIO;
    }

    uint32_t key_field;
    std::memcpy(&key_field, key.data(), KEY_SIZE);

    off_t off = ::lseek(values_fd_, 0, SEEK_END);
    if (off < 0) {
        return -EIO;
    }

    const char* data = value.data();
    size_t len = value.size();

    ssize_t written = ::write(values_fd_, data, len);
    if (written != (ssize_t)len) {
        return -EIO;
    }

    ++put_count_;
    bool do_fsync = (KV_FSYNC_PERIOD <= 1) || (put_count_ % KV_FSYNC_PERIOD == 0);

    // value log fsync -> value persistence
    if (do_fsync) {
        if (::fsync(values_fd_) < 0) {
            return -EIO;
        }
    }

    // index.log append
    uint64_t offset64 = static_cast<uint64_t>(off);
    uint32_t length32 = static_cast<uint32_t>(len);

    if (::write(index_fd_, &key_field, sizeof(key_field)) != (ssize_t)sizeof(key_field)) {
        return -EIO;
    }
    if (::write(index_fd_, &offset64, sizeof(offset64)) != (ssize_t)sizeof(offset64)) {
        return -EIO;
    }
    if (::write(index_fd_, &length32, sizeof(length32)) != (ssize_t)sizeof(length32)) {
        return -EIO;
    }

    // index fsync -> index persistence
    if (do_fsync) {
        if (::fsync(index_fd_) < 0) {
            return -EIO;
        }
    }

    IndexEntry e{};
    e.offset = offset64;
    e.length = length32;
    index_[key_field] = e;

    return 0;
}

int FsBackend::KeyValueGet(const std::string& key, std::string& value) {
    if (key.size() != KEY_SIZE) {
        return -EINVAL;
    }
    if (values_fd_ < 0 || index_fd_ < 0) {
        return -EIO;
    }

    uint32_t key_field;
    std::memcpy(&key_field, key.data(), KEY_SIZE);

    auto it = index_.find(key_field);
    if (it == index_.end()) {
        return -ENOSUCHKEY;
    }

    const IndexEntry& e = it->second;
    if (e.length == 0) {
        value.clear();
        return 0;
    }

    if (e.length > MAX_BUFLEN) {
        return -EIO;
    }

    std::string buf;
    buf.resize(e.length);

    ssize_t r = ::pread(values_fd_, &buf[0], e.length, static_cast<off_t>(e.offset));
    if (r != (ssize_t)e.length) {
        return -EIO;
    }

    value.swap(buf);

    return static_cast<int>(e.length);
}

