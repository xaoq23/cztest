// The optional parts: the compile time name check and std::expected.
#include <cztest/cztest.hpp>

#include <string>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <expected>
#endif

TEST(Metadata, TheTestNameIsCheckedAtCompileTime) {
    // The check is a consteval function, so a name that would break the
    // --filter syntax is a compile error.  This test only proves that the
    // normal names pass.
    static_assert(cztest::detail::CheckTestName("Suite").size() == 5);
    static_assert(cztest::detail::CheckTestName("Case").size() == 4);
}

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
TEST(Expected, OkHoldsAValue) {
    const std::expected<int, std::string> result{42};
    EXPECT_TRUE(cztest::ExpectOk(result).ok);
}

TEST(Expected, AnErrorIsReported) {
    const std::expected<int, std::string> result = std::unexpected(std::string{"no value"});
    const auto failed = cztest::ExpectOk(result);
    EXPECT_TRUE(failed.failed());
    EXPECT_NE(failed.message.find("no value"), std::string::npos);
}
#endif
