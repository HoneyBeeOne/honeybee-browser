cmake_minimum_required(VERSION 3.20)

project(
  honeybee
  VERSION 0.1.0
  DESCRIPTION "A community-built web browser from Cairo University students"
  LANGUAGES CXX
)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Optimization flags by build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-g -O0 -DDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3 -DNDEBUG)
endif()

# تفعيل نظام الاختبارات
enable_testing()

# جلب مكتبة Catch2 للاختبارات
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Find dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM4 REQUIRED IMPORTED_TARGET gtkmm-4.0)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

# ----------------------------------------------------------------------------
# lexbor: vendored HTML parser (submodule in third_party/lexbor)
# ----------------------------------------------------------------------------
# Configure lexbor before adding it: build static only, skip its tests,
# examples, benchmarks, and install rules. We embed it, we don't install it.
set(LEXBOR_BUILD_SHARED      OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_STATIC      ON  CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_TESTS_CPP   OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_BENCHMARKS  OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_UTILS       OFF CACHE BOOL "" FORCE)
set(LEXBOR_BUILD_WASM        OFF CACHE BOOL "" FORCE)
set(LEXBOR_INSTALL_HEADERS   OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/lexbor EXCLUDE_FROM_ALL)

# Add source directory
add_subdirectory(src)
add_subdirectory(tests)
