// Parameterised tests.
#include <cztest/cztest.hpp>

#include <string>
#include <vector>

namespace {

struct Case {
  int a{0};
  int b{0};
  int expected{0};
};

class AdderTest : public cztest::TestWithParam<Case> {
 protected:
  void SetUp() override { value_ = GetParam(); }

  Case value_;
};

}  // namespace

TEST_P(AdderTest, AddsCorrectly) {
  EXPECT_EQ(value_.a + value_.b, value_.expected);
}

INSTANTIATE_TEST_SUITE_P(Basic, AdderTest,
                          cztest::Values(Case{1, 2, 3}, Case{10, 20, 30}, Case{-1, 1, 0}));

// --- a fixture without parameters is not needed for a parameterised test ---
namespace {

class RangeTest : public cztest::TestWithParam<int> {
 protected:
  void SetUp() override { value_ = GetParam(); }

  int value_{0};
};

}  // namespace

TEST_P(RangeTest, IsPositive) {
  EXPECT_GT(value_, 0);
}

INSTANTIATE_TEST_SUITE_P(Positive, RangeTest, cztest::Range(1, 4));

// --- the generated names can be selected with --filter ---
TEST(Param, TheGeneratedNamesAreStable) {
  // "Basic/AdderTest.AddsCorrectly/0" is the name of the first instance.
  SUCCEED();
}
