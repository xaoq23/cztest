# cztest

**一个面向现代 C++ 的 C++20 原生、单头文件单元/集成测试框架。**
接入只需要一个 `-I`；mock 的粒度是单个函数。

```cpp
#include <cztest/cztest.hpp>

TEST(Math, Addition) {
    EXPECT_EQ(1 + 1, 2);
}
```

- **纯头文件** —— 5 个头文件，零依赖，无需链接任何东西
- **函数级 mock** —— 按方法声明期望、匹配参数、校验调用次数
- **C++20 优先** —— `std::source_location`、concepts、`std::span`、`std::string_view`
- **自带运行器** —— 过滤、列表、JUnit XML、彩色输出
- **为 [cizon2](https://github.com/cizon-dev/cizon) 而生** —— `-Iinclude` 由它自动添加

版本 0.2.0（`CZTEST_VERSION_MAJOR` / `MINOR` / `PATCH`）。

> English version: [README.md](README.md)

## 一览

| | |
| --- | --- |
| 语言标准 | C++20（`std::source_location`、concepts） |
| 依赖 | 无，只用标准库 |
| 编译器基线 | GCC 11+、Clang 14+、MSVC 19.30+（实测 GCC 16.2 与 Clang 22） |
| mock | 虚方法，每个方法一组期望 |
| 明确不做 | mock 自由函数、全局 mock 状态、death test |

### 和常见框架的对比

| | gtest / gmock | Catch2 / doctest | cztest |
| --- | --- | --- | --- |
| 构建 | 编译并链接两个库 | 单头文件 | 单头文件 |
| mock | 有，含自由函数 | 无 | 有，虚方法 |
| 断言名 | `EXPECT_EQ` | `CHECK_EQ` | `EXPECT_EQ` |
| 失败位置 | 文件 + 行号 | 文件 + 行号 | 文件 + 行号 **+ 函数名** |
| 断言失败后 | 继续（`EXPECT`）或中止（`ASSERT`） | 同 | 同 |
| 编译期检查 | 部分 | 部分 | mock、匹配器、动作、用例名 |
| 报错会告诉你怎么做 | 否 | 否 | **是** |

## 为什么还要一个框架

三个理由，也正是代码里其它所有取舍的来源。

**1. 词汇是大家已经会的那一套。** `TEST`、`TEST_F`、`EXPECT_EQ`、`EXPECT_CALL`、`Times`、
`WillOnce`、`Return` —— 全是 GoogleTest 的名字。从 gtest 迁过来的代码**不需要学任何新概念**，
只需要换一个 include。

**2. 断言是函数，不是魔法。** 宏只是普通函数的糖：

```cpp
if (auto result = cztest::ExpectEqual(a, b); !result) {
    cztest::ReportFailure(std::source_location::current(), result.message);
}
```

它们可以被组合、可以被自己的测试测、判定谓词还是 `constexpr` 的 ✓（编译期可查 ✓）。

**3. 单头文件意味着放弃 mock 自由函数。** 拦截一个自由函数需要链接期符号替换
（`--wrap`、弱符号、`LD_PRELOAD`、IAT patch），那意味着一个编译单元、一个全局注册表，
以及一套在 Windows 上行为不同的机制。**单头文件与自由函数 mock 二选一** ✓ —— cztest 选了前者 ✓，
改成 mock 虚方法 ✓。

## 快速开始

一个测试文件：

```cpp
// tests/test_math.cpp
#include <cztest/cztest.hpp>

TEST(Math, Addition) {
    EXPECT_EQ(1 + 1, 2);
}
```

一个入口点（每个测试二进制一个）：

```cpp
// tests/main.cpp
#include <cztest/cztest.hpp>

CZTEST_MAIN()
```

构建与运行：

```console
$ c++ -std=c++20 -Iinclude tests/*.cpp -o tests
$ ./tests
[==========] Running 1 test(s).
[       OK ] Math.Addition
[==========] 1 test(s) ran. (0 ms total)
[  PASSED  ] 1 test(s).
```

在 ELF 和 Mach-O 上框架会装一个弱符号 `main` ✓，所以那里连入口点文件都不需要 ✓。
Windows（COFF）必须有且只有一个翻译单元提供 `main` ✓：`CZTEST_MAIN()` ✓，
或者在 include 之前定义 `CZTEST_IMPLEMENT_MAIN` ✓。

## 目录结构

```
cztest/
├── include/cztest/          # 框架本体，全部是头文件
│   ├── cztest.hpp           #   汇总头 + 入口点
│   ├── core.hpp             #   注册表、断言、运行器
│   ├── matchers.hpp         #   参数匹配器
│   ├── mock.hpp             #   mock 机制
│   ├── param.hpp            #   参数化测试
│   └── compat.hpp           #   对不支持的 GoogleTest 写法给出教学式报错
├── tests/                   # 框架自己的测试
├── docs/
│   ├── tutorial.md           #   从最小用例到集成测试的完整教程
│   └── migration-gtest.md    #   从 GoogleTest 过来要改什么
└── cizon.toml
```

把 `include/cztest` 拷进你的项目，就完成了接入 ✓。

## API

### 断言

| 宏 | 含义 |
| --- | --- |
| `EXPECT_TRUE(x)` / `EXPECT_FALSE(x)` | 布尔判断 |
| `EXPECT_EQ(a,b)` `NE` `LT` `LE` `GT` `GE` | 比较 |
| `EXPECT_STREQ(a,b)` / `EXPECT_STRNE(a,b)` | C 字符串比较，支持 `nullptr` |
| `EXPECT_NEAR(a,b,eps)` | 浮点比较，自己给容差 |
| `EXPECT_DOUBLE_EQ(a,b)` / `EXPECT_FLOAT_EQ(a,b)` | 浮点比较，容差 4 个 ULP |
| `EXPECT_THROW(stmt, Type)` / `NO_THROW` / `ANY_THROW` | 异常 |
| `EXPECT_THAT(value, matcher)` | 用匹配器判断 |
| `EXPECT_OK(expr)` | `std::expected` 持有值（C++23 起） |
| `ADD_FAILURE()` / `FAIL()` / `SUCCEED()` | 手动失败与成功 |
| `SCOPED_TRACE("...")` | 给失败补上下文 |
| `SKIP("...")` | 结束用例但不判失败 |

每个 `EXPECT_*` 都有对应的 `ASSERT_*` ✓。`EXPECT_*` 记录失败后继续 ✓，
`ASSERT_*` 记录失败后立刻终止当前用例 ✓。

```cpp
const auto result = Parse("a=1");
ASSERT_TRUE(result.ok);              // 失败即停，后面不能用了
EXPECT_EQ(result.fields.size(), 2);   // 继续跑，能一次看到更多问题
```

所有断言都可以追加说明：

```cpp
EXPECT_EQ(result.size(), 3) << "期望三个，实际 " << result.size();
```

#### 断言是普通函数

```cpp
if (auto result = cztest::ExpectEqual(a, b); !result) {
    cztest::ReportFailure(std::source_location::current(), result.message);
}
```

判定谓词是 `constexpr` 的 ✓：

```cpp
static_assert(cztest::Equal(1 + 1, 2));
```

#### 失败知道发生在哪里

```
[  FAILED  ] Parser.ReadsTwoFields
C:\path\tests\test_parser.cpp:23: Failure
Expected: result.fields.size() == 2
  Actual: 1 vs 2
```

位置来自 `std::source_location` ✓，断言位于你自己的函数里时还会带上函数名 ✓：

```
C:\path\tests\test_modern.cpp:9 (int demo::Helper(int)): Failure
```

#### Trace 补上上下文

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

### Fixture（测试夹具）

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

`SetUp` 在夹具的每个用例前跑 ✓，`TearDown` 在之后跑 ✓（**用例失败也会跑** ✓）。

### 参数化测试

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

展开后的用例名是 `Basic/TrimTest.RemovesTheSpaces/0`、`/1` …… ✓ 可以精确选中某一个 ✓。
参数源有 `cztest::Values(...)`、`cztest::Range(begin, end)`、`cztest::Bool()` ✓。

### Mock

**1. 接口**

```cpp
struct IHttp {
    virtual ~IHttp() = default;
    virtual bool Get(const std::string& url, std::string& body) = 0;
};
```

**2. mock 类**

```cpp
class MockHttp : public IHttp, public cztest::MockBase {
 public:
    CZTEST_MOCK_INTERFACE(IHttp);
    CZTEST_MOCK_METHOD(bool, Get, const std::string&, std::string&);
};
CZTEST_MOCK_COMPLETE(MockHttp);
```

`CZTEST_MOCK_METHOD(返回值, 函数名, 参数类型...)` 的参数类型**不带外括号** ✓。
const 成员函数用 `CZTEST_MOCK_CONST_METHOD` ✓；无参数函数用
`CZTEST_MOCK_NOARG_METHOD` / `CZTEST_MOCK_NOARG_CONST_METHOD` ✓。

**3. 设期望、注入、调用**

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

| 期望的组成部分 | 含义 |
| --- | --- |
| `.Times(n)` | 期望调用几次 |
| `.Times(AtLeast(n))` / `AtMost(n)` / `AnyNumber()` / `Exactly(n)` | 更灵活的次数约束 |
| `.Once()` | 等价于 `Times(1)` |
| `.WillOnce(action)` | **下一次**调用用的动作 ✓ 多次调用形成序列 ✓ |
| `.WillRepeatedly(action)` | 之后所有调用用的动作 |
| `.DoAndReturn(lambda)` | 同 `WillRepeatedly`，名字更贴切 |

| 动作 | 含义 |
| --- | --- |
| `cztest::Return(v)` | 固定值 |
| `cztest::ReturnRef(v)` | 已有对象的引用 |
| `cztest::Invoke(f)` | 任意可调用对象 |

动作可以捕获**只可移动**的对象 ✓ —— cztest 自带一个 move-only 可调用体 ✓，
不依赖 `std::move_only_function`（不是每个标准库都提供 ✓）。

mock 是严格的 ✓：没有匹配到任何期望的调用会立刻失败 ✓ 并打印实际参数 ✓ 和已有的期望 ✓；
次数没满足的期望在用例结束时报告 ✓。
调用次数超过期望上限时 ✓，失败会指向那条期望所在的行 ✓。

#### 匹配器

`Eq` `Ne` `Lt` `Le` `Gt` `Ge` `Any` `Not` `StrEq` `HasSubstr` `StartsWith` `EndsWith`
`IsNull` `NotNull` `Truly` ✓。直接写常量值就是 `Eq` ✓。

匹配器可以组合 ✓，自己写的匹配器只需要一个 `Matches` 成员 ✓：

```cpp
EXPECT_THAT(value, cztest::AllOf(cztest::Ge(1), cztest::Lt(100)));   // 全部满足
EXPECT_THAT(value, cztest::AnyOf(cztest::Eq(1), cztest::Eq(2)));    // 任一满足
EXPECT_THAT(value, cztest::Not(cztest::Eq(3)));                     // 取反
EXPECT_THAT(items, cztest::ElementsAre(cztest::Eq(1), cztest::Ge(2)));  // 逐个元素

struct IsEven {
    bool Matches(int value) const { return value % 2 == 0; }
};
EXPECT_THAT(value, IsEven{});
```

比较类匹配器在类型提供 `<=>` 时走三路比较 ✓，所以只定义了 `operator<=>` 的类型也能用 ✓。

#### 两个编译期护栏

```cpp
class MockHttp : public IHttp, public cztest::MockBase {
 public:
    CZTEST_MOCK_INTERFACE(IHttp);   // 接口必须有虚析构
    CZTEST_MOCK_METHOD(bool, Get, const std::string&, std::string&);
};
CZTEST_MOCK_COMPLETE(MockHttp);     // mock 必须实现了接口的所有方法
```

方法签名和接口不一致的情况，`override` 本身就会在宏那一行报错 ✓。

#### 线程安全边界

**mock 不是线程安全的** ✓：调用计数和期望表都是没有锁的普通成员 ✓。
一个线程一个 mock，或者自己加锁 ✓。

## 运行器

```
--filter=<pattern>   只运行匹配的用例（支持 * 和 ? 通配符）
--list               列出所有用例后退出
--repeat=<n>         每个用例重复运行 n 次
--quiet / -q         只输出失败详情和汇总
--verbose / -v       额外输出每个用例的 "[ RUN ]" 行
--color=on|off       强制开启/关闭彩色输出
--junit=<file>       输出 JUnit XML 报告
--help               显示帮助
```

用例名格式为 `Suite.Name` ✓：

```bash
./tests --filter=Mock.*
./tests --filter="Basic/TrimTest.RemovesTheSpaces/1"
```

用 `--repeat` 时 ✓ 每一行都会标明这是第几次运行 ✓ —— 偶发失败的用例因此一眼就能看出来 ✓：

```
[       OK ] Flaky.FailsOnTheSecondRun (run 1 of 3)
[  FAILED  ] Flaky.FailsOnTheSecondRun (run 2 of 3)
```

每个用例只占一行 ✓，失败详情紧跟在其后 ✓：

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

退出码：全部通过或跳过是 0 ✓，否则是 1 ✓。

JUnit 报告里带着失败的完整信息 ✓，也会标记被跳过的用例 ✓ —— CI 的面板因此能显示
到底哪里出了问题 ✓：

```xml
<testcase classname="Mock" name="SequenceOfActions">
  <failure message="Expected: Get(1) == 2">C:\path\tests\test_mock.cpp:31: Expected: Get(1) == 2
  Actual: 1 vs 2</failure>
</testcase>
```

两个用例重名是一个错误 ✓ —— 运行器会拒绝启动 ✓ 并指出两处的位置 ✓：

```
[==========] Duplicate test names:
  Math.Addition
    C:\path\tests\test_a.cpp:10
    C:\path\tests\test_b.cpp:14
Every test needs its own name: rename the ones that repeat.
```

编译器抓不到这件事 ✓ —— 因为用例分布在不同的翻译单元里 ✓；运行器能抓到 ✓，
因为它全都看得见 ✓。

## 与 cizon2 集成

```toml
# cizon.toml
[project]
name = "bintree"
edition = "c++20"
compile_commands = true

[compiler]
# strict 展开为 -Wall -Wextra -Wpedantic，werror 让警告变成错误
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
cizon2 build                        # 只构建普通 target
cizon2 test                         # 构建并运行全部 kind = "test" 的 target
cizon2 test bintree_tests           # 只跑一个
cizon2 test -- --filter=Mock.*      # 参数透传给测试进程
```

`kind = "test"` 的 target 会被 `cizon2 build` 和 `cizon2 run` 完全忽略 ✓，
只有 `cizon2 test` 会构建并运行它 ✓。测试进程按退出码判定 ✓ ——
这也是为什么 cizon2 不需要认识这个框架 ✓。

### 把 cztest 作为依赖引入

cztest 是纯头文件库 ✓，作为依赖不需要编译任何东西 ✓：

```bash
cizon2 add https://github.com/you/cztest   # 写入 [dependencies] 与 cizon.lock
cizon2 sync                                 # 还原/校验依赖，不构建
```

cizon2 会自己加上 `-Ideps/cztest/include` 和 `-Ideps/cztest` ✓，
所以依赖方**不需要写 `-I`** ✓，也不需要 `system_links` ✓ ——
对比 gtest 要 `-lgtest_main -lgtest` ✓。

### 单元测试与集成测试

把两者分开 ✓：它们属于不同的 target ✓，回答的是不同的问题 ✓。

| | 单元测试 | 集成测试 |
| --- | --- | --- |
| 依赖 | 全是假的（mock） | 真的 |
| 速度 | 毫秒 | 秒 |
| 失败意味着 | 我的逻辑错了 | 两个部件对不上 |

需要真实文件、数据库或 socket 的集成测试**依然是测试** ✓：
它属于 `tests/integration/` ✓、自己的 target ✓，而且必须做好隔离 ✓
（临时目录 ✓、内存数据库 ✓、由系统分配的端口 ✓）。

## 从 GoogleTest 迁移

断言层完全不用改 ✓。机械部分：

```bash
# 命名空间
sed -i 's/::testing::/cztest::/g' tests/*.cpp

# fixture 的基类
sed -i 's/public ::testing::Test/public cztest::Test/' tests/*.cpp

# mock 方法声明：
#   MOCK_METHOD(ret, Name, (types...), (override));
# → CZTEST_MOCK_METHOD(ret, Name, types...)
sed -i 's/MOCK_METHOD(\(.*\), (\(.*\)), (override));/CZTEST_MOCK_METHOD(\1, \2);/' tests/*.cpp
```

有两处脚本替不了 ✓：`::testing::_` 要改成 `cztest::Any()` ✓，
`ON_CALL(...).WillByDefault(...)` 要改成 `EXPECT_CALL(...).WillRepeatedly(...)` ✓。

对于 cztest 没有的 GoogleTest 特性 ✓，编译器会告诉你该用什么 ✓：

```
error: static assertion failed: cztest has no ON_CALL. Use EXPECT_CALL(mock, Method(args))
       .WillRepeatedly(action) for the default behaviour, ...
```

完整清单和每个匹配器的对应关系见
[docs/migration-gtest.md](docs/migration-gtest.md) ✓。

## 设计说明

代码背后的原则，及其代价。

**单头文件，或者自由函数 mock —— 只能选一个。** 拦截自由函数需要链接期符号替换 ✓，
那需要一个编译单元 ✓ 且不能干净地跨平台 ✓。cztest 选了单头文件 ✓，改成 mock 虚方法 ✓。

**用最大框架的词汇。** 名字都是 GoogleTest 的 ✓，迁移过来的代码不需要学新概念 ✓。

**断言是函数，宏是糖。** 这样断言可以组合 ✓、可以被单测 ✓、可以用于常量表达式 ✓。

**能在编译期查的，绝不留到运行期。** 动作和匹配器用 concept 约束 ✓，
mock 方法由 `override` 对照接口检查 ✓，两个护栏覆盖虚析构和漏掉的方法 ✓，
用例名由 `consteval` 函数检查 ✓。

**报错要说该怎么做。** cztest 不支持的 GoogleTest 写法会给出替代方案 ✓，
而不是一句 "not declared in this scope" ✓。

**边界写在纸上。** 不支持自由函数 mock ✓、mock 非线程安全 ✓、没有 death test ✓ ——
全都写进文档 ✓，而不是靠默契 ✓。

## 已知限制

- `EXPECT_CALL(obj, Method(args))` 依赖 `##` 拼接（`obj.gmock_Method`）✓，
  这是 GCC / Clang 的实际行为 ✓。
- mock 方法的参数类型必须与接口**完全一致** ✓，`const` 和引用都要写全 ✓。
- 无参数的 mock 方法要用 `CZTEST_MOCK_NOARG_METHOD` ✓。
- 一个 mock 方法最多 8 个参数 ✓。
- mock 不是线程安全的 ✓。
- 没有 death test ✓、没有 typed test ✓、不能用 GoogleMock 的匹配器写 `EXPECT_THAT` ✓。

## 文档

| 文档 | 内容 |
| --- | --- |
| [docs/tutorial.md](docs/tutorial.md) | 完整教程：从最小用例到集成测试 |
| [docs/migration-gtest.md](docs/migration-gtest.md) | 从 GoogleTest 过来要改什么，附一次真实迁移的记录 |
| [README.md](README.md) | 本文档的英文版 |

## License

MIT
