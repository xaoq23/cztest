// cztest - a modern, header-only C++ test framework with function-level mocks.
//
// core.hpp - test registry, assertions, runner and reporting.
//
// License: MIT
#ifndef CZTEST_CORE_HPP
#define CZTEST_CORE_HPP

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
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

namespace cztest {

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
    ctx->AddFailure(location, std::move(message));
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

template <class E, class F>
[[nodiscard]] AssertionResult ExpectThrow(F&& fn, std::string_view expr = "statement",
                                          std::string_view type_name = "the exception type") {
  try {
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
#define CZTEST_CHECK_(result, fatal)                                                  \
  if (auto cztest_result = (result); cztest_result.ok) {                             \
  } else                                                                              \
    ::cztest::detail::FailureRecorder(::std::source_location::current(), fatal) =     \
        (::cztest::detail::Message(cztest_result.message))

#define CZTEST_EXPECT_(result) CZTEST_CHECK_((result), false)
#define CZTEST_ASSERT_(result) CZTEST_CHECK_((result), true)

// EXPECT_* / ASSERT_* ---------------------------------------------------
#define EXPECT_TRUE(condition) CZTEST_EXPECT_(::cztest::ExpectTrue((condition), #condition))
#define EXPECT_FALSE(condition) CZTEST_EXPECT_(::cztest::ExpectFalse((condition), #condition))
#define ASSERT_TRUE(condition) CZTEST_ASSERT_(::cztest::ExpectTrue((condition), #condition))
#define ASSERT_FALSE(condition) CZTEST_ASSERT_(::cztest::ExpectFalse((condition), #condition))

#define EXPECT_EQ(a, b) CZTEST_EXPECT_(::cztest::ExpectEqual((a), (b), #a " == " #b))
#define EXPECT_NE(a, b) CZTEST_EXPECT_(::cztest::ExpectNotEqual((a), (b), #a " != " #b))
#define EXPECT_LT(a, b) CZTEST_EXPECT_(::cztest::ExpectLess((a), (b), #a " < " #b))
#define EXPECT_LE(a, b) CZTEST_EXPECT_(::cztest::ExpectLessEqual((a), (b), #a " <= " #b))
#define EXPECT_GT(a, b) CZTEST_EXPECT_(::cztest::ExpectGreater((a), (b), #a " > " #b))
#define EXPECT_GE(a, b) CZTEST_EXPECT_(::cztest::ExpectGreaterEqual((a), (b), #a " >= " #b))

#define ASSERT_EQ(a, b) CZTEST_ASSERT_(::cztest::ExpectEqual((a), (b), #a " == " #b))
#define ASSERT_NE(a, b) CZTEST_ASSERT_(::cztest::ExpectNotEqual((a), (b), #a " != " #b))
#define ASSERT_LT(a, b) CZTEST_ASSERT_(::cztest::ExpectLess((a), (b), #a " < " #b))
#define ASSERT_LE(a, b) CZTEST_ASSERT_(::cztest::ExpectLessEqual((a), (b), #a " <= " #b))
#define ASSERT_GT(a, b) CZTEST_ASSERT_(::cztest::ExpectGreater((a), (b), #a " > " #b))
#define ASSERT_GE(a, b) CZTEST_ASSERT_(::cztest::ExpectGreaterEqual((a), (b), #a " >= " #b))

#define EXPECT_STREQ(a, b) CZTEST_EXPECT_(::cztest::ExpectStringEqual((a), (b), #a, #b))
#define EXPECT_STRNE(a, b) CZTEST_EXPECT_(::cztest::ExpectStringNotEqual((a), (b), #a, #b))
#define ASSERT_STREQ(a, b) CZTEST_ASSERT_(::cztest::ExpectStringEqual((a), (b), #a, #b))
#define ASSERT_STRNE(a, b) CZTEST_ASSERT_(::cztest::ExpectStringNotEqual((a), (b), #a, #b))

#define EXPECT_NEAR(a, b, tolerance) CZTEST_EXPECT_(::cztest::ExpectNear((a), (b), (tolerance), #a, #b))
#define ASSERT_NEAR(a, b, tolerance) CZTEST_ASSERT_(::cztest::ExpectNear((a), (b), (tolerance), #a, #b))

#define EXPECT_THROW(statement, exception_type) \
  CZTEST_EXPECT_(::cztest::ExpectThrow<exception_type>([&]() { statement; }, #statement, #exception_type))
#define ASSERT_THROW(statement, exception_type) \
  CZTEST_ASSERT_(::cztest::ExpectThrow<exception_type>([&]() { statement; }, #statement, #exception_type))
#define EXPECT_NO_THROW(statement) \
  CZTEST_EXPECT_(::cztest::ExpectNoThrow([&]() { statement; }, #statement))
#define ASSERT_NO_THROW(statement) \
  CZTEST_ASSERT_(::cztest::ExpectNoThrow([&]() { statement; }, #statement))
#define EXPECT_ANY_THROW(statement) \
  CZTEST_EXPECT_(::cztest::ExpectAnyThrow([&]() { statement; }, #statement))
#define ASSERT_ANY_THROW(statement) \
  CZTEST_ASSERT_(::cztest::ExpectAnyThrow([&]() { statement; }, #statement))

#define ADD_FAILURE() CZTEST_EXPECT_(::cztest::AssertionResult::Failure(""))
#define FAIL() CZTEST_ASSERT_(::cztest::AssertionResult::Failure(""))
#define SUCCEED() ((void)0)


// ---------------------------------------------------------------------------
// Test registration
// ---------------------------------------------------------------------------
namespace detail {

struct TestInfo {
  std::string suite;
  std::string name;
  std::function<void()> body;
  std::source_location location{};

  std::string full_name() const { return suite + "." + name; }
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
  Registrar(const char* suite, const char* name, std::source_location location,
             std::function<void()> body) {
    Registry::Get().Add(TestInfo{suite, name, std::move(body), location});
  }
};

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
      #suite, #name, ::std::source_location::current(),                                                        \
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
      ::registrar_(#fixture, #name, ::std::source_location::current(), [] {                                    \
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

inline bool WildcardMatch(const char* pattern, const char* text) {
  if (*pattern == '\0') return *text == '\0';
  if (*pattern == '*') {
    for (const char* t = text;; ++t) {
      if (WildcardMatch(pattern + 1, t)) return true;
      if (*t == '\0') return false;
    }
  }
  if (*text == '\0') return false;
  if (*pattern == '?' || *pattern == *text) return WildcardMatch(pattern + 1, text + 1);
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

/// Renders a source location as "file:line (function)".
inline std::string DescribeLocation(std::source_location location) {
  std::string text = location.file_name();
  text += ":";
  text += std::to_string(location.line());
  if (location.function_name() != nullptr && *location.function_name() != '\0') {
    text += " (";
    text += location.function_name();
    text += ")";
  }
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

    std::vector<const TestInfo*> selected;
    for (const auto& test : all) {
      if (WildcardMatch(options_.filter.c_str(), test.full_name().c_str())) {
        selected.push_back(&test);
      }
    }

    if (options_.list_tests) {
      for (const auto* test : selected) {
        std::cout << test->full_name() << "\n";
      }
      return 0;
    }

    const bool color = options_.color_set ? options_.color : StdoutIsTty();
    Console console(color);
    const bool quiet = options_.quiet;

    std::vector<std::string> failed;
    int total_run = 0;
    const auto start = std::chrono::steady_clock::now();

    if (!quiet) {
      console.Print(nullptr,
                    "[==========] Running " + std::to_string(selected.size()) + " test(s).");
    }

    for (const auto* test : selected) {
      for (int repeat = 0; repeat < options_.repeat; ++repeat) {
        if (options_.verbose) {
          console.Print("\033[0;33m", "[ RUN      ] " + test->full_name());
        }
        const auto result = RunSingle(*test);
        total_run += result.counted ? 1 : 0;
        if (!result.passed) {
          failed.push_back(test->full_name());
          // The name of a failing test is always printed: it identifies the
          // failure, even in quiet mode.
          console.Print("\033[0;31m", "[  FAILED  ] " + test->full_name());
          for (const auto& failure : result.failures) {
            console.Print(nullptr, DescribeLocation(failure.location) + ": Failure\n" + failure.message);
          }
        } else if (!quiet) {
          console.Print("\033[0;32m", "[       OK ] " + test->full_name());
        }
      }
    }

    const auto total_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();

    console.Print(nullptr, "[==========] " + std::to_string(total_run) + " test(s) ran. (" +
                              std::to_string(total_ms) + " ms total)");
    const int passed = total_run - static_cast<int>(failed.size());
    if (passed > 0) {
      console.Print("\033[0;32m", "[  PASSED  ] " + std::to_string(passed) + " test(s).");
    }
    if (!failed.empty()) {
      console.Print("\033[0;31m", "[  FAILED  ] " + std::to_string(failed.size()) + " test(s) failed.");
    }

    if (!options_.junit_path.empty()) {
      WriteJUnitXml(options_.junit_path, selected, failed);
    }

    return failed.empty() ? 0 : 1;
  }

 private:
  struct SingleResult {
    bool passed{true};
    bool counted{true};
    std::vector<FailureRecord> failures;
  };

  SingleResult RunSingle(const TestInfo& test) {
    TestContext* ctx = TestContext::Current();
    if (ctx == nullptr) {
      static TestContext fallback;
      TestContext::Current() = &fallback;
      ctx = &fallback;
    }
    ctx->Begin(test.full_name());
    try {
      test.body();
    } catch (const FatalAssertionFailure&) {
      // The assertion already reported the failure; abort the test body.
    } catch (const std::exception& e) {
      ctx->AddFailure(test.location, std::string("Unexpected exception: ") + e.what());
    } catch (...) {
      ctx->AddFailure(test.location, "Unexpected unknown exception");
    }

    SingleResult result;
    result.passed = !ctx->HasFailures();
    result.failures = ctx->failures();
    ctx->End();
    return result;
  }

  static void WriteJUnitXml(const std::string& path, const std::vector<const TestInfo*>& tests,
                             const std::vector<std::string>& failed) {
    std::ofstream file(path);
    if (!file) return;
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<testsuites>\n";
    for (const auto* test : tests) {
      const bool ok = std::find(failed.begin(), failed.end(), test->full_name()) == failed.end();
      file << "  <testsuite name=\"" << test->suite << "\" tests=\"1\" failures=\"" << (ok ? 0 : 1)
           << "\">\n";
      file << "    <testcase classname=\"" << test->suite << "\" name=\"" << test->name << "\"";
      if (ok) {
        file << "/>\n";
      } else {
        file << ">\n      <failure/>\n    </testcase>\n";
      }
      file << "  </testsuite>\n";
    }
    file << "</testsuites>\n";
  }

  RunOptions options_;
};

inline void PrintUsage(const char* program) {
  std::cout << "Usage: " << program << " [options]\n"
            << "  --filter=<pattern>   run only tests matching <pattern> ('*' and '?' wildcards)\n"
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
