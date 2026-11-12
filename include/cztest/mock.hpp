// cztest - function level mocking support.
//
// A mock is a class that derives from an interface and from cztest::MockBase.
// Every mocked function is declared with the CZTEST_MOCK_METHOD macro, which
// generates the virtual override plus the "gmock_<name>" helper used by
// EXPECT_CALL.
//
//   struct IStorage {
//     virtual ~IStorage() = default;
//     virtual int  Get(int key) = 0;
//     virtual void Put(int key, std::string value) = 0;
//   };
//
//   class MockStorage : public IStorage, public cztest::MockBase {
//    public:
//     CZTEST_MOCK_METHOD(int, Get, int);
//     CZTEST_MOCK_METHOD(void, Put, int, std::string);
//   };
//
//   TEST(Storage, Reads) {
//     MockStorage mock;
//     EXPECT_CALL(mock, Get(42)).Times(1).WillOnce(Return(7));
//     EXPECT_EQ(mock.Get(42), 7);
//   }
//
// License: MIT
#ifndef CZTEST_MOCK_HPP
#define CZTEST_MOCK_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "cztest/core.hpp"
#include "cztest/matchers.hpp"

namespace cztest {

// How many times a mocked function is expected to be called.
struct Cardinality {
  int min{0};
  int max{-1};  // -1 means "unbounded"
};

inline Cardinality Exactly(int times) { return Cardinality{times, times}; }
inline Cardinality AtLeast(int times) { return Cardinality{times, -1}; }
inline Cardinality AtMost(int times) { return Cardinality{0, times}; }
inline Cardinality AnyNumber() { return Cardinality{0, -1}; }

// Thrown when a mocked function is called in a way that cannot produce a
// return value (e.g. a reference returning function without expectation).
class MockFailure : public std::exception {
 public:
  explicit MockFailure(std::string message) : message_(std::move(message)) {}
  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

namespace detail {

// ---------------------------------------------------------------------------
// MoveOnlyFunction: the type an action is stored in.
//
// std::function requires a copyable action, so an action that captures a move
// only object (a unique_ptr, for example) could not be stored.  The standard
// type for that is std::move_only_function, but it is C++23 and not every
// library ships it (libstdc++ defines the feature macro without providing the
// class), so the small type is written here instead: it works on C++20 and
// does not depend on the library.
// ---------------------------------------------------------------------------
template <class Signature>
class MoveOnlyFunction;

template <class R, class... Args>
class MoveOnlyFunction<R(Args...)> {
 public:
  MoveOnlyFunction() = delete;
  MoveOnlyFunction(const MoveOnlyFunction&) = delete;
  MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;
  MoveOnlyFunction(MoveOnlyFunction&&) = default;
  MoveOnlyFunction& operator=(MoveOnlyFunction&&) = default;

  template <class F>
    requires std::is_invocable_r_v<R, F&, Args...>
  explicit MoveOnlyFunction(F&& callable)
      : impl_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(callable))) {}

  R operator()(Args... args) const { return impl_->call(std::forward<Args>(args)...); }

  explicit operator bool() const { return impl_ != nullptr; }

 private:
  struct Concept {
    virtual ~Concept() = default;
    virtual R call(Args... args) const = 0;
  };

  template <class F>
  struct Model final : Concept {
    explicit Model(F callable) : callable_(std::move(callable)) {}

    R call(Args... args) const override {
      return std::invoke(callable_, std::forward<Args>(args)...);
    }

    mutable F callable_;
  };

  std::unique_ptr<Concept> impl_;
};

// ---------------------------------------------------------------------------
// Concepts that describe what an action has to look like.
//
// They are checked where the user meets them, so that a wrong action
// produces a readable error instead of a template backtrace.
// ---------------------------------------------------------------------------

/// An action can stand in for a mocked function: it is called with the
/// arguments of the function and its result is converted to the return type.
template <class A, class Ret, class... Args>
concept ActionFor =
    requires(A action, Args... args) {
  { action(args...) };
} &&
    (std::same_as<Ret, void> ||
     std::convertible_to<std::invoke_result_t<A&, Args...>, Ret>);

/// The arguments of a mocked function arrive at the expectation as a span of
/// pointers; the expectation casts them back to the exact parameter types.
using ArgSpan = std::span<void* const>;

// ---------------------------------------------------------------------------
// The source location of the EXPECT_CALL that created the expectation.
// ---------------------------------------------------------------------------

inline std::source_location& CurrentSite() {
  static thread_local std::source_location site{};
  return site;
}

class SiteGuard {
 public:
  SiteGuard(std::source_location location) : previous_(CurrentSite()) { CurrentSite() = location; }
  ~SiteGuard() { CurrentSite() = previous_; }

 private:
  std::source_location previous_;
};

inline SiteGuard MakeSite(std::source_location location) { return SiteGuard(location); }

// ---------------------------------------------------------------------------
// Type erased view of one expectation.
// ---------------------------------------------------------------------------
class ExpectationEntry {
 public:
  virtual ~ExpectationEntry() = default;
  virtual bool Matches(detail::ArgSpan args) const = 0;
  virtual void Perform(detail::ArgSpan args, void* ret_out) = 0;
  virtual void CountCall() = 0;
  virtual bool Saturated() const = 0;
  virtual bool Satisfied() const = 0;
  virtual std::string Describe() const = 0;
  virtual std::source_location Site() const = 0;
};

// Storage for the return value of a mocked function.  Reference returning
// functions keep a pointer to the referenced object.
template <class Ret>
struct ReturnSlot {
  using Value = std::remove_reference_t<Ret>;
  std::optional<Value> value;
  Value* pointer{nullptr};
};

// ---------------------------------------------------------------------------
// Helpers used to build failure messages.
// ---------------------------------------------------------------------------
inline void AppendArg(std::string& message, bool& first, const std::string& text) {
  if (!first) message += ", ";
  first = false;
  message += text;
}

template <class... Args>
std::string DescribeCall(const char* name, const Args&... args) {
  std::string message = "Unexpected call to mocked function '";
  message += name;
  message += "'(";
  bool first = true;
  (AppendArg(message, first, PrintValue(args)), ...);
  (void)first;
  message += ")";
  return message;
}

// How an argument is passed to the action of an expectation.  By value
// arguments are copied when they are copyable and moved otherwise.
template <class A>
using ActionArg = std::conditional_t<
    std::is_lvalue_reference_v<A>, A,
    std::conditional_t<std::is_copy_constructible_v<std::remove_reference_t<A>>,
                        std::remove_reference_t<A>&, std::remove_reference_t<A>&&>>;

// ---------------------------------------------------------------------------
// Expectation: fully typed, knows the signature of the mocked function.
// ---------------------------------------------------------------------------
template <class Ret, class... Args>
class Expectation final : public ExpectationEntry {
 public:
  using Action = detail::MoveOnlyFunction<Ret(Args...)>;
  using MatcherTuple = std::tuple<Matcher<std::decay_t<Args>>...>;

  Expectation(std::string name, MatcherTuple matchers, std::source_location site)
      : name_(std::move(name)), matchers_(std::move(matchers)), site_(site) {}

  // --- ExpectationEntry --------------------------------------------------
  bool Matches(detail::ArgSpan args) const override {
    return MatchImpl(args, std::index_sequence_for<Args...>{});
  }

  void Perform(detail::ArgSpan args, void* ret_out) override {
    if (!once_actions_.empty()) {
      Action action = std::move(once_actions_.front());
      once_actions_.erase(once_actions_.begin());
      InvokeAction(action, args, ret_out, std::index_sequence_for<Args...>{});
    } else if (default_action_.has_value()) {
      InvokeAction(*default_action_, args, ret_out, std::index_sequence_for<Args...>{});
    }
  }

  void CountCall() override {
    ++count_;
    if (count_ > 1 && !has_explicit_action_ && !explicit_cardinality_) {
      ReportFailure(site_,
                    "Mocked function '" + name_ + "' was called " + std::to_string(count_) +
                        " time(s). Use WillRepeatedly() to allow repeated calls.",
                    false);
    }
  }

  bool Saturated() const override { return max_ >= 0 && count_ >= max_; }
  bool Satisfied() const override { return count_ >= min_ && (max_ < 0 || count_ <= max_); }

  std::string Describe() const override {
    std::string message = "call to '" + name_ + "' with (";
    bool first = true;
    DescribeMatchers(message, first, std::index_sequence_for<Args...>{});
    message += ") expected ";
    message += std::to_string(min_);
    message += " time(s)";
    if (max_ >= 0 && max_ != min_) {
      message += "..";
      message += std::to_string(max_);
    }
    message += ", actual ";
    message += std::to_string(count_);
    message += " time(s)";
    return message;
  }

  std::source_location Site() const override { return site_; }

  // --- configuration -----------------------------------------------------
  void SetCardinality(Cardinality cardinality) {
    min_ = cardinality.min;
    max_ = cardinality.max;
    explicit_cardinality_ = true;
  }

  void AddOnceAction(Action action) {
    once_actions_.push_back(std::move(action));
    has_explicit_action_ = true;
  }

  void SetDefaultAction(Action action) {
    default_action_ = std::move(action);
    has_explicit_action_ = true;
  }

 private:
  template <std::size_t... I>
  bool MatchImpl([[maybe_unused]] detail::ArgSpan args, std::index_sequence<I...>) const {
    return (std::get<I>(matchers_)
                .Matches(*static_cast<const std::remove_reference_t<
                            std::tuple_element_t<I, std::tuple<Args...>>>*>(args[I])) &&
            ...);
  }

  template <std::size_t... I>
  void InvokeAction(const Action& action, [[maybe_unused]] detail::ArgSpan args, void* ret_out,
                    std::index_sequence<I...>) {
    using Tuple = std::tuple<ActionArg<std::tuple_element_t<I, std::tuple<Args...>>>...>;
    Tuple tuple(std::forward<ActionArg<std::tuple_element_t<I, std::tuple<Args...>>>>(
                    *static_cast<std::remove_reference_t<
                        std::tuple_element_t<I, std::tuple<Args...>>>*>(args[I]))...);
    if constexpr (std::is_void_v<Ret>) {
      std::apply(action, std::move(tuple));
    } else if constexpr (std::is_lvalue_reference_v<Ret>) {
      Ret result = std::apply(action, std::move(tuple));
      auto* slot = static_cast<ReturnSlot<Ret>*>(ret_out);
      slot->pointer = std::addressof(result);
    } else {
      auto* slot = static_cast<ReturnSlot<Ret>*>(ret_out);
      slot->value = std::apply(action, std::move(tuple));
    }
  }

  template <std::size_t... I>
  void DescribeMatchers(std::string& message, bool& first, std::index_sequence<I...>) const {
    (AppendArg(message, first, std::get<I>(matchers_).Describe()), ...);
  }

  std::string name_;
  MatcherTuple matchers_;
  std::source_location site_;
  std::vector<Action> once_actions_;
  std::optional<Action> default_action_;
  bool has_explicit_action_{false};
  bool explicit_cardinality_{false};
  int min_{0};
  int max_{-1};
  int count_{0};
};

// ---------------------------------------------------------------------------
// ExpectationSpec: the fluent interface returned by EXPECT_CALL.
// ---------------------------------------------------------------------------
template <class Ret, class... Args>
class ExpectationSpec {
 public:
  using Action = detail::MoveOnlyFunction<Ret(Args...)>;

  explicit ExpectationSpec(std::shared_ptr<Expectation<Ret, Args...>> entry) : entry_(std::move(entry)) {}

  ExpectationSpec& Times(int times) {
    entry_->SetCardinality(Cardinality{times, times});
    return *this;
  }

  ExpectationSpec& Times(Cardinality cardinality) {
    entry_->SetCardinality(cardinality);
    return *this;
  }

  ExpectationSpec& Once() { return Times(1); }

  /// Sets the action of the next call.
  ///
  /// The action is checked against the signature of the mocked function, so a
  /// lambda with the wrong parameters is reported as such instead of failing
  /// to convert into the std::function.
  template <class A>
    requires detail::ActionFor<A, Ret, Args...>
  ExpectationSpec& WillOnce(A&& action) {
    static_assert(detail::ActionFor<A, Ret, Args...>,
                  "The action does not fit the mocked function: it has to take the "
                  "parameters of the function and return its return type.");
    entry_->AddOnceAction(Action(std::forward<A>(action)));
    return *this;
  }

  /// Sets the action of every following call.
  template <class A>
    requires detail::ActionFor<A, Ret, Args...>
  ExpectationSpec& WillRepeatedly(A&& action) {
    static_assert(detail::ActionFor<A, Ret, Args...>,
                  "The action does not fit the mocked function: it has to take the "
                  "parameters of the function and return its return type.");
    entry_->SetDefaultAction(Action(std::forward<A>(action)));
    return *this;
  }

  /// Same as WillRepeatedly(); the name reads better when the action computes
  /// the return value.
  template <class A>
    requires detail::ActionFor<A, Ret, Args...>
  ExpectationSpec& DoAndReturn(A&& action) {
    return WillRepeatedly(std::forward<A>(action));
  }

 private:
  std::shared_ptr<Expectation<Ret, Args...>> entry_;
};

}  // namespace detail

// ---------------------------------------------------------------------------
// MockBase: mix-in that gives a class the mocking behaviour.
//
// A mock is not thread safe: the call counter and the expectation registry
// are plain members without a lock.  Use one mock per thread, or guard the
// calls yourself.
// ---------------------------------------------------------------------------
class MockBase : public detail::MockVerifier {
 public:
  MockBase();
  ~MockBase() override;

  // Verifies the expectations once (called automatically).
  void Verify() override;

  // --- internal API ------------------------------------------------------
  void RegisterExpectation(const char* name, std::shared_ptr<detail::ExpectationEntry> entry) const {
    expectations_[name].push_back(std::move(entry));
  }

  // Finds the expectation matching the call.  The most recently registered
  // matching expectation wins; saturated ones are only used when nothing else
  // matches (and the over-saturation is reported).
  detail::ExpectationEntry* FindExpectation(const char* name, detail::ArgSpan args,
                                             bool* saturated_match) const {
    *saturated_match = false;
    const auto it = expectations_.find(name);
    if (it == expectations_.end()) return nullptr;

    detail::ExpectationEntry* saturated = nullptr;
    for (auto entry = it->second.rbegin(); entry != it->second.rend(); ++entry) {
      if (!(*entry)->Matches(args)) continue;
      if ((*entry)->Saturated()) {
        if (saturated == nullptr) saturated = entry->get();
        continue;
      }
      return entry->get();
    }
    if (saturated != nullptr) *saturated_match = true;
    return saturated;
  }

  /// Where the expectations of a method were declared; an empty location when
  /// the method has none.
  std::source_location ExpectationSite(const char* name) const {
    const auto it = expectations_.find(name);
    if (it == expectations_.end() || it->second.empty()) return {};
    return it->second.front()->Site();
  }

 private:
  mutable std::map<std::string, std::vector<std::shared_ptr<detail::ExpectationEntry>>> expectations_;
  bool verified_{false};
};

inline MockBase::MockBase() {
  if (auto* ctx = detail::TestContext::Current()) {
    if (ctx->running()) ctx->AddMock(this);
  }
}

inline MockBase::~MockBase() {
  if (auto* ctx = detail::TestContext::Current()) {
    if (ctx->running()) {
      Verify();
      ctx->RemoveMock(this);
    }
  }
}

inline void MockBase::Verify() {
  if (verified_) return;
  verified_ = true;
  for (const auto& entry : expectations_) {
    for (const auto& expectation : entry.second) {
      if (!expectation->Satisfied()) {
        const auto site = expectation->Site();
        detail::ReportFailure(site,
                              "Mock expectation failed: " + expectation->Describe(), false);
      }
    }
  }
}

namespace detail {

// ---------------------------------------------------------------------------
// MockInvoke: called from the generated mock method.
// ---------------------------------------------------------------------------
template <class Ret, class... Args>
Ret MockInvoke(const MockBase& mock, const char* name, std::source_location location, Args&... args) {
  // The arguments travel to the expectation as a span of pointers; the
  // expectation casts them back to the exact types of the mocked function.
  // The cast drops the const of a `const T&` parameter: the pointee is the
  // caller's object, and the expectation only reads it (or moves out of it
  // when the parameter is move only).
  std::array<void*, sizeof...(Args)> arg_ptrs{
      const_cast<void*>(static_cast<const void*>(std::addressof(args)))...};
  const ArgSpan args_span(arg_ptrs);

  bool saturated = false;
  auto* entry = mock.FindExpectation(name, args_span, &saturated);
  if (entry == nullptr) {
    // Point at the expectation that did not match: that is the line in the
    // test the reader has to change.
    const auto site = mock.ExpectationSite(name);
    ReportFailure(::cztest::detail::HasLocation(site) ? site : location,
                  DescribeCall(name, args...), false);
    if constexpr (std::is_void_v<Ret>) {
      return;
    } else if constexpr (std::is_lvalue_reference_v<Ret>) {
      throw MockFailure(std::string("Unexpected call to mocked function '") + name +
                        "'; no matching expectation was set.");
    } else if constexpr (std::is_default_constructible_v<Ret>) {
      return Ret{};
    } else {
      throw MockFailure(std::string("Unexpected call to mocked function '") + name +
                        "'; no matching expectation was set.");
    }
  }
  if (saturated) {
    ReportFailure(std::source_location::current(),
                  "Mocked function '" + std::string(name) + "' was called more times than expected: " +
                      entry->Describe(),
                  false);
  }
  entry->CountCall();

  if constexpr (std::is_void_v<Ret>) {
    entry->Perform(args_span, nullptr);
    return;
  } else {
    ReturnSlot<Ret> slot;
    entry->Perform(args_span, &slot);
    if constexpr (std::is_lvalue_reference_v<Ret>) {
      if (slot.pointer == nullptr) {
        throw MockFailure(std::string("Mocked function '") + name +
                          "' did not produce a reference return value.");
      }
      return *slot.pointer;
    } else {
      if (!slot.value.has_value()) {
        throw MockFailure(std::string("Mocked function '") + name +
                          "' has no action; use WillOnce()/WillRepeatedly() to provide one.");
      }
      return *slot.value;
    }
  }
}

// Creates the expectation and registers it with the mock.
template <class Ret, class... Args, class... MatcherArgs>
ExpectationSpec<Ret, Args...> MakeSpec(const MockBase& mock, const char* name,
                                      MatcherArgs&&... matchers) {
  using ExpectationType = Expectation<Ret, Args...>;
  using MatcherTuple = typename ExpectationType::MatcherTuple;
  auto entry = std::make_shared<ExpectationType>(std::string(name),
                                                  MatcherTuple(std::forward<MatcherArgs>(matchers)...),
                                                  CurrentSite());
  mock.RegisterExpectation(name, entry);
  return ExpectationSpec<Ret, Args...>(std::move(entry));
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
template <class V>
class ReturnAction {
 public:
  explicit ReturnAction(V value) : value_(std::move(value)) {}

  template <class... Args>
  V operator()(Args&&...) const {
    return value_;
  }

 private:
  V value_;
};

template <class V>
ReturnAction<std::decay_t<V>> Return(V&& value) {
  return ReturnAction<std::decay_t<V>>(std::forward<V>(value));
}

template <class V>
class ReturnRefAction {
 public:
  explicit ReturnRefAction(V& value) : value_(std::addressof(value)) {}

  template <class... Args>
  V& operator()(Args&&...) const {
    return *value_;
  }

 private:
  V* value_;
};

template <class V>
ReturnRefAction<V> ReturnRef(V& value) {
  return ReturnRefAction<V>(value);
}

template <class F>
auto Invoke(F callable) {
  return std::move(callable);
}

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------
#define CZTEST_NARG(...) CZTEST_NARG_(__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define CZTEST_NARG_(_1, _2, _3, _4, _5, _6, _7, _8, _9, N, ...) N

#define CZTEST_MOCK_DISPATCH_(Q, ret, name, n, ...) \
  CZTEST_MOCK_PICK_(Q, n)(Q, ret, name, __VA_ARGS__)
#define CZTEST_MOCK_PICK_(Q, n) CZTEST_MOCK_##n

#define CZTEST_MOCK_DISPATCH_NOARG_(Q, ret, name) CZTEST_MOCK_0(Q, ret, name)

/// The mocked interface needs a virtual destructor: deleting through a
/// pointer to the interface is undefined behaviour otherwise.
///
///   class MockStorage : public IStorage, public cztest::MockBase {
///    public:
///     CZTEST_MOCK_INTERFACE(IStorage);
///     CZTEST_MOCK_METHOD(int, Get, int);
///   };
#define CZTEST_MOCK_INTERFACE(Iface)                                             \
  static_assert(std::has_virtual_destructor_v<Iface>,                            \
                 "The mocked interface has no virtual destructor. Add "          \
                 "'virtual ~Iface() = default;' to the interface: deleting "       \
                 "through a pointer to the interface is undefined behaviour.")

/// The mock has to implement every method of the interface.  Place the check
/// after the class:
///
///   class MockStorage : public IStorage, public cztest::MockBase { ... };
///   CZTEST_MOCK_COMPLETE(MockStorage);
#define CZTEST_MOCK_COMPLETE(Mock)                                                    \
  static_assert(!std::is_abstract_v<Mock>,                                            \
                 "The mock is still abstract: one of the methods of the interface "   \
                 "has no CZTEST_MOCK_METHOD.")
#define CZTEST_MOCK_0(Q, ret, name)                                                  \
  ret name() Q override {                                                           \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret>(*this, #name, ::std::source_location::current());                           \
  }                                                                                 \
  auto gmock_##name() Q {                                                            \
    return ::cztest::detail::MakeSpec<ret>(*this, #name);                             \
  }

#define CZTEST_MOCK_1(Q, ret, name, t1)                                                            \
  ret name(t1 a1) Q override {                                                                    \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret, t1>(*this, #name, ::std::source_location::current(), a1);                                 \
  }                                                                                               \
  template <class M1>                                                                             \
  auto gmock_##name(M1&& m1) Q {                                                                   \
    return ::cztest::detail::MakeSpec<ret, t1>(                                                    \
        *this, #name, ::cztest::detail::AsMatcher<std::decay_t<t1>>(std::forward<M1>(m1)));          \
  }

#define CZTEST_MOCK_2(Q, ret, name, t1, t2)                                                        \
  ret name(t1 a1, t2 a2) Q override {                                                            \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret, t1, t2>(*this, #name, ::std::source_location::current(), a1, a2);                         \
  }                                                                                               \
  template <class M1, class M2>                                                                   \
  auto gmock_##name(M1&& m1, M2&& m2) Q {                                                         \
    return ::cztest::detail::MakeSpec<ret, t1, t2>(                                               \
        *this, #name, ::cztest::detail::AsMatcher<std::decay_t<t1>>(std::forward<M1>(m1)),           \
        ::cztest::detail::AsMatcher<std::decay_t<t2>>(std::forward<M2>(m2)));                       \
  }

#define CZTEST_MOCK_3(Q, ret, name, t1, t2, t3)                                                    \
  ret name(t1 a1, t2 a2, t3 a3) Q override {                                                     \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret, t1, t2, t3>(*this, #name, ::std::source_location::current(), a1, a2, a3);                  \
  }                                                                                               \
  template <class M1, class M2, class M3>                                                          \
  auto gmock_##name(M1&& m1, M2&& m2, M3&& m3) Q {                                                \
    return ::cztest::detail::MakeSpec<ret, t1, t2, t3>(                                          \
        *this, #name, ::cztest::detail::AsMatcher<std::decay_t<t1>>(std::forward<M1>(m1)),           \
        ::cztest::detail::AsMatcher<std::decay_t<t2>>(std::forward<M2>(m2)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t3>>(std::forward<M3>(m3)));                        \
  }

#define CZTEST_MOCK_4(Q, ret, name, t1, t2, t3, t4)                                                \
  ret name(t1 a1, t2 a2, t3 a3, t4 a4) Q override {                                              \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret, t1, t2, t3, t4>(*this, #name, ::std::source_location::current(), a1, a2, a3, a4);          \
  }                                                                                               \
  template <class M1, class M2, class M3, class M4>                                               \
  auto gmock_##name(M1&& m1, M2&& m2, M3&& m3, M4&& m4) Q {                                       \
    return ::cztest::detail::MakeSpec<ret, t1, t2, t3, t4>(                                      \
        *this, #name, ::cztest::detail::AsMatcher<std::decay_t<t1>>(std::forward<M1>(m1)),           \
        ::cztest::detail::AsMatcher<std::decay_t<t2>>(std::forward<M2>(m2)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t3>>(std::forward<M3>(m3)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t4>>(std::forward<M4>(m4)));                        \
  }

#define CZTEST_MOCK_5(Q, ret, name, t1, t2, t3, t4, t5)                                            \
  ret name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5) Q override {                                       \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret, t1, t2, t3, t4, t5>(*this, #name, ::std::source_location::current(), a1, a2, a3, a4, a5);  \
  }                                                                                               \
  template <class M1, class M2, class M3, class M4, class M5>                                      \
  auto gmock_##name(M1&& m1, M2&& m2, M3&& m3, M4&& m4, M5&& m5) Q {                              \
    return ::cztest::detail::MakeSpec<ret, t1, t2, t3, t4, t5>(                                   \
        *this, #name, ::cztest::detail::AsMatcher<std::decay_t<t1>>(std::forward<M1>(m1)),           \
        ::cztest::detail::AsMatcher<std::decay_t<t2>>(std::forward<M2>(m2)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t3>>(std::forward<M3>(m3)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t4>>(std::forward<M4>(m4)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t5>>(std::forward<M5>(m5)));                        \
  }

#define CZTEST_MOCK_6(Q, ret, name, t1, t2, t3, t4, t5, t6)                                        \
  ret name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6) Q override {                                 \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret, t1, t2, t3, t4, t5, t6>(*this, #name, ::std::source_location::current(), a1, a2, a3, a4,  \
                                                                     a5, a6);                       \
  }                                                                                               \
  template <class M1, class M2, class M3, class M4, class M5, class M6>                            \
  auto gmock_##name(M1&& m1, M2&& m2, M3&& m3, M4&& m4, M5&& m5, M6&& m6) Q {                      \
    return ::cztest::detail::MakeSpec<ret, t1, t2, t3, t4, t5, t6>(                               \
        *this, #name, ::cztest::detail::AsMatcher<std::decay_t<t1>>(std::forward<M1>(m1)),           \
        ::cztest::detail::AsMatcher<std::decay_t<t2>>(std::forward<M2>(m2)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t3>>(std::forward<M3>(m3)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t4>>(std::forward<M4>(m4)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t5>>(std::forward<M5>(m5)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t6>>(std::forward<M6>(m6)));                        \
  }

#define CZTEST_MOCK_7(Q, ret, name, t1, t2, t3, t4, t5, t6, t7)                                    \
  ret name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6, t7 a7) Q override {                         \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret, t1, t2, t3, t4, t5, t6, t7>(                         \
        *this, #name, a1, a2, a3, a4, a5, a6, a7);                                               \
  }                                                                                               \
  template <class M1, class M2, class M3, class M4, class M5, class M6, class M7>                   \
  auto gmock_##name(M1&& m1, M2&& m2, M3&& m3, M4&& m4, M5&& m5, M6&& m6, M7&& m7) Q {            \
    return ::cztest::detail::MakeSpec<ret, t1, t2, t3, t4, t5, t6, t7>(                           \
        *this, #name, ::cztest::detail::AsMatcher<std::decay_t<t1>>(std::forward<M1>(m1)),           \
        ::cztest::detail::AsMatcher<std::decay_t<t2>>(std::forward<M2>(m2)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t3>>(std::forward<M3>(m3)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t4>>(std::forward<M4>(m4)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t5>>(std::forward<M5>(m5)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t6>>(std::forward<M6>(m6)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t7>>(std::forward<M7>(m7)));                        \
  }

#define CZTEST_MOCK_8(Q, ret, name, t1, t2, t3, t4, t5, t6, t7, t8)                                \
  ret name(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5, t6 a6, t7 a7, t8 a8) Q override {                   \
    static_assert(std::is_base_of_v<::cztest::MockBase, std::remove_cvref_t<decltype(*this)>>, \
        "A mock class has to inherit cztest::MockBase: " \
        "class MockX : public IX, public cztest::MockBase { ... };");           \
    return ::cztest::detail::MockInvoke<ret, t1, t2, t3, t4, t5, t6, t7, t8>(                       \
        *this, #name, a1, a2, a3, a4, a5, a6, a7, a8);                                           \
  }                                                                                               \
  template <class M1, class M2, class M3, class M4, class M5, class M6, class M7, class M8>         \
  auto gmock_##name(M1&& m1, M2&& m2, M3&& m3, M4&& m4, M5&& m5, M6&& m6, M7&& m7, M8&& m8) Q {      \
    return ::cztest::detail::MakeSpec<ret, t1, t2, t3, t4, t5, t6, t7, t8>(                        \
        *this, #name, ::cztest::detail::AsMatcher<std::decay_t<t1>>(std::forward<M1>(m1)),           \
        ::cztest::detail::AsMatcher<std::decay_t<t2>>(std::forward<M2>(m2)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t3>>(std::forward<M3>(m3)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t4>>(std::forward<M4>(m4)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t5>>(std::forward<M5>(m5)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t6>>(std::forward<M6>(m6)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t7>>(std::forward<M7>(m7)),                         \
        ::cztest::detail::AsMatcher<std::decay_t<t8>>(std::forward<M8>(m8)));                        \
  }

// Declares a mocked function.  The arguments are the parameter types of the
// mocked function; they must match the interface exactly.
//
//   CZTEST_MOCK_METHOD(int, Get, int);
//   CZTEST_MOCK_METHOD(void, Put, int, const std::string&);
//
// Functions without parameters use the dedicated macro so that no empty
// variadic argument is needed:
//
//   CZTEST_MOCK_NOARG_METHOD(void, Reset);
#define CZTEST_MOCK_METHOD(ret, name, ...) \
  CZTEST_MOCK_DISPATCH_(, ret, name, CZTEST_NARG(__VA_ARGS__), __VA_ARGS__)

// Same as CZTEST_MOCK_METHOD but for const member functions.
#define CZTEST_MOCK_CONST_METHOD(ret, name, ...) \
  CZTEST_MOCK_DISPATCH_(const, ret, name, CZTEST_NARG(__VA_ARGS__), __VA_ARGS__)

// Mocked function without parameters.
#define CZTEST_MOCK_NOARG_METHOD(ret, name) CZTEST_MOCK_DISPATCH_NOARG_(, ret, name)
#define CZTEST_MOCK_NOARG_CONST_METHOD(ret, name) CZTEST_MOCK_DISPATCH_NOARG_(const, ret, name)

// Sets an expectation on a mocked function.
//
//   EXPECT_CALL(mock, Get(42)).Times(1).WillOnce(Return(7));
//   EXPECT_CALL(mock, Log(HasSubstr("error"))).Times(AnyNumber());
#define EXPECT_CALL(obj, call) \
  (::cztest::detail::MakeSite(::std::source_location::current()), (obj).gmock_##call)

}  // namespace cztest

#endif  // CZTEST_MOCK_HPP
