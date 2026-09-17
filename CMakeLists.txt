cmake_minimum_required(VERSION 3.16)
project(spark_media_02 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
# 第三方依赖路径（假设主项目已构建好）
set(SPARK_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/../../server/third_party")
set(SPARK_THIRDPARTY_INCLUDE_DIR "${SPARK_THIRDPARTY_DIR}/include")
set(SPARK_THIRDPARTY_LIB_DIR "${SPARK_THIRDPARTY_DIR}/lib")
# 可执行文件
add_executable(spark_media_02
src/main.cpp
src/server/http_server.cpp
src/rtc/peer_connection.cpp
)
# 包含目录
target_include_directories(spark_media_02 PUBLIC
${CMAKE_SOURCE_DIR}/include
"${SPARK_THIRDPARTY_INCLUDE_DIR}"
)
# 链接库
target_link_directories(spark_media_02 PUBLIC "${SPARK_THIRDPARTY_LIB_DIR}")
target_link_libraries(spark_media_02 PUBLIC
Threads::Threads
# libdatachannel 及其依赖（顺序很重要！）
"${SPARK_THIRDPARTY_LIB_DIR}/libdatachannel.a"
"${SPARK_THIRDPARTY_LIB_DIR}/libjuice.a"
"${SPARK_THIRDPARTY_LIB_DIR}/libusrsctp.a"
"${SPARK_THIRDPARTY_LIB_DIR}/libsrtp2.a"
"${SPARK_THIRDPARTY_LIB_DIR}/libssl.a"
"${SPARK_THIRDPARTY_LIB_DIR}/libcrypto.a"
# 系统库
dl
m
pthread
)
