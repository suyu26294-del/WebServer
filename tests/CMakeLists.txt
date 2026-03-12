cmake_minimum_required(VERSION 3.16)

# ── Buffer 单元测试 ───────────────────────────────────────────────────────────
add_executable(test_buffer
    test_buffer.cpp
    ${CMAKE_SOURCE_DIR}/src/buffer/Buffer.cpp
)
target_include_directories(test_buffer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(test_buffer PRIVATE cxx_std_17)
add_test(NAME BufferTest COMMAND test_buffer)

# ── TimerHeap 单元测试 ────────────────────────────────────────────────────────
add_executable(test_timer
    test_timer.cpp
    ${CMAKE_SOURCE_DIR}/src/timer/TimerHeap.cpp
)
target_include_directories(test_timer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(test_timer PRIVATE cxx_std_17)
add_test(NAME TimerTest COMMAND test_timer)

# ── HttpRequest 单元测试 ──────────────────────────────────────────────────────
add_executable(test_httprequest
    test_httprequest.cpp
    ${CMAKE_SOURCE_DIR}/src/buffer/Buffer.cpp
    ${CMAKE_SOURCE_DIR}/src/http/HttpRequest.cpp
)
target_include_directories(test_httprequest PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(test_httprequest PRIVATE cxx_std_17)
target_link_libraries(test_httprequest PRIVATE Threads::Threads)
if(HAVE_MYSQL AND MYSQL_LIB)
    target_link_libraries(test_httprequest PRIVATE ${MYSQL_LIB})
    target_compile_definitions(test_httprequest PRIVATE HAVE_MYSQL)
    target_sources(test_httprequest PRIVATE
        ${CMAKE_SOURCE_DIR}/src/pool/SqlConnPool.cpp)
endif()
add_test(NAME HttpRequestTest COMMAND test_httprequest)
