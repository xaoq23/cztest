// SCOPED_TRACE and SKIP.
#include <cztest/cztest.hpp>

#include <string>
#include <vector>

TEST(Trace, TheStackIsUnwoundAfterTheScope) {
    EXPECT_TRUE(cztest::detail::TraceStack().empty());

    {
        SCOPED_TRACE("inner");
        EXPECT_EQ(cztest::detail::TraceStack().size(), 1u);
        {
            SCOPED_TRACE("inner-most");
            EXPECT_EQ(cztest::detail::TraceStack().size(), 2u);
        }
        EXPECT_EQ(cztest::detail::TraceStack().size(), 1u);
    }

    EXPECT_TRUE(cztest::detail::TraceStack().empty());
}

TEST(Trace, TheMessageIsAStreamExpression) {
    const std::vector<std::string> items{"a", "b"};

    for (const auto& item : items) {
        SCOPED_TRACE("item: " << item << " (" << item.size() << " chars)");
        SUCCEED();
    }
}

TEST(Skip, EndsTheTestWithoutFailing) {
    SKIP("not implemented yet");
    FAIL() << "must not be reached";
}

TEST(Skip, TheReasonIsCarried) {
    // The runner prints the reason of a skipped test, so it has to survive
    // the trip through the exception.
    try {
        SKIP("not ready yet");
        FAIL() << "must not be reached";
    } catch (const cztest::SkipTest& skip) {
        EXPECT_STREQ(skip.what(), "not ready yet");
    }
}
