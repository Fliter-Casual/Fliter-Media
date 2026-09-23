cmake_minimum_required(VERSION 3.16)
project(Fliter-Media LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# 第三方依赖路径（假设主项目已构建好）
set(FLITER_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/../lib/third_party")
set(FLITER_THIRDPARTY_INCLUDE_DIR "${FLITER_THIRDPARTY_DIR}/include")
set(FLITER_THIRDPARTY_LIB_DIR "${FLITER_THIRDPARTY_DIR}/lib")

# 头文件搜索路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${THIRD_PARTY_INCLUDE_DIR}
)

# 源文件
set(SOURCES
    src/main.cpp
    src/server/http_server.cpp
    src/rtc/peer_connection.cpp
)

# 可执行文件
add_executable(Fliter-Media ${SOURCES})

# 链接库
# 注意：静态库链接顺序很重要，被依赖的库要放在后面
target_link_libraries(Fliter-Media
    Threads::Threads
    # libdatachannel 静态库
    "${THIRD_PARTY_LIB_DIR}/libdatachannel.a"
    # libdatachannel 依赖的静态库（必须放在 libdatachannel 之后）
    "${THIRD_PARTY_LIB_DIR}/libjuice.a"
    "${THIRD_PARTY_LIB_DIR}/libusrsctp.a"
    "${THIRD_PARTY_LIB_DIR}/libsrtp2.a"
    # OpenSSL
    "${THIRD_PARTY_LIB_DIR}/libssl.a"
    "${THIRD_PARTY_LIB_DIR}/libcrypto.a"
    # 系统库
    dl
    m
    pthread
)

# 安装
install(TARGETS Fliter-Media DESTINATION bin)
