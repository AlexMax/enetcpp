cmake_minimum_required(VERSION 3.5)

include(CheckCXXCompilerFlag)

# Check all passed flags and assign them to the target if found.
function(target_cxx_compiler_flags)
    cmake_parse_arguments(PARSE_ARGV 1 _ARGS "" "" "PUBLIC")
    foreach(_FLAG ${_ARGS_PUBLIC})
        check_cxx_compiler_flag(${_FLAG} "ENETCPP_PUBLIC_${_FLAG}")
        if(${ENETCPP_PUBLIC_${_FLAG}})
            target_compile_options(${ARGV0} PUBLIC ${_FLAG})
        endif()
    endforeach()
endfunction()

project(enetcpp LANGUAGES CXX)

set(ENETCPP_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/callbacks.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/enetcpp.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/list.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/protocol.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/time.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/types.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/utility.h")

set(ENETCPP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/callbacks.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/compress.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/host.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/list.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/packet.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/peer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/protocol.cpp")

# Add sources and headers based on platform.
if(WIN32)
    list(APPEND ENETCPP_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/win32.h")
    list(APPEND ENETCPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/win32.cpp")
elseif(UNIX)
    list(APPEND ENETCPP_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/enetcpp/unix.h")
    list(APPEND ENETCPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/unix.cpp")
else()
    status(FATAL_ERROR "Unknown system, please create your own platform abstraction.")
endif()

# EnetCPP static library.
add_library(enetcpp STATIC ${ENETCPP_SOURCES} ${ENETCPP_HEADERS})
if(MSVC)
    # MSVC and ClangCL
    # Clang's /W4 aliases -Wall -Wextra but -Wall is turned into -Weverything.
    target_cxx_compiler_flags(enetcpp PUBLIC /W4 -Wmost -Wc++14-compat)
else()
    # GCC and Clang
    target_cxx_compiler_flags(enetcpp PUBLIC -Wall -Wextra -Wmost -Wc++14-compat)
endif()
target_include_directories(enetcpp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(enetcpp)
if(WIN32)
    target_link_libraries(enetcpp PUBLIC winmm ws2_32)
endif()
