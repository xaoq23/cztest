// The modern assertion core: constexpr predicates, plain functions and the
// streaming syntax.
#include <cztest/cztest.hpp>

#include <cmath>
#include <source_location>
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
// --- the macro parameters must not leak into the user's expressions -------
//
// A macro parameter that appears in the user's expression would be
// substituted into it, so variables with such names have to keep working.
TEST(Modern, VariablesNamedLikeMacroParametersAreFine) {
    const int result = 2;
    const int a = 2;
    const int b = 2;
    const bool condition = true;
    const int statement = 2;

    EXPECT_EQ(result, 2);
    EXPECT_EQ(a, b);
    EXPECT_TRUE(condition);
    EXPECT_EQ(statement, 2);
    EXPECT_STREQ("x", "x");
    EXPECT_NEAR(1.0, 1.0, 0.1);
    EXPECT_NO_THROW((void)statement);
}

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

TEST(Modern, FloatingPointValuesArePrintedWithRoundTripPrecision) {
  // 0.1 + 0.2 is not 0.3; a message with six digits would hide it.
  const auto result = cztest::ExpectEqual(0.1 + 0.2, 0.3);
  EXPECT_TRUE(result.failed());
  EXPECT_NE(result.message.find("0.30000000000000004"), std::string::npos);
}

// --- the failure location ----------------------------------------------------
namespace {

std::source_location LocationInHelper() { return std::source_location::current(); }

}  // namespace

TEST(Modern, TheGeneratedFunctionNameIsNotShown) {
  // The assertion sits in the body that the framework generated, so its
  // name would tell the reader nothing.
  const auto text = cztest::detail::DescribeLocation(std::source_location::current());
  EXPECT_EQ(text.find("cztest_body"), std::string::npos);
  EXPECT_EQ(text.find('('), std::string::npos);
}

TEST(Modern, TheUserFunctionNameIsShown) {
  const auto text = cztest::detail::DescribeLocation(LocationInHelper());
  EXPECT_NE(text.find("LocationInHelper"), std::string::npos);
}

TEST(Modern, TheCallingConventionIsNotShown) {
  // Clang on Windows writes it into the name of the function.
  EXPECT_EQ(cztest::detail::CleanFunctionName("int __cdecl demo::Helper(int)"),
            "int demo::Helper(int)");
}
