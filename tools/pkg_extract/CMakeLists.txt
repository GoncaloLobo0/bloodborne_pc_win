# pkg-extract: fake-signed (backup) PS4 packages to a game folder. Needs Crypto++ and zlib
# (windows/setup.cmd installs them).
#   cmake -S tools/pkg_extract -B out/pkg_extract -G Ninja && ninja -C out/pkg_extract
cmake_minimum_required(VERSION 3.24)
project(pkg_extract LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(CRYPTOPP REQUIRED IMPORTED_TARGET libcrypto++)
find_package(ZLIB REQUIRED)
add_executable(pkg-extract main.cpp pkg.cpp pkg_type.cpp crypto.cpp)
# common/types.h and common/endian.h of the vendored shadPS4 code.
target_include_directories(pkg-extract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../gpu/shadps4)
target_link_libraries(pkg-extract PRIVATE PkgConfig::CRYPTOPP ZLIB::ZLIB)
set_target_properties(pkg-extract PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../out)
