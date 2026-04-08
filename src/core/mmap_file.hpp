#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <string>

// ------------------------------------------------------------
//  MmapFile
//
//  Cross-platform memory-mapped read-only file wrapper.
//  POSIX (macOS / Linux): mmap / munmap
//  Windows: CreateFileMapping / MapViewOfFile
//
//  Usage:
//      MmapFile f("path/to/logfile");
//      const char* data = f.data();
//      size_t      size = f.size();
// ------------------------------------------------------------

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class MmapFile {
public:
    explicit MmapFile(const std::string& path) {
        file_ = CreateFileA(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );
        if (file_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("MmapFile: cannot open file: " + path);

        LARGE_INTEGER li{};
        if (!GetFileSizeEx(file_, &li))
            throw std::runtime_error("MmapFile: cannot get file size: " + path);
        size_ = static_cast<size_t>(li.QuadPart);

        if (size_ == 0) {
            data_ = nullptr;
            return;
        }

        mapping_ = CreateFileMappingA(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_)
            throw std::runtime_error("MmapFile: CreateFileMapping failed: " + path);

        data_ = static_cast<const char*>(
            MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0)
        );
        if (!data_)
            throw std::runtime_error("MmapFile: MapViewOfFile failed: " + path);
    }

    ~MmapFile() {
        if (data_)    UnmapViewOfFile(data_);
        if (mapping_) CloseHandle(mapping_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    }

    MmapFile(const MmapFile&)            = delete;
    MmapFile& operator=(const MmapFile&) = delete;

    MmapFile(MmapFile&& o) noexcept
        : data_(o.data_), size_(o.size_), file_(o.file_),
          mapping_(o.mapping_)
    {
        o.data_    = nullptr;
        o.size_    = 0;
        o.file_    = INVALID_HANDLE_VALUE;
        o.mapping_ = nullptr;
    }

    const char* data() const { return data_; }
    size_t      size() const { return size_; }

    // Return a string_view of a byte range — zero-copy.
    std::string_view slice(size_t offset, size_t length) const {
        assert(offset + length <= size_);
        return { data_ + offset, length };
    }

private:
    const char* data_    = nullptr;
    size_t      size_    = 0;
    HANDLE      file_    = INVALID_HANDLE_VALUE;
    HANDLE      mapping_ = nullptr;
};

#else // POSIX

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string_view>

class MmapFile {
public:
    explicit MmapFile(const std::string& path) {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0)
            throw std::runtime_error("MmapFile: cannot open file: " + path);

        struct stat st{};
        if (::fstat(fd, &st) < 0) {
            ::close(fd);
            throw std::runtime_error("MmapFile: fstat failed: " + path);
        }
        size_ = static_cast<size_t>(st.st_size);

        if (size_ == 0) {
            ::close(fd);
            data_ = nullptr;
            return;
        }

        void* ptr = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd); // fd no longer needed after mmap
        if (ptr == MAP_FAILED)
            throw std::runtime_error("MmapFile: mmap failed: " + path);

        // Hint sequential access pattern to the kernel
        ::madvise(ptr, size_, MADV_SEQUENTIAL);

        data_ = static_cast<const char*>(ptr);
    }

    ~MmapFile() {
        if (data_) ::munmap(const_cast<char*>(data_), size_);
    }

    MmapFile(const MmapFile&)            = delete;
    MmapFile& operator=(const MmapFile&) = delete;

    MmapFile(MmapFile&& o) noexcept
        : data_(o.data_), size_(o.size_)
    {
        o.data_ = nullptr;
        o.size_ = 0;
    }

    const char* data() const { return data_; }
    size_t      size() const { return size_; }

    std::string_view slice(size_t offset, size_t length) const {
        assert(offset + length <= size_);
        return { data_ + offset, length };
    }

private:
    const char* data_ = nullptr;
    size_t      size_ = 0;
};

#endif // _WIN32
