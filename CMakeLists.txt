cmake_minimum_required(VERSION 3.16)
project(Fliter-Media LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
# 第三方依赖路径（假设主项目已构建好）
set(FLITER_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/../lib/third_party")
set(FLITER_THIRDPARTY_INCLUDE_DIR "${FLITER_THIRDPARTY_DIR}/include")
set(FLITER_THIRDPARTY_LIB_DIR "${FLITER_THIRDPARTY_DIR}/lib")
# 可执行文件
add_executable(Fliter-Media
src/main.cpp
src/server/http_server.cpp
src/rtc/peer_connection.cpp
)
# 包含目录
target_include_directories(Fliter-Media PUBLIC
${CMAKE_SOURCE_DIR}/include
"${FLITER_THIRDPARTY_INCLUDE_DIR}"
)
# 链接库
target_link_directories(Fliter-Media PUBLIC "${FLITER_THIRDPARTY_LIB_DIR}")
target_link_libraries(Fliter-Media PUBLIC
Threads::Threads
# libdatachannel 及其依赖（顺序很重要！）
"${FLITER_THIRDPARTY_LIB_DIR}/libdatachannel.a"
"${FLITER_THIRDPARTY_LIB_DIR}/libjuice.a"
"${FLITER_THIRDPARTY_LIB_DIR}/libusrsctp.a"
"${FLITER_THIRDPARTY_LIB_DIR}/libsrtp2.a"
"${FLITER_THIRDPARTY_LIB_DIR}/libssl.a"
"${FLITER_THIRDPARTY_LIB_DIR}/libcrypto.a"
# 系统库
dl
m
pthread
)
