cmake_minimum_required(VERSION 3.16)
project(decompress C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(decompress
    src/main.c
    src/zip_parser.c
)

target_include_directories(decompress PRIVATE src)

find_package(ZLIB)
if(ZLIB_FOUND)
    target_compile_definitions(decompress PRIVATE HAVE_ZLIB)
    target_link_libraries(decompress PRIVATE ZLIB::ZLIB)
endif()
