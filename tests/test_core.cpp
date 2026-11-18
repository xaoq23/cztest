// Basic assertions, fixtures and the runner.
#include <cztest/cztest.hpp>

#include <stdexcept>
#include <string>
#include <vector>

TEST(Assertions, Boolean) {
  EXPECT_TRUE(true);
  EXPECT_FALSE(false);
  ASSERT_TRUE(1 + 1 == 2);
}

TEST(Assertions, Comparison) {
  EXPECT_EQ(1 + 1, 2);
  EXPECT_NE(1, 2);
  EXPECT_LT(1, 2);
  EXPECT_LE(2, 2);
  EXPECT_GT(3, 2);
  EXPECT_GE(2, 2);
  EXPECT_EQ(std::string("abc"), "abc");
}

TEST(Assertions, Strings) {
  EXPECT_STREQ("hello", "hello");
  EXPECT_STRNE("hello", "world");
}

TEST(Assertions, FloatingPoint) {
  EXPECT_NEAR(0.1 + 0.2, 0.3, 1e-9);
  EXPECT_NEAR(1.0, 1.0, 0.0);
}

TEST(Assertions, Exceptions) {
  EXPECT_THROW(throw std::runtime_error("boom"), std::runtime_error);
  EXPECT_NO_THROW((void)0);
  EXPECT_ANY_THROW(throw 42);
}

TEST(Assertions, StreamingMessages) {
  const int value = 3;
  EXPECT_EQ(value, 3) << "value should be three";
  EXPECT_TRUE(true) << "with " << value << " as well";
}

class ContainerFixture : public cztest::Test {
 public:
  void SetUp() override { items_ = {1, 2, 3}; }
  void TearDown() override { items_.clear(); }

  std::vector<int> items_;
};

TEST_F(ContainerFixture, SizeIsThree) {
  EXPECT_EQ(items_.size(), 3u);
  EXPECT_EQ(items_.front(), 1);
}

TEST_F(ContainerFixture, CanBeExtended) {
  items_.push_back(4);
  EXPECT_EQ(items_.size(), 4u);
}

// Only Windows (COFF) needs an explicit main: on ELF/Mach-O cztest.hpp already
// installs a weak main(), and defining another one here would be a redefinition.
#if defined(_WIN32)
CZTEST_MAIN()
#endif
