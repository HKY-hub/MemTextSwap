cmake_minimum_required(VERSION 3.20)

project(MemTextSwap LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /EHsc /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(GTI_ARCH_SUFFIX "32")
else()
    set(GTI_ARCH_SUFFIX "64")
endif()

add_subdirectory(third_party/minhook)
add_subdirectory(src/NativeCore)

option(GTI_BUILD_TESTS "Build native unit tests" ON)
if(GTI_BUILD_TESTS)
    add_subdirectory(tests/native)
endif()
