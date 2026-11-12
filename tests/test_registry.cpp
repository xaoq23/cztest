// The registry: it has to notice the tests that share a name, and the filter
// that selects the tests.
#include <cztest/cztest.hpp>

#include <string>
#include <vector>

namespace {

cztest::detail::TestInfo MakeTest(std::string suite, std::string name) {
  return cztest::detail::TestInfo{std::move(suite), std::move(name), {}, {}};
}

}  // namespace

TEST(Registry, TheNamesAreUnique) {
  std::vector<cztest::detail::TestInfo> tests;
  tests.push_back(MakeTest("Math", "Addition"));
  tests.push_back(MakeTest("Math", "Subtraction"));
  tests.push_back(MakeTest("Parser", "Addition"));

  EXPECT_TRUE(cztest::detail::FindDuplicateTests(tests).empty());
}

TEST(Registry, ARepeatedNameIsFound) {
  std::vector<cztest::detail::TestInfo> tests;
  tests.push_back(MakeTest("Math", "Addition"));
  tests.push_back(MakeTest("Math", "Subtraction"));
  tests.push_back(MakeTest("Math", "Addition"));

  const auto duplicates = cztest::detail::FindDuplicateTests(tests);
  ASSERT_EQ(duplicates.size(), 1u);
  EXPECT_EQ(duplicates[0].full_name, "Math.Addition");
  // Both copies are reported, so the reader knows which ones to rename.
  EXPECT_EQ(duplicates[0].locations.size(), 2u);
}

TEST(Registry, EveryRepeatedNameIsFound) {
  std::vector<cztest::detail::TestInfo> tests;
  tests.push_back(MakeTest("Math", "Addition"));
  tests.push_back(MakeTest("Math", "Addition"));
  tests.push_back(MakeTest("Parser", "Reads"));
  tests.push_back(MakeTest("Parser", "Reads"));

  const auto duplicates = cztest::detail::FindDuplicateTests(tests);
  ASSERT_EQ(duplicates.size(), 2u);
  EXPECT_EQ(duplicates[0].full_name, "Math.Addition");
  EXPECT_EQ(duplicates[1].full_name, "Parser.Reads");
}

TEST(Filter, TheWildcardsWork) {
  EXPECT_TRUE(cztest::detail::WildcardMatch("*", "Math.Addition"));
  EXPECT_TRUE(cztest::detail::WildcardMatch("Math.*", "Math.Addition"));
  EXPECT_TRUE(cztest::detail::WildcardMatch("*.Addition", "Math.Addition"));
  EXPECT_TRUE(cztest::detail::WildcardMatch("Math.Addition", "Math.Addition"));
  EXPECT_TRUE(cztest::detail::WildcardMatch("M?th.*", "Math.Addition"));
  // The pattern has to match the whole name.
  EXPECT_FALSE(cztest::detail::WildcardMatch("Math", "Math.Addition"));
  EXPECT_FALSE(cztest::detail::WildcardMatch("Math.*", "Parser.Addition"));
  EXPECT_FALSE(cztest::detail::WildcardMatch("*Addition", "Math.Subtraction"));
}
