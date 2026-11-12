# cztest

现代 C++ 的**纯头文件**单元测试框架，支持**函数级 mock**。只要把 `include` 目录加进搜索路径
（`-Iinclude`）即可使用，没有任何第三方依赖，也不需要链接额外的库。

* 头文件 only：`#include <cztest/cztest.hpp>` 即可
* 函数级 mock：逐个函数声明期望、匹配参数、校验调用次数
* C++17 起可用（已在 GCC 16 / Clang 22 上验证，C++17 与 C++20 均可）
* 自带测试运行器：过滤、列表、JUnit XML 报告、彩色输出
* 与 [cizon2](https://github.com/cizon-dev/cizon) 构建工具无缝配合（`-Iinclude` 由 cizon2 自动添加）

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

| 宏 | 说明 |
| --- | --- |
| `EXPECT_TRUE(x)` / `EXPECT_FALSE(x)` | 布尔判断 |
| `EXPECT_EQ(a,b)` `NE` `LT` `LE` `GT` `GE` | 比较（`ASSERT_` 版本在失败时立刻终止当前用例） |
| `EXPECT_STREQ(a,b)` / `EXPECT_STRNE(a,b)` | C 字符串比较（支持 `nullptr`） |
| `EXPECT_NEAR(a,b,eps)` | 浮点近似比较 |
| `EXPECT_THROW(stmt, Type)` / `NO_THROW` / `ANY_THROW` | 异常断言 |
| `ADD_FAILURE()` / `FAIL()` | 手动失败 / 致命失败 |

所有断言都可以追加自定义信息：

```cpp
EXPECT_EQ(result.size(), 3) << "expected three items, got " << result.size();
```

`EXPECT_*` 记录失败后继续执行，`ASSERT_*` 记录失败后终止当前用例。

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

匹配时**最后注册且能匹配的期望优先**（与 GoogleMock 一致）；当多个期望都能匹配时，
最近声明的那个生效，其余的期望仍然需要满足自己的次数约束。

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
name = "cizontest"
edition = "c++17"
compile_commands = true

[compiler]
flags = ["-Wall", "-Wextra", "-Wpedantic"]

[[target]]
name = "cztest_selftest"
kind = "test"
sources = ["tests"]
```

`kind = "test"` 的 target 只有 `cizon2 test` 会构建和运行，`cizon2 build` / `cizon2 run`
会完全忽略它。cizon2 会自动把项目的 `include` 目录加入头文件搜索路径，因此测试代码里
直接 `#include <cztest/cztest.hpp>` 即可，无需额外的 `-I` 参数。

```bash
cizon2 build                    # 只构建普通 target（不含测试）
cizon2 test                     # 构建并运行全部 kind = "test" 的 target
cizon2 test cztest_selftest      # 只跑指定的测试 target
cizon2 test -- --filter=Mock.*  # 透传参数给测试进程
```

`cizon2 test` 与测试框架无关，只认进程的退出码（0 = 通过），因此本框架无需任何改动即可接入。
测试进程自己的输出会被原样回放，cizon2 自己只补充 "Passed / Failed" 与汇总。

> 需要 cizon2 的 `kind = "test"` 支持（较新版本）。旧版本请用 `kind = "bin"` +
> `cizon2 run cztest_selftest`，效果等价。

---

## 已知限制

* `EXPECT_CALL(obj, Method(args))` 依赖 `##` 拼接 `obj.gmock_Method`，这是 GCC / Clang 的
  实际行为；MSVC 的传统预处理器可能需要改写为 `EXPECT_CALL(obj, Method, (args))` 形式。
* mock 函数的参数类型必须与接口**完全一致**（`const`、引用都要写全）。
* 无参数函数必须使用 `CZTEST_MOCK_NOARG_METHOD` 系列宏。
* 一次 `EXPECT_CALL` 最多 8 个参数。

---

## License

MIT
