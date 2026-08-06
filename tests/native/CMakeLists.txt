include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)

set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(gti_native_tests
    test_crc_frame.cpp
    test_filter.cpp
    test_hook.cpp
    test_string_util.cpp
)
target_link_libraries(gti_native_tests PRIVATE gti_core gtest_main)
target_include_directories(gti_native_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/NativeCore/include)
