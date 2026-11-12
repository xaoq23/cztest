// cztest - argument matchers used by the mock framework.
//
// License: MIT
#ifndef CZTEST_MATCHERS_HPP
#define CZTEST_MATCHERS_HPP

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "cztest/core.hpp"

namespace cztest {

namespace detail {

// Common base of every matcher so that generic code can detect them.
class MatcherBase {
 public:
  virtual ~MatcherBase() = default;
  virtual std::string Describe() const = 0;
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

}  // namespace detail

// ---------------------------------------------------------------------------
// Matcher<T>: matches an argument of type T.
//
// The matcher keeps either a comparison operator plus a reference value, or a
// custom predicate.  Matching an argument of a different (but compatible)
// type is supported through the templated Matches() overload.
// ---------------------------------------------------------------------------
template <class T>
class Matcher : public detail::MatcherBase {
 public:
  using ValueType = T;

  enum class Op { kAny, kEq, kNe, kLt, kLe, kGt, kGe };

  Matcher() = default;

  // Plain values are compared with ==.
  template <class V, class = std::enable_if_t<!std::is_base_of_v<detail::MatcherBase, std::decay_t<V>>>>
  Matcher(V&& value) : op_(Op::kEq), value_(static_cast<T>(std::forward<V>(value))), has_value_(true) {
    desc_ = "is equal to " + cztest::detail::PrintValue(value_);
  }

  // Conversion from a matcher of a different value type.
  template <class U,
            class = std::enable_if_t<!std::is_same_v<U, T> && std::is_constructible_v<T, U>>>
  Matcher(const Matcher<U>& other) : op_(static_cast<Op>(other.op_)), desc_(other.desc_) {
    if (other.has_value_) {
      value_ = static_cast<T>(other.value_);
      has_value_ = true;
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
    matcher.has_value_ = true;
    matcher.desc_ = "is equal to " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> NotEqual(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kNe;
    matcher.value_ = value;
    matcher.has_value_ = true;
    matcher.desc_ = "is not equal to " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> Less(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kLt;
    matcher.value_ = value;
    matcher.has_value_ = true;
    matcher.desc_ = "is less than " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> LessEqual(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kLe;
    matcher.value_ = value;
    matcher.has_value_ = true;
    matcher.desc_ = "is at most " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> Greater(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kGt;
    matcher.value_ = value;
    matcher.has_value_ = true;
    matcher.desc_ = "is greater than " + cztest::detail::PrintValue(value);
    return matcher;
  }

  static Matcher<T> GreaterEqual(const T& value) {
    Matcher<T> matcher;
    matcher.op_ = Op::kGe;
    matcher.value_ = value;
    matcher.has_value_ = true;
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
    if (op_ == Op::kAny) return true;

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

    if constexpr (std::is_same_v<std::decay_t<U>, T>) {
      return Compare(value, value_);
    } else if constexpr (std::is_constructible_v<T, U>) {
      return Compare(T(value), value_);
    } else if constexpr (std::is_constructible_v<U, T>) {
      return Compare(static_cast<U>(value_), value);
    } else {
      static_assert(std::is_constructible_v<U, T>,
                    "The argument type is not compatible with this matcher.");
      return false;
    }
  }

  std::string Describe() const override { return desc_; }

 private:
  template <class A, class B>
  bool Compare(const A& actual, const B& expected) const {
    switch (op_) {
      case Op::kEq: return actual == expected;
      case Op::kNe: return !(actual == expected);
      case Op::kLt: return actual < expected;
      case Op::kLe: return actual <= expected;
      case Op::kGt: return actual > expected;
      case Op::kGe: return actual >= expected;
      case Op::kAny: break;
    }
    return true;
  }

  template <class>
  friend class Matcher;

  Op op_{Op::kAny};
  T value_{};
  bool has_value_{false};
  bool has_predicate_{false};
  std::function<bool(const T&)> predicate_;
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

  std::string Describe() const override { return "not (" + inner_.Describe() + ")"; }

 private:
  M inner_;
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

namespace detail {

// Converts a user supplied argument into a Matcher<T>.
template <class T, class M>
Matcher<T> AsMatcher(M&& value) {
  using D = std::decay_t<M>;
  if constexpr (std::is_base_of_v<MatcherBase, D>) {
    return static_cast<Matcher<T>>(std::forward<M>(value));
  } else {
    static_assert(std::is_constructible_v<T, D>,
                  "The expected value is not compatible with the mocked parameter type.");
    return Matcher<T>::Equal(T(std::forward<M>(value)));
  }
}

}  // namespace detail
}  // namespace cztest

#endif  // CZTEST_MATCHERS_HPP
