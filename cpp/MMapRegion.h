#pragma once

#include <string>
#include <stdexcept>

namespace turbo_db {

class MMapRegion {
public:
    MMapRegion();
    ~MMapRegion();

    // Map a file to memory, up to a certain size
    void init(const std::string& path, size_t size);
    
    // Unmap memory and close
    void close();

    // Sync a region of memory back to disk (async=true for MS_ASYNC, false for MS_SYNC)
    void sync(size_t offset = 0, size_t length = 0, bool async = false);

    // Read/Write accessors
    void write(size_t offset, const std::string& data);
    void write(size_t offset, const uint8_t* data, size_t length);
    
    std::string read(size_t offset, size_t length);
    const uint8_t* get_address(size_t offset) const;

    // Expand the mmap if the current size is too small
    void ensure_capacity(size_t required_size);

    // Property accessors
    std::string getPath() const { return path_; }
    size_t getSize() const { return size_; }
    void* getAddress() const { return base_addr_; }

private:
    void* base_addr_;
    size_t size_;
    int fd_;
    std::string path_;
    bool mapped_;
};

}
