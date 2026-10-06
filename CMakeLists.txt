cmake_minimum_required(VERSION 3.28)
project(mc-cpp CXX)

# ============================================================================
# mc-cpp — Minecraft Java 26.2 (protocol 776) C++ rewrite, plan §1/§3
# Deps (only three, per plan §1): asio 1.30 standalone, spdlog 1.15
# (header-only, bundled fmt), zlib 1.3. No vcpkg/conan/network fetch.
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)             # 默认 Release
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG") # -O2 (plan P0 验收口径)

add_compile_options(-Wall -Wextra -Wpedantic)

# asio standalone; spdlog header-only (捆绑 fmt，不用 SPDLOG_FMT_EXTERNAL)。
# FMT_HEADER_ONLY: 捆绑 fmt 的 vformat 等符号纯头文件化，避免 fmt 链接错误
add_compile_definitions(ASIO_STANDALONE SPDLOG_HEADER_ONLY FMT_HEADER_ONLY=1)

# asio + spdlog 头统一经 -isystem 引入（SYSTEM 抑制第三方头警告）
include_directories(SYSTEM /home/z/my-project/tools/cpptools/usr/include)

# zlib：P1 压缩协议使用，先占位链接
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(src)

# P0-b: 抓包代理 tools/capture-proxy（独立可执行，另一个代理负责，届时取消注释）
# add_subdirectory(tools/capture-proxy)
