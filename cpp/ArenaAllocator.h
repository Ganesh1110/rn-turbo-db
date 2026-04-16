#pragma once

#include <cstddef>
#include <vector>
#include <stdexcept>
#include <cstring>

namespace secure_db {

class ArenaAllocator {
public:
    ArenaAllocator(size_t capacity) : capacity_(capacity), offset_(0) {
        buffer_ = new uint8_t[capacity_];
    }
    
    ~ArenaAllocator() {
        delete[] buffer_;
    }

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    void* allocate(size_t bytes) {
        if (offset_ + bytes > capacity_) {
            grow(offset_ + bytes);
        }
        void* ptr = buffer_ + offset_;
        offset_ += bytes;
        return ptr;
    }

    void reserve(size_t additional_bytes) {
        if (offset_ + additional_bytes > capacity_) {
            grow(offset_ + additional_bytes);
        }
    }

    void writeAt(size_t offset, const uint8_t* data, size_t len) {
        if (offset + len > capacity_) {
            grow(offset + len);
        }
        std::memcpy(buffer_ + offset, data, len);
        if (offset + len > offset_) {
            offset_ = offset + len;
        }
    }

    void reset() {
        offset_ = 0;
    }
    
    uint8_t* data() const { return buffer_; }
    size_t size() const { return offset_; }
    size_t capacity() const { return capacity_; }

private:
    size_t capacity_;
    size_t offset_;
    uint8_t* buffer_;

    void grow(size_t new_capacity) {
        size_t new_cap = capacity_ * 2;
        while (new_cap < new_capacity) {
            new_cap *= 2;
        }
        uint8_t* new_buffer = new uint8_t[new_cap];
        std::memcpy(new_buffer, buffer_, offset_);
        delete[] buffer_;
        buffer_ = new_buffer;
        capacity_ = new_cap;
    }
};

}
