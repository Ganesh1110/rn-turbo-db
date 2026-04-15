cmake_minimum_required(VERSION 3.4.1)
project(SecureDB)

set(PACKAGE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_library(
    react-native-secure-db    SHARED
    ${PACKAGE_ROOT}/cpp/SecureDBImpl.cpp
    ${PACKAGE_ROOT}/cpp/DBEngine.cpp
    ${PACKAGE_ROOT}/cpp/PersistentBPlusTree.cpp
    ${PACKAGE_ROOT}/cpp/WALManager.cpp
    ${PACKAGE_ROOT}/cpp/MMapRegion.cpp
    ${PACKAGE_ROOT}/cpp/BinarySerializer.cpp
    ${PACKAGE_ROOT}/cpp/BufferedBTree.cpp
    src/main/cpp/SecureCryptoAndroid.cpp
    src/main/cpp/SecureDBModule.cpp
)

set_target_properties(
    react-native-secure-db PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Include directories - will be provided by Prefab from host React Native
include_directories(
    ${PACKAGE_ROOT}/cpp
    src/main/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/jni
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/jni/react/renderer/components/SecureDBSpec
)

# Link libraries - Prefab will resolve these from host React Native
target_link_libraries(
    react-native-secure-db
    jsi
    reactnative
    log
    android
)
