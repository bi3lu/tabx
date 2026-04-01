cmake_minimum_required(VERSION 3.20)

project(fast_parser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(PYBIND11_FINDPYTHON ON)

option(FAST_PARSER_NATIVE
    "Enable -march=native for local builds. Turn OFF when building distribution wheels."
    ON
)

add_library(fast_parser STATIC
    cpp/parser.cpp
)

target_include_directories(fast_parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/cpp
)

target_compile_options(fast_parser PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3>
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
)

if(FAST_PARSER_NATIVE AND NOT MSVC)
    target_compile_options(fast_parser PRIVATE -march=native)
endif()

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG QUIET)

if(NOT pybind11_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(pybind11)
endif()

pybind11_add_module(_core
    python/wrapper.cpp
)

target_link_libraries(_core PRIVATE fast_parser)
target_include_directories(_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cpp)

# Install rule used by scikit-build-core and cmake --install.
install(TARGETS _core DESTINATION csvpp)

set_target_properties(fast_parser PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/lib
)

# Place the extension inside the package directory so that
# the repo-root build tree is importable without installation.
set_target_properties(_core PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/python/csvpp
)