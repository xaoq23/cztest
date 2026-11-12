// The modern assertion core: constexpr predicates, plain functions and the
// streaming syntax.
#include <cztest/cztest.hpp>

#include <cmath>
#include <string>

// --- the predicates are usable in a static_assert -------------------------
static_assert(cztest::Equal(1 + 1, 2));
static_assert(cztest::NotEqual(1, 2));
static_assert(cztest::Less(1, 2));
static_assert(cztest::LessEqual(2, 2));
static_assert(cztest::Greater(3, 2));
static_assert(cztest::GreaterEqual(2, 2));

// --- the assertion functions are ordinary functions ------------------------
TEST(Modern, ExpectFunctionsReturnAResult) {
  const auto ok = cztest::ExpectEqual(1 + 1, 2);
  EXPECT_TRUE(ok.ok);
  EXPECT_FALSE(ok.failed());

  const auto failed = cztest::ExpectEqual(1 + 1, 3, "1 + 1 == 3");
  EXPECT_TRUE(failed.failed());
  EXPECT_NE(failed.message.find("1 + 1 == 3"), std::string::npos);
  EXPECT_NE(failed.message.find("2 vs 3"), std::string::npos);
}

TEST(Modern, ExpectFunctionsCoverEveryAssertion) {
  EXPECT_TRUE(cztest::ExpectTrue(true).ok);
  EXPECT_TRUE(cztest::ExpectFalse(false).ok);
  EXPECT_TRUE(cztest::ExpectStringEqual("a", "a").ok);
  EXPECT_TRUE(cztest::ExpectStringNotEqual("a", "b").ok);
  EXPECT_TRUE(cztest::ExpectNear(1.0, 1.05, 0.1).ok);
  EXPECT_TRUE(cztest::ExpectNoThrow([] {}).ok);
  EXPECT_TRUE(cztest::ExpectAnyThrow([] { throw 1; }).ok);
  EXPECT_TRUE(cztest::ExpectThrow<int>([] { throw 1; }).ok);
}

// --- a test can branch on the outcome without using a macro ----------------
TEST(Modern, AssertionsCanBeUsedAsFunctions) {
  const int left = 3;
  const int right = 3;
  if (auto result = cztest::ExpectEqual(left, right); !result) {
    cztest::ReportFailure(std::source_location::current(), result.message);
    FAIL() << "unreachable";
  }
  SUCCEED();
}

// --- the macro keeps the else of the surrounding if ------------------------
TEST(Modern, TheMacroDoesNotSwallowTheUsersElse) {
  bool took_the_else = false;
  if (false)
    EXPECT_TRUE(true);
  else
    took_the_else = true;

  EXPECT_TRUE(took_the_else);
}

// --- the streaming syntax still works --------------------------------------
TEST(Modern, StreamingMessagesStillWork) {
  const int value = 7;
  EXPECT_EQ(value, 7) << "value is " << value;
  EXPECT_TRUE(value > 0) << "positive: " << value;
}

// --- floating point comparison ---------------------------------------------
TEST(Modern, DoubleComparisonUsesFourUnitsInTheLastPlace) {
  EXPECT_DOUBLE_EQ(0.1 + 0.2, 0.3);
  EXPECT_DOUBLE_EQ(1.0, 1.0);
  EXPECT_FLOAT_EQ(1.0f / 3.0f, 0.33333334f);
  EXPECT_FALSE(cztest::detail::AlmostEquals(1.0, 1.0000001));
  EXPECT_TRUE(cztest::detail::AlmostEquals(1.0, 1.0000000000000002));
  // NaN is never almost equal to anything, not even to itself.
  EXPECT_FALSE(cztest::detail::AlmostEquals(std::nan(""), std::nan("")));
}
