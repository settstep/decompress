cmake_minimum_required(VERSION 3.16)
project(decompress C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(DECOMPRESS_BUILD_EXE "Build decompress executable" ON)
option(DECOMPRESS_BUILD_LIB "Build reusable parser library" OFF)

add_library(decompress_parser STATIC
    src/zip_parser.c
)

target_include_directories(decompress_parser PUBLIC src)

if(DECOMPRESS_BUILD_LIB)
    add_library(decompress_lib ALIAS decompress_parser)
endif()

if(DECOMPRESS_BUILD_EXE)
    add_executable(decompress
        src/main.c
    )
    target_link_libraries(decompress PRIVATE decompress_parser)
endif()

set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB)
if(DECOMPRESS_BUILD_EXE AND ZLIB_FOUND)
    target_compile_definitions(decompress PRIVATE HAVE_ZLIB)
    target_include_directories(decompress PRIVATE ${ZLIB_INCLUDE_DIRS})
    if(WIN32 AND EXISTS "C:/msys64/ucrt64/lib/libz.a")
        target_link_libraries(decompress PRIVATE "C:/msys64/ucrt64/lib/libz.a")
    else()
        target_link_libraries(decompress PRIVATE ZLIB::ZLIB)
    endif()
endif()
