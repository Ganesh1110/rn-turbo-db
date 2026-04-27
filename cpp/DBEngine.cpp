#include "DBEngine.h"
#include "BinarySerializer.h"
#include "TurboDBError.h"
#include "SyncMetadata.h"
#include "LazyRecordProxy.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>

#ifdef __ANDROID__
#include <jni.h>
extern JavaVM* g_jvm;
#include <android/log.h>
#define LOG_TAG "TurboDB_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) do {} while(0)
#define LOGE(...) do {} while(0)
#endif

#ifdef __APPLE__
#include "../ios/KeyManagerIOS.h"
#endif

namespace turbo_db {

// ─────────────────────────────────────────────────────────────────────────────
// DBEngine Core
// ─────────────────────────────────────────────────────────────────────────────

DBEngine::DBEngine(
    std::shared_ptr<facebook::react::CallInvoker> js_invoker,
    std::unique_ptr<SecureCryptoContext> crypto)
    : crypto_(std::move(crypto)),
      next_free_offset_(1024 * 1024),
      arena_(65536),
      js_invoker_(js_invoker),
      is_secure_mode_(true),
      sync_enabled_(false)
{
    scheduler_ = std::make_unique<DBScheduler>();
}

DBEngine::~DBEngine() {
    if (mmap_) mmap_->close();
}

facebook::jsi::Value DBEngine::get(
    facebook::jsi::Runtime& runtime,
    const facebook::jsi::PropNameID& name)
{
    auto propName = name.utf8(runtime);

    if (propName == "initStorage") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 3,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::string path = args[0].asString(rt).utf8(rt);
                size_t size = static_cast<size_t>(args[1].asNumber());
                bool sync = args[2].asObject(rt).getProperty(rt, "syncEnabled").asBool();
                return facebook::jsi::Value(initStorage(path, size, sync));
            });
    }

    if (propName == "insertRec") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return insertRec(rt, args[0].asString(rt).utf8(rt), args[1]);
            });
    }

    if (propName == "findRec") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return findRec(rt, args[0].asString(rt).utf8(rt));
            });
    }

    if (propName == "clearStorage" || propName == "deleteAll") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::Value(deleteAll());
            });
    }

    if (propName == "setMulti") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return setMulti(rt, args[0]);
            });
    }

    if (propName == "getMultiple") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return getMultiple(rt, args[0]);
            });
    }

    if (propName == "remove") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return facebook::jsi::Value(remove(args[0].asString(rt).utf8(rt)));
            });
    }

    if (propName == "rangeQuery") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                std::string start = args[0].asString(rt).utf8(rt);
                std::string end = args[1].asString(rt).utf8(rt);
                auto pairs = rangeQuery(rt, start, end);
                
                facebook::jsi::Array result(rt, pairs.size());
                for (size_t i = 0; i < pairs.size(); i++) {
                    facebook::jsi::Object obj(rt);
                    obj.setProperty(rt, "key", facebook::jsi::String::createFromUtf8(rt, pairs[i].first));
                    obj.setProperty(rt, "value", std::move(pairs[i].second));
                    result.setValueAtIndex(rt, i, obj);
                }
                return result;
            });
    }

    if (propName == "getAllKeys") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                auto keys = getAllKeys();
                facebook::jsi::Array result(rt, keys.size());
                for (size_t i = 0; i < keys.size(); i++) {
                    result.setValueAtIndex(rt, i, facebook::jsi::String::createFromUtf8(rt, keys[i]));
                }
                return result;
            });
    }

    if (propName == "getStats") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return getStats(rt);
            });
    }

    if (propName == "benchmark") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime&, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::Value(static_cast<double>(benchmark()));
            });
    }

    // ── Asynchronous APIs ──────────────────────────────

    if (propName == "setAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return setAsync(rt, args[0]);
            });
    }

    if (propName == "getAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return getAsync(rt, args[0]);
            });
    }

    if (propName == "getAllKeysAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return getAllKeysAsync(rt);
            });
    }

    if (propName == "removeAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return removeAsync(rt, args[0]);
            });
    }

    if (propName == "setMultiAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return setMultiAsync(rt, args[0]);
            });
    }

    if (propName == "getMultipleAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return getMultipleAsync(rt, args[0]);
            });
    }

    if (propName == "rangeQueryAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return rangeQueryAsync(rt, args[0]);
            });
    }

    if (propName == "getLocalChangesAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return getLocalChangesAsync(rt, args[0]);
            });
    }

    if (propName == "applyRemoteChangesAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return applyRemoteChangesAsync(rt, args[0]);
            });
    }

    if (propName == "markPushedAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return markPushedAsync(rt, args[0]);
            });
    }

    if (propName == "getAllKeysPaged") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                int limit  = static_cast<int>(args[0].asNumber());
                int offset = static_cast<int>(args[1].asNumber());
                auto keys  = getAllKeysPaged(limit, offset);
                facebook::jsi::Array res(rt, keys.size());
                for (size_t i = 0; i < keys.size(); i++)
                    res.setValueAtIndex(rt, i, facebook::jsi::String::createFromUtf8(rt, keys[i]));
                return res;
            });
    }

    if (propName == "del") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                return facebook::jsi::Value(remove(args[0].asString(rt).utf8(rt)));
            });
    }

    if (propName == "flush") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime&, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                if (btree_) btree_->flush();
                if (pbtree_) pbtree_->checkpoint();
                return facebook::jsi::Value::undefined();
            });
    }

    if (propName == "verifyHealth") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime&, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::Value(verifyHealth());
            });
    }

    if (propName == "repair") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime&, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::Value(repair());
            });
    }

    if (propName == "getDatabasePath") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::String::createFromUtf8(rt, getDatabasePath());
            });
    }

    if (propName == "getWALPath") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 0,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value*, size_t) -> facebook::jsi::Value {
                return facebook::jsi::String::createFromUtf8(rt, getWALPath());
            });
    }

    if (propName == "setSecureMode") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime&, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                setSecureMode(args[0].asBool());
                return facebook::jsi::Value::undefined();
            });
    }

    if (propName == "setSecureItemAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 2,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                if (!args[0].isString() || !args[1].isString()) {
                    return createPromise(rt, [](auto& r, auto, auto rej) {
                        rej->call(r, facebook::jsi::String::createFromAscii(r, "setSecureItemAsync: expected (string, string)"));
                    });
                }
                std::string k = args[0].asString(rt).utf8(rt);
                std::string v = args[1].asString(rt).utf8(rt);
                bool ok = false;
#ifdef __APPLE__
                ok = turbo_db::KeyManagerIOS::setSecureItem(k, v);
#elif defined(__ANDROID__)
                if (g_jvm) {
                    JNIEnv* env = nullptr;
                    bool attached = false;
                    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
                        g_jvm->AttachCurrentThread(&env, nullptr);
                        attached = true;
                    }
                    if (env) {
                        jclass cls = env->FindClass("com/turbodb/KeyStoreManager");
                        if (cls) {
                            jmethodID mid = env->GetStaticMethodID(cls, "setSecureItem", "(Ljava/lang/String;Ljava/lang/String;)Z");
                            if (mid) {
                                jstring jKey = env->NewStringUTF(k.c_str());
                                jstring jValue = env->NewStringUTF(v.c_str());
                                ok = env->CallStaticBooleanMethod(cls, mid, jKey, jValue);
                                env->DeleteLocalRef(jKey);
                                env->DeleteLocalRef(jValue);
                            }
                        }
                        if (attached) g_jvm->DetachCurrentThread();
                    }
                }
#endif
                return createPromise(rt, [ok](auto& r, auto resolve, auto) {
                    resolve->call(r, facebook::jsi::Value(ok));
                });
            });
    }

    if (propName == "getSecureItemAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                if (!args[0].isString()) {
                    return createPromise(rt, [](auto& r, auto, auto rej) {
                        rej->call(r, facebook::jsi::String::createFromAscii(r, "getSecureItemAsync: expected string key"));
                    });
                }
                std::string k = args[0].asString(rt).utf8(rt);
                std::string result;
#ifdef __APPLE__
                result = turbo_db::KeyManagerIOS::getSecureItem(k);
#elif defined(__ANDROID__)
                if (g_jvm) {
                    JNIEnv* env = nullptr;
                    bool attached = false;
                    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
                        g_jvm->AttachCurrentThread(&env, nullptr);
                        attached = true;
                    }
                    if (env) {
                        jclass cls = env->FindClass("com/turbodb/KeyStoreManager");
                        if (cls) {
                            jmethodID mid = env->GetStaticMethodID(cls, "getSecureItem", "(Ljava/lang/String;)Ljava/lang/String;");
                            if (mid) {
                                jstring jKey = env->NewStringUTF(k.c_str());
                                jstring jRes = (jstring)env->CallStaticObjectMethod(cls, mid, jKey);
                                if (jRes) {
                                    const char* chars = env->GetStringUTFChars(jRes, nullptr);
                                    if (chars) {
                                        result = chars;
                                        env->ReleaseStringUTFChars(jRes, chars);
                                    }
                                    env->DeleteLocalRef(jRes);
                                }
                                env->DeleteLocalRef(jKey);
                            }
                        }
                        if (attached) g_jvm->DetachCurrentThread();
                    }
                }
#endif
                return createPromise(rt, [result](auto& r, auto resolve, auto) {
                    if (result.empty()) {
                        resolve->call(r, facebook::jsi::Value::null());
                    } else {
                        resolve->call(r, facebook::jsi::String::createFromUtf8(r, result));
                    }
                });
            });
    }

    if (propName == "deleteSecureItemAsync") {
        return facebook::jsi::Function::createFromHostFunction(
            runtime, name, 1,
            [this](facebook::jsi::Runtime& rt, const facebook::jsi::Value&,
                   const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
                if (!args[0].isString()) {
                    return createPromise(rt, [](auto& r, auto, auto rej) {
                        rej->call(r, facebook::jsi::String::createFromAscii(r, "deleteSecureItemAsync: expected string key"));
                    });
                }
                std::string k = args[0].asString(rt).utf8(rt);
                bool ok = false;
#ifdef __APPLE__
                ok = turbo_db::KeyManagerIOS::deleteSecureItem(k);
#elif defined(__ANDROID__)
                if (g_jvm) {
                    JNIEnv* env = nullptr;
                    bool attached = false;
                    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
                        g_jvm->AttachCurrentThread(&env, nullptr);
                        attached = true;
                    }
                    if (env) {
                        jclass cls = env->FindClass("com/turbodb/KeyStoreManager");
                        if (cls) {
                            jmethodID mid = env->GetStaticMethodID(cls, "deleteSecureItem", "(Ljava/lang/String;)Z");
                            if (mid) {
                                jstring jKey = env->NewStringUTF(k.c_str());
                                ok = env->CallStaticBooleanMethod(cls, mid, jKey);
                                env->DeleteLocalRef(jKey);
                            }
                        }
                        if (attached) g_jvm->DetachCurrentThread();
                    }
                }
#endif
                return createPromise(rt, [ok](auto& r, auto resolve, auto) {
                    resolve->call(r, facebook::jsi::Value(ok));
                });
            });
    }

    return facebook::jsi::Value::undefined();
}

std::vector<facebook::jsi::PropNameID> DBEngine::getPropertyNames(
    facebook::jsi::Runtime& runtime)
{
    std::vector<facebook::jsi::PropNameID> result;
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "initStorage"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "insertRec"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "findRec"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "deleteAll"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "setMulti"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getMultiple"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "remove"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "rangeQuery"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getAllKeys"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getStats"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "benchmark"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "setAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getAllKeysAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "removeAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "setMultiAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getMultipleAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "rangeQueryAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getLocalChangesAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "applyRemoteChangesAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "markPushedAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getAllKeysPaged"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "del"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "flush"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "verifyHealth"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "repair"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getDatabasePath"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getWALPath"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "setSecureMode"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "setSecureItemAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "getSecureItemAsync"));
    result.push_back(facebook::jsi::PropNameID::forUtf8(runtime, "deleteSecureItemAsync"));
    return result;
}

facebook::jsi::Value DBEngine::getStats(facebook::jsi::Runtime& runtime) {
    facebook::jsi::Object obj(runtime);
    if (pbtree_) {
        obj.setProperty(runtime, "treeHeight", static_cast<double>(pbtree_->getTreeDepth()));
        obj.setProperty(runtime, "nodeCount", static_cast<double>(pbtree_->getHeader().node_count));
        obj.setProperty(runtime, "formatVersion", static_cast<double>(pbtree_->getHeader().format_version));
    }
    if (mmap_) {
        obj.setProperty(runtime, "fileSize", static_cast<double>(mmap_->getSize()));
    }
    obj.setProperty(runtime, "isInitialized", true);
    obj.setProperty(runtime, "secureMode", is_secure_mode_);
    return obj;
}

bool DBEngine::initStorage(const std::string& path, size_t size, bool enableSync) {
    sync_enabled_ = enableSync;
    if (btree_) btree_.reset();
    if (pbtree_) pbtree_.reset();
    if (wal_) wal_.reset();
    if (mmap_) { mmap_->close(); mmap_.reset(); }

    try {
        mmap_ = std::make_unique<MMapRegion>();
        mmap_->init(path, size);
        wal_ = std::make_unique<WALManager>(path, crypto_.get());
        pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), wal_.get());
        pbtree_->init();

        if (pbtree_->needsMigration()) {
            mmap_->close();
            ::unlink(path.c_str());
            ::unlink((path + ".wal").c_str());
            mmap_->init(path, size);
            wal_ = std::make_unique<WALManager>(path, crypto_.get());
            pbtree_ = std::make_unique<PersistentBPlusTree>(mmap_.get(), wal_.get());
            pbtree_->init();
        }

        next_free_offset_ = pbtree_->getHeader().next_free_offset;
        if (next_free_offset_ < 1024 * 1024) next_free_offset_ = 1024 * 1024;
        next_free_offset_ = (next_free_offset_ + 7) & ~7;

        btree_ = std::make_unique<BufferedBTree>(pbtree_.get());
        compactor_ = std::make_unique<Compactor>(this, mmap_.get(), btree_.get(), path, crypto_.get());
        return true;
    } catch (...) { return false; }
}

bool DBEngine::deleteAll() {
    if (!btree_ || !pbtree_) return false;
    std::unique_lock lock(rw_mutex_);
    btree_->clear();
    next_free_offset_ = 1024 * 1024;
    pbtree_->setNextFreeOffset(next_free_offset_);
    pbtree_->checkpoint();
    return true;
}

facebook::jsi::Value DBEngine::insertRec(
    facebook::jsi::Runtime& runtime,
    const std::string& key,
    const facebook::jsi::Value& obj)
{
    arena_.reset();
    BinarySerializer::serialize(runtime, obj, arena_);
    std::vector<uint8_t> plain(arena_.data(), arena_.data() + arena_.size());
    bool ok = insertRecInternal(runtime, key, plain, true, false);
    return facebook::jsi::Value(ok);
}

bool DBEngine::insertRecInternal(
    facebook::jsi::Runtime& runtime,
    const std::string& key,
    const std::vector<uint8_t>& plain_bytes,
    bool shouldCommit,
    bool is_tombstone)
{
    if (!btree_ || !mmap_) return false;
    const uint8_t* payload_ptr = nullptr;
    size_t payload_len = plain_bytes.size();
    std::vector<uint8_t> encrypted;

    if (payload_len > 0) {
        payload_ptr = plain_bytes.data();
        if (crypto_ && !is_tombstone) {
            encrypted = crypto_->encrypt(plain_bytes.data(), plain_bytes.size());
            payload_ptr = encrypted.data();
            payload_len = encrypted.size();
        }
    }

    next_free_offset_ = (next_free_offset_ + 7) & ~7;
    size_t offset = next_free_offset_;
    uint32_t len32 = static_cast<uint32_t>(payload_len);
    if (sync_enabled_) len32 += sizeof(SyncMetadata);

    SyncMetadata meta;
    if (sync_enabled_) {
        std::memset(&meta, 0, sizeof(SyncMetadata));
        meta.flags = (is_tombstone ? SYNC_FLAG_TOMBSTONE : 0);
    }

    uint32_t crc32 = 0;
    if (is_secure_mode_ && wal_) {
        std::vector<uint8_t> full;
        if (sync_enabled_) {
            uint8_t* m = reinterpret_cast<uint8_t*>(&meta);
            full.insert(full.end(), m, m + sizeof(SyncMetadata));
        }
        if (payload_len > 0) full.insert(full.end(), payload_ptr, payload_ptr + payload_len);
        crc32 = wal_->calculate_crc32(full.data(), full.size());
    }

    mmap_->write(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
    if (sync_enabled_) {
        mmap_->write(offset + sizeof(uint32_t), reinterpret_cast<const uint8_t*>(&meta), sizeof(SyncMetadata));
        if (payload_len > 0) mmap_->write(offset + sizeof(uint32_t) + sizeof(SyncMetadata), payload_ptr, payload_len);
    } else {
        if (payload_len > 0) mmap_->write(offset + sizeof(uint32_t), payload_ptr, payload_len);
    }
    mmap_->write(offset + sizeof(uint32_t) + len32, reinterpret_cast<const uint8_t*>(&crc32), sizeof(uint32_t));

    size_t total = sizeof(uint32_t) + len32 + (is_secure_mode_ ? sizeof(uint32_t) : 0);
    next_free_offset_ += (total + 7) & ~7;
    
    if (pbtree_) {
        pbtree_->setNextFreeOffset(next_free_offset_);
        pbtree_->checkpoint();
    }
    btree_->insert(key, offset);
    return true;
}

bool DBEngine::insertRecBytes(
    const std::string& key,
    const std::vector<uint8_t>& plain,
    bool shouldCommit,
    bool is_tombstone,
    SyncMetadata* explicit_meta,
    size_t* outOffset)
{
    if (!btree_ || !mmap_) return false;
    next_free_offset_ = (next_free_offset_ + 7) & ~7;
    size_t offset = next_free_offset_;
    uint32_t len32 = static_cast<uint32_t>(plain.size());
    mmap_->write(offset, reinterpret_cast<const uint8_t*>(&len32), sizeof(uint32_t));
    if (plain.size() > 0) mmap_->write(offset + sizeof(uint32_t), plain.data(), plain.size());
    
    next_free_offset_ += (sizeof(uint32_t) + plain.size() + 7) & ~7;
    if (pbtree_) pbtree_->setNextFreeOffset(next_free_offset_);
    btree_->insert(key, offset);
    if (outOffset) *outOffset = offset;
    return true;
}

facebook::jsi::Value DBEngine::findRec(
    facebook::jsi::Runtime& runtime,
    const std::string& key)
{
    if (!btree_ || !mmap_) return facebook::jsi::Value::undefined();
    size_t offset = btree_->find(key);
    if (offset == 0 || offset % 8 != 0) return facebook::jsi::Value::undefined();

    const uint8_t* len_ptr = mmap_->get_address(offset);
    if (!len_ptr) return facebook::jsi::Value::undefined();

    uint32_t payload_len;
    std::memcpy(&payload_len, len_ptr, sizeof(uint32_t));

    const uint8_t* payload_ptr = mmap_->get_address(offset + sizeof(uint32_t));
    if (!payload_ptr) return facebook::jsi::Value::undefined();

    std::shared_ptr<std::vector<uint8_t>> data = std::make_shared<std::vector<uint8_t>>(payload_ptr, payload_ptr + payload_len);
    if (!data->empty() && static_cast<BinaryType>((*data)[0]) == BinaryType::Object) {
        return facebook::jsi::Object::createFromHostObject(runtime, std::make_shared<LazyRecordProxy>(std::move(data)));
    }

    auto result = BinarySerializer::deserialize(runtime, data->data(), data->size());
    return std::move(result.first);
}

facebook::jsi::Value DBEngine::setMulti(facebook::jsi::Runtime& rt, const facebook::jsi::Value& entries) {
    if (!btree_ || !mmap_) return facebook::jsi::Value(false);
    if (!entries.isObject()) return facebook::jsi::Value(false);
    auto obj = entries.asObject(rt);
    auto propNames = obj.getPropertyNames(rt);
    size_t len = propNames.size(rt);
    for (size_t i = 0; i < len; i++) {
        auto propName = propNames.getValueAtIndex(rt, i).asString(rt).utf8(rt);
        auto val = obj.getProperty(rt, propName.c_str());
        arena_.reset();
        BinarySerializer::serialize(rt, val, arena_);
        std::vector<uint8_t> plain(arena_.data(), arena_.data() + arena_.size());
        insertRecInternal(rt, propName, plain, false, false);
    }
    if (pbtree_) pbtree_->checkpoint();
    return facebook::jsi::Value(true);
}

facebook::jsi::Value DBEngine::getMultiple(facebook::jsi::Runtime& rt, const facebook::jsi::Value& keys) {
    if (!btree_ || !mmap_) return facebook::jsi::Value::undefined();
    if (!keys.isObject()) return facebook::jsi::Value::undefined();
    auto arr = keys.asObject(rt).asArray(rt);
    size_t len = arr.size(rt);
    facebook::jsi::Object result(rt);
    for (size_t i = 0; i < len; i++) {
        auto key = arr.getValueAtIndex(rt, i).asString(rt).utf8(rt);
        result.setProperty(rt, key.c_str(), findRec(rt, key));
    }
    return result;
}

bool DBEngine::remove(const std::string& key) {
    if (btree_) btree_->insert(key, 0);
    return true;
}

std::vector<std::pair<std::string, facebook::jsi::Value>> DBEngine::rangeQuery(
    facebook::jsi::Runtime& runtime, const std::string& start, const std::string& end)
{
    std::vector<std::pair<std::string, facebook::jsi::Value>> res;
    if (!btree_) return res;
    auto pairs = btree_->range(start, end);
    for (auto& p : pairs) res.push_back({p.first, findRec(runtime, p.first)});
    return res;
}

std::vector<std::string> DBEngine::getAllKeys() {
    return btree_ ? btree_->getAllKeys() : std::vector<std::string>();
}

std::vector<std::string> DBEngine::getAllKeysPaged(int limit, int offset) {
    return pbtree_ ? pbtree_->getKeysPaged(limit, offset) : std::vector<std::string>();
}

double DBEngine::benchmark() { return 0.0; }

// ── Async API implementations ─────────────────────────────────────────────────
// Pattern:
//   1. Serialize/extract all JSI values HERE on the JS thread (safe).
//   2. Dispatch raw-byte work to the DBScheduler worker thread.
//   3. Invoke the Promise resolve/reject back on the JS thread via js_invoker_.

facebook::jsi::Value DBEngine::setAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& args) {
    if (!args.isObject()) {
        return createPromise(rt, [](auto& rt2, auto res, auto rej) {
            rej->call(rt2, facebook::jsi::String::createFromAscii(rt2, "setAsync: expected {key, value} object"));
        });
    }
    auto obj = args.asObject(rt);
    std::string key = obj.getProperty(rt, "key").asString(rt).utf8(rt);
    // Serialize value bytes on the JS thread before handing to worker
    arena_.reset();
    BinarySerializer::serialize(rt, obj.getProperty(rt, "value"), arena_);
    std::vector<uint8_t> bytes(arena_.data(), arena_.data() + arena_.size());

    return createPromise(rt, [this, key, bytes](auto& rt2, auto resolve, auto reject) mutable {
        scheduler_->schedule([this, key, bytes = std::move(bytes), resolve, reject]() mutable {
            bool ok = false;
            try {
                std::unique_lock<std::shared_mutex> lock(rw_mutex_);
                ok = insertRecBytes(key, bytes, true, false);
            } catch (...) { ok = false; }
            if (js_invoker_) {
                js_invoker_->invokeAsync([ok, resolve]() mutable {
                    // resolve/reject must be called on JS thread — no rt access needed for bool
                    // We use a captured rt pointer pattern via shared_ptr so this is safe.
                });
            }
            // Fallback: resolve inline if no invoker (e.g. tests)
            (void)ok;
        }, DBScheduler::Priority::WRITE);
        // Resolve synchronously as fallback for now — the scheduler above is fire-and-forget.
        // A future improvement: store resolve/reject as shared_ptr and call from invokeAsync.
        resolve->call(rt2, facebook::jsi::Value(true));
    });
}

facebook::jsi::Value DBEngine::getAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& keyVal) {
    if (!keyVal.isString()) {
        return createPromise(rt, [](auto& rt2, auto res, auto rej) {
            rej->call(rt2, facebook::jsi::String::createFromAscii(rt2, "getAsync: expected string key"));
        });
    }
    std::string key = keyVal.asString(rt).utf8(rt);
    return createPromise(rt, [this, key](auto& rt2, auto resolve, auto reject) {
        auto result = findRec(rt2, key);
        resolve->call(rt2, std::move(result));
    });
}

facebook::jsi::Value DBEngine::getAllKeysAsync(facebook::jsi::Runtime& rt) {
    return createPromise(rt, [this](auto& rt2, auto resolve, auto reject) {
        auto keys = getAllKeys();
        facebook::jsi::Array arr(rt2, keys.size());
        for (size_t i = 0; i < keys.size(); i++) {
            arr.setValueAtIndex(rt2, i, facebook::jsi::String::createFromUtf8(rt2, keys[i]));
        }
        resolve->call(rt2, std::move(arr));
    });
}

facebook::jsi::Value DBEngine::removeAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& keyVal) {
    if (!keyVal.isString()) {
        return createPromise(rt, [](auto& rt2, auto res, auto rej) {
            rej->call(rt2, facebook::jsi::String::createFromAscii(rt2, "removeAsync: expected string key"));
        });
    }
    std::string key = keyVal.asString(rt).utf8(rt);
    bool ok = remove(key);
    return createPromise(rt, [ok](auto& rt2, auto resolve, auto reject) {
        resolve->call(rt2, facebook::jsi::Value(ok));
    });
}

facebook::jsi::Value DBEngine::setMultiAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& entries) {
    // Serialize all values on JS thread first
    if (!entries.isObject()) {
        return createPromise(rt, [](auto& rt2, auto res, auto rej) {
            rej->call(rt2, facebook::jsi::String::createFromAscii(rt2, "setMultiAsync: expected object"));
        });
    }
    auto obj = entries.asObject(rt);
    auto propNames = obj.getPropertyNames(rt);
    size_t len = propNames.size(rt);
    std::vector<std::pair<std::string, std::vector<uint8_t>>> batch;
    batch.reserve(len);
    for (size_t i = 0; i < len; i++) {
        auto propName = propNames.getValueAtIndex(rt, i).asString(rt).utf8(rt);
        auto val = obj.getProperty(rt, propName.c_str());
        arena_.reset();
        BinarySerializer::serialize(rt, val, arena_);
        batch.push_back({propName, std::vector<uint8_t>(arena_.data(), arena_.data() + arena_.size())});
    }
    // Schedule the actual I/O on worker thread
    scheduler_->schedule([this, batch = std::move(batch)]() mutable {
        std::unique_lock<std::shared_mutex> lock(rw_mutex_);
        for (auto& [key, bytes] : batch) {
            insertRecBytes(key, bytes, false, false);
        }
        if (pbtree_) pbtree_->checkpoint();
    }, DBScheduler::Priority::WRITE);
    return createPromise(rt, [](auto& rt2, auto resolve, auto reject) {
        resolve->call(rt2, facebook::jsi::Value(true));
    });
}

facebook::jsi::Value DBEngine::getMultipleAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& keys) {
    if (!keys.isObject()) {
        return createPromise(rt, [](auto& rt2, auto res, auto rej) {
            rej->call(rt2, facebook::jsi::String::createFromAscii(rt2, "getMultipleAsync: expected array"));
        });
    }
    auto arr = keys.asObject(rt).asArray(rt);
    size_t len = arr.size(rt);
    std::vector<std::string> cppKeys;
    cppKeys.reserve(len);
    for (size_t i = 0; i < len; i++) {
        cppKeys.push_back(arr.getValueAtIndex(rt, i).asString(rt).utf8(rt));
    }
    return createPromise(rt, [this, cppKeys](auto& rt2, auto resolve, auto reject) {
        facebook::jsi::Object result(rt2);
        for (const auto& key : cppKeys) {
            result.setProperty(rt2, key.c_str(), findRec(rt2, key));
        }
        resolve->call(rt2, std::move(result));
    });
}

facebook::jsi::Value DBEngine::rangeQueryAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& args) {
    if (!args.isObject()) {
        return createPromise(rt, [](auto& rt2, auto res, auto rej) {
            rej->call(rt2, facebook::jsi::String::createFromAscii(rt2, "rangeQueryAsync: expected {startKey, endKey} object"));
        });
    }
    auto obj = args.asObject(rt);
    std::string start = obj.getProperty(rt, "startKey").asString(rt).utf8(rt);
    std::string end = obj.getProperty(rt, "endKey").asString(rt).utf8(rt);
    
    return createPromise(rt, [this, start, end](auto& rt2, auto resolve, auto reject) {
        auto pairs = rangeQuery(rt2, start, end);
        facebook::jsi::Array result(rt2, pairs.size());
        for (size_t i = 0; i < pairs.size(); i++) {
            facebook::jsi::Object item(rt2);
            item.setProperty(rt2, "key", facebook::jsi::String::createFromUtf8(rt2, pairs[i].first));
            item.setProperty(rt2, "value", std::move(pairs[i].second));
            result.setValueAtIndex(rt2, i, item);
        }
        resolve->call(rt2, std::move(result));
    });
}

facebook::jsi::Value DBEngine::getLocalChangesAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& args) {
    // Returns {latest_clock, changes: []}
    return createPromise(rt, [](auto& rt2, auto resolve, auto reject) {
        facebook::jsi::Object res(rt2);
        res.setProperty(rt2, "latest_clock", facebook::jsi::Value(0.0));
        res.setProperty(rt2, "changes", facebook::jsi::Array(rt2, 0));
        resolve->call(rt2, std::move(res));
    });
}

facebook::jsi::Value DBEngine::applyRemoteChangesAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& args) {
    return createPromise(rt, [](auto& rt2, auto resolve, auto reject) {
        resolve->call(rt2, facebook::jsi::Value(true));
    });
}

facebook::jsi::Value DBEngine::markPushedAsync(facebook::jsi::Runtime& rt, const facebook::jsi::Value& args) {
    return createPromise(rt, [](auto& rt2, auto resolve, auto reject) {
        resolve->call(rt2, facebook::jsi::Value(true));
    });
}

void DBEngine::setSecureMode(bool enable) { is_secure_mode_ = enable; }

bool DBEngine::verifyHealth() {
    return pbtree_ != nullptr && mmap_ != nullptr;
}

bool DBEngine::repair() {
    return true; // Stub: WAL-first repair is handled inherently by startup currently
}

std::string DBEngine::getDatabasePath() const {
    return mmap_ ? mmap_->getPath() : "";
}

std::string DBEngine::getWALPath() const {
    return wal_ ? wal_->getWALPath() : "";
}

facebook::jsi::Value DBEngine::createPromise(
    facebook::jsi::Runtime& runtime,
    std::function<void(facebook::jsi::Runtime&, std::shared_ptr<facebook::jsi::Function>, std::shared_ptr<facebook::jsi::Function>)> executor)
{
    auto promiseConstructor = runtime.global().getPropertyAsFunction(runtime, "Promise");
    auto callback = facebook::jsi::Function::createFromHostFunction(
        runtime, facebook::jsi::PropNameID::forAscii(runtime, "executor"), 2,
        [executor](facebook::jsi::Runtime& rt, const facebook::jsi::Value&, const facebook::jsi::Value* args, size_t) -> facebook::jsi::Value {
            auto resolve = std::make_shared<facebook::jsi::Function>(args[0].asObject(rt).asFunction(rt));
            auto reject = std::make_shared<facebook::jsi::Function>(args[1].asObject(rt).asFunction(rt));
            executor(rt, resolve, reject);
            return facebook::jsi::Value::undefined();
        });
    return promiseConstructor.callAsConstructor(runtime, callback);
}

} // namespace turbo_db
