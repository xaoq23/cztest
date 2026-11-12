// The matcher algebra: AllOf, AnyOf, Not and ElementsAre.
#include <cztest/cztest.hpp>

#include <string>
#include <vector>

namespace {

/// A type that only knows the three way comparison.
struct Version {
    int major{0};
    int minor{0};

    std::strong_ordering operator<=>(const Version&) const = default;
};

}  // namespace

TEST(Matcher, AllOfNeedsEveryMatcherToMatch) {
    EXPECT_TRUE(cztest::Matches(cztest::AllOf(cztest::Ge(1), cztest::Lt(10)), 5));
    EXPECT_FALSE(cztest::Matches(cztest::AllOf(cztest::Ge(1), cztest::Lt(10)), 11));
    EXPECT_FALSE(cztest::Matches(cztest::AllOf(cztest::Ge(1), cztest::Lt(10)), 0));
}

TEST(Matcher, AnyOfNeedsOneMatcherToMatch) {
    EXPECT_TRUE(cztest::Matches(cztest::AnyOf(cztest::Eq(1), cztest::Eq(2)), 2));
    EXPECT_FALSE(cztest::Matches(cztest::AnyOf(cztest::Eq(1), cztest::Eq(2)), 3));
}

TEST(Matcher, NotInvertsAMatcher) {
    EXPECT_TRUE(cztest::Matches(cztest::Not(cztest::Eq(1)), 2));
    EXPECT_FALSE(cztest::Matches(cztest::Not(cztest::Eq(1)), 1));
}

TEST(Matcher, TheMatchersNest) {
    const auto matcher = cztest::AllOf(cztest::Not(cztest::Eq(3)), cztest::AnyOf(cztest::Eq(1), cztest::Eq(2)));
    EXPECT_TRUE(cztest::Matches(matcher, 1));
    EXPECT_TRUE(cztest::Matches(matcher, 2));
    EXPECT_FALSE(cztest::Matches(matcher, 3));
    EXPECT_FALSE(cztest::Matches(matcher, 4));
}

TEST(Matcher, ElementsAreComparesTheElementsOfAContainer) {
    const std::vector<int> values{1, 2, 3};

    EXPECT_TRUE(cztest::Matches(cztest::ElementsAre(cztest::Eq(1), cztest::Ge(2), cztest::Lt(4)), values));
    // The count has to match as well.
    EXPECT_FALSE(cztest::Matches(cztest::ElementsAre(cztest::Eq(1), cztest::Eq(2)), values));
    EXPECT_FALSE(cztest::Matches(cztest::ElementsAre(cztest::Eq(1), cztest::Eq(2), cztest::Eq(4)), values));
}

TEST(Matcher, ElementsAreWorksOnAnyContainer) {
    const std::string text{"abc"};
    EXPECT_TRUE(cztest::Matches(cztest::ElementsAre(cztest::Eq('a'), cztest::Eq('b'), cztest::Eq('c')), text));
}

TEST(Matcher, ExpectThatUsesTheGoogleMockSpelling) {
    EXPECT_THAT(42, cztest::AllOf(cztest::Ge(1), cztest::Le(100)));
    EXPECT_THAT(std::string{"hello"}, cztest::HasSubstr("ell"));

    const auto failed = cztest::ExpectThat(42, cztest::AllOf(cztest::Gt(100)));
    EXPECT_TRUE(failed.failed());
    EXPECT_NE(failed.message.find("matches all of"), std::string::npos);
}

TEST(Matcher, TheThreeWayComparisonIsUsedWhenTheTypeHasOne) {
    // Version only defines <=>, so the comparisons go through it.
    EXPECT_TRUE(cztest::Lt(Version{2, 0}).Matches(Version{1, 9}));
    EXPECT_FALSE(cztest::Lt(Version{2, 0}).Matches(Version{2, 1}));
    EXPECT_TRUE(cztest::Ge(Version{2, 0}).Matches(Version{2, 0}));
}

TEST(Matcher, TheComposedMatchersDescribeThemselves) {
    const auto matcher = cztest::AllOf(cztest::Ge(1), cztest::Lt(10));
    EXPECT_NE(matcher.Describe().find("matches all of"), std::string::npos);
    EXPECT_NE(matcher.Describe().find("is at least"), std::string::npos);
}

// The negative cases matter as much as the positive ones: a matcher that
// accepts everything would make every expectation pass.
TEST(Matcher, TheStringMatchersRejectWhatTheyShould) {
    EXPECT_TRUE(cztest::Matches(cztest::HasSubstr("ell"), std::string{"hello"}));
    EXPECT_FALSE(cztest::Matches(cztest::HasSubstr("xyz"), std::string{"hello"}));
    EXPECT_TRUE(cztest::Matches(cztest::StrEq("hello"), std::string{"hello"}));
    EXPECT_FALSE(cztest::Matches(cztest::StrEq("hello"), std::string{"world"}));
    EXPECT_TRUE(cztest::Matches(cztest::StartsWith("he"), std::string{"hello"}));
    EXPECT_FALSE(cztest::Matches(cztest::StartsWith("lo"), std::string{"hello"}));
    EXPECT_TRUE(cztest::Matches(cztest::EndsWith("lo"), std::string{"hello"}));
    EXPECT_FALSE(cztest::Matches(cztest::EndsWith("he"), std::string{"hello"}));
}

TEST(Matcher, TheNullMatchersRejectWhatTheyShould) {
    const int* nothing = nullptr;
    const int value = 0;
    EXPECT_TRUE(cztest::Matches(cztest::IsNull(), nothing));
    EXPECT_FALSE(cztest::Matches(cztest::IsNull(), &value));
    EXPECT_TRUE(cztest::Matches(cztest::NotNull(), &value));
    EXPECT_FALSE(cztest::Matches(cztest::NotNull(), nothing));
}

TEST(Matcher, TheTrulyMatcherRejectsWhatItShould) {
    const auto even = cztest::Truly([](int v) { return v % 2 == 0; });
    EXPECT_TRUE(cztest::Matches(even, 4));
    EXPECT_FALSE(cztest::Matches(even, 5));
}

TEST(Matcher, TheComparisonsRejectWhatTheyShould) {
    EXPECT_TRUE(cztest::Ge(3).Matches(3));
    EXPECT_FALSE(cztest::Ge(3).Matches(2));
    EXPECT_TRUE(cztest::Le(3).Matches(3));
    EXPECT_FALSE(cztest::Le(3).Matches(4));
    EXPECT_TRUE(cztest::Gt(3).Matches(4));
    EXPECT_FALSE(cztest::Gt(3).Matches(3));
}
