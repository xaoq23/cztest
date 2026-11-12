# cztest

**A C++20-native, single-header unit and integration testing framework for modern C++.**
Importing it costs one `-I`. Mocking works at the level of a single function.

```cpp
#include <cztest/cztest.hpp>

TEST(Math, Addition) {
    EXPECT_EQ(1 + 1, 2);
}
```

- **Header only** — five headers, no dependencies, nothing to link.
- **Function level mocks** — expectations per method, argument matchers, call counts.
- **C++20 first** — `std::source_location`, concepts, `std::span`, `std::string_view`.
- **Its own runner** — filtering, listing, JUnit XML, colored output.
- **Made for [cizon2](https://github.com/cizon-dev/cizon)** — `-Iinclude` is added automatically.

Version 0.3.0 (`CZTEST_VERSION_MAJOR` / `MINOR` / `PATCH`).

> Chinese version: [README-zh.md](README-zh.md)

## At a glance

| | |
| --- | --- |
| Language level | C++20 (`std::source_location`, concepts) |
| Dependencies | none, only the standard library |
| Compiler baseline | GCC 11+, Clang 14+, MSVC 19.30+ (verified on GCC 16.2 and Clang 22) |
| Mocking | virtual methods, one expectation per method |
| Deliberately not | mocking free functions, global mock state, death tests |

### Compared with the usual suspects

| | gtest / gmock | Catch2 / doctest | cztest |
| --- | --- | --- | --- |
| Build | compile and link two libraries | header only | header only |
| Mocking | yes, including free functions | no | yes, virtual methods |
| Assertion names | `EXPECT_EQ` | `CHECK_EQ` | `EXPECT_EQ` |
| Failure location | file and line | file and line | file, line **and function** |
| Failing assertions | continue (`EXPECT`) or abort (`ASSERT`) | same | same |
| Compile time checks | some | some | mocks, matchers, actions, test names |
| A framework that says what to do | no | no | yes |

## Why another framework

Three reasons, and they are the reason for everything else in the code.

**1. The vocabulary is the one people already know.** `TEST`, `TEST_F`, `EXPECT_EQ`,
`EXPECT_CALL`, `Times`, `WillOnce`, `Return` — all of them are GoogleTest's names. Code that
moves over from gtest needs no new concepts, only a different include.

**2. The assertions are functions, not magic.** The macros are sugar over ordinary
functions:

```cpp
if (auto result = cztest::ExpectEqual(a, b); !result) {
    cztest::ReportFailure(std::source_location::current(), result.message);
}
```

They can be combined, they can be unit tested, and the comparison predicates are
`constexpr`, so a property can be checked at compile time.

**3. A single header rules out mocking free functions.** Intercepting a free
function needs link-time symbol replacement (`--wrap`, weak symbols, `LD_PRELOAD`, IAT
patching). That means a compiled component, a global registry, and a mechanism that does
not work the same on Windows. **Single header and free function mocking are mutually
exclusive** — cztest chose the single header, and mocks virtual methods instead.

## Quick start

A test file:

```cpp
// tests/test_math.cpp
#include <cztest/cztest.hpp>

TEST(Math, Addition) {
    EXPECT_EQ(1 + 1, 2);
}
```

An entry point (one per test binary):

```cpp
// tests/main.cpp
#include <cztest/cztest.hpp>

CZTEST_MAIN()
```

Build and run:

```console
$ c++ -std=c++20 -Iinclude tests/*.cpp -o tests
$ ./tests
[==========] Running 1 test(s).
[       OK ] Math.Addition
[==========] 1 test(s) ran. (0 ms total)
[  PASSED  ] 1 test(s).
```

On ELF and Mach-O the framework installs a weak `main`, so the entry point file is
optional there. On Windows (COFF) exactly one translation unit has to provide `main`:
`CZTEST_MAIN()`, or define `CZTEST_IMPLEMENT_MAIN` before the include.

## Layout

```
cztest/
├── include/cztest/          # the framework, all headers
│   ├── cztest.hpp           #   umbrella header and entry point
│   ├── core.hpp             #   registry, assertions, runner
│   ├── matchers.hpp         #   argument matchers
│   ├── mock.hpp             #   the mocking machinery
│   ├── param.hpp            #   parameterised tests
│   └── compat.hpp           #   teaching errors for the GoogleTest features that are missing
├── tests/                   # the framework's own tests
├── docs/
│   ├── tutorial.md           #   from the smallest test to integration tests
│   └── migration-gtest.md    #   what changes when you come from GoogleTest
└── cizon.toml
```

Copying `include/cztest` into a project is all it takes to use the framework.

## The API

### Assertions

| Macro | Meaning |
| --- | --- |
| `EXPECT_TRUE(x)` / `EXPECT_FALSE(x)` | boolean check |
| `EXPECT_EQ(a,b)` `NE` `LT` `LE` `GT` `GE` | comparison |
| `EXPECT_STREQ(a,b)` / `EXPECT_STRNE(a,b)` | C string comparison, `nullptr` safe |
| `EXPECT_NEAR(a,b,eps)` | floating point with an explicit tolerance |
| `EXPECT_DOUBLE_EQ(a,b)` / `EXPECT_FLOAT_EQ(a,b)` | floating point, four units in the last place |
| `EXPECT_THROW(stmt, Type)` / `NO_THROW` / `ANY_THROW` | exceptions |
| `EXPECT_THAT(value, matcher)` | match the value against a matcher |
| `EXPECT_OK(expr)` | a `std::expected` holds a value (C++23 and later) |
| `ADD_FAILURE()` / `FAIL()` / `SUCCEED()` | manual failure and success |
| `SCOPED_TRACE("...")` | add context to a failure |
| `SKIP("...")` | end the test without failing it |

Every `EXPECT_*` has an `ASSERT_*` twin. `EXPECT_*` records the failure and carries on,
`ASSERT_*` records it and ends the test right there.

```cpp
const auto result = Parse("a=1");
ASSERT_TRUE(result.ok);              // stop here, the rest cannot be used
EXPECT_EQ(result.fields.size(), 2);   // keep going, see more problems
```

Every assertion takes a message:

```cpp
EXPECT_EQ(result.size(), 3) << "expected three items, got " << result.size();
```

#### The assertions are ordinary functions

```cpp
if (auto result = cztest::ExpectEqual(a, b); !result) {
    cztest::ReportFailure(std::source_location::current(), result.message);
}
```

The comparison predicates are `constexpr`:

```cpp
static_assert(cztest::Equal(1 + 1, 2));
```

#### A failure knows where it happened

```
[  FAILED  ] Parser.ReadsTwoFields
C:\path\tests\test_parser.cpp:23: Failure
Expected: result.fields.size() == 2
  Actual: 1 vs 2
```

The location comes from `std::source_location`, so it carries the function as well when
the assertion sits in a function of your own:

```
C:\path\tests\test_modern.cpp:9 (int demo::Helper(int)): Failure
```

#### Traces add the context

```cpp
for (const auto& item : items) {
    SCOPED_TRACE("item: " << item.name);
    EXPECT_EQ(item.value, 0);
}
```

```
Expected: item.value == 0
  Actual: 3 vs 0
Traces:
  [2] index: 1
  [1] item: beta
```

### Fixtures

```cpp
class ConfigFixture : public cztest::Test {
 public:
    void SetUp() override { config_ = Load("config.toml"); }
    void TearDown() override { std::remove("config.toml"); }

    Config config_;
};

TEST_F(ConfigFixture, ReadsThePort) {
    EXPECT_EQ(config_.port, 8080);
}
```

`SetUp` runs before every test of the fixture, `TearDown` after it — also when the
test failed.

### Parameterised tests

```cpp
namespace {

struct Case {
    std::string input;
    int expected;
};

class TrimTest : public cztest::TestWithParam<Case> {
 protected:
    void SetUp() override { value_ = GetParam(); }
    Case value_;
};

}  // namespace

TEST_P(TrimTest, RemovesTheSpaces) {
    EXPECT_EQ(Trim(value_.input).size(), value_.expected);
}

INSTANTIATE_TEST_SUITE_P(Basic, TrimTest, cztest::Values(
    Case{"  a", 1},
    Case{"a  ", 1}));
```

The cases are named `Basic/TrimTest.RemovesTheSpaces/0`, `/1`, … and can be selected
individually. The parameter sources are `cztest::Values(...)`, `cztest::Range(begin, end)`
and `cztest::Bool()`.

### Mocks

**1. The interface**

```cpp
struct IHttp {
    virtual ~IHttp() = default;
    virtual bool Get(const std::string& url, std::string& body) = 0;
};
```

**2. The mock**

```cpp
class MockHttp : public IHttp, public cztest::MockBase {
 public:
    CZTEST_MOCK_INTERFACE(IHttp);
    CZTEST_MOCK_METHOD(bool, Get, const std::string&, std::string&);
};
CZTEST_MOCK_COMPLETE(MockHttp);
```

`CZTEST_MOCK_METHOD(return type, name, parameter types...)` takes the parameter types
without parentheses. Use `CZTEST_MOCK_CONST_METHOD` for a const member function and
`CZTEST_MOCK_NOARG_METHOD` / `CZTEST_MOCK_NOARG_CONST_METHOD` for a function without
parameters.

**3. Set the expectation, inject, call**

```cpp
TEST(Http, DownloadsTheBody) {
    MockHttp http;
    EXPECT_CALL(http, Get("https://x/y", cztest::Any()))
        .Times(1)
        .WillOnce(cztest::Invoke([](const std::string&, std::string& out) {
            out = "hello";
            return true;
        }));

    Client client(http);
    EXPECT_EQ(client.Download("https://x/y"), "hello");
}
```

| Part of an expectation | Meaning |
| --- | --- |
| `.Times(n)` | how often the call is expected |
| `.Times(AtLeast(n))` / `AtMost(n)` / `AnyNumber()` / `Exactly(n)` | more flexible counts |
| `.Once()` | the same as `Times(1)` |
| `.WillOnce(action)` | the action of the **next** call; several of them form a sequence |
| `.WillRepeatedly(action)` | the action of every following call |
| `.DoAndReturn(lambda)` | the same as `WillRepeatedly`, better name |

| Action | Meaning |
| --- | --- |
| `cztest::Return(v)` | a fixed value |
| `cztest::ReturnRef(v)` | a reference to an existing object |
| `cztest::Invoke(f)` | any callable |

An action may capture a **move only** object; cztest ships its own move only callable
instead of relying on `std::move_only_function`, which not every library provides.

A mock is strict: a call that matches no expectation fails immediately and prints the
arguments it received together with the expectations that were set, and an expectation
whose count is not met is reported when the test ends. A call that goes over the limit
of an expectation is reported at the line of that expectation.

#### Matchers

`Eq` `Ne` `Lt` `Le` `Gt` `Ge` `Any` `Not` `StrEq` `HasSubstr` `StartsWith` `EndsWith`
`IsNull` `NotNull` `Truly`. A plain value means `Eq`.

Matchers compose, and one of your own only needs a `Matches` member:

```cpp
EXPECT_THAT(value, cztest::AllOf(cztest::Ge(1), cztest::Lt(100)));   // all of them
EXPECT_THAT(value, cztest::AnyOf(cztest::Eq(1), cztest::Eq(2)));    // any of them
EXPECT_THAT(value, cztest::Not(cztest::Eq(3)));                     // negated
EXPECT_THAT(items, cztest::ElementsAre(cztest::Eq(1), cztest::Ge(2)));  // element by element

struct IsEven {
    bool Matches(int value) const { return value % 2 == 0; }
};
EXPECT_THAT(value, IsEven{});
```

The comparison matchers use the three way comparison when the type has one, so a type
that only defines `operator<=>` works as well.

#### Two compile time guards

```cpp
class MockHttp : public IHttp, public cztest::MockBase {
 public:
    CZTEST_MOCK_INTERFACE(IHttp);   // the interface needs a virtual destructor
    CZTEST_MOCK_METHOD(bool, Get, const std::string&, std::string&);
};
CZTEST_MOCK_COMPLETE(MockHttp);     // the mock implements every method
```

A method whose signature does not match the interface is reported by `override`, at the
line of the macro.

#### Thread safety

**A mock is not thread safe**: the call counter and the expectation registry are plain
members without a lock. Use one mock per thread, or guard the calls yourself.

## The runner

```
--filter=<pattern>   run only the matching tests ('*' and '?' wildcards)
--list               list the registered tests and exit
--repeat=<n>         run every test n times
--quiet / -q         only print failures and the summary
--verbose / -v       also print a "[ RUN ]" line before every test
--color=on|off       force colored output on or off
--junit=<file>       write a JUnit XML report
--help               show the help
```

The names are `Suite.Name`:

```bash
./tests --filter=Mock.*
./tests --filter="Basic/TrimTest.RemovesTheSpaces/1"
```

A filter that matches nothing runs no test at all, and that is easy to miss in the
log of a CI run, so the runner says so:

```
[==========] No test matched the filter 'Mock'
Check the names with --list.
```

With `--repeat` every line says which run it belongs to, so a test that only fails
sometimes is easy to spot:

```
[       OK ] Flaky.FailsOnTheSecondRun (run 1 of 3)
[  FAILED  ] Flaky.FailsOnTheSecondRun (run 2 of 3)
```

One line per test, the details of a failure right below it:

```
[==========] Running 4 test(s).
[       OK ] Assertions.Comparison
[  FAILED  ] Mock.SequenceOfActions
C:\path\tests\test_mock.cpp:31: Failure
Expected: Get(1) == 2
  Actual: 1 vs 2
[  SKIPPED ] Skip.EndsTheTestWithoutFailing (not implemented yet)
[==========] 4 test(s) ran. (0 ms total)
[  PASSED  ] 3 test(s).
[  SKIPPED ] 1 test(s).
[  FAILED  ] 1 test(s) failed.
```

The exit code is 0 when everything passed or was skipped, and 1 otherwise.

The JUnit report holds the messages of the failures and marks the skipped tests, so
the dashboards of the CI show what went wrong:

```xml
<testcase classname="Mock" name="SequenceOfActions">
  <failure message="Expected: Get(1) == 2">C:\path\tests\test_mock.cpp:31: Expected: Get(1) == 2
  Actual: 1 vs 2</failure>
</testcase>
```

Two tests with the same name are a mistake, so the runner refuses to start and says
where the copies are:

```
[==========] Duplicate test names:
  Math.Addition
    C:\path\tests\test_a.cpp:10
    C:\path\tests\test_b.cpp:14
Every test needs its own name: rename the ones that repeat.
```

The compiler cannot catch this, because the tests live in different translation
units; the runner can, because it sees them all.

## With cizon2

```toml
# cizon.toml
[project]
name = "bintree"
edition = "c++20"
compile_commands = true

[compiler]
# strict expands to -Wall -Wextra -Wpedantic, werror turns warnings into errors
strict = true
werror = true

[[target]]
name = "bintree"
kind = "static_lib"
sources = ["src"]

[[target]]
name = "bintree_tests"
kind = "test"
sources = ["tests/unit"]
links = ["bintree"]

[[target]]
name = "bintree_integration_tests"
kind = "test"
sources = ["tests/integration"]
links = ["bintree"]
```

```bash
cizon2 build                        # only the normal targets
cizon2 test                         # builds and runs every kind = "test" target
cizon2 test bintree_tests           # one target
cizon2 test -- --filter=Mock.*      # arguments are passed to the test process
```

A `kind = "test"` target is ignored by `cizon2 build` and `cizon2 run`; only
`cizon2 test` builds and runs it. The test process is judged by its exit code, which is
why the framework does not need to be known to cizon2 at all.

### Using cztest as a dependency

cztest is header only, so a dependency needs no compilation:

```bash
cizon2 add https://github.com/you/cztest   # writes [dependencies] and cizon.lock
cizon2 sync                                 # restores the checkouts, without building
```

cizon2 adds `-Ideps/cztest/include` and `-Ideps/cztest` on its own, so the project
needs no `-I` flag and no `system_links` — compare gtest, which needs
`-lgtest_main -lgtest`.

### Unit tests and integration tests

Keep the two apart: they belong in separate targets, and they answer different
questions.

| | unit test | integration test |
| --- | --- | --- |
| dependencies | all fake (mocks) | real |
| speed | milliseconds | seconds |
| a failure means | the logic is wrong | two parts do not fit together |

An integration test that needs a file, a database or a socket is still a test: it
belongs in `tests/integration/`, in its own target, and it has to be isolated
(a temporary directory, an in-memory database, a port the system picks).

## Migrating from GoogleTest

The assertion layer needs no changes at all. The mechanical part:

```bash
# the namespace
sed -i 's/::testing::/cztest::/g' tests/*.cpp

# the fixture base class
sed -i 's/public ::testing::Test/public cztest::Test/' tests/*.cpp

# the mock method declaration:
#   MOCK_METHOD(ret, Name, (types...), (override));
# → CZTEST_MOCK_METHOD(ret, Name, types...)
sed -i 's/MOCK_METHOD(\(.*\), (\(.*\)), (override));/CZTEST_MOCK_METHOD(\1, \2);/' tests/*.cpp
```

Two things cannot be replaced by a script: `::testing::_` becomes `cztest::Any()`, and
`ON_CALL(...).WillByDefault(...)` becomes
`EXPECT_CALL(...).WillRepeatedly(...)`.

For the GoogleTest features cztest does not have, the compiler explains what to use
instead:

```
error: static assertion failed: cztest has no ON_CALL. Use EXPECT_CALL(mock, Method(args))
       .WillRepeatedly(action) for the default behaviour, ...
```

The complete list, with the counterpart of every matcher, is in
[docs/migration-gtest.md](docs/migration-gtest.md).

## Design notes

The principles behind the code, and what they cost.

**Single header, or free function mocking — not both.** Intercepting a free function
needs link-time symbol replacement, which needs a compiled component and does not
port cleanly. cztest took the single header and mocks virtual methods.

**The vocabulary of the biggest framework.** The names are GoogleTest's, so that
migrating code needs no new concepts.

**The assertion is a function; the macro is sugar.** That makes the assertions
composable, unit testable and usable in a constant expression.

**Everything that can be checked at compile time is.** The action and the matcher are
constrained with concepts, the mock method is checked against the interface by
`override`, two guards cover the virtual destructor and a missing mock method, and the
name of a test is checked by a `consteval` function.

**An error message has to say what to do.** The GoogleTest spellings that cztest does
not support fail with the alternative, not with "not declared in this scope".

**The boundaries are written down.** No free function mocking, no thread safe mock, no
death tests: all of it is in the documentation rather than left to a guess.

## Limitations

- `EXPECT_CALL(obj, Method(args))` relies on `##` pasting (`obj.gmock_Method`), which is
  what GCC and Clang do in practice.
- The parameter types of a mocked method have to match the interface exactly,
  `const` and references included.
- A mocked method without parameters needs `CZTEST_MOCK_NOARG_METHOD`.
- At most eight parameters per mocked method.
- A mock is not thread safe.
- No death tests, no typed tests, no `EXPECT_THAT` with the matchers of GoogleMock.

## Documentation

| Document | Content |
| --- | --- |
| [docs/tutorial.md](docs/tutorial.md) | the complete tutorial, from the smallest test to integration tests |
| [docs/migration-gtest.md](docs/migration-gtest.md) | what changes when you come from GoogleTest, with a record of a real migration |
| [README-zh.md](README-zh.md) | the Chinese version of this document |

## License

MIT
