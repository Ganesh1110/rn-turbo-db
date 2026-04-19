#include "AsyncCryptoContext.h"

namespace turbo_db {

AsyncCryptoContext::AsyncCryptoContext(std::unique_ptr<SecureCryptoContext> inner, std::shared_ptr<ThreadPool> thread_pool)
    : inner_(std::move(inner)), thread_pool_(thread_pool) {}

AsyncCryptoContext::~AsyncCryptoContext() {
    waitForPending();
}

std::vector<uint8_t> AsyncCryptoContext::encrypt(const uint8_t* plaintext, size_t length) {
    return inner_->encrypt(plaintext, length);
}

std::vector<uint8_t> AsyncCryptoContext::decrypt(const uint8_t* ciphertext, size_t length) {
    return inner_->decrypt(ciphertext, length);
}

void AsyncCryptoContext::encryptInto(const uint8_t* plaintext, size_t length, 
                                      uint8_t* out_buffer, size_t& out_length) {
    inner_->encryptInto(plaintext, length, out_buffer, out_length);
}

bool AsyncCryptoContext::decryptInto(const uint8_t* ciphertext, size_t length,
                                      uint8_t* out_buffer, size_t& out_length) {
    return inner_->decryptInto(ciphertext, length, out_buffer, out_length);
}

std::vector<uint8_t> AsyncCryptoContext::decryptAtOffset(const uint8_t* ciphertext, size_t length, uint64_t record_offset) {
    return inner_->decryptAtOffset(ciphertext, length, record_offset);
}

bool AsyncCryptoContext::decryptIntoAtOffset(const uint8_t* ciphertext, size_t length, uint64_t record_offset,
                                              uint8_t* out_buffer, size_t& out_length) {
    return inner_->decryptIntoAtOffset(ciphertext, length, record_offset, out_buffer, out_length);
}

std::future<std::vector<uint8_t>> AsyncCryptoContext::encryptAsync(const uint8_t* plaintext, size_t length) {
    auto promise = std::make_shared<std::promise<std::vector<uint8_t>>>();
    auto future = promise->get_future();
    
    pending_operations_++;
    
    thread_pool_->enqueue([this, promise, plaintext, length]() {
        try {
            auto result = inner_->encrypt(plaintext, length);
            promise->set_value(std::move(result));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
        pending_operations_--;
    });
    
    return future;
}

std::future<std::vector<uint8_t>> AsyncCryptoContext::decryptAsync(const uint8_t* ciphertext, size_t length) {
    auto promise = std::make_shared<std::promise<std::vector<uint8_t>>>();
    auto future = promise->get_future();
    
    pending_operations_++;
    
    thread_pool_->enqueue([this, promise, ciphertext, length]() {
        try {
            auto result = inner_->decrypt(ciphertext, length);
            promise->set_value(std::move(result));
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
        pending_operations_--;
    });
    
    return future;
}

void AsyncCryptoContext::waitForPending() {
    while (pending_operations_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}