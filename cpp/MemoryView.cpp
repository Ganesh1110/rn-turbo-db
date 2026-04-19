#include "MemoryView.h"
#include "MMapRegion.h"
#include <vector>

namespace turbo_db {

MemoryView::MemoryView() : data_(nullptr), length_(0), offset_(0), valid_(false) {}

MemoryView::~MemoryView() = default;

void MemoryView::init(const MMapRegion* mmap, size_t offset, size_t length) {
    if (mmap) {
        data_ = mmap->get_address(offset);
        if (data_) {
            offset_ = offset;
            length_ = length;
            valid_ = true;
            return;
        }
    }
    valid_ = false;
}

void MemoryView::reset() {
    data_ = nullptr;
    length_ = 0;
    offset_ = 0;
    valid_ = false;
}

std::string MemoryView::toString() const {
    if (!valid_ || !data_ || length_ == 0) return {};
    return std::string(reinterpret_cast<const char*>(data_), length_);
}

std::vector<uint8_t> MemoryView::toVector() const {
    if (!valid_ || !data_ || length_ == 0) return {};
    return std::vector<uint8_t>(data_, data_ + length_);
}

}