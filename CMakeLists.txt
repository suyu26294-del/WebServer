cmake_minimum_required(VERSION 3.16)
project(WebServer VERSION 1.0.0 LANGUAGES CXX)

# ── 编译选项 ──────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release 模式默认开启优化
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译警告
add_compile_options(
    -Wall -Wextra -Wshadow -Wconversion
    -Wpedantic -Wno-unused-parameter
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
    $<$<CONFIG:Debug>:-g3 -fsanitize=address,undefined>
)

add_link_options(
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

# ── 头文件路径 ────────────────────────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── 查找依赖 ──────────────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# MySQL（可选：若不可用则禁用数据库相关功能）
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h
    HINTS /usr/include /usr/local/include)
find_library(MYSQL_LIB NAMES mysqlclient
    HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)

if(MYSQL_INCLUDE_DIR AND MYSQL_LIB)
    message(STATUS "MySQL found: ${MYSQL_LIB}")
    set(HAVE_MYSQL TRUE)
    add_compile_definitions(HAVE_MYSQL)
    include_directories(${MYSQL_INCLUDE_DIR})
else()
    message(WARNING "MySQL not found. DB features disabled.")
    set(HAVE_MYSQL FALSE)
endif()

# ── 源文件收集 ────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/buffer/Buffer.cpp
    src/log/Log.cpp
    src/timer/TimerHeap.cpp
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
    src/http/HttpConn.cpp
    src/server/Server.cpp
)

if(HAVE_MYSQL)
    list(APPEND SOURCES src/pool/SqlConnPool.cpp)
endif()

# ── 可执行文件 ────────────────────────────────────────────────────────────────
add_executable(webserver ${SOURCES})

target_link_libraries(webserver
    PRIVATE
        Threads::Threads
        $<$<BOOL:${HAVE_MYSQL}>:${MYSQL_LIB}>
)

# ── 安装规则 ──────────────────────────────────────────────────────────────────
install(TARGETS webserver DESTINATION bin)
install(DIRECTORY www/   DESTINATION www)
install(DIRECTORY conf/  DESTINATION conf)
install(DIRECTORY sql/   DESTINATION sql)

# ── 测试 ──────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests EXCLUDE_FROM_ALL)

# ── 打印构建信息 ──────────────────────────────────────────────────────────────
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "MySQL       : ${HAVE_MYSQL}")
