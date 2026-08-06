#include <gtest/gtest.h>

#include "gti/hooks.h"

namespace {

int g_called = 0;

__declspec(noinline) int __cdecl TargetFunction(int value) {
    ++g_called;
    return value + 1;
}

using TargetFn = int(__cdecl*)(int);
TargetFn g_original = nullptr;

int __cdecl DetourFunction(int value) {
    return g_original(value) * 10;
}

}  // namespace

TEST(Hook, InProcessDetourRoundTrip) {
    g_called = 0;
    ASSERT_TRUE(gti::HookManager::Install("test.target", reinterpret_cast<void*>(&TargetFunction),
                                          reinterpret_cast<void*>(&DetourFunction),
                                          reinterpret_cast<void**>(&g_original)));
    EXPECT_EQ(TargetFunction(5), 60);
    EXPECT_EQ(g_called, 1);
    EXPECT_TRUE(gti::HookManager::UninstallAll());
    EXPECT_EQ(TargetFunction(5), 6);
    EXPECT_EQ(g_called, 2);
}
