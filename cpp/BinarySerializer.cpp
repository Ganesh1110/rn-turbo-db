#include "BinarySerializer.h"
#include <cstring>
#include <stdexcept>

namespace turbo_db {

static void packType(ArenaAllocator& arena, BinaryType t) {
    uint8_t* ptr = static_cast<uint8_t*>(arena.allocate(1));
    *ptr = static_cast<uint8_t>(t);
}

void BinarySerializer::serialize(facebook::jsi::Runtime& rt, const facebook::jsi::Value& val, ArenaAllocator& arena) {
    try {
        if (val.isNull() || val.isUndefined()) {
            packType(arena, BinaryType::Null);
        } else if (val.isBool()) {
            packType(arena, BinaryType::Boolean);
            uint8_t* ptr = static_cast<uint8_t*>(arena.allocate(1));
            *ptr = val.getBool() ? 1 : 0;
        } else if (val.isNumber()) {
            packType(arena, BinaryType::Number);
            double num = val.getNumber();
            uint8_t* ptr = static_cast<uint8_t*>(arena.allocate(sizeof(double)));
            std::memcpy(ptr, &num, sizeof(double));
        } else if (val.isString()) {
            packType(arena, BinaryType::String);
            std::string str = val.getString(rt).utf8(rt);
            uint32_t len = static_cast<uint32_t>(str.length());
            uint8_t* len_ptr = static_cast<uint8_t*>(arena.allocate(sizeof(uint32_t)));
            std::memcpy(len_ptr, &len, sizeof(uint32_t));
            uint8_t* str_ptr = static_cast<uint8_t*>(arena.allocate(len));
            std::memcpy(str_ptr, str.data(), len);
        } else if (val.isObject()) {
            facebook::jsi::Object obj = val.getObject(rt);
            if (obj.isArray(rt)) {
                packType(arena, BinaryType::Array);
                facebook::jsi::Array arr = obj.getArray(rt);
                uint32_t len = static_cast<uint32_t>(arr.size(rt));
                
                uint8_t* len_ptr = static_cast<uint8_t*>(arena.allocate(sizeof(uint32_t)));
                std::memcpy(len_ptr, &len, sizeof(uint32_t));
                
                for (size_t i = 0; i < len; ++i) {
                    serialize(rt, arr.getValueAtIndex(rt, i), arena);
                }
            } else {
                packType(arena, BinaryType::Object);
                facebook::jsi::Array keys = obj.getPropertyNames(rt);
                uint32_t len = static_cast<uint32_t>(keys.size(rt));
                
                uint8_t* len_ptr = static_cast<uint8_t*>(arena.allocate(sizeof(uint32_t)));
                std::memcpy(len_ptr, &len, sizeof(uint32_t));
                
                for (size_t i = 0; i < len; ++i) {
                    facebook::jsi::Value keyVal = keys.getValueAtIndex(rt, i);
                    std::string keyStr;
                    if (keyVal.isString()) {
                        keyStr = keyVal.getString(rt).utf8(rt);
                    } else if (keyVal.isNumber()) {
                        keyStr = std::to_string(keyVal.getNumber());
                    } else {
                        keyStr = "unknown";
                    }
                    
                    uint32_t keyLen = static_cast<uint32_t>(keyStr.length());
                    uint8_t* klen_ptr = static_cast<uint8_t*>(arena.allocate(sizeof(uint32_t)));
                    std::memcpy(klen_ptr, &keyLen, sizeof(uint32_t));
                    uint8_t* kstr_ptr = static_cast<uint8_t*>(arena.allocate(keyLen));
                    std::memcpy(kstr_ptr, keyStr.data(), keyLen);
                    
                    serialize(rt, obj.getProperty(rt, keyStr.c_str()), arena);
                }
            }
        } else {
            // Fallback for types like Symbol, BigInt (if not handled), or Functions
            packType(arena, BinaryType::Null);
        }
    } catch (...) {
        // Safe fallback to Null on any serialization error to prevent crash
        packType(arena, BinaryType::Null);
    }
}

std::pair<facebook::jsi::Value, size_t> BinarySerializer::deserialize(facebook::jsi::Runtime& rt, const uint8_t* ptr, size_t remaining_size) {
    if (remaining_size < 1) throw std::runtime_error("Buffer underflow");
    size_t consumed = 1;
    BinaryType type = static_cast<BinaryType>(ptr[0]);
    
    switch (type) {
        case BinaryType::Null:
            return {facebook::jsi::Value::null(), consumed};
            
        case BinaryType::Boolean:
            if (remaining_size < consumed + 1) throw std::runtime_error("Buffer underflow boolean");
            consumed += 1;
            return {facebook::jsi::Value(ptr[1] != 0), consumed};
            
        case BinaryType::Number: {
            if (remaining_size < consumed + sizeof(double)) throw std::runtime_error("Buffer underflow number");
            double num;
            std::memcpy(&num, ptr + consumed, sizeof(double));
            consumed += sizeof(double);
            return {facebook::jsi::Value(num), consumed};
        }
        case BinaryType::String: {
            if (remaining_size < consumed + sizeof(uint32_t)) throw std::runtime_error("Buffer underflow string len");
            uint32_t len;
            std::memcpy(&len, ptr + consumed, sizeof(uint32_t));
            consumed += sizeof(uint32_t);
            
            if (remaining_size < consumed + len) throw std::runtime_error("Buffer underflow string");
            std::string str(reinterpret_cast<const char*>(ptr + consumed), len);
            consumed += len;
            return {facebook::jsi::String::createFromUtf8(rt, str), consumed};
        }
        case BinaryType::Array: {
            if (remaining_size < consumed + sizeof(uint32_t)) throw std::runtime_error("Buffer underflow array len");
            uint32_t len;
            std::memcpy(&len, ptr + consumed, sizeof(uint32_t));
            consumed += sizeof(uint32_t);
            
            facebook::jsi::Array arr(rt, len);
            for (size_t i = 0; i < len; ++i) {
                auto [val, iter_consumed] = deserialize(rt, ptr + consumed, remaining_size - consumed);
                arr.setValueAtIndex(rt, i, val);
                consumed += iter_consumed;
            }
            return {std::move(arr), consumed};
        }
        case BinaryType::Object: {
            if (remaining_size < consumed + sizeof(uint32_t)) throw std::runtime_error("Buffer underflow object len");
            uint32_t len;
            std::memcpy(&len, ptr + consumed, sizeof(uint32_t));
            consumed += sizeof(uint32_t);
            
            facebook::jsi::Object obj(rt);
            for (size_t i = 0; i < len; ++i) {
                if (remaining_size < consumed + sizeof(uint32_t)) throw std::runtime_error("Buffer underflow key len");
                uint32_t keyLen;
                std::memcpy(&keyLen, ptr + consumed, sizeof(uint32_t));
                consumed += sizeof(uint32_t);
                
                if (remaining_size < consumed + keyLen) throw std::runtime_error("Buffer underflow key");
                std::string keyStr(reinterpret_cast<const char*>(ptr + consumed), keyLen);
                consumed += keyLen;
                
                auto [val, iter_consumed] = deserialize(rt, ptr + consumed, remaining_size - consumed);
                obj.setProperty(rt, keyStr.c_str(), val);
                consumed += iter_consumed;
            }
            return {std::move(obj), consumed};
        }
        default:
            throw std::runtime_error("Unknown BinaryType");
    }
}

size_t BinarySerializer::getBinarySize(const uint8_t* ptr, size_t remaining_size) {
    if (remaining_size < 1) return 0;
    size_t consumed = 1;
    BinaryType type = static_cast<BinaryType>(ptr[0]);
    
    switch (type) {
        case BinaryType::Null:
            return consumed;
        case BinaryType::Boolean:
            return consumed + 1;
        case BinaryType::Number:
            return consumed + sizeof(double);
        case BinaryType::String: {
            if (remaining_size < consumed + sizeof(uint32_t)) return remaining_size;
            uint32_t len;
            std::memcpy(&len, ptr + consumed, sizeof(uint32_t));
            return consumed + sizeof(uint32_t) + len;
        }
        case BinaryType::Array: {
            if (remaining_size < consumed + sizeof(uint32_t)) return remaining_size;
            uint32_t len;
            std::memcpy(&len, ptr + consumed, sizeof(uint32_t));
            consumed += sizeof(uint32_t);
            for (size_t i = 0; i < len; ++i) {
                consumed += getBinarySize(ptr + consumed, remaining_size - consumed);
            }
            return consumed;
        }
        case BinaryType::Object: {
            if (remaining_size < consumed + sizeof(uint32_t)) return remaining_size;
            uint32_t len;
            std::memcpy(&len, ptr + consumed, sizeof(uint32_t));
            consumed += sizeof(uint32_t);
            for (size_t i = 0; i < len; ++i) {
                // Skip key
                if (remaining_size < consumed + sizeof(uint32_t)) return remaining_size;
                uint32_t keyLen;
                std::memcpy(&keyLen, ptr + consumed, sizeof(uint32_t));
                consumed += sizeof(uint32_t) + keyLen;
                // Skip value
                consumed += getBinarySize(ptr + consumed, remaining_size - consumed);
            }
            return consumed;
        }
        default:
            return remaining_size;
    }
}

}
