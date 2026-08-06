#include <gtest/gtest.h>

#include "gti/text_filter.h"

using namespace gti;

TEST(TextFilter, RejectsEmptyAndTooLong) {
    TextFilter filter;
    std::string reason;
    EXPECT_FALSE(filter.ShouldTranslate("", &reason));
    EXPECT_EQ(reason, "empty");
    EXPECT_FALSE(filter.ShouldTranslate(std::string(1025, 'a'), &reason));
    EXPECT_EQ(reason, "length");
}

TEST(TextFilter, RejectsAlreadyChinese) {
    TextFilter filter;
    std::string reason;
    EXPECT_FALSE(filter.ShouldTranslate("你好世界", &reason));
    EXPECT_EQ(reason, "already-cjk");
    EXPECT_FALSE(filter.ShouldTranslate("Hello 世界", &reason));
    EXPECT_EQ(reason, "already-cjk");
}

TEST(TextFilter, RejectsNonLettersAndPaths) {
    TextFilter filter;
    std::string reason;
    EXPECT_FALSE(filter.ShouldTranslate("12345", &reason));
    EXPECT_EQ(reason, "no-letters");
    EXPECT_FALSE(filter.ShouldTranslate("C:\\Program Files\\x", &reason));
    EXPECT_EQ(reason, "path-or-url");
    EXPECT_FALSE(filter.ShouldTranslate("https://example.com", &reason));
    EXPECT_EQ(reason, "path-or-url");
}

TEST(TextFilter, AcceptsDialogue) {
    TextFilter filter;
    std::string reason;
    EXPECT_TRUE(filter.ShouldTranslate("Hello World #1", &reason));
    EXPECT_TRUE(filter.ShouldTranslate("What are you doing?", &reason));
}

TEST(TextFilter, DedupeRejectsRepeat) {
    TextFilter filter;
    std::string reason;
    EXPECT_TRUE(filter.ShouldTranslate("repeat me", &reason));
    EXPECT_FALSE(filter.ShouldTranslate("repeat me", &reason));
    EXPECT_EQ(reason, "recent-dedupe");
}
