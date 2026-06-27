cmake_minimum_required(VERSION 3.13)
project(micp
    VERSION 0.1.0
    DESCRIPTION "MICP - Multica Industrial Communication Protocol reference stack"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(MICP_BUILD_TESTS "Build MICP unit tests" ON)
option(MICP_BUILD_EXAMPLES "Build MICP example programs" ON)
option(MICP_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

# ---------------------------------------------------------------- library ---
add_library(micp STATIC
    src/micp_crc.c
    src/micp_frame.c
    src/micp_session.c
    src/micp_types.c
    src2/micp2_signal.c
    src2/micp2_matrix.c)

target_include_directories(micp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(micp PRIVATE -Wall -Wextra -Wpedantic)
    if(MICP_WARNINGS_AS_ERRORS)
        target_compile_options(micp PRIVATE -Werror)
    endif()
endif()

add_library(micp::micp ALIAS micp)

# --------------------------------------------------------------- examples ---
if(MICP_BUILD_EXAMPLES)
    add_executable(micp_loopback_demo examples/loopback_demo.c)
    target_link_libraries(micp_loopback_demo PRIVATE micp)
    add_executable(micp2_matrix_demo examples/micp2_matrix_demo.c)
    target_link_libraries(micp2_matrix_demo PRIVATE micp)
endif()

# ------------------------------------------------------------------ tests ---
if(MICP_BUILD_TESTS)
    enable_testing()
    foreach(t crc frame session micp2_signal)
        add_executable(test_${t} tests/test_${t}.c)
        target_link_libraries(test_${t} PRIVATE micp)
        target_include_directories(test_${t} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        add_test(NAME ${t} COMMAND test_${t})
    endforeach()
endif()

# ----------------------------------------------------------------- install --
install(TARGETS micp ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
