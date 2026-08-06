set(GTI_CORE_SOURCES
    src/crc32.cpp
    src/log.cpp
    src/text_filter.cpp
    src/string_util.cpp
    src/signature_scan.cpp
    src/engine_detect.cpp
    src/hook_manager.cpp
    src/ipc_client.cpp
    src/hooks_testtarget.cpp
    src/hooks_unity_il2cpp.cpp
    src/hooks_unity_mono.cpp
    src/hooks_renpy.cpp
    src/hooks_rpgmaker.cpp
)

add_library(gti_core STATIC ${GTI_CORE_SOURCES})
target_include_directories(gti_core PUBLIC include)
target_link_libraries(gti_core PUBLIC minhook)

add_library(nativecore SHARED src/dllmain.cpp)
target_link_libraries(nativecore PRIVATE gti_core)
target_sources(nativecore PRIVATE exports.def)
set_target_properties(nativecore PROPERTIES
    OUTPUT_NAME "NativeCore${GTI_ARCH_SUFFIX}"
    PREFIX ""
)

add_executable(injector tools/injector.cpp)
target_link_libraries(injector PRIVATE gti_core)
set_target_properties(injector PROPERTIES OUTPUT_NAME "Injector${GTI_ARCH_SUFFIX}")

add_executable(testtarget tools/testtarget.cpp)
set_target_properties(testtarget PROPERTIES OUTPUT_NAME "TestTarget${GTI_ARCH_SUFFIX}")

add_executable(nativepipeprobe tools/nativepipeprobe.cpp)
target_link_libraries(nativepipeprobe PRIVATE gti_core)
set_target_properties(nativepipeprobe PROPERTIES OUTPUT_NAME "NativePipeProbe${GTI_ARCH_SUFFIX}")
