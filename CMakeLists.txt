cmake_minimum_required(VERSION 3.16)
project(grpc_vacancy_service CXX)

# ── ccache (faster rebuilds) ──────────────────────────────────────────────────
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Dependencies ─────────────────────────────────────────────────────────────

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC     CONFIG REQUIRED)
find_package(GTest    CONFIG QUIET)

if(NOT GTest_FOUND)
    find_package(GTest REQUIRED)
endif()

find_package(Threads REQUIRED)

# ─── Protobuf code generation ─────────────────────────────────────────────────

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_SRC "${PROTO_DIR}/vacancy.proto")

# Generated files land in the build tree
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

# Get plugin paths
get_target_property(GRPC_CPP_PLUGIN_EXEC gRPC::grpc_cpp_plugin LOCATION)

add_custom_command(
    OUTPUT
        "${PROTO_GEN_DIR}/vacancy.pb.cc"
        "${PROTO_GEN_DIR}/vacancy.pb.h"
        "${PROTO_GEN_DIR}/vacancy.grpc.pb.cc"
        "${PROTO_GEN_DIR}/vacancy.grpc.pb.h"
    COMMAND protobuf::protoc
        "--proto_path=${PROTO_DIR}"
        "--cpp_out=${PROTO_GEN_DIR}"
        "--grpc_out=${PROTO_GEN_DIR}"
        "--plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXEC}"
        "${PROTO_SRC}"
    DEPENDS "${PROTO_SRC}"
    COMMENT "Generating protobuf/gRPC code from vacancy.proto"
)

# Shared generated library (used by server, client, tests)
add_library(vacancy_proto STATIC
    "${PROTO_GEN_DIR}/vacancy.pb.cc"
    "${PROTO_GEN_DIR}/vacancy.grpc.pb.cc"
)
target_include_directories(vacancy_proto PUBLIC "${PROTO_GEN_DIR}")
target_link_libraries(vacancy_proto PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
)

# ─── Strict warning flags ─────────────────────────────────────────────────────

set(STRICT_WARNINGS
    -Wall -Wextra -Werror -Wpedantic
    -Wconversion -Wshadow -Wnull-dereference
    -Wno-unused-parameter          # gRPC generated code triggers this
    -fno-omit-frame-pointer
)

# ─── Server ───────────────────────────────────────────────────────────────────

add_executable(grpc_server
    src/main.cpp
)
target_include_directories(grpc_server PRIVATE
    "${PROTO_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(grpc_server PRIVATE
    vacancy_proto
    gRPC::grpc++
    gRPC::grpc++_reflection
)
target_compile_options(grpc_server PRIVATE ${STRICT_WARNINGS})

# ─── Client ───────────────────────────────────────────────────────────────────

add_executable(grpc_client
    client/main.cpp
)
target_include_directories(grpc_client PRIVATE
    "${PROTO_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/client"
)
target_link_libraries(grpc_client PRIVATE
    vacancy_proto
    gRPC::grpc++
)
target_compile_options(grpc_client PRIVATE ${STRICT_WARNINGS})

# ─── Tests ────────────────────────────────────────────────────────────────────

enable_testing()

add_executable(vacancy_tests
    tests/test_vacancy_service.cpp
    tests/test_mt_interceptor.cpp
)
target_include_directories(vacancy_tests PRIVATE
    "${PROTO_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(vacancy_tests PRIVATE
    vacancy_proto
    gRPC::grpc++
    gRPC::grpc++_reflection
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)
target_compile_options(vacancy_tests PRIVATE ${STRICT_WARNINGS})

include(GoogleTest)
gtest_discover_tests(vacancy_tests)

# ─── Install ──────────────────────────────────────────────────────────────────

install(TARGETS grpc_server grpc_client
        RUNTIME DESTINATION bin)
