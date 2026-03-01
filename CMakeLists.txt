cmake_minimum_required(VERSION 3.15)
project(decompress VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Find zlib (provides inflate for DEFLATE-compressed entries)
# --------------------------------------------------------------------------
find_package(ZLIB REQUIRED)

# --------------------------------------------------------------------------
# Library: zip record reading + decompression
# --------------------------------------------------------------------------
add_library(ziplib STATIC
    src/zip_records.c
)

target_include_directories(ziplib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(ziplib PUBLIC ZLIB::ZLIB)

# Compiler warnings
if(MSVC)
    target_compile_options(ziplib PRIVATE /W4)
else()
    target_compile_options(ziplib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------------------------------
# Executable: console tool
# --------------------------------------------------------------------------
add_executable(decompress src/main.c)
target_link_libraries(decompress PRIVATE ziplib)

if(MSVC)
    target_compile_options(decompress PRIVATE /W4)
else()
    target_compile_options(decompress PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------------------------------
# Installation (optional)
# --------------------------------------------------------------------------
install(TARGETS decompress RUNTIME DESTINATION bin)
install(FILES src/zip_records.h DESTINATION include)
