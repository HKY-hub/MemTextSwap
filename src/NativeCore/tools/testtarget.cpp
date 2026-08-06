#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <windows.h>

extern "C" __declspec(dllexport) __declspec(noinline) const char* __cdecl GTI_ProduceLine(
    int* counter) {
    thread_local char buffer[128];
    int current = (*counter) + 1;
    *counter = current;
    std::snprintf(buffer, sizeof(buffer), "Hello World #%d", current);
    return buffer;
}

int main(int argc, char** argv) {
    long lines = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--lines") == 0 && i + 1 < argc) {
            lines = std::atol(argv[++i]);
        }
    }
    std::printf("[GTI-READY] pid=%lu arch=%s\n", GetCurrentProcessId(),
                sizeof(void*) == 8 ? "x64" : "x86");
    std::fflush(stdout);

    int counter = 0;
    long emitted = 0;
    while (lines < 0 || emitted < lines) {
        std::printf("%s\n", GTI_ProduceLine(&counter));
        std::fflush(stdout);
        ++emitted;
        Sleep(500);
    }
    std::printf("[GTI-DONE] lines=%ld\n", emitted);
    return 0;
}
