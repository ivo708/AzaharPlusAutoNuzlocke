#pragma once
#include <memory>
#include <string>
#include <vector>
#include "core/file_sys/disk_archive.h"

namespace FileSys {

class XoredDiskFile : public DiskFile {
public:
    XoredDiskFile(FileUtil::IOFile&& file, const Mode& mode,
                  std::unique_ptr<DelayGenerator> delay_generator, const std::string& key)
        : DiskFile(std::move(file), mode, std::move(delay_generator)),
          key_bytes_(key.begin(), key.end()), key_len_(key_bytes_.size()) {}

    ResultVal<std::size_t> Read(u64 offset, size_t length, u8* buffer) const override {
        auto result = DiskFile::Read(offset, length, buffer);
        if (result) {
            std::size_t read_bytes = *result;
            XorBuffer(buffer, read_bytes);
        }
        return result;
    }

    ResultVal<std::size_t> Write(u64 offset, size_t length, bool flush, bool update_timestamp,
                                 const u8* buffer) override {
        std::vector<u8> tmp(buffer, buffer + length);
        XorBuffer(tmp.data(), tmp.size());
        return DiskFile::Write(offset, length, flush, update_timestamp, tmp.data());
    }

private:
    void XorBuffer(u8* buf, size_t len) const {
        for (size_t i = 0; i < len; ++i) {
            buf[i] ^= key_bytes_[i % key_len_];
        }
    }

    std::vector<u8> key_bytes_;
    size_t key_len_;
};

} // namespace FileSys
