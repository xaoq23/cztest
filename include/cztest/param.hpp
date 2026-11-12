// cztest - parameterised test support.
//
// A parameterised test is declared in two steps: TEST_P declares the test for
// a fixture that derives from cztest::TestWithParam<T>, and
// INSTANTIATE_TEST_SUITE_P expands it into one concrete test per value.
//
//   class AdderTest : public cztest::TestWithParam<struct Case> {
//    protected:
//     void SetUp() override { value_ = GetParam(); }
//     struct Case { int a, b, expected; };
//     Case value_;
//   };
//
//   TEST_P(AdderTest, AddsCorrectly) {
//     EXPECT_EQ(value_.a + value_.b, value_.expected);
//   }
//
//   INSTANTIATE_TEST_SUITE_P(Basic, AdderTest, cztest::Values(
//       Case{1, 2, 3}, Case{10, 20, 30}));
//
// The generated tests are named "Basic/AdderTest.AddsCorrectly/0" and can be
// selected with --filter.
//
// License: MIT
#ifndef CZTEST_PARAM_HPP
#define CZTEST_PARAM_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cztest/core.hpp"

namespace cztest {

// ---------------------------------------------------------------------------
// TestWithParam: base class of a parameterised fixture.
// ---------------------------------------------------------------------------
template <class T>
class TestWithParam : public Test {
 public:
  using ParamType = T;

  /// The value of the current test instance.
  const ParamType& GetParam() const { return param_; }

  /// Sets the value; called by the framework before SetUp().
  void SetParam(ParamType value) { param_ = std::move(value); }

 private:
  ParamType param_{};
};

// ---------------------------------------------------------------------------
// Parameter sources
// ---------------------------------------------------------------------------
template <class T>
class ValueList {
 public:
  explicit ValueList(std::vector<T> values) : values_(std::move(values)) {}

  const std::vector<T>& values() const { return values_; }

 private:
  std::vector<T> values_;
};

/// A list of values; every value becomes one test instance.
template <class T, class... Rest>
ValueList<T> Values(T first, Rest... rest) {
  static_assert((std::is_same_v<T, Rest> && ...),
                "all values of a parameter list must have the same type");
  std::vector<T> values;
  values.reserve(sizeof...(Rest) + 1);
  values.push_back(std::move(first));
  (values.push_back(static_cast<T>(std::move(rest))), ...);
  return ValueList<T>(std::move(values));
}

/// The half open range [begin, end).
template <class T>
ValueList<T> Range(T begin, T end, T step = T{1}) {
  std::vector<T> values;
  for (T value = begin; value < end; value += step) {
    values.push_back(value);
  }
  return ValueList<T>(std::move(values));
}

/// The two boolean values.
inline ValueList<bool> Bool() { return ValueList<bool>(std::vector<bool>{false, true}); }

namespace detail {

// ---------------------------------------------------------------------------
// A parameterised test declared by TEST_P.
// ---------------------------------------------------------------------------
struct ParamPattern {
  std::string fixture;              // the fixture name, matched by the instantiation
  std::string test;                 // the test name
  std::function<void(Test&)> body;  // runs the test body on a fixture instance
  std::source_location location;
};

class ParamRegistry {
 public:
  static ParamRegistry& Get() {
    static ParamRegistry registry;
    return registry;
  }

  void Add(ParamPattern pattern) { patterns_.push_back(std::move(pattern)); }
  const std::vector<ParamPattern>& patterns() const { return patterns_; }

 private:
  std::vector<ParamPattern> patterns_;
};

// ---------------------------------------------------------------------------
// A pending instantiation declared by INSTANTIATE_TEST_SUITE_P.
//
// The values are type erased; the factory knows the concrete parameter type
// and turns a value plus a pattern into the body of one concrete test.
//
// An instance registers itself, so the order of the declarations in the
// translation unit does not matter.
// ---------------------------------------------------------------------------
struct PendingInstantiation;

inline std::vector<std::shared_ptr<PendingInstantiation>>& PendingInstantiations() {
  static std::vector<std::shared_ptr<PendingInstantiation>> pending;
  return pending;
}

struct PendingInstantiation {
  using Factory =
      std::function<std::function<void()>(const ParamPattern&, const std::shared_ptr<void>&)>;

  std::string prefix;
  std::string fixture;
  std::vector<std::shared_ptr<void>> values;
  Factory factory;

  PendingInstantiation(std::string prefix, std::string fixture,
                       std::vector<std::shared_ptr<void>> values, Factory factory)
      : prefix(std::move(prefix)),
        fixture(std::move(fixture)),
        values(std::move(values)),
        factory(std::move(factory)) {
    PendingInstantiations().push_back(std::make_shared<PendingInstantiation>(*this));
  }
};

template <class T>
std::vector<std::shared_ptr<void>> MakeParamValues(const ValueList<T>& list) {
  std::vector<std::shared_ptr<void>> values;
  values.reserve(list.values().size());
  for (const T& value : list.values()) {
    values.push_back(std::make_shared<T>(value));
  }
  return values;
}

/// Expands the pending instantiations into concrete tests.
inline void ResolveParamInstantiations() {
  for (const auto& pending : PendingInstantiations()) {
    for (const auto& pattern : ParamRegistry::Get().patterns()) {
      if (pattern.fixture != pending->fixture) continue;
      for (std::size_t index = 0; index < pending->values.size(); ++index) {
        auto body = pending->factory(pattern, pending->values[index]);
        Registry::Get().Add(TestInfo{pending->prefix + "/" + pattern.fixture,
                                    pattern.test + "/" + std::to_string(index), std::move(body),
                                    pattern.location});
      }
    }
  }
  PendingInstantiations().clear();
}

/// Installs the resolver into the runner.
struct ParamResolverRegistrar {
  ParamResolverRegistrar() { ParamResolvers().push_back([] { ResolveParamInstantiations(); }); }
};

inline const ParamResolverRegistrar param_resolver_registrar{};

}  // namespace detail
}  // namespace cztest

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------
#define CZTEST_PARAM_FIXTURE_(fixture, name) \
  CZTEST_CONCAT(cztest_param_fixture_, CZTEST_CONCAT(fixture, CZTEST_CONCAT(_, name)))

/// Declares a parameterised test.
#define TEST_P(fixture, name)                                                              \
  class CZTEST_PARAM_FIXTURE_(fixture, name) : public fixture {                             \
   public:                                                                                  \
    void TestBody();                                                                        \
    static bool RegisterPattern();                                                          \
  };                                                                                        \
  bool CZTEST_PARAM_FIXTURE_(fixture, name)::RegisterPattern() {                              \
    ::cztest::detail::ParamRegistry::Get().Add(::cztest::detail::ParamPattern{              \
        #fixture, #name,                                                                     \
        [](::cztest::Test& test) {                                                           \
          static_cast<CZTEST_PARAM_FIXTURE_(fixture, name)*>(&test)->TestBody();              \
        },                                                                                   \
        ::std::source_location::current()});                                                 \
    return true;                                                                            \
  }                                                                                          \
  static const bool CZTEST_CONCAT(cztest_param_registered_, __LINE__) =                       \
      CZTEST_PARAM_FIXTURE_(fixture, name)::RegisterPattern();                                \
  void CZTEST_PARAM_FIXTURE_(fixture, name)::TestBody()

/// Expands a parameterised test into one test per value.
///
/// The generator is taken as a variadic argument on purpose: a value list
/// such as cztest::Values(Case{1, 2}, Case{3, 4}) contains commas inside
/// braces, and only a variadic parameter keeps them together.
#define INSTANTIATE_TEST_SUITE_P(prefix, fixture, ...)                                         \
  static const ::cztest::detail::PendingInstantiation CZTEST_CONCAT(cztest_param_instance_,    \
                                                                      __LINE__)(                   \
      #prefix, #fixture, ::cztest::detail::MakeParamValues(__VA_ARGS__),                        \
      [](const ::cztest::detail::ParamPattern& pattern,                                         \
         const std::shared_ptr<void>& raw) {                                                    \
        using ParamType = typename fixture::ParamType;                                          \
        const ParamType value = *static_cast<const ParamType*>(raw.get());                      \
        return std::function<void()>{[pattern, value]() {                                        \
          fixture instance;                                                                     \
          instance.SetParam(value);                                                             \
          static_cast<::cztest::Test&>(instance).SetUp();                                       \
          pattern.body(instance);                                                              \
          static_cast<::cztest::Test&>(instance).TearDown();                                    \
        }};                                                                                     \
      })

#endif  // CZTEST_PARAM_HPP
