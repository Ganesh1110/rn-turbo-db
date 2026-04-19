#pragma once

#include "SecureCryptoContext.h"
#include "ThreadPool.h"
#include <memory>
#include <functional>
#include <future>
#include <atomic>

namespace turbo_db {

class AsyncCryptoContext : public SecureCryptoContext {
public:
    AsyncCryptoContext(std::unique_ptr<SecureCryptoContext> inner, std::shared_ptr<ThreadPool> thread_pool);
    ~AsyncCryptoContext() override;

    std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t length) override;
    std::vector<uint8_t> decrypt(const uint8_t* ciphertext, size_t length) override;

    void encryptInto(const uint8_t* plaintext, size_t length, 
                     uint8_t* out_buffer, size_t& out_length) override;
    bool decryptInto(const uint8_t* ciphertext, size_t length,
                     uint8_t* out_buffer, size_t& out_length) override;

    std::vector<uint8_t> decryptAtOffset(const uint8_t* ciphertext, size_t length, uint64_t record_offset) override;
    bool decryptIntoAtOffset(const uint8_t* ciphertext, size_t length, uint64_t record_offset,
                             uint8_t* out_buffer, size_t& out_length) override;

    std::future<std::vector<uint8_t>> encryptAsync(const uint8_t* plaintext, size_t length);
    std::future<std::vector<uint8_t>> decryptAsync(const uint8_t* ciphertext, size_t length);
    
    void waitForPending();

private:
    std::unique_ptr<SecureCryptoContext> inner_;
    std::shared_ptr<ThreadPool> thread_pool_;
    std::atomic<size_t> pending_operations_{0};
};

}