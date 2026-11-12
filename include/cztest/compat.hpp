// cztest/compat.hpp — teaching errors for the GoogleTest features cztest does
// not have.
//
// The point of this header is not to make the GoogleTest spellings work; it is
// to make the compiler explain what to do instead.  A newcomer (or an agent)
// that writes ON_CALL or EXPECT_THAT gets a message that names the cztest way
// of doing the same thing.
//
// The header is included by <cztest/cztest.hpp>.  Define CZTEST_NO_COMPAT
// before including it to leave the global namespace alone (for example when
// GoogleTest is used next to cztest).

#ifndef CZTEST_COMPAT_HPP
#define CZTEST_COMPAT_HPP

#if !defined(CZTEST_NO_COMPAT)

namespace cztest::compat {

/// A condition that is always false, so that a static_assert fires.
template <class T = void>
struct AlwaysFalse : std::false_type {};

}  // namespace cztest::compat

// ON_CALL has no counterpart: cztest keeps the expectations of a mock inside
// the mock, and there is no "default action" that an expectation does not
// override.
#define ON_CALL(...)                                                                 \
  static_assert(::cztest::compat::AlwaysFalse<>::value,                                \
                 "cztest has no ON_CALL. Use EXPECT_CALL(mock, Method(args))"          \
                 ".WillRepeatedly(action) for the default behaviour, and add one "     \
                 "EXPECT_CALL per distinct call if the answers differ.")

// EXPECT_THAT and the matcher algebra are not part of cztest.
#define EXPECT_THAT(...)                                                             \
  static_assert(::cztest::compat::AlwaysFalse<>::value,                                \
                 "cztest has no EXPECT_THAT. Compare the value directly "               \
                 "(EXPECT_EQ) or use cztest::Truly(lambda) for a custom condition.")

// GoogleMock's MOCK_METHOD has a different syntax than CZTEST_MOCK_METHOD.
#define MOCK_METHOD(...)                                                             \
  static_assert(::cztest::compat::AlwaysFalse<>::value,                                \
                 "cztest uses CZTEST_MOCK_METHOD(ret, Name, types...) instead of "     \
                 "MOCK_METHOD(ret, Name, (types...), (override)). The parameter "     \
                 "types go in the third argument without the extra parentheses.")

// A death test needs process level support, which cztest does not have.
#define EXPECT_DEATH(...)                                                            \
  static_assert(::cztest::compat::AlwaysFalse<>::value,                                \
                 "cztest has no death tests. Test the error path directly: make "       \
                 "the function return a result you can assert on.")

#define ASSERT_DEATH(...) EXPECT_DEATH(__VA_ARGS__)

// A typed test is a parameterised test in cztest.
#define TYPED_TEST(...)                                                               \
  static_assert(::cztest::compat::AlwaysFalse<>::value,                                \
                 "cztest has no typed tests. Use TEST_P with cztest::Values instead.")

// The skipping semantic of cztest is SKIP().
#define GTEST_SKIP(...)                                                              \
  static_assert(::cztest::compat::AlwaysFalse<>::value,                                \
                 "cztest spells skipping SKIP(...), not GTEST_SKIP().")

#endif  // CZTEST_NO_COMPAT

#endif  // CZTEST_COMPAT_HPP
