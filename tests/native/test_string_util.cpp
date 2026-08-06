#include <gtest/gtest.h>

#include "gti/string_util.h"

using namespace gti;

TEST(StringUtil, Fnv1a64KnownVector) {
    EXPECT_EQ(Fnv1a64(""), 14695981039346656037ULL);
    EXPECT_EQ(Fnv1a64("Hello World #1"), 10291341955925813465ULL);
}

TEST(StringUtil, CjkDetection) {
    EXPECT_TRUE(HasCjkUtf8("你好"));
    EXPECT_TRUE(HasCjkUtf8("abc 中文"));
    EXPECT_FALSE(HasCjkUtf8("hello world"));
    EXPECT_FALSE(HasCjkUtf8(""));
}

TEST(StringUtil, Utf8WidenRoundTrip) {
    const wchar_t* wide = L"你好, world!";
    std::string utf8 = WidenToUtf8(wide);
    EXPECT_EQ(Utf8ToWiden(utf8.c_str()), std::wstring(wide));
}

TEST(StringUtil, HasAsciiLetter) {
    EXPECT_TRUE(HasAsciiLetter("abc"));
    EXPECT_FALSE(HasAsciiLetter("123"));
    EXPECT_FALSE(HasAsciiLetter(""));
}
