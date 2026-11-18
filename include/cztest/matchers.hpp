// cztest - argument matchers used by the mock framework.
//
// License: Apache-2.0
#ifndef CZTEST_MATCHERS_HPP
#define CZTEST_MATCHERS_HPP

#include <concepts>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cztest/core.hpp"

namespace cztest {

/// A type that can hold a string; the string matchers work on it.
template <class T>
concept StringLike =
    std::is_constructible_v<std::string, T> || std::is_convertible_v<T, std::string_view>;

/// A type that can be null; the null matchers work on it.
template <class T>
concept PointerLike = std::is_pointer_v<T>;

namespace detail {

// Common base of every matcher so that generic code can detect them.
class MatcherBase {
 public:
  virtual ~MatcherBase() = default;
  virtual std::string Describe() const = 0;
};

/// A matcher decides whether a value of type V is accepted.
///
/// A user can write a matcher of their own: it only needs a Matches member,
/// it does not have to derive from MatcherBase.
template <class M, class V>
concept MatcherFor = requires(const M& matcher, const V& value) {
  { matcher.Matches(value) } -> std::convertible_to<bool>;
};

// A string view that owns its data when the source type cannot be viewed
// directly (e.g. a std::string argument matched against a std::string_view).
class StringViewHolder {
 public:
  StringViewHolder(std::string_view view) : view_(view) {}
  StringViewHolder(std::string text) : owned_(std::move(text)), view_(owned_) {}

  operator std::string_view() const { return view_; }

 private:
  std::string owned_;
  std::string_view view_;
};

template <class T>
StringViewHolder AsStringView(const T& value) {
  if constexpr (std::is_convertible_v<T, std::string_view>) {
    return StringViewHolder(std::string_view(value));
  } else if constexpr (std::is_constructible_v<std::string, T>) {
    return StringViewHolder(std::string(value));
  } else {
    static_assert(std::is_constructible_v<std::string, T>,
                  "This matcher requires a string-like argument type.");
    return StringViewHolder(std::string_view());
  }
}

// ---------------------------------------------------------------------------
// Comparisons.
//
// The three way comparison is used when the type has one, so that a type that
// only defines <=> works as well; the operators are the fallback.
// ---------------------------------------------------------------------------
template <class A, class B>
constexpr bool Less(const A& a, const B& b) {
  if constexpr (requires { a <=> b; }) {
    return (a <=> b) < 0;
  } else {
    return a < b;
  }
}

template <class A, class B>
constexpr bool LessEqual(const A& a, const B& b) {
  if constexpr (requires { a <=> b; }) {
    return (a <=> b) <= 0;
  } else {
    return a <= b;
  }
}

template <class A, class B>
constexpr bool Greater(const A& a, const B& b) {
  if constexpr (requires { a <=> b; }) {
    return (a <=> b) > 0;
  } else {
    return a > b;
  }
}

template <class A, class B>
constexpr bool GreaterEqual(const A& a, const B& b) {
  if constexpr (requires { a <=> b; }) {
    return (a <=> b) >= 0;
  } else {
    return a >= b;
  }
}

/// The description of a matcher, when it has one.
template <class M>
std::string DescribeMatcher(const M& matcher) {
  if constexpr (requires { matcher.Describe(); }) {
    return matcher.Describe();
  } else {
    return "a matcher";
  }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Matcher<T>: matches an argument of type T.
//
// The matcher keeps either a comparison operator plus a reference value, or a
// custom predicate.  The comparison of a value matcher is bound at compile
// time in the factory that creates it, so a matcher only instantiates the
// operators it uses: Any() needs none, Eq() needs ==, Lt() needs < (or <=>).
// The value is held in an optional, so T need not be default constructible.
// Matching an argument of a different (but compatible) type is supported
// through the templated Matches() overload.
// ---------------------------------------------------------------------------
template <class T>
class Matcher : public detail::MatcherBase {
 public:
  using ValueType = T;

  enum class Op { kAny, kEq, kNe, kLt, kLe, kGt, kGe };

  Matcher() = default;

  // Plain values are compared with ==.
  template <class V, class = std::enable_if_t<!std::is_base_of_v<detail::MatcherBase, std::decay_t<V>>>>
  Matcher(V&& value)
      : op_(Op::kEq),
        value_(static_cast<T>(std::forward<V>(value))),
        compare_(&CompareWith<Op::kEq>),
        desc_("is equal to " + cztest::detail::PrintValue(*value_)) {}

  // Conversion from a matcher of a different value type.
  template <class U,
            class = std::enable_if_t<!std::is_same_v<U, T> && std::is_constructible_v<T, U>>>
  Matcher(const Matcher<U>& other) : op_(static_cast<Op>(other.op_)), desc_(other.desc_) {
    if (other.value_.has_value()) {
      value_ = static_cast<T>(*other.value_);
      compare_ = ComparisonFor(op_);
    }
    if (other.has_predicate_) {
      has_predicate_ = true;
      if constexpr (std::is_constructible_v<U, T>) {
        // The predicate of the source matcher can be applied to values of the
        // new type.
        predicate_ = [other](const T& value) { return other.predicate_(static_cast<const U&>(value)); };
      } else {
        // The predicate cannot be applied to the new value type; the matcher
        // keeps its description but never matches.
        predicate_ = [](const T&) { return false; };
        desc_ += " (the matcher predicate does not support this argument type)";
      }
    }
  }

  // --- factories ---------------------------------------------------------
  static Matcher<T> Any() {
    Matcher<T> matcher;
    matcher.op_ = Op::kAny;
    matcher.desc_ = "is anything";
    return matcher;
  }

  static Matcher<T> Equal(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kEq;
    matcher.value_ = value;
    matcher.compare_ = &CompareWith<Op::kEq>;
    matcher.desc_ = "is equal to " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> NotEqual(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kNe;
    matcher.value_ = value;
    matcher.compare_ = &CompareWith<Op::kNe>;
    matcher.desc_ = "is not equal to " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> Less(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kLt;
    matcher.value_ = value;
    matcher.compare_ = &CompareWith<Op::kLt>;
    matcher.desc_ = "is less than " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> LessEqual(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kLe;
    matcher.value_ = value;
    matcher.compare_ = &CompareWith<Op::kLe>;
    matcher.desc_ = "is at most " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> Greater(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kGt;
    matcher.value_ = value;
    matcher.compare_ = &CompareWith<Op::kGt>;
    matcher.desc_ = "is greater than " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> GreaterEqual(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kGe;
    matcher.value_ = value;
    matcher.compare_ = &CompareWith<Op::kGe>;
    matcher.desc_ = "is at least " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> Custom(std::function<bool(const T&)> predicate, std::string description) {
    Matcher<T> matcher;
    matcher.has_predicate_ = true;
    matcher.predicate_ = std::move(predicate);
    matcher.desc_ = std::move(description);
    return matcher;
  }

  static Matcher<T> StringEqual(const std::string& text) {
    return Custom(
        [text](const T& value) {
          return std::string_view(detail::AsStringView(value)) == std::string_view(text);
        },
        "is equal to \"" + text + "\"");
  }

  static Matcher<T> HasSubstring(const std::string& text) {
    return Custom(
        [text](const T& value) {
          return std::string_view(detail::AsStringView(value)).find(text) != std::string_view::npos;
        },
        "has substring \"" + text + "\"");
  }

  static Matcher<T> StartsWith(const std::string& text) {
    return Custom(
        [text](const T& value) {
          return std::string_view(detail::AsStringView(value)).rfind(text, 0) == 0;
        },
        "starts with \"" + text + "\"");
  }

  static Matcher<T> EndsWith(const std::string& text) {
    return Custom(
        [text](const T& value) {
          const auto view = std::string_view(detail::AsStringView(value));
          return view.size() >= text.size() &&
                 view.compare(view.size() - text.size(), text.size(), text) == 0;
        },
        "ends with \"" + text + "\"");
  }

  static Matcher<T> IsNull() {
    return Custom([](const T& value) { return value == nullptr; }, "is null");
  }

  static Matcher<T> NotNull() {
    return Custom([](const T& value) { return value != nullptr; }, "is not null");
  }

  static Matcher<T> Negate(const Matcher<T>& inner) {
    return Custom([inner](const T& value) { return !inner.Matches(value); },
                  "not (" + inner.Describe() + ")");
  }

  // --- matching ----------------------------------------------------------
  template <class U>
  bool Matches(const U& value) const {
    // A custom predicate comes first: a matcher built with Custom() has no
    // comparison operator, but it does have a predicate.
    if (has_predicate_) {
      if constexpr (std::is_same_v<std::decay_t<U>, T>) {
        return predicate_(value);
      } else if constexpr (std::is_constructible_v<T, U>) {
        return predicate_(T(value));
      } else {
        static_assert(std::is_constructible_v<T, U>,
                      "The argument type is not compatible with this matcher.");
        return false;
      }
    }

    if (op_ == Op::kAny) return true;

    if constexpr (std::is_same_v<std::decay_t<U>, T>) {
      return compare_(value, *value_);
    } else if constexpr (std::is_constructible_v<T, U>) {
      return compare_(T(value), *value_);
    } else if constexpr (std::is_constructible_v<U, T>) {
      // Compare in the argument type when it can hold the reference value
      // (the original fallback).  The compatibility static_asserts above
      // already reject incompatible types, so this is rarely reached.
      const U expected = static_cast<U>(*value_);
      switch (op_) {
        case Op::kEq: return value == expected;
        case Op::kNe: return !(value == expected);
        case Op::kLt: return detail::Less(value, expected);
        case Op::kLe: return detail::LessEqual(value, expected);
        case Op::kGt: return detail::Greater(value, expected);
        case Op::kGe: return detail::GreaterEqual(value, expected);
        case Op::kAny: break;
      }
      return true;
    } else {
      static_assert(std::is_constructible_v<U, T>,
                    "The argument type is not compatible with this matcher.");
      return false;
    }
  }

  std::string Describe() const override { return desc_; }

 private:
  // The comparison of one op, instantiated only for the op a factory binds.
  template <Op O>
  static bool CompareWith(const T& actual, const T& expected) {
    if constexpr (O == Op::kEq) return actual == expected;
    else if constexpr (O == Op::kNe) return !(actual == expected);
    else if constexpr (O == Op::kLt) return detail::Less(actual, expected);
    else if constexpr (O == Op::kLe) return detail::LessEqual(actual, expected);
    else if constexpr (O == Op::kGt) return detail::Greater(actual, expected);
    else if constexpr (O == Op::kGe) return detail::GreaterEqual(actual, expected);
    else return true;
  }

  // Runtime dispatch, used only when a matcher is converted to another value
  // type and the comparison has to be rebuilt for the new type.
  static bool (*ComparisonFor(Op op))(const T&, const T&) {
    switch (op) {
      case Op::kEq: return &CompareWith<Op::kEq>;
      case Op::kNe: return &CompareWith<Op::kNe>;
      case Op::kLt: return &CompareWith<Op::kLt>;
      case Op::kLe: return &CompareWith<Op::kLe>;
      case Op::kGt: return &CompareWith<Op::kGt>;
      case Op::kGe: return &CompareWith<Op::kGe>;
      case Op::kAny: return nullptr;
    }
    return nullptr;
  }

  template <class>
  friend class Matcher;

  Op op_{Op::kAny};
  std::optional<T> value_{};
  bool has_predicate_{false};
  std::function<bool(const T&)> predicate_;
  bool (*compare_)(const T&, const T&){nullptr};
  std::string desc_{"is anything"};
};

// ---------------------------------------------------------------------------
// Polymorphic matchers: convertible to a Matcher<T> of any suitable T.
// ---------------------------------------------------------------------------
class AnyMatcher : public detail::MatcherBase {
 public:
  template <class T>
  operator Matcher<T>() const {
    return Matcher<T>::Any();
  }

  template <class U>
  bool Matches(const U&) const {
    return true;
  }

  std::string Describe() const override { return "is anything"; }
};

class StringMatcher : public detail::MatcherBase {
 public:
  enum class Kind { kEqual, kSubstring, kStartsWith, kEndsWith };

  StringMatcher(Kind kind, std::string text) : kind_(kind), text_(std::move(text)) {}

  template <class T>
    requires StringLike<T>
  operator Matcher<T>() const {
    switch (kind_) {
      case Kind::kEqual: return Matcher<T>::StringEqual(text_);
      case Kind::kSubstring: return Matcher<T>::HasSubstring(text_);
      case Kind::kStartsWith: return Matcher<T>::StartsWith(text_);
      case Kind::kEndsWith: return Matcher<T>::EndsWith(text_);
    }
    return Matcher<T>::Any();
  }

  template <class U>
  bool Matches(const U& value) const {
    return static_cast<Matcher<std::string>>(*this).Matches(value);
  }

  std::string Describe() const override {
    switch (kind_) {
      case Kind::kEqual: return "is equal to \"" + text_ + "\"";
      case Kind::kSubstring: return "has substring \"" + text_ + "\"";
      case Kind::kStartsWith: return "starts with \"" + text_ + "\"";
      case Kind::kEndsWith: return "ends with \"" + text_ + "\"";
    }
    return "matches";
  }

 private:
  Kind kind_;
  std::string text_;
};

class NullMatcher : public detail::MatcherBase {
 public:
  explicit NullMatcher(bool expect_null) : expect_null_(expect_null) {}

  template <class T>
    requires PointerLike<T>
  operator Matcher<T>() const {
    return expect_null_ ? Matcher<T>::IsNull() : Matcher<T>::NotNull();
  }

  template <class U>
  bool Matches(const U& value) const {
    return expect_null_ ? (value == nullptr) : !(value == nullptr);
  }

  std::string Describe() const override { return expect_null_ ? "is null" : "is not null"; }

 private:
  bool expect_null_;
};

template <class F>
class TrulyMatcher : public detail::MatcherBase {
 public:
  explicit TrulyMatcher(F predicate) : predicate_(std::move(predicate)) {}

  template <class T>
  operator Matcher<T>() const {
    auto predicate = predicate_;
    return Matcher<T>::Custom(
        [predicate](const T& value) { return static_cast<bool>(predicate(value)); },
        "satisfies a custom predicate");
  }

  template <class U>
  bool Matches(const U& value) const {
    return static_cast<bool>(predicate_(value));
  }

  std::string Describe() const override { return "satisfies a custom predicate"; }

 private:
  F predicate_;
};

template <class M>
class NegatedMatcher : public detail::MatcherBase {
 public:
  explicit NegatedMatcher(M inner) : inner_(std::move(inner)) {}

  template <class T>
  operator Matcher<T>() const {
    return Matcher<T>::Negate(static_cast<Matcher<T>>(inner_));
  }

  template <class U>
  bool Matches(const U& value) const {
    return !inner_.Matches(value);
  }

  std::string Describe() const override { return "not (" + detail::DescribeMatcher(inner_) + ")"; }

 private:
  M inner_;
};

// ---------------------------------------------------------------------------
// Composition: AllOf, AnyOf and ElementsAre.
//
// The composed matchers are polymorphic: they convert to a Matcher<T> of the
// type they are used with, so they can be nested freely.
// ---------------------------------------------------------------------------
template <class... Ms>
class AllOfMatcher : public detail::MatcherBase {
 public:
  static_assert(sizeof...(Ms) > 0, "AllOf needs at least one matcher.");

  explicit AllOfMatcher(Ms... matchers) : matchers_(std::move(matchers)...) {}

  template <class T>
  operator Matcher<T>() const {
    auto check = [matchers = matchers_](const T& value) {
      return std::apply(
          [&value](const auto&... matcher) {
            return (static_cast<Matcher<T>>(matcher).Matches(value) && ...);
          },
          matchers);
    };
    return Matcher<T>::Custom(std::move(check), Describe());
  }

  template <class U>
  bool Matches(const U& value) const {
    return std::apply(
        [&value](const auto&... matcher) { return (matcher.Matches(value) && ...); }, matchers_);
  }

  std::string Describe() const override {
    std::string text = "matches all of (";
    bool first = true;
    std::apply(
        [&](const auto&... matcher) {
            ((text += (first ? (first = false, "") : ", "), text += detail::DescribeMatcher(matcher)), ...);
        },
        matchers_);
    return text + ")";
  }

 private:
  std::tuple<Ms...> matchers_;
};

template <class... Ms>
class AnyOfMatcher : public detail::MatcherBase {
 public:
  static_assert(sizeof...(Ms) > 0, "AnyOf needs at least one matcher.");

  explicit AnyOfMatcher(Ms... matchers) : matchers_(std::move(matchers)...) {}

  template <class T>
  operator Matcher<T>() const {
    auto check = [matchers = matchers_](const T& value) {
      return std::apply(
          [&value](const auto&... matcher) {
            return (static_cast<Matcher<T>>(matcher).Matches(value) || ...);
          },
          matchers);
    };
    return Matcher<T>::Custom(std::move(check), Describe());
  }

  template <class U>
  bool Matches(const U& value) const {
    return std::apply(
        [&value](const auto&... matcher) { return (matcher.Matches(value) || ...); }, matchers_);
  }

  std::string Describe() const override {
    std::string text = "matches any of (";
    bool first = true;
    std::apply(
        [&](const auto&... matcher) {
            ((text += (first ? (first = false, "") : ", "), text += detail::DescribeMatcher(matcher)), ...);
        },
        matchers_);
    return text + ")";
  }

 private:
  std::tuple<Ms...> matchers_;
};

template <class... Ms>
class ElementsAreMatcher : public detail::MatcherBase {
 public:
  static_assert(sizeof...(Ms) > 0, "ElementsAre needs at least one matcher.");

  explicit ElementsAreMatcher(Ms... matchers) : matchers_(std::move(matchers)...) {}

  template <class T>
  operator Matcher<T>() const {
    auto check = [matchers = matchers_](const T& container) {
      using Element = std::decay_t<decltype(*std::begin(container))>;
      auto it = std::begin(container);
      const auto end = std::end(container);
      bool ok = true;
      std::apply(
          [&](const auto&... matcher) {
            (
                [&] {
                  if (!ok || it == end) {
                    ok = false;
                    return;
                  }
                  if (!static_cast<Matcher<Element>>(matcher).Matches(*it)) {
                    ok = false;
                    return;
                  }
                  ++it;
                }(),
                ...);
          },
          matchers);
      return ok && it == end;
    };
    return Matcher<T>::Custom(std::move(check), Describe());
  }

  std::string Describe() const override {
    std::string text = "has the elements (";
    bool first = true;
    std::apply(
        [&](const auto&... matcher) {
            ((text += (first ? (first = false, "") : ", "), text += detail::DescribeMatcher(matcher)), ...);
        },
        matchers_);
    return text + ")";
  }

 private:
  std::tuple<Ms...> matchers_;
};

// ---------------------------------------------------------------------------
// Public matcher factories
// ---------------------------------------------------------------------------
template <class V>
Matcher<std::decay_t<V>> Eq(V&& value) {
  using T = std::decay_t<V>;
  return Matcher<T>::Equal(static_cast<T>(std::forward<V>(value)));
}

template <class V>
Matcher<std::decay_t<V>> Ne(V&& value) {
  using T = std::decay_t<V>;
  return Matcher<T>::NotEqual(static_cast<T>(std::forward<V>(value)));
}

template <class V>
Matcher<std::decay_t<V>> Lt(V&& value) {
  using T = std::decay_t<V>;
  return Matcher<T>::Less(static_cast<T>(std::forward<V>(value)));
}

template <class V>
Matcher<std::decay_t<V>> Le(V&& value) {
  using T = std::decay_t<V>;
  return Matcher<T>::LessEqual(static_cast<T>(std::forward<V>(value)));
}

template <class V>
Matcher<std::decay_t<V>> Gt(V&& value) {
  using T = std::decay_t<V>;
  return Matcher<T>::Greater(static_cast<T>(std::forward<V>(value)));
}

template <class V>
Matcher<std::decay_t<V>> Ge(V&& value) {
  using T = std::decay_t<V>;
  return Matcher<T>::GreaterEqual(static_cast<T>(std::forward<V>(value)));
}

inline StringMatcher StrEq(std::string text) { return StringMatcher(StringMatcher::Kind::kEqual, std::move(text)); }
inline StringMatcher HasSubstr(std::string text) {
  return StringMatcher(StringMatcher::Kind::kSubstring, std::move(text));
}
inline StringMatcher StartsWith(std::string text) {
  return StringMatcher(StringMatcher::Kind::kStartsWith, std::move(text));
}
inline StringMatcher EndsWith(std::string text) {
  return StringMatcher(StringMatcher::Kind::kEndsWith, std::move(text));
}

inline NullMatcher IsNull() { return NullMatcher(true); }
inline NullMatcher NotNull() { return NullMatcher(false); }

inline AnyMatcher Any() { return AnyMatcher{}; }

template <class F>
TrulyMatcher<std::decay_t<F>> Truly(F&& predicate) {
  return TrulyMatcher<std::decay_t<F>>(std::forward<F>(predicate));
}

template <class M>
auto Not(M&& matcher) {
  using D = std::decay_t<M>;
  return NegatedMatcher<D>(D(std::forward<M>(matcher)));
}

/// Matches when every one of the matchers matches.
template <class... Ms>
auto AllOf(Ms... matchers) {
  return AllOfMatcher<Ms...>(std::move(matchers)...);
}

/// Matches when at least one of the matchers matches.
template <class... Ms>
auto AnyOf(Ms... matchers) {
  return AnyOfMatcher<Ms...>(std::move(matchers)...);
}

/// Matches a container whose elements match the given matchers, one by one.
template <class... Ms>
auto ElementsAre(Ms... matchers) {
  return ElementsAreMatcher<Ms...>(std::move(matchers)...);
}

/// Matches a value against a matcher; the matcher is converted to the type of
/// the value first, so the composed matchers work as well.
template <class M, class V>
bool Matches(const M& matcher, const V& value) {
  using Value = std::decay_t<V>;
  return static_cast<Matcher<Value>>(matcher).Matches(value);
}

// ---------------------------------------------------------------------------
// EXPECT_THAT: the spelling GoogleMock uses.
// ---------------------------------------------------------------------------
template <class V, class M>
[[nodiscard]] AssertionResult ExpectThat(const V& value, const M& matcher) {
  using Value = std::decay_t<V>;
  static_assert(std::is_constructible_v<Matcher<Value>, const M&>,
                "This matcher cannot be used with this value type. cztest's matchers are Eq, "
                "Ne, Lt, Le, Gt, Ge, Any, Not, StrEq, HasSubstr, StartsWith, EndsWith, IsNull, "
                "NotNull, Truly, AllOf, AnyOf and ElementsAre; the matchers of GoogleMock are "
                "not supported.");
  const Matcher<Value> converted(matcher);
  if (converted.Matches(value)) return AssertionResult::Success();

  std::string msg = "Expected: the value to match ";
  msg += converted.Describe();
  msg += "\n  Actual: ";
  msg += detail::PrintValue(value);
  return AssertionResult::Failure(std::move(msg));
}

#define CZTEST_EXPECT_THAT(cztest_value_, cztest_matcher_) \
  CZTEST_EXPECT_(::cztest::ExpectThat((cztest_value_), (cztest_matcher_)))

#define CZTEST_ASSERT_THAT(cztest_value_, cztest_matcher_) \
  CZTEST_ASSERT_(::cztest::ExpectThat((cztest_value_), (cztest_matcher_)))

#define EXPECT_THAT(value, matcher) CZTEST_EXPECT_THAT((value), (matcher))
#define ASSERT_THAT(value, matcher) CZTEST_ASSERT_THAT((value), (matcher))

namespace detail {

// Converts a user supplied argument into a Matcher<T>.
//
// Three kinds of arguments are accepted:
//   - a matcher of the framework (it converts itself),
//   - a matcher of the user: any type with a Matches(value) member,
//   - a plain value, which is compared with ==.
template <class T, class M>
Matcher<T> AsMatcher(M&& value) {
  using D = std::decay_t<M>;
  if constexpr (std::is_base_of_v<MatcherBase, D>) {
    return static_cast<Matcher<T>>(std::forward<M>(value));
  } else if constexpr (cztest::detail::MatcherFor<D, T>) {
    D matcher(std::forward<M>(value));
    return Matcher<T>::Custom(
        [matcher = std::move(matcher)](const T& candidate) { return matcher.Matches(candidate); },
        "matches a user defined matcher");
  } else {
    static_assert(std::is_constructible_v<T, D>,
                  "This value cannot be used as an expectation for a parameter of this "
                  "type. Pass a value of the parameter type, or a matcher that accepts "
                  "it (cztest::Any() accepts every value).");
    return Matcher<T>::Equal(T(std::forward<M>(value)));
  }
}

}  // namespace detail
}  // namespace cztest

#endif  // CZTEST_MATCHERS_HPP
