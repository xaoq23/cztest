# cztest

现代 C++ 的**纯头文件**单元测试框架，支持**函数级 mock**。只要把 `include` 目录加进搜索路径
（`-Iinclude`）即可使用，没有任何第三方依赖，也不需要链接额外的库。

* 头文件 only：`#include <cztest/cztest.hpp>` 即可
* 函数级 mock：逐个函数声明期望、匹配参数、校验调用次数
* **C++20 起可用**（`std::source_location` 等现代特性；不承诺 C++17 及以下）
* 自带测试运行器：过滤、列表、JUnit XML 报告、彩色输出
* 与 [cizon2](https://github.com/cizon-dev/cizon) 构建工具无缝配合（`-Iinclude` 由 cizon2 自动添加）

## 定位

**cztest 是 cizon2 生态的第一方测试框架**，不是 gtest 的兼容层：

| | 说明 |
| --- | --- |
| **为 cizon2 而生** | 退出码契约（0 = 通过）与 `kind = "test"` target 配套；`--filter` / `--junit` 经 `cizon2 test --` 直接透传；作为依赖时零编译单元、零链接、不需要 `system_links` |
| **现代 C++，不背老包袱** | 最低 C++20；断言核心是普通函数而不是宏魔法；判定谓词是 `constexpr` 的；失败位置用 `std::source_location`，带函数名 |
| **不追求 gtest 兼容** | 宏名（`TEST` / `EXPECT_*`）沿用以降低迁移成本，但实现不继承 gtest 的历史包袱：不支持 mock 自由函数（那是符号替换，Windows 上不可移植且需要编译单元），只支持虚方法 |
| **取舍写在这里** | 换来的：4 个头文件、零依赖、`-I` 即用。放弃的：自由函数 mock、nice/strict mock 变体、全局 mock 环境 |

## 命名约定

框架名为 **cztest**，与 CMake 自带的 `ctest` 命令、以及 GoogleTest 的 `gtest` 都不冲突：

| 类别 | 命名 | 说明 |
| --- | --- | --- |
| 头文件 | `<cztest/cztest.hpp>` | 唯一公开入口，其余头文件在 `include/cztest/` 内 |
| 命名空间 | `cztest` / `cztest::detail` | |
| 框架专用宏 | `CZTEST_*` | `CZTEST_MAIN`、`CZTEST_MOCK_METHOD`、`CZTEST_IMPLEMENT_MAIN` … |
| 通用测试宏 | `TEST` `TEST_F` `EXPECT_*` `ASSERT_*` `EXPECT_CALL` | 与 GoogleTest 保持一致，迁移时几乎不用改 |

带 `CZTEST_` 前缀的都是 cztest 特有的（入口点、mock 声明）；不带前缀的是业界通用写法，
刻意与 GoogleTest 对齐，方便从 gtest/gmock 迁移过来的代码直接使用。

---

## 目录结构

```
cztest/
├── include/cztest/          # 框架本体（全部是头文件）
│   ├── cztest.hpp           #   汇总头文件 + 入口点
│   ├── core.hpp             #   测试注册、断言、运行器、报告
│   ├── matchers.hpp         #   参数匹配器
│   └── mock.hpp             #   mock 机制
├── tests/                   # 框架自测用例
│   ├── test_core.cpp
│   ├── test_mock.cpp
│   └── test_injection.cpp
└── cizon.toml               # cizon2 构建配置
```

把框架用到自己的项目里，只需要复制 `include/cztest` 这一个目录。

---

## 快速开始

```cpp
#include <cztest/cztest.hpp>

TEST(Math, Add) {
  EXPECT_EQ(1 + 1, 2);
  ASSERT_TRUE(1 + 1 == 2) << "sanity check failed";
}
```

编译与运行：

```bash
clang++ -std=c++17 -Iinclude tests/*.cpp -o tests
./tests
```

### 入口点（main）

| 平台 | 做法 |
| --- | --- |
| Linux / macOS (ELF, Mach-O) | 不用管，框架自带弱符号 `main()`，直接包含头文件即可 |
| Windows (COFF: MSVC / MinGW / clang-cl) | 在**任意一个**测试文件中写一行 `CZTEST_MAIN();` |

```cpp
#include <cztest/cztest.hpp>

CZTEST_MAIN();     // 只在一个 .cpp 里写
```

也可以在包含头文件之前定义宏，效果相同：

```cpp
#define CZTEST_IMPLEMENT_MAIN
#include <cztest/cztest.hpp>
```

如果测试二进制已经有自己的 `main`，定义 `CZTEST_NO_MAIN` 可以关掉框架的入口点，
然后手动调用 `cztest::RunAllTests(argc, argv)`。

---

## 断言

断言的核心是**普通函数**，宏只是薄薄一层糖：

```cpp
namespace cztest {

// 判定谓词：constexpr，可在 static_assert 里用
constexpr bool Equal(const A& a, const B& b);

// 断言函数：返回 AssertionResult
AssertionResult ExpectEqual(const A& a, const B& b, std::string_view expr = "a == b");
AssertionResult ExpectTrue(bool value, std::string_view expr = "condition");
// ExpectNotEqual / ExpectLess / ExpectLessEqual / ExpectGreater /
// ExpectGreaterEqual / ExpectStringEqual / ExpectStringNotEqual /
// ExpectNear / ExpectThrow<E> / ExpectNoThrow / ExpectAnyThrow

// 上报一次失败
void ReportFailure(std::source_location location, std::string message, bool fatal = false);

}
```

| 宏 | 说明 |
| --- | --- |
| `EXPECT_TRUE(x)` / `EXPECT_FALSE(x)` | 布尔判断 |
| `EXPECT_EQ(a,b)` `NE` `LT` `LE` `GT` `GE` | 比较（`ASSERT_` 版本在失败时立刻终止当前用例） |
| `EXPECT_STREQ(a,b)` / `EXPECT_STRNE(a,b)` | C 字符串比较（支持 `nullptr`） |
| `EXPECT_NEAR(a,b,eps)` | 浮点近似比较 |
| `EXPECT_DOUBLE_EQ(a,b)` / `EXPECT_FLOAT_EQ(a,b)` | 浮点比较，容差 4 个 ULP（与 GoogleTest 同规则） |
| `EXPECT_THROW(stmt, Type)` / `NO_THROW` / `ANY_THROW` | 异常断言 |
| `ADD_FAILURE()` / `FAIL()` | 手动失败 / 致命失败 |
| `SCOPED_TRACE("...")` | 给失败补充上下文（作用域内有效） |
| `SKIP("...")` | 结束当前用例但不判失败 |

所有断言都可以追加自定义信息：

```cpp
EXPECT_EQ(result.size(), 3) << "expected three items, got " << result.size();
```

`EXPECT_*` 记录失败后继续执行，`ASSERT_*` 记录失败后终止当前用例。

### 直接用函数（不写宏）

需要根据断言结果分支时，可以直接调用函数：

```cpp
if (auto result = cztest::ExpectEqual(a, b); !result) {
  cztest::ReportFailure(std::source_location::current(), result.message);
}
```

### 编译期检查

判定谓词是 `constexpr` 的，所以可以在编译期就检查：

```cpp
static_assert(cztest::Equal(1 + 1, 2));
```

### 失败位置带函数名

失败位置来自 `std::source_location`，所以报告里能看出断言发生在哪个函数里：

```
[  FAILED  ] Modern.ReportsTheEnclosingFunction
C:\...\tests\test_modern.cpp:9 (int demo::Helper(int)): Failure
Expected: value == 42
  Actual: 1 vs 42
```

断言直接写在用例里时不打印函数名（那时只会是框架生成的内部名字，没有信息量）。

### SCOPED_TRACE：给失败补上下文

```cpp
for (const auto& item : items) {
    SCOPED_TRACE("item: " << item.name);
    EXPECT_EQ(item.value, 0);
}
```

失败时会把所有还开着的 trace 一起报出来（ innermost 先）：

```
Expected: item.value == 0
  Actual: 3 vs 0
Traces:
  [2] index: 1
  [1] item: beta
```

### SKIP：跳过而不是失败

```cpp
if (!ready) {
    SKIP("the service is not up yet");
}
```

用例会被报成 `[  SKIPPED ]`，不计入失败，退出码仍然是 0 ✓。

---

## Fixture（测试夹具）

```cpp
class ContainerFixture : public cztest::Test {
 public:
  void SetUp() override    { items_ = {1, 2, 3}; }
  void TearDown() override { items_.clear(); }

  std::vector<int> items_;
};

TEST_F(ContainerFixture, SizeIsThree) {
  EXPECT_EQ(items_.size(), 3u);
}
```

---

## 参数化测试（TEST_P）

`TEST_P` 声明一次，`INSTANTIATE_TEST_SUITE_P` 按参数展开成多个用例：

```cpp
namespace {

struct Case { int a, b, expected; };

class AdderTest : public cztest::TestWithParam<Case> {
 protected:
  void SetUp() override { value_ = GetParam(); }
  Case value_;
};

}  // namespace

TEST_P(AdderTest, AddsCorrectly) {
  EXPECT_EQ(value_.a + value_.b, value_.expected);
}

INSTANTIATE_TEST_SUITE_P(Basic, AdderTest, cztest::Values(
    Case{1, 2, 3}, Case{10, 20, 30}, Case{-1, 1, 0}));
```

展开后的用例名为 `Basic/AdderTest.AddsCorrectly/0`、`/1`、`/2`，可以直接过滤：

```bash
./tests --filter="Basic/*"            # 整组
./tests --filter="*AddsCorrectly/1"    # 单个实例
```

参数源：

| 生成器 | 说明 |
| --- | --- |
| `cztest::Values(v1, v2, ...)` | 显式列出参数值（类型必须一致） |
| `cztest::Range(begin, end)` | 半开区间 `[begin, end)` |
| `cztest::Bool()` | `false` / `true` |

> 函数调用里**不能带尾逗号**（`Values(a, b,)` 在 C++20/23 都是语法错误），写成
> `Values(a, b)` 即可。

`GetParam()` 拿到当前实例的参数；`SetUp()` 里读取它最常见。

---

## 函数级 Mock

### 1. 声明接口

```cpp
struct IStorage {
  virtual ~IStorage() = default;
  virtual int  Get(int key) = 0;
  virtual void Put(int key, const std::string& value) = 0;
  virtual std::string Name() const = 0;
};
```

### 2. 写 mock 类

mock 类同时继承接口和 `cztest::MockBase`，每个被 mock 的函数用宏声明一次：

```cpp
class MockStorage : public IStorage, public cztest::MockBase {
 public:
  CZTEST_MOCK_METHOD(int, Get, int);                       // 参数类型
  CZTEST_MOCK_METHOD(void, Put, int, const std::string&);
  CZTEST_MOCK_NOARG_CONST_METHOD(std::string, Name);       // 无参数 / const 成员函数
};
```

* `CZTEST_MOCK_METHOD(返回值, 函数名, 参数类型...)` —— 最多 8 个参数
* `CZTEST_MOCK_CONST_METHOD(...)` —— const 成员函数
* `CZTEST_MOCK_NOARG_METHOD(...)` / `CZTEST_MOCK_NOARG_CONST_METHOD(...)` —— 无参数函数
  （单独提供是因为 C++17 的预处理器无法在不带警告的情况下统计空参数列表）

### 3. 设置期望并使用

```cpp
TEST(Storage, Reads) {
  MockStorage mock;

  EXPECT_CALL(mock, Get(42)).Times(1).WillOnce(cztest::Return(7));

  EXPECT_EQ(mock.Get(42), 7);
}
```

`EXPECT_CALL` 的链式接口：

| 接口 | 说明 |
| --- | --- |
| `.Times(n)` | 期望调用次数 |
| `.Times(cztest::AtLeast(n))` / `AtMost(n)` / `AnyNumber()` / `Exactly(n)` | 更灵活的次数约束 |
| `.Once()` | 等价于 `Times(1)` |
| `.WillOnce(action)` | 下一次调用使用的动作（可多次调用，形成序列） |
| `.WillRepeatedly(action)` | 之后所有调用都使用的动作 |
| `.DoAndReturn(lambda)` | 用 lambda 计算返回值，等价于 `WillRepeatedly` |

动作：

| 动作 | 说明 |
| --- | --- |
| `cztest::Return(v)` | 返回固定值 |
| `cztest::ReturnRef(v)` | 返回已有对象的引用（函数返回引用时使用） |
| `cztest::Invoke(f)` / 直接传 lambda | 调用自定义可调用对象 |

### 4. 匹配参数

```cpp
EXPECT_CALL(mock, Get(cztest::Gt(10)))         .Times(1).WillOnce(cztest::Return(1));
EXPECT_CALL(mock, Put(1, cztest::HasSubstr("err"))).Times(cztest::AnyNumber());
EXPECT_CALL(mock, Get(cztest::Any()))          .Times(cztest::AnyNumber()).WillRepeatedly(cztest::Return(0));
```

可用匹配器：`Eq` `Ne` `Lt` `Le` `Gt` `Ge` `Any` `Not` `Truly` `StrEq` `HasSubstr`
`StartsWith` `EndsWith` `IsNull` `NotNull`。也可以直接写常量值，等价于 `Eq(...)`。

**自定义匹配器**只需要一个 `Matches` 成员，不必继承任何东西：

```cpp
struct IsEven {
    bool Matches(int value) const { return value % 2 == 0; }
};

EXPECT_CALL(mock, Get(IsEven{}));   // 只接受偶数
```

匹配时**最后注册且能匹配的期望优先**（与 GoogleMock 一致）；当多个期望都能匹配时，
最近声明的那个生效，其余的期望仍然需要满足自己的次数约束。

### 4.1 动作的约束

动作必须与被 mock 的函数签名一致，这一点在编译期检查：

```cpp
EXPECT_CALL(mock, Get(1)).WillOnce([](int key) { return key * 2; });   // ✓
EXPECT_CALL(mock, Get(1)).WillOnce([](std::string) { return 0; });      // ✗ 编译错误
```

签名不匹配时报的是"约束不满足"并指出概念名（`ActionFor`），而不是 `std::function`
那句著名的 "no match for the conversion"。

### 5. 自动校验

* 没有匹配到任何期望的调用 → 立刻报错
* 期望的调用次数不满足 → 用例结束时报错（mock 是局部变量时在析构时校验）
* 同一个期望在没有设置 `WillRepeatedly()` 的情况下被调用多次 → 报错并提示改用 `WillRepeatedly()`

---

## 运行器

```
--filter=<pattern>   只运行匹配的用例，支持 * 和 ? 通配符
--list               列出所有用例后退出
--repeat=<n>         每个用例重复运行 n 次
--quiet / -q         只输出失败详情和汇总（CI 用）
--verbose / -v       额外输出每个用例的 "[ RUN ]" 行（排查卡死时用）
--color=on|off       强制开启/关闭彩色输出（默认自动检测）
--junit=<file>       输出 JUnit XML 报告
--help               显示帮助
```

用例名格式为 `Suite.Name`，例如：

```bash
./tests --filter=Mock.*
./tests --filter=ContainerFixture.SizeIsThree
```

### 输出

默认每个用例只占一行：

```
[==========] Running 28 test(s).
[       OK ] Assertions.Boolean
[       OK ] Assertions.Comparison
[  FAILED  ] Mock.SequenceOfActions
C:\path\tests\test_mock.cpp:31: Failure
Expected: Get(1) == 2
  Actual: 1 vs 2
[==========] 28 test(s) ran. (3 ms total)
[  PASSED  ] 27 test(s).
[  FAILED  ] 1 test(s) failed.
```

* 失败详情紧跟在失败的用例下面，不用往上翻
* `--quiet` 只保留失败用例和汇总，适合 CI
* `--verbose` 恢复 "[ RUN ]" 行，用例卡死时能看出卡在哪一个

---

## 与 cizon2 集成

`cizon.toml`：

```toml
[project]
name = "cztest"
edition = "c++20"
compile_commands = true

[compiler]
# strict 展开为 -Wall -Wextra -Wpedantic，werror 让警告变成错误
strict = true
werror = true

[[target]]
name = "cztest_selftest"
kind = "test"
sources = ["tests"]
```

`kind = "test"` 的 target 只有 `cizon2 test` 会构建和运行，`cizon2 build` / `cizon2 run`
会完全忽略它。cizon2 会自动把项目的 `include` 目录加入头文件搜索路径，因此测试代码里
直接 `#include <cztest/cztest.hpp>` 即可，无需额外的 `-I` 参数。

```bash
cizon2 build                       # 只构建普通 target（不含测试）
cizon2 test                        # 构建并运行全部 kind = "test" 的 target
cizon2 test cztest_selftest        # 只跑指定的测试 target
cizon2 test -- --filter=Mock.*     # 透传参数给测试进程
```

`cizon2 test` 与测试框架无关，只认进程的退出码（0 = 通过），因此本框架无需任何改动即可接入。
测试进程自己的输出会被原样回放，cizon2 自己只补充 "Passed / Failed" 与汇总。

### 把 cztest 作为依赖引入

cztest 是纯头文件库，作为依赖引入时不需要编译任何东西：

```bash
cizon2 add https://github.com/you/cztest     # 写入 [dependencies] 与 cizon.lock
cizon2 sync                                   # 还原/校验依赖（不构建）
```

cizon2 会自动加上 `-Ideps/cztest/include` 和 `-Ideps/cztest`，所以依赖方**不需要手写 `-I`**，
也不需要 `system_links`（对比 gtest 要 `-lgtest_main -lgtest`）。

> 需要 cizon2 v0.7 及以上（`kind = "test"`）。旧版本请用 `kind = "bin"` +
> `cizon2 run cztest_selftest`，效果等价。

---

## 教学式报错

cztest 不支持的 GoogleTest 写法，**报错会直接告诉你该怎么做**（而不是一句 "not declared in this scope"）：

```
error: static assertion failed: cztest has no ON_CALL. Use EXPECT_CALL(mock, Method(args))
       .WillRepeatedly(action) for the default behaviour, and add one EXPECT_CALL per distinct
       call if the answers differ.
```

覆盖的写法：`ON_CALL` `EXPECT_THAT` `MOCK_METHOD` `EXPECT_DEATH` `TYPED_TEST` `GTEST_SKIP` ✓
（对应 `cztest/compat.hpp`，默认随 `<cztest/cztest.hpp>` 一起包含；
同时使用 GoogleTest 时可以 `#define CZTEST_NO_COMPAT` 关掉它。）

mock 类忘了继承 `cztest::MockBase` 时，报错也是一句话：

```
error: static assertion failed: A mock class has to inherit cztest::MockBase:
       class MockX : public IX, public cztest::MockBase { ... };
```

---

## 已知限制

* `EXPECT_CALL(obj, Method(args))` 依赖 `##` 拼接 `obj.gmock_Method`，这是 GCC / Clang 的
  实际行为；MSVC 的传统预处理器可能需要改写为 `EXPECT_CALL(obj, Method, (args))` 形式。
* mock 函数的参数类型必须与接口**完全一致**（`const`、引用都要写全）。
* 无参数函数必须使用 `CZTEST_MOCK_NOARG_METHOD` 系列宏。
* 一次 `EXPECT_CALL` 最多 8 个参数。

---

## 文档

| 文档 | 内容 |
| --- | --- |
| [docs/tutorial.md](docs/tutorial.md) | 完整使用教程：从最小用例到集成测试 |
| [docs/migration-gtest.md](docs/migration-gtest.md) | 从 GoogleTest 迁移过来要改什么（附实测编译记录） |

---

## License

MIT
