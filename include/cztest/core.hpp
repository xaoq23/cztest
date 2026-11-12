// cztest - a modern, header-only C++ test framework with function-level mocks.
//
// core.hpp - test registry, assertions, runner and reporting.
//
// License: Apache-2.0
#ifndef CZTEST_CORE_HPP
#define CZTEST_CORE_HPP

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <source_location>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <version>
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <expected>
#endif

namespace cztest {

// ---------------------------------------------------------------------------
// The version of the framework.
// ---------------------------------------------------------------------------
#define CZTEST_VERSION_MAJOR 0
#define CZTEST_VERSION_MINOR 4
#define CZTEST_VERSION_PATCH 0

// ---------------------------------------------------------------------------
// Value printing
// ---------------------------------------------------------------------------
namespace detail {

template <class T, class = void>
struct IsStreamable : std::false_type {};

template <class T>
struct IsStreamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <class T>
std::string PrintValue(const T& value) {
  using D = std::decay_t<T>;
  if constexpr (std::is_same_v<D, bool>) {
    return value ? "true" : "false";
  } else if constexpr (std::is_same_v<D, std::nullptr_t>) {
    return "nullptr";
  } else if constexpr (std::is_array_v<T> &&
                       std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>) {
    return "\"" + std::string(static_cast<const char*>(value)) + "\"";
  } else if constexpr (std::is_pointer_v<D> && std::is_same_v<std::remove_cv_t<D>, char>) {
    if (value == nullptr) return "nullptr";
    return std::string("\"") + value + "\"";
  } else if constexpr (std::is_same_v<D, std::string>) {
    return "\"" + value + "\"";
  } else if constexpr (std::is_floating_point_v<D>) {
    // Print with the precision that round trips: the default of six digits
    // would show 0.1 + 0.2 as "0.3" and hide the difference a test is
    // about.
    std::ostringstream os;
    os << std::setprecision(std::numeric_limits<D>::max_digits10) << value;
    return os.str();
  } else if constexpr (IsStreamable<T>::value) {
    std::ostringstream os;
    os << value;
    return os.str();
  } else {
    return "<unprintable value>";
  }
}

inline std::string PrintValue(std::nullptr_t) { return "nullptr"; }

// ---------------------------------------------------------------------------
// Message: accumulates the "<< ..." part of a failed assertion.
// ---------------------------------------------------------------------------
class Message {
 public:
  Message() = default;
  explicit Message(std::string text) : text_(std::move(text)) {}
  Message(const Message&) = default;
  Message& operator=(const Message&) = default;

  template <class T>
  Message& operator<<(const T& value) {
    Append(PrintValue(value));
    return *this;
  }

  Message& operator<<(const char* value) {
    Append(value != nullptr ? value : "(null)");
    return *this;
  }

  const std::string& str() const { return text_; }

 private:
  void Append(const std::string& text) {
    if (text.empty()) return;
    if (!text_.empty() && text_.back() != '\n') text_ += "\n";
    text_ += text;
  }

  std::string text_;
};

}  // namespace detail

// ---------------------------------------------------------------------------
// AssertionResult: the outcome of a single assertion.
// ---------------------------------------------------------------------------
struct AssertionResult {
  bool ok{true};
  std::string message;

  constexpr explicit operator bool() const { return ok; }
  constexpr bool failed() const { return !ok; }

  static AssertionResult Success() { return AssertionResult{true, {}}; }
  static AssertionResult Failure(std::string message) { return AssertionResult{false, std::move(message)}; }
};

// Thrown by ASSERT_* to abort the current test.
class FatalAssertionFailure : public std::exception {
 public:
  const char* what() const noexcept override { return "fatal assertion failure"; }
};

namespace detail {

// ---------------------------------------------------------------------------
// MockVerifier: implemented by cztest::MockBase so that the core can verify
// mock expectations without knowing the mock's concrete type.
// ---------------------------------------------------------------------------
class MockVerifier {
 public:
  virtual ~MockVerifier() = default;
  virtual void Verify() = 0;
};

struct FailureRecord {
  std::source_location location;
  std::string message;
};

// ---------------------------------------------------------------------------
// TestContext: per-test state (failures + registered mocks).
// ---------------------------------------------------------------------------
class TestContext {
 public:
  static TestContext*& Current() {
    static TestContext* ctx = nullptr;
    return ctx;
  }

  void Begin(std::string test_name) {
    test_name_ = std::move(test_name);
    failures_.clear();
    mocks_.clear();
    running_ = true;
  }

  void End() {
    for (auto* mock : mocks_) {
      if (mock != nullptr) mock->Verify();
    }
    mocks_.clear();
    failures_.clear();
    test_name_.clear();
    running_ = false;
  }

  bool running() const { return running_; }
  bool HasFailures() const { return !failures_.empty(); }
  const std::vector<FailureRecord>& failures() const { return failures_; }
  const std::string& test_name() const { return test_name_; }

  void AddFailure(std::source_location location, std::string message) {
    failures_.push_back(FailureRecord{location, std::move(message)});
  }

  /// Forgets the failures that were recorded so far.
  ///
  /// The framework's own tests use it to look at what a failing mock reports
  /// without failing the test that looks at it.
  void ClearFailures() { failures_.clear(); }

  void AddMock(MockVerifier* mock) { mocks_.push_back(mock); }

  void RemoveMock(MockVerifier* mock) {
    mocks_.erase(std::remove(mocks_.begin(), mocks_.end(), mock), mocks_.end());
  }

 private:
  std::string test_name_;
  std::vector<FailureRecord> failures_;
  std::vector<MockVerifier*> mocks_;
  bool running_{false};
};

// ---------------------------------------------------------------------------
// Traces: the context a failure happened in.
//
// SCOPED_TRACE pushes a message for the lifetime of a scope; a failure that
// happens inside that scope reports the messages of all the open scopes,
// innermost first.
// ---------------------------------------------------------------------------
inline std::vector<std::string>& TraceStack() {
  static thread_local std::vector<std::string> stack;
  return stack;
}

/// Appends the open traces to a failure message.
inline std::string AppendTraces(std::string message) {
  const auto& stack = TraceStack();
  if (stack.empty()) return message;
  message += "\nTraces:";
  for (std::size_t i = stack.size(); i-- > 0;) {
    message += "\n  [";
    message += std::to_string(i + 1);
    message += "] ";
    message += stack[i];
  }
  return message;
}

/// Pushes a trace for the lifetime of the object.
class TraceScope {
 public:
  explicit TraceScope(std::string message) { TraceStack().push_back(std::move(message)); }
  ~TraceScope() {
    if (!TraceStack().empty()) TraceStack().pop_back();
  }

  TraceScope(const TraceScope&) = delete;
  TraceScope& operator=(const TraceScope&) = delete;
};

}  // namespace detail

/// Thrown by SKIP() to end the current test without failing it.
class SkipTest : public std::exception {
 public:
  explicit SkipTest(std::string reason) : reason_(std::move(reason)) {}
  const char* what() const noexcept override { return reason_.c_str(); }

 private:
  std::string reason_;
};

namespace detail {

// The message of a trace or of a skip is a stream expression, the way
// GoogleTest spells it: SCOPED_TRACE("item: " << item).
[[noreturn]] inline void SkipNow(std::string reason) { throw SkipTest(std::move(reason)); }

// ---------------------------------------------------------------------------
// ReportFailure: records a failure in the running test.
//
// `fatal` additionally aborts the test by throwing FatalAssertionFailure. The
// throw happens here and not in a destructor, so a fatal assertion never
// escapes through an exception that is already in flight.
// ---------------------------------------------------------------------------
inline void ReportFailure(std::source_location location, std::string message, bool fatal) {
  if (TestContext* ctx = TestContext::Current()) {
    if (!ctx->running()) {
      // Outside of a test there is nobody to report to.
      std::cerr << "cztest: " << message << "\n";
      if (fatal) throw FatalAssertionFailure();
      return;
    }
    ctx->AddFailure(location, AppendTraces(std::move(message)));
  } else {
    std::cerr << "cztest: failure outside of a test: " << message << "\n";
  }
  if (fatal) throw FatalAssertionFailure();
}

// ---------------------------------------------------------------------------
// FailureRecorder: reports a failure when a Message is assigned to it.
//
// The assignment is what performs the report, so a streamed "<< ..." message is
// complete before the failure is recorded.
// ---------------------------------------------------------------------------
class FailureRecorder {
 public:
  FailureRecorder(std::source_location location, bool fatal)
      : location_(location), fatal_(fatal) {}

  void operator=(const Message& message) const {
    ReportFailure(location_, message.str(), fatal_);
  }

 private:
  std::source_location location_;
  bool fatal_;
};

}  // namespace detail

// ---------------------------------------------------------------------------
// ReportFailure: the public entry point for recording a failure.
//
// `fatal` additionally aborts the current test.
// ---------------------------------------------------------------------------
inline void ReportFailure(std::source_location location, std::string message, bool fatal = false) {
  detail::ReportFailure(location, std::move(message), fatal);
}

// ---------------------------------------------------------------------------
// Public helpers used by the assertion macros.
// ---------------------------------------------------------------------------
namespace detail {

/// Builds the standard "Expected / Actual" message of a comparison.
inline std::string ComparisonMessage(std::string_view expr, std::string_view actual_a,
                                     std::string_view actual_b) {
  std::string msg = "Expected: ";
  msg += expr;
  msg += "\n  Actual: ";
  msg += actual_a;
  msg += " vs ";
  msg += actual_b;
  return msg;
}

inline bool StringEquals(const char* a, const char* b) {
  return (a == nullptr || b == nullptr) ? (a == b) : (std::strcmp(a, b) == 0);
}

/// Maps a floating point value to an integer whose order follows the order of
/// the values, so that the distance between two of them counts the
/// representable steps between them.
template <class T>
std::int64_t OrderedFloatKey(T value) {
  if constexpr (sizeof(T) == sizeof(std::int32_t)) {
    std::int32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits < 0) ? (static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) - bits)
                       : static_cast<std::int64_t>(bits);
  } else {
    std::int64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits < 0) ? (std::numeric_limits<std::int64_t>::min() - bits) : bits;
  }
}

/// Compares two floating point numbers with the tolerance of four units in
/// the last place, the rule GoogleTest uses for EXPECT_DOUBLE_EQ.
template <class T>
bool AlmostEquals(T lhs, T rhs) {
  if (std::isnan(lhs) || std::isnan(rhs)) return false;
  if (lhs == rhs) return true;
  if (std::isinf(lhs) || std::isinf(rhs)) return false;
  const std::int64_t left = OrderedFloatKey(lhs);
  const std::int64_t right = OrderedFloatKey(rhs);
  const std::uint64_t distance =
      left >= right ? static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right)
                     : static_cast<std::uint64_t>(right) - static_cast<std::uint64_t>(left);
  return distance <= 4;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Comparison predicates.
//
// They are constexpr and free of any allocation, so a property can be checked
// at compile time:
//
//   static_assert(cztest::Equal(1 + 1, 2));
// ---------------------------------------------------------------------------
template <class A, class B>
[[nodiscard]] constexpr bool Equal(const A& a, const B& b) {
  return a == b;
}

template <class A, class B>
[[nodiscard]] constexpr bool NotEqual(const A& a, const B& b) {
  return a != b;
}

template <class A, class B>
[[nodiscard]] constexpr bool Less(const A& a, const B& b) {
  return a < b;
}

template <class A, class B>
[[nodiscard]] constexpr bool LessEqual(const A& a, const B& b) {
  return a <= b;
}

template <class A, class B>
[[nodiscard]] constexpr bool Greater(const A& a, const B& b) {
  return a > b;
}

template <class A, class B>
[[nodiscard]] constexpr bool GreaterEqual(const A& a, const B& b) {
  return a >= b;
}

// ---------------------------------------------------------------------------
// Assertions.
//
// Every assertion is an ordinary function that returns an AssertionResult;
// the EXPECT_* / ASSERT_* macros are thin sugar on top of them.  The functions
// can be used directly when a test needs to branch on the outcome:
//
//   if (auto r = cztest::ExpectEqual(a, b); !r) {
//     cztest::ReportFailure(std::source_location::current(), r.message);
//   }
// ---------------------------------------------------------------------------
#define CZTEST_DEFINE_EXPECT(op, name, text)                                                   \
  template <class A, class B>                                                                 \
  [[nodiscard]] AssertionResult name(const A& a, const B& b, std::string_view expr = text) {  \
    if (a op b) return AssertionResult::Success();                                            \
    return AssertionResult::Failure(                                                          \
        detail::ComparisonMessage(expr, detail::PrintValue(a), detail::PrintValue(b)));        \
  }

CZTEST_DEFINE_EXPECT(==, ExpectEqual, "a == b")
CZTEST_DEFINE_EXPECT(!=, ExpectNotEqual, "a != b")
CZTEST_DEFINE_EXPECT(<, ExpectLess, "a < b")
CZTEST_DEFINE_EXPECT(<=, ExpectLessEqual, "a <= b")
CZTEST_DEFINE_EXPECT(>, ExpectGreater, "a > b")
CZTEST_DEFINE_EXPECT(>=, ExpectGreaterEqual, "a >= b")

#undef CZTEST_DEFINE_EXPECT

[[nodiscard]] inline AssertionResult ExpectTrue(bool value, std::string_view expr = "condition") {
  if (value) return AssertionResult::Success();
  std::string msg = "Expected: ";
  msg += expr;
  msg += " to evaluate to true\n  Actual: false";
  return AssertionResult::Failure(std::move(msg));
}

[[nodiscard]] inline AssertionResult ExpectFalse(bool value, std::string_view expr = "condition") {
  if (!value) return AssertionResult::Success();
  std::string msg = "Expected: ";
  msg += expr;
  msg += " to evaluate to false\n  Actual: true";
  return AssertionResult::Failure(std::move(msg));
}

[[nodiscard]] inline AssertionResult ExpectStringEqual(const char* a, const char* b,
                                                       std::string_view expr_a = "a",
                                                       std::string_view expr_b = "b") {
  if (detail::StringEquals(a, b)) return AssertionResult::Success();
  return AssertionResult::Failure(detail::ComparisonMessage(
      std::string(expr_a) + " == " + std::string(expr_b), detail::PrintValue(a),
      detail::PrintValue(b)));
}

[[nodiscard]] inline AssertionResult ExpectStringNotEqual(const char* a, const char* b,
                                                          std::string_view expr_a = "a",
                                                          std::string_view expr_b = "b") {
  if (!detail::StringEquals(a, b)) return AssertionResult::Success();
  std::string msg = "Expected: ";
  msg += expr_a;
  msg += " != ";
  msg += expr_b;
  msg += "\n  Actual: both are ";
  msg += detail::PrintValue(a);
  return AssertionResult::Failure(std::move(msg));
}

[[nodiscard]] inline AssertionResult ExpectNear(double a, double b, double tolerance,
                                                 std::string_view expr_a = "a",
                                                 std::string_view expr_b = "b") {
  const double diff = a > b ? a - b : b - a;
  if (diff <= tolerance) return AssertionResult::Success();
  std::string msg = "Expected: ";
  msg += expr_a;
  msg += " ~= ";
  msg += expr_b;
  msg += "\n  Actual: ";
  msg += detail::PrintValue(a);
  msg += " vs ";
  msg += detail::PrintValue(b);
  msg += " (difference ";
  msg += detail::PrintValue(diff);
  msg += " > tolerance ";
  msg += detail::PrintValue(tolerance);
  msg += ")";
  return AssertionResult::Failure(std::move(msg));
}

/// Compares two floating point numbers with the tolerance of four units in
/// the last place.
template <class T>
[[nodiscard]] AssertionResult ExpectAlmostEqual(T a, T b, std::string_view expr_a = "a",
                                                 std::string_view expr_b = "b") {
  if (detail::AlmostEquals(a, b)) return AssertionResult::Success();
  std::string msg = "Expected: ";
  msg += expr_a;
  msg += " == ";
  msg += expr_b;
  msg += "\n  Actual: ";
  msg += detail::PrintValue(a);
  msg += " vs ";
  msg += detail::PrintValue(b);
  msg += " (they differ by more than 4 units in the last place)";
  return AssertionResult::Failure(std::move(msg));
}

template <class E, class F>
[[nodiscard]] AssertionResult ExpectThrow(F&& fn, std::string_view expr = "statement",
                                          std::string_view type_name = "the exception type") {  try {
    fn();
  } catch (const E&) {
    return AssertionResult::Success();
  } catch (...) {
    std::string msg = "Expected: ";
    msg += expr;
    msg += " throws ";
    msg += type_name;
    msg += "\n  Actual: threw a different exception type";
    return AssertionResult::Failure(std::move(msg));
  }
  std::string msg = "Expected: ";
  msg += expr;
  msg += " throws ";
  msg += type_name;
  msg += "\n  Actual: no exception was thrown";
  return AssertionResult::Failure(std::move(msg));
}

template <class F>
[[nodiscard]] AssertionResult ExpectNoThrow(F&& fn, std::string_view expr = "statement") {
  try {
    fn();
  } catch (const std::exception& e) {
    std::string msg = "Expected: ";
    msg += expr;
    msg += " does not throw\n  Actual: threw \"";
    msg += e.what();
    msg += "\"";
    return AssertionResult::Failure(std::move(msg));
  } catch (...) {
    std::string msg = "Expected: ";
    msg += expr;
    msg += " does not throw\n  Actual: threw an unknown exception";
    return AssertionResult::Failure(std::move(msg));
  }
  return AssertionResult::Success();
}

template <class F>
[[nodiscard]] AssertionResult ExpectAnyThrow(F&& fn, std::string_view expr = "statement") {
  try {
    fn();
  } catch (...) {
    return AssertionResult::Success();
  }
  std::string msg = "Expected: ";
  msg += expr;
  msg += " throws\n  Actual: no exception was thrown";
  return AssertionResult::Failure(std::move(msg));
}

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L

/// The error of a std::expected, when it can be printed.
template <class E>
std::string DescribeError(const E& error) {
  if constexpr (detail::IsStreamable<E>::value) {
    return detail::PrintValue(error);
  } else {
    return "an error";
  }
}

/// Asserts that a std::expected holds a value.
template <class T, class E>
[[nodiscard]] AssertionResult ExpectOk(const std::expected<T, E>& result) {
  if (result.has_value()) return AssertionResult::Success();
  std::string msg = "Expected: the result to hold a value\n  Actual: ";
  msg += DescribeError(result.error());
  return AssertionResult::Failure(std::move(msg));
}

#define CZTEST_EXPECT_OK(cztest_expr_) CZTEST_EXPECT_(::cztest::ExpectOk((cztest_expr_)))
#define CZTEST_ASSERT_OK(cztest_expr_) CZTEST_ASSERT_(::cztest::ExpectOk((cztest_expr_)))
#define EXPECT_OK(expr) CZTEST_EXPECT_OK((expr))
#define ASSERT_OK(expr) CZTEST_ASSERT_OK((expr))

#else  // __cpp_lib_expected

namespace detail {

/// A condition that is always false, so that a static_assert fires.
template <class T = void>
inline constexpr bool kAlwaysFalse = false;

}  // namespace detail

// The macros exist in every mode: using one of them gives an error that says
// what to do, instead of "EXPECT_OK was not declared in this scope".
#define CZTEST_EXPECT_OK(cztest_expr_)                                                \
  static_assert(::cztest::detail::kAlwaysFalse<>,                                     \
                "cztest: EXPECT_OK needs std::expected, which needs C++23. Compile "    \
                "with -std=c++23, or check the result by hand: "                         \
                "EXPECT_TRUE(result.has_value()).")
#define CZTEST_ASSERT_OK(cztest_expr_) CZTEST_EXPECT_OK((cztest_expr_))
#define EXPECT_OK(expr) CZTEST_EXPECT_OK((expr))
#define ASSERT_OK(expr) CZTEST_ASSERT_OK((expr))

#endif  // __cpp_lib_expected

// ---------------------------------------------------------------------------
// Assertion macros
// ---------------------------------------------------------------------------
//
// The macros are thin sugar over the cztest::Expect* functions.  The "if
// (init; cond)" form is used on purpose: the inner if carries its own else
// branch, so a user written
//
//   if (x) EXPECT_EQ(a, b); else DoSomething();
//
// still binds the else to the user's if.  That is why no "ambiguous else"
// blocker is needed here.
//
// The location of the failure is taken from std::source_location, so it points
// at the assertion in the test source -- including the enclosing function.
//
// The parameter names carry a prefix on purpose: a macro parameter that
// appears in the user's expression would be substituted into it, so a test
// with a variable called "result" would break CZTEST_CHECK_.
#define CZTEST_CHECK_(cztest_expr_, cztest_fatal_)                                          \
  if (auto cztest_result_ = (cztest_expr_); cztest_result_.ok) {                           \
  } else                                                                                     \
    ::cztest::detail::FailureRecorder(::std::source_location::current(), cztest_fatal_) =    \
        (::cztest::detail::Message(cztest_result_.message))

#define CZTEST_EXPECT_(cztest_expr_) CZTEST_CHECK_((cztest_expr_), false)
#define CZTEST_ASSERT_(cztest_expr_) CZTEST_CHECK_((cztest_expr_), true)

// EXPECT_* / ASSERT_* ---------------------------------------------------
#define EXPECT_TRUE(cztest_cond_) CZTEST_EXPECT_(::cztest::ExpectTrue((cztest_cond_), #cztest_cond_))
#define EXPECT_FALSE(cztest_cond_) CZTEST_EXPECT_(::cztest::ExpectFalse((cztest_cond_), #cztest_cond_))
#define ASSERT_TRUE(cztest_cond_) CZTEST_ASSERT_(::cztest::ExpectTrue((cztest_cond_), #cztest_cond_))
#define ASSERT_FALSE(cztest_cond_) CZTEST_ASSERT_(::cztest::ExpectFalse((cztest_cond_), #cztest_cond_))

#define EXPECT_EQ(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectEqual((cztest_a_), (cztest_b_), #cztest_a_ " == " #cztest_b_))
#define EXPECT_NE(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectNotEqual((cztest_a_), (cztest_b_), #cztest_a_ " != " #cztest_b_))
#define EXPECT_LT(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectLess((cztest_a_), (cztest_b_), #cztest_a_ " < " #cztest_b_))
#define EXPECT_LE(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectLessEqual((cztest_a_), (cztest_b_), #cztest_a_ " <= " #cztest_b_))
#define EXPECT_GT(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectGreater((cztest_a_), (cztest_b_), #cztest_a_ " > " #cztest_b_))
#define EXPECT_GE(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectGreaterEqual((cztest_a_), (cztest_b_), #cztest_a_ " >= " #cztest_b_))

#define ASSERT_EQ(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectEqual((cztest_a_), (cztest_b_), #cztest_a_ " == " #cztest_b_))
#define ASSERT_NE(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectNotEqual((cztest_a_), (cztest_b_), #cztest_a_ " != " #cztest_b_))
#define ASSERT_LT(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectLess((cztest_a_), (cztest_b_), #cztest_a_ " < " #cztest_b_))
#define ASSERT_LE(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectLessEqual((cztest_a_), (cztest_b_), #cztest_a_ " <= " #cztest_b_))
#define ASSERT_GT(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectGreater((cztest_a_), (cztest_b_), #cztest_a_ " > " #cztest_b_))
#define ASSERT_GE(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectGreaterEqual((cztest_a_), (cztest_b_), #cztest_a_ " >= " #cztest_b_))

#define EXPECT_STREQ(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectStringEqual((cztest_a_), (cztest_b_), #cztest_a_, #cztest_b_))
#define EXPECT_STRNE(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectStringNotEqual((cztest_a_), (cztest_b_), #cztest_a_, #cztest_b_))
#define ASSERT_STREQ(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectStringEqual((cztest_a_), (cztest_b_), #cztest_a_, #cztest_b_))
#define ASSERT_STRNE(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectStringNotEqual((cztest_a_), (cztest_b_), #cztest_a_, #cztest_b_))

#define EXPECT_NEAR(cztest_a_, cztest_b_, cztest_tol_) CZTEST_EXPECT_(::cztest::ExpectNear((cztest_a_), (cztest_b_), (cztest_tol_), #cztest_a_, #cztest_b_))
#define ASSERT_NEAR(cztest_a_, cztest_b_, cztest_tol_) CZTEST_ASSERT_(::cztest::ExpectNear((cztest_a_), (cztest_b_), (cztest_tol_), #cztest_a_, #cztest_b_))

#define EXPECT_DOUBLE_EQ(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectAlmostEqual((cztest_a_), (cztest_b_), #cztest_a_, #cztest_b_))
#define EXPECT_FLOAT_EQ(cztest_a_, cztest_b_) CZTEST_EXPECT_(::cztest::ExpectAlmostEqual((cztest_a_), (cztest_b_), #cztest_a_, #cztest_b_))
#define ASSERT_DOUBLE_EQ(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectAlmostEqual((cztest_a_), (cztest_b_), #cztest_a_, #cztest_b_))
#define ASSERT_FLOAT_EQ(cztest_a_, cztest_b_) CZTEST_ASSERT_(::cztest::ExpectAlmostEqual((cztest_a_), (cztest_b_), #cztest_a_, #cztest_b_))

#define EXPECT_THROW(cztest_stmt_, cztest_type_) \
  CZTEST_EXPECT_(::cztest::ExpectThrow<cztest_type_>([&]() { cztest_stmt_; }, #cztest_stmt_, #cztest_type_))
#define ASSERT_THROW(cztest_stmt_, cztest_type_) \
  CZTEST_ASSERT_(::cztest::ExpectThrow<cztest_type_>([&]() { cztest_stmt_; }, #cztest_stmt_, #cztest_type_))
#define EXPECT_NO_THROW(cztest_stmt_) \
  CZTEST_EXPECT_(::cztest::ExpectNoThrow([&]() { cztest_stmt_; }, #cztest_stmt_))
#define ASSERT_NO_THROW(cztest_stmt_) \
  CZTEST_ASSERT_(::cztest::ExpectNoThrow([&]() { cztest_stmt_; }, #cztest_stmt_))
#define EXPECT_ANY_THROW(cztest_stmt_) \
  CZTEST_EXPECT_(::cztest::ExpectAnyThrow([&]() { cztest_stmt_; }, #cztest_stmt_))
#define ASSERT_ANY_THROW(cztest_stmt_) \
  CZTEST_ASSERT_(::cztest::ExpectAnyThrow([&]() { cztest_stmt_; }, #cztest_stmt_))

#define ADD_FAILURE() CZTEST_EXPECT_(::cztest::AssertionResult::Failure(""))
#define FAIL() CZTEST_ASSERT_(::cztest::AssertionResult::Failure(""))
#define SUCCEED() ((void)0)

// ---------------------------------------------------------------------------
// SCOPED_TRACE: describes the context a failure happened in.
//
// The message lives as long as the scope; a failure inside that scope reports
// all the open messages, innermost first.
//
//   for (const auto& item : items) {
//     SCOPED_TRACE("item: " << item.name);
//     EXPECT_EQ(item.value, 0);
//   }
//
// The name matches GoogleTest's, so migrating code keeps working.
// ---------------------------------------------------------------------------
#define CZTEST_SCOPED_TRACE(...)                                                    \
  ::cztest::detail::TraceScope CZTEST_CONCAT(cztest_trace_, __LINE__)(              \
      (::std::ostringstream{} << "" __VA_ARGS__).str())

#define SCOPED_TRACE(...) CZTEST_SCOPED_TRACE(__VA_ARGS__)

// ---------------------------------------------------------------------------
// SKIP: ends the current test without failing it.
//
// The test is reported as skipped and does not count as a failure.
// ---------------------------------------------------------------------------
#define CZTEST_SKIP(...) \
  ::cztest::detail::SkipNow((::std::ostringstream{} << "" __VA_ARGS__).str())

#define SKIP(...) CZTEST_SKIP(__VA_ARGS__)


// ---------------------------------------------------------------------------
// A test name has to work with the --filter syntax: no space, no colon and no
// wildcard character.  The check runs at compile time.
// ---------------------------------------------------------------------------
namespace detail {

consteval std::string_view CheckTestName(std::string_view name) {
  for (const char character : name) {
    if (character == ' ' || character == ':' || character == '/' || character == '*' ||
        character == '?') {
      throw "a test name must not contain a space, a colon or a wildcard character";
    }
  }
  return name;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Test registration
// ---------------------------------------------------------------------------
namespace detail {

struct TestInfo {
  std::string suite;
  std::string name;
  // The body of a plain test is a plain function pointer.  A std::function
  // that is built from a lambda builds a class of its own for every lambda,
  // and the compiler has to build one for every test; that costs about
  // twelve milliseconds per test.
  void (*body)();
  // A parameterised test carries its value, so it needs a callable that can
  // hold something.  There is one per case, and there are few of them.
  std::function<void()> stateful_body;
  std::source_location location{};

  std::string full_name() const { return suite + "." + name; }

  void Run() const {
    if (body != nullptr) {
      body();
    } else {
      stateful_body();
    }
  }
};

class Registry {
 public:
  static Registry& Get() {
    static Registry instance;
    return instance;
  }

  void Add(TestInfo info) { tests_.push_back(std::move(info)); }

  const std::vector<TestInfo>& tests() const { return tests_; }

 private:
  std::vector<TestInfo> tests_;
};

class Registrar {
 public:
  // The body is a plain function pointer and not a std::function: a
  // std::function that is built from a lambda builds a class of its own for
  // every lambda, and the compiler has to build one for every test.  That
  // costs about twelve milliseconds per test, which is more than the test
  // itself takes to run.
  Registrar(std::string_view suite, std::string_view name, std::source_location location,
             void (*body)()) {
    Registry::Get().Add(TestInfo{std::string(suite), std::string(name), body, {}, location});
  }
};

/// A test name that more than one test uses.
struct DuplicateTest {
  std::string full_name;
  std::vector<std::source_location> locations;
};

/// Finds the names that are used by more than one test.
///
/// Two tests with the same "Suite.Name" are a mistake: they cannot be told
/// apart in the output, and a --filter selects both of them.  The compiler
/// cannot catch it, because the tests live in different translation units.
inline std::vector<DuplicateTest> FindDuplicateTests(const std::vector<TestInfo>& tests) {
  std::map<std::string, std::vector<std::source_location>> by_name;
  for (const auto& test : tests) {
    by_name[test.full_name()].push_back(test.location);
  }

  std::vector<DuplicateTest> duplicates;
  for (const auto& [name, locations] : by_name) {
    if (locations.size() < 2) continue;
    // The registration order of the translation units is not fixed, so the
    // places are sorted to keep the report the same from run to run.
    auto sorted = locations;
    std::sort(sorted.begin(), sorted.end(),
              [](const std::source_location& a, const std::source_location& b) {
                const char* file_a = a.file_name() != nullptr ? a.file_name() : "";
                const char* file_b = b.file_name() != nullptr ? b.file_name() : "";
                const int by_file = std::strcmp(file_a, file_b);
                if (by_file != 0) return by_file < 0;
                return a.line() < b.line();
              });
    duplicates.push_back(DuplicateTest{name, std::move(sorted)});
  }
  return duplicates;
}

}  // namespace detail

// Fixture base class -------------------------------------------------------
class Test {
 public:
  virtual ~Test() = default;
  virtual void SetUp() {}
  virtual void TearDown() {}

 protected:
  Test() = default;
};

#define CZTEST_CONCAT_(a, b) a##b
#define CZTEST_CONCAT(a, b) CZTEST_CONCAT_(a, b)

#define TEST(suite, name)                                                                      \
  static void CZTEST_CONCAT(cztest_body_, CZTEST_CONCAT(suite, CZTEST_CONCAT(_, name)))(            \
      ::cztest::detail::TestContext&);                                                          \
  static const ::cztest::detail::Registrar CZTEST_CONCAT(                                         \
      cztest_reg_, CZTEST_CONCAT(suite, CZTEST_CONCAT(_, name)))(                                  \
      ::cztest::detail::CheckTestName(#suite), ::cztest::detail::CheckTestName(#name),              \
      ::std::source_location::current(),                                                        \
      [] {                                                                                      \
        CZTEST_CONCAT(cztest_body_, CZTEST_CONCAT(suite, CZTEST_CONCAT(_, name)))(                  \
            *::cztest::detail::TestContext::Current());                                          \
      });                                                                                       \
  static void CZTEST_CONCAT(cztest_body_, CZTEST_CONCAT(suite, CZTEST_CONCAT(_, name)))(            \
      ::cztest::detail::TestContext&)

#define TEST_F(fixture, name)                                                                  \
  class CZTEST_CONCAT(cztest_fixture_, CZTEST_CONCAT(fixture, CZTEST_CONCAT(_, name)))               \
      : public fixture {                                                                       \
   public:                                                                                     \
    void TestBody();                                                                           \
    static const ::cztest::detail::Registrar registrar_;                                        \
  };                                                                                           \
  const ::cztest::detail::Registrar CZTEST_CONCAT(cztest_fixture_,                                \
                                                 CZTEST_CONCAT(fixture, CZTEST_CONCAT(_, name)))   \
      ::registrar_(::cztest::detail::CheckTestName(#fixture),                              \
                                ::cztest::detail::CheckTestName(#name),                              \
                                ::std::source_location::current(), [] {                             \
        CZTEST_CONCAT(cztest_fixture_, CZTEST_CONCAT(fixture, CZTEST_CONCAT(_, name))) instance;    \
        static_cast<::cztest::Test&>(instance).SetUp();                                         \
        instance.TestBody();                                                                   \
        static_cast<::cztest::Test&>(instance).TearDown();                                      \
      });                                                                                       \
  void CZTEST_CONCAT(cztest_fixture_, CZTEST_CONCAT(fixture, CZTEST_CONCAT(_, name)))::TestBody()

// Aliases with a project specific prefix.
#define CZTEST_TEST(suite, name) TEST(suite, name)
#define CZTEST_TEST_F(fixture, name) TEST_F(fixture, name)

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------
namespace detail {

// 0 = not started, 1 = running, 2 = finished.
inline int& RunState() {
  static int state = 0;
  return state;
}

/// Hooks that run before the tests are collected.  The parameterised test
/// support installs its resolver here, so a test binary that does not use it
/// pays nothing.
inline std::vector<std::function<void()>>& ParamResolvers() {
  static std::vector<std::function<void()>> resolvers;
  return resolvers;
}

inline int& ExitCode() {
  static int code = 0;
  return code;
}

struct RunOptions {
  std::string filter{"*"};
  bool list_tests{false};
  bool color{false};
  bool color_set{false};
  int repeat{1};
  std::string junit_path;
  bool verbose{false};  // also print a "[ RUN ]" line before every test
  bool quiet{false};    // only print failures and the summary
};

inline bool WildcardMatch(std::string_view pattern, std::string_view text) {
  if (pattern.empty()) return text.empty();
  if (pattern.front() == '*') {
    for (std::size_t i = 0; i <= text.size(); ++i) {
      if (WildcardMatch(pattern.substr(1), text.substr(i))) return true;
    }
    return false;
  }
  if (text.empty()) return false;
  if (pattern.front() == '?' || pattern.front() == text.front()) {
    return WildcardMatch(pattern.substr(1), text.substr(1));
  }
  return false;
}

/// The filter of the runner.
///
/// The syntax is the one of GoogleTest: the patterns are separated by a
/// colon, and a pattern that starts with a dash excludes the tests it
/// matches.  A test runs when it matches one of the positive patterns (or
/// when there is none) and none of the negative ones.
struct Filter {
  std::vector<std::string> positive;
  std::vector<std::string> negative;
};

inline Filter ParseFilter(const std::string& text) {
  Filter filter;
  std::size_t start = 0;
  while (true) {
    const auto end = text.find(':', start);
    const auto part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!part.empty()) {
      if (part.front() == '-') {
        filter.negative.push_back(part.substr(1));
      } else {
        filter.positive.push_back(part);
      }
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return filter;
}

inline bool FilterMatches(const Filter& filter, std::string_view text) {
  for (const auto& pattern : filter.negative) {
    if (WildcardMatch(pattern, text)) return false;
  }
  if (filter.positive.empty()) return true;
  for (const auto& pattern : filter.positive) {
    if (WildcardMatch(pattern, text)) return true;
  }
  return false;
}

inline bool ParseColorMode(const std::string& value, bool* out) {
  if (value == "on" || value == "always" || value == "yes" || value == "1") {
    *out = true;
    return true;
  }
  if (value == "off" || value == "never" || value == "no" || value == "0") {
    *out = false;
    return true;
  }
  return false;
}

inline bool StdoutIsTty() {
#if defined(_WIN32)
  return ::_isatty(::_fileno(stdout)) != 0;
#else
  return ::isatty(::fileno(stdout)) != 0;
#endif
}

class Console {
 public:
  explicit Console(bool color) : color_(color) {}

  void Print(const char* color, const std::string& text) {
    if (color_ && color != nullptr) {
      std::cout << color << text << "\033[0m";
    } else {
      std::cout << text;
    }
    std::cout << '\n';
  }

 private:
  bool color_;
};

/// Whether a location points at something.
inline bool HasLocation(std::source_location location) {
  return location.file_name() != nullptr && *location.file_name() != '\0';
}

/// Whether a function name belongs to a body that the framework generated.
///
/// The name is searched for anywhere in the text, because the compilers
/// print it in their own way: GCC and Clang put the return type and the
/// parameters in front of and behind it, MSVC prints the plain name.
inline bool IsGeneratedFunction(std::string_view name) {
  return name.find("cztest_body_") != std::string_view::npos ||
         name.find("cztest_fixture_") != std::string_view::npos ||
         name.find("cztest_param_fixture_") != std::string_view::npos;
}

/// Removes the calling convention from a function name.
///
/// Clang on Windows writes it into the name ("int __cdecl demo::Helper(int)"),
/// which tells the reader nothing about the code.
inline std::string CleanFunctionName(std::string_view name) {
  std::string text(name);
  for (const char* convention : {"__cdecl", "__stdcall", "__fastcall", "__vectorcall"}) {
    const std::string token = std::string(" ") + convention;
    std::size_t at = 0;
    while ((at = text.find(token, at)) != std::string::npos) {
      text.erase(at, token.size());
    }
  }
  return text;
}

/// Renders a source location as "file:line (function)".
///
/// The function name is omitted when the assertion sits directly in a test
/// body: there the name would be the one of the generated body function,
/// which tells the reader nothing.  An empty location renders as nothing at
/// all, which happens when the framework has nothing to point at.
inline std::string DescribeLocation(std::source_location location) {
  if (location.file_name() == nullptr || *location.file_name() == '\0') return {};
  std::string text = location.file_name();
  text += ":";
  text += std::to_string(location.line());
  const char* function = location.function_name();
  if (function != nullptr && *function != '\0') {
    const std::string_view name(function);
    if (!IsGeneratedFunction(name)) {
      text += " (";
      text += CleanFunctionName(name);
      text += ")";
    }
  }
  return text;
}

/// Renders a source location as "file:line".
///
/// Used for the place where a test was registered: there the function name
/// is the one of the generated initialiser, which tells the reader nothing.
inline std::string DescribePlace(std::source_location location) {
  if (!HasLocation(location)) return "<unknown location>";
  std::string text = location.file_name();
  text += ":";
  text += std::to_string(location.line());
  return text;
}

class TestRunner {
 public:
  explicit TestRunner(const RunOptions& options) : options_(options) {}

  int Run() {
    // Resolve the parameterised tests before the cases are collected.
    for (auto& resolver : detail::ParamResolvers()) {
      resolver();
    }

    auto& registry = Registry::Get();
    const auto& all = registry.tests();

    const bool color = options_.color_set ? options_.color : StdoutIsTty();
    Console console(color);
    const bool quiet = options_.quiet;

    // Two tests with the same name are a mistake, so the runner refuses to
    // start and says where the copies are.
    const auto duplicates = FindDuplicateTests(all);
    if (!duplicates.empty()) {
      console.Print("\033[0;31m", "[==========] Duplicate test names:");
      for (const auto& duplicate : duplicates) {
        console.Print(nullptr, "  " + duplicate.full_name);
        for (const auto& location : duplicate.locations) {
          console.Print(nullptr, "    " + DescribePlace(location));
        }
      }
      console.Print(nullptr,
                    "Every test needs its own name: rename the ones that repeat.");
      return 1;
    }

    std::vector<const TestInfo*> selected;
    const auto filter = ParseFilter(options_.filter);
    for (const auto& test : all) {
      if (FilterMatches(filter, test.full_name())) {
        selected.push_back(&test);
      }
    }

    if (selected.empty()) {
      // A filter that matches nothing runs no test at all, and that is
      // easy to miss in the log of a CI run.
      console.Print("\033[0;33m", "[==========] No test matched the filter '" + options_.filter + "'");
      console.Print(nullptr, "Check the names with --list.");
    }

    if (options_.list_tests) {
      for (const auto* test : selected) {
        std::cout << test->full_name() << "\n";
      }
      return 0;
    }

    std::vector<FailedTest> failed;
    std::vector<SkippedTest> skipped_tests;
    std::map<std::string, double> durations;
    int total_run = 0;
    const auto start = std::chrono::steady_clock::now();

    if (!quiet) {
      console.Print(nullptr,
                    "[==========] Running " + std::to_string(selected.size()) + " test(s).");
    }

    // With --repeat the summary counts the runs, not the tests, and every
    // line says which run it belongs to.
    const bool repeated = options_.repeat > 1;
    const std::string unit = repeated ? "test run(s)" : "test(s)";

    for (const auto* test : selected) {
      for (int repeat = 0; repeat < options_.repeat; ++repeat) {
        if (options_.verbose) {
          console.Print("\033[0;33m", "[ RUN      ] " + test->full_name());
        }
        const auto result = RunSingle(*test);
        total_run += result.counted ? 1 : 0;
        durations[test->full_name()] = result.seconds;
        const std::string run_note =
            repeated ? " (run " + std::to_string(repeat + 1) + " of " +
                           std::to_string(options_.repeat) + ")"
                     : std::string();
        if (result.skipped) {
          skipped_tests.push_back(SkippedTest{test->full_name(), result.skip_reason});
          if (!quiet) {
            std::string line = "[  SKIPPED ] " + test->full_name() + run_note;
            if (!result.skip_reason.empty()) {
              line += " (";
              line += result.skip_reason;
              line += ")";
            }
            console.Print("\033[0;33m", line);
          }
        } else if (!result.passed) {
          failed.push_back(FailedTest{test->full_name(), result.failures});
          // The name of a failing test is always printed: it identifies the
          // failure, even in quiet mode.
          console.Print("\033[0;31m", "[  FAILED  ] " + test->full_name() + run_note);
          for (const auto& failure : result.failures) {
            const auto where = DescribeLocation(failure.location);
            console.Print(nullptr, (where.empty() ? std::string() : where + ": ") + "Failure\n" + failure.message);
          }
        } else if (!quiet) {
          console.Print("\033[0;32m", "[       OK ] " + test->full_name() + run_note);
        }
      }
    }

    const auto total_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    console.Print(nullptr, "[==========] " + std::to_string(total_run) + " " + unit + " ran. (" +
                              std::to_string(total_ms) + " ms total)");
    const int passed = total_run - static_cast<int>(failed.size()) -
                       static_cast<int>(skipped_tests.size());
    if (passed > 0) {
      console.Print("\033[0;32m", "[  PASSED  ] " + std::to_string(passed) + " " + unit + ".");
    }
    if (!skipped_tests.empty()) {
      console.Print("\033[0;33m", "[  SKIPPED ] " + std::to_string(skipped_tests.size()) + " " + unit + ".");
    }
    if (!failed.empty()) {
      console.Print("\033[0;31m", "[  FAILED  ] " + std::to_string(failed.size()) + " " + unit + " failed.");
    }

    if (!options_.junit_path.empty()) {
      WriteJUnitXml(options_.junit_path, selected, failed, skipped_tests, durations);
    }

    return failed.empty() ? 0 : 1;
  }

 private:
  struct SingleResult {
    bool passed{true};
    bool counted{true};
    bool skipped{false};
    std::string skip_reason;
    double seconds{0.0};
    std::vector<FailureRecord> failures;
  };

  /// A test that failed, with the failures it reported.
  struct FailedTest {
    std::string name;
    std::vector<FailureRecord> failures;
  };

  /// A test that was skipped, with the reason it gave.
  struct SkippedTest {
    std::string name;
    std::string reason;
  };

  SingleResult RunSingle(const TestInfo& test) {
    TestContext* ctx = TestContext::Current();
    if (ctx == nullptr) {
      static TestContext fallback;
      TestContext::Current() = &fallback;
      ctx = &fallback;
    }
    ctx->Begin(test.full_name());
    bool skipped = false;
    std::string skip_reason;
    const auto started = std::chrono::steady_clock::now();
    try {
      test.Run();
    } catch (const FatalAssertionFailure&) {
      // The assertion already reported the failure; abort the test body.
    } catch (const SkipTest& skip) {
      // The test asked to be skipped; it did not fail.
      skipped = true;
      skip_reason = skip.what();
    } catch (const std::exception& e) {
      ctx->AddFailure(test.location, std::string("Unexpected exception: ") + e.what());
    } catch (...) {
      ctx->AddFailure(test.location, "Unexpected unknown exception");
    }

    SingleResult result;
    result.passed = !ctx->HasFailures();
    result.skipped = skipped;
    result.skip_reason = std::move(skip_reason);
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    if (skipped) {
      // A skipped test did not really run: its failures do not count.
      result.failures.clear();
      result.passed = true;
    } else {
      result.failures = ctx->failures();
    }
    ctx->End();
    return result;
  }

  /// Escapes the characters that XML does not allow in text and attributes.
  static std::string EscapeXml(const std::string& text) {
    std::string out;
    for (const char c : text) {
      switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c; break;
      }
    }
    return out;
  }

  /// Renders a duration in seconds, with the microsecond precision that the
  /// dashboards of the CI expect.
  static std::string FormatSeconds(double seconds) {
    std::ostringstream out;
    out.setf(std::ios::fixed, std::ios::floatfield);
    out.precision(6);
    out << seconds;
    return out.str();
  }

  static void WriteJUnitXml(const std::string& path, const std::vector<const TestInfo*>& tests,
                             const std::vector<FailedTest>& failed,
                             const std::vector<SkippedTest>& skipped,
                             const std::map<std::string, double>& durations) {
    std::ofstream file(path);
    if (!file) return;
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<testsuites>\n";
    for (const auto* test : tests) {
      const auto failure = std::find_if(failed.begin(), failed.end(), [&](const FailedTest& entry) {
        return entry.name == test->full_name();
      });
      const auto skip = std::find_if(skipped.begin(), skipped.end(), [&](const SkippedTest& entry) {
        return entry.name == test->full_name();
      });
      const auto duration = durations.find(test->full_name());
      const std::string time =
          duration == durations.end() ? std::string("0.000000") : FormatSeconds(duration->second);

      file << "  <testsuite name=\"" << EscapeXml(test->suite) << "\" tests=\"1\" failures=\""
           << (failure == failed.end() ? 0 : 1) << "\" time=\"" << time << "\">\n";
      file << "    <testcase classname=\"" << EscapeXml(test->suite) << "\" name=\""
           << EscapeXml(test->name) << "\" time=\"" << time << "\"";
      if (failure != failed.end()) {
        file << ">\n";
        for (const auto& record : failure->failures) {
          // The message is in the attribute and in the text, because the
          // tools of the CI read one or the other.
          const auto newline = record.message.find('\n');
          const auto first_line = record.message.substr(0, newline);
          const auto where = DescribeLocation(record.location);
          file << "      <failure message=\"" << EscapeXml(first_line) << "\">"
               << EscapeXml(where.empty() ? record.message : where + ": " + record.message)
               << "</failure>\n";
        }
        file << "    </testcase>\n";
      } else if (skip != skipped.end()) {
        file << ">\n      <skipped message=\"" << EscapeXml(skip->reason) << "\"/>\n"
             << "    </testcase>\n";
      } else {
        file << "/>\n";
      }
      file << "  </testsuite>\n";
    }
    file << "</testsuites>\n";
  }

  RunOptions options_;
};

inline void PrintUsage(const char* program) {
  std::cout << "Usage: " << program << " [options]\n"
            << "  --filter=<patterns>  run only the tests that match ('*' and '?' wildcards,\n"
            << "                      patterns separated by ':', a leading '-' excludes)\n"
            << "  --list               list the registered tests and exit\n"
            << "  --repeat=<n>         run every test n times\n"
            << "  --quiet              only print failures and the summary\n"
            << "  --verbose            also print a '[ RUN ]' line before every test\n"
            << "  --color=on|off       force colored output on or off\n"
            << "  --junit=<file>       write a JUnit XML report to <file>\n"
            << "  --help               show this message\n";
}

inline RunOptions ParseCommandLine(int argc, char** argv) {
  RunOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--filter=", 0) == 0) {
      options.filter = arg.substr(std::strlen("--filter="));
    } else if (arg == "--list" || arg == "--list_tests") {
      options.list_tests = true;
    } else if (arg.rfind("--repeat=", 0) == 0) {
      options.repeat = std::max(1, std::atoi(arg.c_str() + std::strlen("--repeat=")));
    } else if (arg == "--quiet" || arg == "-q") {
      options.quiet = true;
    } else if (arg == "--verbose" || arg == "-v") {
      options.verbose = true;
    } else if (arg.rfind("--color=", 0) == 0) {
      options.color_set = ParseColorMode(arg.substr(std::strlen("--color=")), &options.color);
    } else if (arg.rfind("--junit=", 0) == 0) {
      options.junit_path = arg.substr(std::strlen("--junit="));
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      std::exit(0);
    } else if (!arg.empty() && arg[0] != '-') {
      options.filter = arg;
    }
  }
  return options;
}

}  // namespace detail

inline int RunAllTests(int argc = 0, char** argv = nullptr) {
  if (detail::RunState() == 2) return detail::ExitCode();
  detail::RunState() = 1;
  const detail::RunOptions options = detail::ParseCommandLine(argc, argv);
  detail::TestRunner runner(options);
  const int code = runner.Run();
  detail::ExitCode() = code;
  detail::RunState() = 2;
  return code;
}

}  // namespace cztest

#endif  // CZTEST_CORE_HPP
