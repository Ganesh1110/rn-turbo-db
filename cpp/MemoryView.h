#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace turbo_db {

class MMapRegion;

class MemoryView {
public:
    MemoryView();
    ~MemoryView();
    
    void init(const MMapRegion* mmap, size_t offset, size_t length);
    void reset();
    
    const uint8_t* data() const { return data_; }
    size_t length() const { return length_; }
    bool isValid() const { return valid_; }
    
    template<typename T>
    const T* as() const {
        return reinterpret_cast<const T*>(data_);
    }
    
    std::string toString() const;
    std::vector<uint8_t> toVector() const;

private:
    const uint8_t* data_;
    size_t length_;
    size_t offset_;
    bool valid_;
};

}