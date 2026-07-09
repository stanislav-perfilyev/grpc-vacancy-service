# FindgRPC.cmake — fallback when gRPCConfig.cmake is not installed.
#
# Used on Ubuntu 22.04 where libgrpc++-dev (1.46.x) does not ship cmake
# config files. Creates the same imported targets that gRPCConfig.cmake would:
#   gRPC::grpc++             — core gRPC C++ library
#   gRPC::grpc++_reflection  — server reflection
#   gRPC::grpc_cpp_plugin    — protoc plugin executable

find_package(PkgConfig REQUIRED)

pkg_check_modules(PC_GRPCPP REQUIRED grpc++)
pkg_check_modules(PC_GRPCPP_REFL QUIET grpc++_reflection)

find_library(gRPC_grpcpp_LIBRARY
    NAMES grpc++
    HINTS ${PC_GRPCPP_LIBRARY_DIRS}
    REQUIRED
)

find_library(gRPC_grpcpp_reflection_LIBRARY
    NAMES grpc++_reflection
    HINTS ${PC_GRPCPP_LIBRARY_DIRS}
)

find_program(gRPC_CPP_PLUGIN_EXECUTABLE
    NAMES grpc_cpp_plugin
    REQUIRED
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(gRPC
    REQUIRED_VARS gRPC_grpcpp_LIBRARY gRPC_CPP_PLUGIN_EXECUTABLE
)

if(gRPC_FOUND)
    if(NOT TARGET gRPC::grpc++)
        add_library(gRPC::grpc++ UNKNOWN IMPORTED)
        set_target_properties(gRPC::grpc++ PROPERTIES
            IMPORTED_LOCATION             "${gRPC_grpcpp_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PC_GRPCPP_INCLUDE_DIRS}"
        )
    endif()

    if(NOT TARGET gRPC::grpc++_reflection AND gRPC_grpcpp_reflection_LIBRARY)
        add_library(gRPC::grpc++_reflection UNKNOWN IMPORTED)
        set_target_properties(gRPC::grpc++_reflection PROPERTIES
            IMPORTED_LOCATION "${gRPC_grpcpp_reflection_LIBRARY}"
        )
    elseif(NOT TARGET gRPC::grpc++_reflection)
        # Stub target so downstream link_libraries does not break
        add_library(gRPC::grpc++_reflection INTERFACE IMPORTED)
    endif()

    if(NOT TARGET gRPC::grpc_cpp_plugin)
        add_executable(gRPC::grpc_cpp_plugin IMPORTED)
        set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES
            IMPORTED_LOCATION "${gRPC_CPP_PLUGIN_EXECUTABLE}"
        )
    endif()
endif()
