# cztest 使用教程

从最小的一个用例，到带 mock 的单元测试，再到集成测试。每一节都可以直接复制编译。

- [0. 30 秒上手](#0-30-秒上手)
- [1. 断言](#1-断言)
- [2. 夹具：共享准备与清理](#2-夹具共享准备与清理)
- [3. 参数化测试](#3-参数化测试)
- [4. Mock：函数级的替身](#4-mock函数级的替身)
- [5. 从单元测试到集成测试](#5-从单元测试到集成测试)
- [6. 与 cizon2 配合](#6-与-cizon2-配合)
- [7. 常见错误清单](#7-常见错误清单)

---

## 0. 30 秒上手

新建一个测试文件：

```cpp
// tests/test_math.cpp
#include <cztest/cztest.hpp>

TEST(Math, Addition) {
    EXPECT_EQ(1 + 1, 2);
}
```

再新建一个入口点文件（一个测试二进制只能有一个 `main`）：

```cpp
// tests/main.cpp
#include <cztest/cztest.hpp>

CZTEST_MAIN()
```

用 cizon2 构建并运行：

```toml
# cizon.toml
[project]
name = "demo"
edition = "c++20"

[[target]]
name = "demo_tests"
kind = "test"
sources = ["tests"]
```

```bash
cizon2 test
```

输出：

```
[==========] Running 1 test(s).
[       OK ] Math.Addition
[==========] 1 test(s) ran. (0 ms total)
[  PASSED  ] 1 test(s).
   Passed demo_tests (0.31s)
  Finished test result: ok. 1 passed; 1 total
```

要点：

- 框架是**纯头文件**的，只要 `include/` 在头文件搜索路径里就能用（cizon2 会自动加）
- `TEST(套件名, 用例名)` 声明一个用例；套件名只是给用例分组用的，随便起
- 断言失败时进程退出码为 1，CI 直接就能判断

---

## 1. 断言

### 1.1 断言家族

| 宏 | 含义 |
| --- | --- |
| `EXPECT_TRUE(x)` / `EXPECT_FALSE(x)` | 布尔判断 |
| `EXPECT_EQ(a,b)` `NE` `LT` `LE` `GT` `GE` | 比较 |
| `EXPECT_STREQ(a,b)` / `EXPECT_STRNE(a,b)` | C 字符串比较（支持 `nullptr`） |
| `EXPECT_NEAR(a,b,eps)` | 浮点近似比较（自己给容差） |
| `EXPECT_DOUBLE_EQ(a,b)` / `EXPECT_FLOAT_EQ(a,b)` | 浮点比较（容差 4 个 ULP，与 GoogleTest 同规则） |
| `EXPECT_THROW(stmt, Type)` / `NO_THROW` / `ANY_THROW` | 异常断言 |
| `ADD_FAILURE()` / `FAIL()` | 手动失败 / 致命失败 |

每个 `EXPECT_*` 都有一个 `ASSERT_*` 版本（`EXPECT_EQ` ↔ `ASSERT_EQ`）。

### 1.2 EXPECT 和 ASSERT 的区别

- `EXPECT_*`：记录失败，**继续往下跑**
- `ASSERT_*`：记录失败，**立刻终止当前用例**

```cpp
TEST(Parser, ReadsTwoFields) {
    const auto result = Parse("a=1");
    ASSERT_TRUE(result.ok);          // 失败就停：下面不能用了（ok 是成员变量，不是函数）
    EXPECT_EQ(result.fields.size(), 2);  // 失败也继续：能看到更多问题
}
```

**为什么用 ASSERT 判空**：如果 `result` 失败了还继续解引用，就是段错误 —— 测试直接崩掉，
你看不到是哪条断言错了：

```cpp
ASSERT_NE(node, nullptr);   // ✓ 失败即停，后面可以安全解引用
EXPECT_NE(node, nullptr);   // ✗ 失败后 node 可能为 null → 崩溃
```

### 1.3 追加说明文字

所有断言都可以 `<<` 接说明：

```cpp
EXPECT_EQ(result.size(), 3) << "期望三个，实际 " << result.size();
EXPECT_TRUE(ok) << "context: " << context;
```

### 1.4 断言核心是普通函数

宏只是糖，真正的判定是函数，所以你可以直接用、也可以组合：

```cpp
if (auto result = cztest::ExpectEqual(a, b); !result) {
    cztest::ReportFailure(std::source_location::current(), result.message);
}
```

判定谓词还是 `constexpr` 的，能在编译期检查：

```cpp
static_assert(cztest::Equal(1 + 1, 2));
```

### 1.5 失败信息长什么样

```
[  FAILED  ] Parser.ReadsTwoFields
C:\path\tests\test_parser.cpp:23: Failure
Expected: result.fields.size() == 2
  Actual: 1 vs 2
```

- `file:line` 直接指向那条断言
- `Expected:` 后面是**你写的表达式原文**
- 断言在 helper 函数里时还会带上函数名：`parser.cpp:23 (Parser::Fields)`

---

## 2. 夹具：共享准备与清理

当多个用例需要同样的准备工作时，用夹具：

```cpp
#include <cztest/cztest.hpp>
#include <fstream>
#include <string>

class ConfigFixture : public cztest::Test {
 public:
    void SetUp() override {
        std::ofstream file("config.toml");
        file << "port = 8080\n";
        file.close();
        config_ = Load("config.toml");
    }

    void TearDown() override {
        std::remove("config.toml");
    }

    Config config_;
};

TEST_F(ConfigFixture, ReadsThePort) {
    EXPECT_EQ(config_.port, 8080);
}

TEST_F(ConfigFixture, ThePortIsPositive) {
    EXPECT_GT(config_.port, 0);
}
```

- 夹具继承 `cztest::Test`
- `SetUp()` 在每个用例前跑，`TearDown()` 在每个用例后跑（**用例失败也会跑**）
- 用例体是夹具的成员，所以能直接访问夹具的成员变量

---

## 3. 参数化测试

同一段逻辑跑多组输入时，用参数化测试：

```cpp
#include <cztest/cztest.hpp>
#include <string>

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
    Case{"a  ", 1},
    Case{"  a  ", 1}));
```

展开后是三个独立用例：

```
Basic/TrimTest.RemovesTheSpaces/0
Basic/TrimTest.RemovesTheSpaces/1
Basic/TrimTest.RemovesTheSpaces/2
```

可以按名字过滤：

```bash
./tests --filter="Basic/*"            # 整组
./tests --filter="*RemovesTheSpaces/1" # 单个实例
```

参数源：

| 生成器 | 说明 |
| --- | --- |
| `cztest::Values(v1, v2, ...)` | 显式列出（所有值必须是同一类型） |
| `cztest::Range(begin, end)` | 半开区间 `[begin, end)` |
| `cztest::Bool()` | `false` / `true` |

> 函数调用里**不能带尾逗号**：`cztest::Values(a, b,)` 是语法错误（C++26 才有）。

---

## 4. Mock：函数级的替身

### 4.1 什么时候需要 mock

当被测代码依赖一个"慢的、不可控的、或者会炸的东西"（网络、文件、数据库、时钟），
而你只想测它**自己的逻辑**时，就把这个依赖抽成一个接口，测试时注入一个假的。

```
没有缝：  Service ──直接调用──> http_get()      ← 测不了，只能真的联网
有缝：    Service ──依赖──> IHttp ──> RealHttp
                        └──> MockHttp          ← 测试时注入这个
```

### 4.2 三步写一个 mock

**第一步：接口**

```cpp
struct IHttp {
    virtual ~IHttp() = default;
    virtual bool Get(const std::string& url, std::string& body) = 0;
};
```

**第二步：mock 类**（继承接口 + `cztest::MockBase`）

```cpp
class MockHttp : public IHttp, public cztest::MockBase {
 public:
    CZTEST_MOCK_METHOD(bool, Get, const std::string&, std::string&);
};
```

`CZTEST_MOCK_METHOD(返回值, 函数名, 参数类型...)` 里的参数类型必须和接口**完全一致**
（`const`、引用都要写全）。const 成员函数用 `CZTEST_MOCK_CONST_METHOD`，无参数函数用
`CZTEST_MOCK_NOARG_METHOD` / `CZTEST_MOCK_NOARG_CONST_METHOD`。

**第三步：设期望、注入、调用**

```cpp
TEST(Http, DownloadsTheBody) {
    MockHttp http;
    EXPECT_CALL(http, Get("https://x/y", cztest::Any()))
        .Times(1)
        .WillOnce(cztest::Invoke([](const std::string&, std::string& out) {
            out = "hello";
            return true;
        }));

    Client client(http);          // 注入
    EXPECT_EQ(client.Download("https://x/y"), "hello");
}
```

### 4.3 期望的语义

| 接口 | 含义 |
| --- | --- |
| `.Times(n)` | 期望调用次数 |
| `.Times(cztest::AtLeast(n))` / `AtMost(n)` / `AnyNumber()` / `Exactly(n)` | 更灵活的次数约束 |
| `.Once()` | 等价于 `Times(1)` |
| `.WillOnce(action)` | **下一次**调用用的动作（可以多次调用，形成序列） |
| `.WillRepeatedly(action)` | 之后所有调用用的动作 |
| `.DoAndReturn(lambda)` | 同 `WillRepeatedly`，名字上更贴切 |

动作：

| 动作 | 说明 |
| --- | --- |
| `cztest::Return(v)` | 返回固定值 |
| `cztest::ReturnRef(v)` | 返回已有对象的引用（函数返回引用时用） |
| `cztest::Invoke(f)` | 调用自定义可调用对象 |

参数匹配：

| 匹配器 | 说明 |
| --- | --- |
| `cztest::Eq(v)` `Ne` `Lt` `Le` `Gt` `Ge` | 比较（直接写常量值等价于 `Eq`） |
| `cztest::Any()` | 任意值 |
| `cztest::StrEq` `HasSubstr` `StartsWith` `EndsWith` | 字符串 |
| `cztest::IsNull()` / `NotNull()` | 空指针 |
| `cztest::Not(m)` | 取反 |
| `cztest::Truly(pred)` | 自定义判定 |

自定义匹配器只需要一个 `Matches` 成员，不用继承任何东西：

```cpp
struct IsEven {
    bool Matches(int value) const { return value % 2 == 0; }
};

EXPECT_CALL(mock, Get(IsEven{}));   // 只接受偶数
```

### 4.4 mock 是严格的

- 一次调用**没有匹配到任何期望** → 立刻失败，并把实际参数打印出来
- 期望的次数没满足 → 用例结束时失败
- 期望被调用了多次但没设 `WillRepeatedly` → 失败并提示改用 `WillRepeatedly`

所以 mock 天然逼着你把"这个依赖会被怎么用"写清楚。

### 4.5 mock 的生命周期

mock 是普通对象，**局部 mock 在析构时校验期望**：

```cpp
TEST(X, Works) {
    MockHttp http;                       // 期望没满足 → 析构时报错 ✓
    EXPECT_CALL(http, Get(...)).Times(1);
}   // ← 这里校验
```

作为夹具成员时，在用例结束时校验 ✓。

---

## 5. 从单元测试到集成测试

### 5.1 两者的边界

| | 单元测试 | 集成测试 |
| --- | --- | --- |
| 依赖 | 全部是假的（mock） | 真的（真的文件 / 真的库 / 真的服务） |
| 速度 | 毫秒级 | 秒级 |
| 失败含义 | "我的逻辑错了" | "两个部件对不上" |
| 数量 | 多 | 少 |
| 稳定性 | 永远稳定 | 可能受环境影响 |

**单元测试永远不该碰网络、真实文件、真实数据库。** 想验证"和真实东西对接得上"，
那是集成测试的事。

### 5.2 用 mock 把不可控的依赖变成可控的

一个读取配置的例子：单元测试用假的读取器，集成测试用真的文件。

```cpp
// include/app/config.hpp
namespace app {

/// 缝：字节从哪里来。
struct IFileReader {
    virtual ~IFileReader() = default;
    virtual bool Read(const std::string& path, std::string& out) const = 0;
};

/// 被测代码：只依赖接口，不依赖文件系统。
std::optional<int> TimeoutFrom(const IFileReader& reader, const std::string& path);

}  // namespace app
```

**单元测试**（tests/unit/test_config.cpp）：

```cpp
#include <cztest/cztest.hpp>
#include "app/config.hpp"

namespace {

class MockReader : public app::IFileReader, public cztest::MockBase {
 public:
    CZTEST_MOCK_CONST_METHOD(bool, Read, const std::string&, std::string&);
};

}  // namespace

TEST(Config, ReadsTheTimeout) {
    MockReader reader;
    EXPECT_CALL(reader, Read("app.toml", cztest::Any()))
        .Times(1)
        .WillOnce(cztest::Invoke([](const std::string&, std::string& out) {
            out = "[server]\ntimeout = 30\n";
            return true;
        }));

    const auto timeout = app::TimeoutFrom(reader, "app.toml");
    ASSERT_TRUE(timeout.has_value());
    EXPECT_EQ(*timeout, 30);
}
```

**集成测试**（tests/integration/test_config.cpp）：用真的文件。

```cpp
#include <cztest/cztest.hpp>
#include <filesystem>
#include <fstream>
#include "app/config.hpp"
#include "app/real_reader.hpp"

namespace {

class RealReaderFixture : public cztest::Test {
 public:
    void SetUp() override {
        path_ = std::filesystem::temp_directory_path() / "cztest-app.toml";
        std::ofstream file(path_);
        file << "[server]\ntimeout = 30\n";
    }

    void TearDown() override { std::filesystem::remove(path_); }

    std::filesystem::path path_;
};

}  // namespace

TEST_F(RealReaderFixture, ReadsARealFile) {
    app::RealReader reader;
    const auto timeout = app::TimeoutFrom(reader, path_.string());
    ASSERT_TRUE(timeout.has_value());
    EXPECT_EQ(*timeout, 30);
}
```

### 5.3 集成测试的隔离

集成测试碰真实资源，就要保证互不干扰：

| 资源 | 隔离办法 |
| --- | --- |
| 文件 | 临时目录 + 唯一文件名，`TearDown` 删除 |
| 数据库 | 内存库，或每个用例一个库/事务 |
| 端口 | 固定端口（自己独占）或端口 0 让系统分配 |
| 时间 | 注入一个"时钟"，测试里控制它 |
| 环境变量 | 在用例里改，用例结束恢复 |

### 5.4 两类测试分开成两个 target

生产代码编成静态库，单元测试和集成测试分别链接（集成测试需要真实实现，单元测试不需要）：

```toml
# 生产代码
[[target]]
name = "app"
kind = "static_lib"
sources = ["src"]

[[target]]
name = "unit_tests"
kind = "test"
sources = ["tests/unit"]

[[target]]
name = "integration_tests"
kind = "test"
sources = ["tests/integration"]
links = ["app"]
```

```bash
cizon2 test unit_tests           # 每次提交都跑
cizon2 test integration_tests    # 每天一次 / 发版前跑
```

---

## 6. 与 cizon2 配合

### 6.1 target 的两种形态

| kind | 谁构建 | 怎么跑 |
| --- | --- | --- |
| `bin` | `cizon2 build` | `cizon2 run <name>` |
| `test` | 只有 `cizon2 test` | `cizon2 test <name>` |

`kind = "test"` 的 target 不会被 `cizon2 build` 碰到 ✓。

### 6.2 运行器参数

```bash
cizon2 test -- --filter=Mock.*      # 按名字过滤（支持 * 和 ?）
cizon2 test -- --list               # 列出所有用例
cizon2 test -- --repeat=10          # 重复跑（查偶发失败）
cizon2 test -- --quiet              # 只输出失败和汇总（CI 用）
cizon2 test -- --junit=report.xml   # JUnit XML 报告（喂 CI）
```

### 6.3 把 cztest 作为依赖

cztest 是纯头文件库，作为依赖引入时**不需要编译任何东西**：

```bash
cizon2 add https://github.com/you/cztest   # 写入 [dependencies] 与 cizon.lock
cizon2 sync                                 # 还原/校验依赖（不构建）
```

cizon2 会自动加上 `-Ideps/cztest/include`，所以依赖方**一行 `-I` 都不用写**，
也不需要 `system_links`（对比 gtest 要 `-lgtest_main -lgtest`）。

---

## 7. 常见错误清单

| 现象 | 原因 | 怎么办 |
| --- | --- | --- |
| 链接时报 `undefined reference to WinMain` | Windows 上没有 `main` | 有一个文件写 `CZTEST_MAIN();` |
| 链接时报 `multiple definition of main` | 多个文件都写了 `CZTEST_MAIN()` | 只留一个 |
| 段错误，看不出哪条断言错了 | `EXPECT_NE(p, nullptr)` 之后解引用了 `p` | 改用 `ASSERT_NE` |
| 报 "Unexpected call to mocked function" | mock 被调用的方式与期望不符 | 检查参数匹配器和调用顺序 |
| 报 "was called N time(s). Use WillRepeatedly()" | 期望被调了多次 | 加 `.WillRepeatedly(...)` 或改 `.Times(n)` |
| 报 "was called more times than expected" | 调用次数超过了期望的上限 | 加 `.WillRepeatedly(...)` 或放宽 `.Times(n)` |
| 报 "Mock expectation failed" | 期望的次数没满足 | 检查 `.Times(...)` 与实际调用次数 |
| 报 "Duplicate test names" | 两个用例同名（多半在不同的文件里） | 改掉其中一个的名字 |
| `cztest::Values(a, b,)` 编译报错 | 函数调用不能带尾逗号 | 去掉尾逗号 |
| 宏里写了 `EXPECT_EQ(a, b) << "x"` 但 `else` 跑错了分支 | 不会发生：cztest 用 `if (init; cond)` 形式，用户自己的 `else` 仍然绑到用户的 `if` | — |
| 断言位置显示的是奇怪函数名 | 断言在 helper 里 | 正常，helper 名会显示出来 |

---

## 附：一份完整的项目骨架

```
myproject/
├── cizon.toml
├── include/
│   ├── cztest/               ← 框架（拷进来，或作为依赖）
│   └── app/                  ← 生产代码的头
├── src/                      ← 生产代码的实现
└── tests/
    ├── main.cpp              ← CZTEST_MAIN()
    ├── unit/                 ← 单元测试（mock 一切）
    └── integration/          ← 集成测试（真实依赖）
```
