cmake_minimum_required(VERSION 3.4.1)
project(SecureDB)

set (CMAKE_VERBOSE_MAKEFILE ON)

# Define the root of the library
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

target_include_directories(
    react-native-secure-db    PUBLIC
    ${PACKAGE_ROOT}/cpp
    src/main/cpp
    generated/jni
)

# Link against React Native's JSI and other core libraries
# These are provided by the React Native Gradle plugin automatically
target_link_libraries(
    react-native-secure-db
    jsi
    reactnative
    log
    android
)
