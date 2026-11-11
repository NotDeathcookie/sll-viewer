cmake_minimum_required(VERSION 3.21)

project(sllviewer VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Work around a known fmt-library/Clang incompatibility: newer Clang versions
# reject fmt's compile-time FMT_STRING() checks as not-constant-expressions.
# This disables that one check; it does not affect how our own code behaves.
# (fmt 10.2.1, which Geode pins, only honors FMT_CONSTEVAL, set empty here.)
add_compile_definitions(FMT_CONSTEVAL=)

add_library(${PROJECT_NAME} SHARED
    main.cpp
)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode installation")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME}
