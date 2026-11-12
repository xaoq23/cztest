// Function level mocking.
#include <cztest/cztest.hpp>

#include <memory>
#include <string>
#include <vector>

// --- the interface under test ---------------------------------------------
struct IStorage {
  virtual ~IStorage() = default;
  virtual int Get(int key) = 0;
  virtual void Put(int key, const std::string& value) = 0;
  virtual std::string Name() const = 0;
};

class MockStorage : public IStorage, public cztest::MockBase {
 public:
  CZTEST_MOCK_METHOD(int, Get, int);
  CZTEST_MOCK_METHOD(void, Put, int, const std::string&);
  CZTEST_MOCK_NOARG_CONST_METHOD(std::string, Name);
};

// A second interface with wider types and a reference returning function.
struct IRegistry {
  virtual ~IRegistry() = default;
  virtual long Lookup(long key) = 0;
  virtual const std::string& Primary() const = 0;
};

class MockRegistry : public IRegistry, public cztest::MockBase {
 public:
  CZTEST_MOCK_METHOD(long, Lookup, long);
  CZTEST_MOCK_NOARG_CONST_METHOD(const std::string&, Primary);
};

// A third interface exercising by value, pointer and move only arguments.
struct IWidget {
  virtual ~IWidget() = default;
  virtual std::string Describe(std::string prefix) = 0;
  virtual int Use(const int* pointer) = 0;
  virtual void Take(std::unique_ptr<int> value) = 0;
};

class MockWidget : public IWidget, public cztest::MockBase {
 public:
  CZTEST_MOCK_METHOD(std::string, Describe, std::string);
  CZTEST_MOCK_METHOD(int, Use, const int*);
  CZTEST_MOCK_METHOD(void, Take, std::unique_ptr<int>);
};

// --- tests -----------------------------------------------------------------
TEST(Mock, ReturnsConfiguredValue) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(42)).Times(1).WillOnce(cztest::Return(7));
  EXPECT_EQ(mock.Get(42), 7);
}

TEST(Mock, MatchesArgumentsWithMatchers) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(cztest::Gt(10))).Times(1).WillOnce(cztest::Return(1));
  EXPECT_EQ(mock.Get(11), 1);
}

TEST(Mock, VoidFunctionIsRecorded) {
  MockStorage mock;
  EXPECT_CALL(mock, Put(1, "value")).Times(1);
  mock.Put(1, "value");
}

TEST(Mock, ConstFunctionIsSupported) {
  MockStorage mock;
  EXPECT_CALL(mock, Name()).Times(1).WillOnce(cztest::Return("storage"));
  EXPECT_EQ(mock.Name(), "storage");
}

TEST(Mock, SequenceOfActions) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(1)).Times(2).WillOnce(cztest::Return(10)).WillOnce(cztest::Return(20));
  EXPECT_EQ(mock.Get(1), 10);
  EXPECT_EQ(mock.Get(1), 20);
}

TEST(Mock, RepeatedlyUsesTheSameAction) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(1)).Times(3).WillRepeatedly(cztest::Return(5));
  EXPECT_EQ(mock.Get(1), 5);
  EXPECT_EQ(mock.Get(1), 5);
  EXPECT_EQ(mock.Get(1), 5);
}

TEST(Mock, DoAndReturnSeesTheArguments) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(cztest::Any())).Times(1).WillRepeatedly(cztest::Invoke([](int key) {
    return key * 2;
  }));
  EXPECT_EQ(mock.Get(21), 42);
}

TEST(Mock, StringMatchers) {
  MockStorage mock;
  EXPECT_CALL(mock, Put(1, cztest::HasSubstr("needle"))).Times(1);
  mock.Put(1, "a needle in a haystack");
}

TEST(Mock, AtLeastCardinality) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(1)).Times(cztest::AtLeast(1)).WillRepeatedly(cztest::Return(3));
  EXPECT_EQ(mock.Get(1), 3);
  EXPECT_EQ(mock.Get(1), 3);
}

TEST(Mock, NewestExpectationWins) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(1)).Times(cztest::AnyNumber()).WillOnce(cztest::Return(1));
  EXPECT_CALL(mock, Get(1)).Times(1).WillOnce(cztest::Return(2));
  EXPECT_EQ(mock.Get(1), 2);
}

TEST(Mock, DifferentArgumentsUseDifferentExpectations) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(1)).Times(1).WillOnce(cztest::Return(100));
  EXPECT_CALL(mock, Get(2)).Times(1).WillOnce(cztest::Return(200));
  EXPECT_EQ(mock.Get(1), 100);
  EXPECT_EQ(mock.Get(2), 200);
}

TEST(Mock, MatchesTheMostRecentlyRegisteredMatchingExpectation) {
  MockStorage mock;
  EXPECT_CALL(mock, Get(cztest::Any())).Times(cztest::AnyNumber()).WillRepeatedly(cztest::Return(1));
  EXPECT_CALL(mock, Get(cztest::Gt(100))).Times(1).WillOnce(cztest::Return(2));
  EXPECT_EQ(mock.Get(200), 2);
  EXPECT_EQ(mock.Get(50), 1);
}

TEST(Mock, WiderTypesAreConverted) {
  MockRegistry mock;
  // The expectation uses an int while the mocked function takes a long.
  EXPECT_CALL(mock, Lookup(42)).Times(1).WillOnce(cztest::Return(7L));
  EXPECT_EQ(mock.Lookup(42L), 7L);
}

TEST(Mock, ReferenceReturnValues) {
  MockRegistry mock;
  static const std::string kPrimary = "primary";
  EXPECT_CALL(mock, Primary()).Times(1).WillOnce(cztest::ReturnRef(kPrimary));
  EXPECT_EQ(mock.Primary(), "primary");
}

TEST(Mock, MockAsFixtureMember) {
  struct Fixture : public cztest::Test {
    MockStorage mock;
  };

  Fixture fixture;
  EXPECT_CALL(fixture.mock, Get(5)).Times(1).WillOnce(cztest::Return(55));
  EXPECT_EQ(fixture.mock.Get(5), 55);
}

TEST(Mock, ByValueStringArgument) {
  MockWidget mock;
  EXPECT_CALL(mock, Describe(cztest::Eq("prefix"))).Times(1).WillOnce(cztest::Return("ok"));
  EXPECT_EQ(mock.Describe("prefix"), "ok");
}

TEST(Mock, StringMatcherOnByValueString) {
  MockWidget mock;
  EXPECT_CALL(mock, Describe(cztest::StartsWith("pre"))).Times(1).WillOnce(cztest::Return("ok"));
  EXPECT_EQ(mock.Describe("prefix"), "ok");
}

TEST(Mock, PointerArguments) {
  MockWidget mock;
  int value = 7;
  EXPECT_CALL(mock, Use(cztest::NotNull())).Times(1).WillOnce(cztest::Return(7));
  EXPECT_EQ(mock.Use(&value), 7);
}

TEST(Mock, MoveOnlyArguments) {
  MockWidget mock;
  EXPECT_CALL(mock, Take(cztest::Any())).Times(1);
  mock.Take(std::make_unique<int>(1));
}

// --- a matcher of the user: it only needs a Matches member ----------------
namespace {

struct IsEven {
    bool Matches(int value) const { return value % 2 == 0; }
};

static_assert(cztest::detail::MatcherFor<IsEven, int>);
static_assert(!cztest::detail::MatcherFor<int, int>);
static_assert(cztest::detail::ActionFor<int (*)(int), int, int>);
static_assert(!cztest::detail::ActionFor<int (*)(int), int, std::string>);

}  // namespace

TEST(Mock, AUserDefinedMatcherWorksWithoutInheriting) {
    MockStorage mock;
    EXPECT_CALL(mock, Get(IsEven{})).Times(1).WillOnce(cztest::Return(1));

    EXPECT_EQ(mock.Get(4), 1);
}

TEST(Mock, Matchers) {  EXPECT_TRUE(cztest::Eq(3).Matches(3));
  EXPECT_FALSE(cztest::Eq(3).Matches(4));
  EXPECT_TRUE(cztest::Ne(3).Matches(4));
  EXPECT_TRUE(cztest::Lt(3).Matches(2));
  EXPECT_TRUE(cztest::Le(3).Matches(3));
  EXPECT_TRUE(cztest::Gt(3).Matches(4));
  EXPECT_TRUE(cztest::Ge(3).Matches(3));
  EXPECT_TRUE(cztest::Any().Matches(42));
  EXPECT_TRUE(cztest::StrEq("abc").Matches(std::string("abc")));
  EXPECT_TRUE(cztest::HasSubstr("bc").Matches(std::string("abcd")));
  EXPECT_TRUE(cztest::StartsWith("ab").Matches(std::string("abcd")));
  EXPECT_TRUE(cztest::EndsWith("cd").Matches(std::string("abcd")));
  EXPECT_TRUE(cztest::Not(cztest::Eq(3)).Matches(4));
  EXPECT_TRUE(cztest::Truly([](int v) { return v % 2 == 0; }).Matches(4));
  EXPECT_FALSE(cztest::Truly([](int v) { return v % 2 == 0; }).Matches(5));

  const int* null_pointer = nullptr;
  EXPECT_TRUE(cztest::IsNull().Matches(null_pointer));
  EXPECT_TRUE(cztest::NotNull().Matches(&null_pointer));
}
