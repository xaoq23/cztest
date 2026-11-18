# 从 GoogleTest 迁移到 cztest

这份文档不是凭印象写的：文中的每个结论都来自一次真实的编译 —— 把一份 GoogleTest 风格的
测试文件直接丢给 cztest 编译，逐轮记录错误并归类。

---

## 一句话结论

| 层 | 迁移成本 |
| --- | --- |
| 断言层（`EXPECT_*` / `ASSERT_*`） | **几乎为零**：名字完全一致，改一行 include 就能编 |
| 夹具（`TEST_F`） | 每个文件改 1 处基类名 |
| mock（gmock） | 每个 mock 类 +1 个基类，每个 mock 方法改 1 行宏 |
| gmock 的高级特性 | **没有对应物**，需要改写或放弃（清单见最后一节） |

实测：一份 130 行、9 个用例的 gtest 风格文件，**4 轮编辑后编译通过**，其中 8 个用例
不需要任何改动。

---

## 逐轮实测记录

### 第 0 轮：原样编译

```
error: gtest/gtest.h: No such file or directory
```

### 第 1 轮：改 include

```diff
-#include <gtest/gtest.h>
+#include <cztest/cztest.hpp>
```

**46 个错误**，归类：

| 数量 | 类别 |
| --- | --- |
| 11 | `::testing` 未声明（命名空间） |
| 9 | `MOCK_METHOD` 语法不同 + 抽象类 |
| 4 | fixture 基类 `::testing::Test` 不认识 |
| 4 | `ON_CALL` / `SCOPED_TRACE` / `GTEST_SKIP` / `EXPECT_THAT`：无对应物或 gtest 特有写法 |

### 第 2 轮：改命名空间与 mock 宏

```diff
-class ParserTest : public ::testing::Test {
+class ParserTest : public cztest::Test {

-    MOCK_METHOD(int, Get, (int), (override));
-    MOCK_METHOD(void, Log, (const std::string&), (override));
+    CZTEST_MOCK_METHOD(int, Get, int);
+    CZTEST_MOCK_METHOD(void, Log, const std::string&);

-    EXPECT_CALL(mock, Get(42)).WillOnce(::testing::Return(7));
+    EXPECT_CALL(mock, Get(42)).WillOnce(cztest::Return(7));
-    EXPECT_CALL(mock, Get(::testing::_)).WillRepeatedly(::testing::Invoke(...));
+    EXPECT_CALL(mock, Get(cztest::Any())).WillRepeatedly(cztest::Invoke(...));
```

**18 个错误**。剩下的都是"没有对应物"的特性，以及一个关键发现：

> gmock 的 `MOCK_METHOD` **不需要基类** —— 它的机制在全局（`InitGoogleMock` 管理）。
> cztest 的 mock 类必须继承 `cztest::MockBase`，否则 mock 类是抽象类。

### 第 3 轮：给 mock 类加基类

```diff
-class MockService : public Service {
+class MockService : public Service, public cztest::MockBase {
```

**13 个错误**，来自 cztest 没有对应物或写法不同的 gtest 特性：
`EXPECT_THAT` / `GTEST_SKIP` / `ON_CALL` / `SCOPED_TRACE` / `EXPECT_DEATH` / `TYPED_TEST`。

### 第 4 轮：处理这几个特性

删掉或改写它们之后：**编译通过，8 个用例直接通过**（第 9 个是故意写的失败用例）

---

## 机械替换清单

一次全局替换就能解决大半：

```bash
# 命名空间
sed -i 's/::testing::/cztest::/g' tests/*.cpp

# mock 方法声明
#   MOCK_METHOD(ret, Name, (types...), (override));
# → CZTEST_MOCK_METHOD(ret, Name, types...);
sed -i 's/MOCK_METHOD(\(.*\), (\(.*\)), (override));/CZTEST_MOCK_METHOD(\1, \2);/' tests/*.cpp

# fixture 基类
sed -i 's/public ::testing::Test/public cztest::Test/' tests/*.cpp
```

**不能全局替换的**：

| gtest | cztest | 为什么不能自动 |
| --- | --- | --- |
| `::testing::_` | `cztest::Any()` | `_` 太短，会误伤 |
| `MOCK_METHOD` 的 `(override)` 等 spec | 无 | 需要人判断 |
| `ON_CALL(mock, M(x)).WillByDefault(...)` | `EXPECT_CALL(mock, M(x)).WillRepeatedly(...)` | 语义要人工对齐 |

---

## 入口点

| gtest | cztest |
| --- | --- |
| `::testing::InitGoogleTest(&argc, argv);` + `RUN_ALL_TESTS();` | `CZTEST_MAIN();` |

cztest 的一行宏会自己解析 `--filter` / `--list` / `--junit` 等参数。

---

## 语义差异（迁移前要知道）

这些不是编译错误，但会让迁移后的测试**行为不同**：

| 方面 | gtest / gmock | cztest |
| --- | --- | --- |
| 未匹配到期望的调用 | 默认是**警告**（nice mock），不失败 | **失败**（默认就是 strict） |
| 期望被调用多次但没设 `WillRepeatedly` | 警告 | **失败** |
| `ASSERT_*` 的中止方式 | `return`（在 helper 函数里会失效） | 抛异常（helper 里也正确中止） |
| 期望的默认次数 | 任意次数 | 任意次数（相同） |
| 多个期望同时匹配 | 最近声明的优先 | 最近声明的优先（相同） |
| 过滤语法 | `pos1:pos2:-neg`（冒号分隔，`-` 排除） | 同样的语法（相同） |

过滤器的语法和 gtest 一样 —— `--gtest_filter=Mock.*:-Mock.Slow` 改成
`--filter=Mock.*:-Mock.Slow` 就能用。

**迁移后可能突然多出一些失败** —— 那是 gmock 原本在警告、cztest 在报错的地方
通常说明原来的测试写得不够明确（"这个依赖到底被调用了几次？"）。

---

## 没有对应物的 gtest 特性

cztest 对这些写法给出的是**教学式报错**（见 `include/cztest/compat.hpp`）：报错里直接写清楚
cztest 里该怎么做，而不是一句 "not declared in this scope"。

| gtest 特性 | 报错里给的替代方案 |
| --- | --- |
| `EXPECT_THAT(v, AllOf(Ge(1), Le(100)))` | **已经有 `EXPECT_THAT`**，但匹配器是 cztest 自己的（`cztest::AllOf` 等）；gmock 的匹配器类型不支持 |
| `EXPECT_THAT(v, ElementsAre(...))` | **已经有** `cztest::ElementsAre(...)` |
| `ON_CALL(...).WillByDefault(...)` | `EXPECT_CALL(...).WillRepeatedly(...)`；每次调用不同答案就写多个 `EXPECT_CALL` |
| `EXPECT_DEATH` | 让函数返回可断言的结果，直接测错误路径 |
| `TYPED_TEST` | `TEST_P` + `cztest::Values` |
| `MOCK_METHOD(ret, Name, (types), (override))` | `CZTEST_MOCK_METHOD(ret, Name, types...)`（第三参数不带外层括号） |
| `NiceMock<T>` / `StrictMock<T>` | 不需要：cztest 默认就是 strict |
| `MockFunction`（mock 自由函数） | 不支持；改成依赖注入（把函数做成接口成员） |
| `::testing::RecordProperty` | 不支持 |
| `testing::internal::CaptureStdout` | 不支持；把输出做成返回值更好测 |

**已经有的**：`SCOPED_TRACE`（同名）、`SKIP()`（对应 `GTEST_SKIP()`）、`EXPECT_THAT`（同名）、
`EXPECT_OK`（对应 `std::expected`）、`TEST_P`（同名）。

### 匹配器的对应关系

| gmock | cztest |
| --- | --- |
| `Eq(v)` `Ne(v)` `Lt(v)` `Le(v)` `Gt(v)` `Ge(v)` | 同名 |
| `_` | `cztest::Any()` |
| `IsNull()` `NotNull()` | 同名 |
| `StrEq(s)` `HasSubstr(s)` `StartsWith(s)` `EndsWith(s)` | 同名 |
| `Truly(f)` | 同名 |
| `Not(m)` | 同名 |
| `AllOf(...)` `AnyOf(...)` | 同名 |
| `ElementsAre(...)` | 同名 |
| `Pair(a, b)` | `cztest::Truly` 或逐字段断言 |
| `Key(a, b)` `Field(...)` | 无对应物 |

---

## 迁移检查表

- [ ] 换 include（`gtest/gtest.h` → `cztest/cztest.hpp`）
- [ ] `::testing::` → `cztest::`
- [ ] fixture 基类 `::testing::Test` → `cztest::Test`
- [ ] `MOCK_METHOD(...)` → `CZTEST_MOCK_METHOD(...)`，参数类型去掉一层括号
- [ ] mock 类加 `public cztest::MockBase`
- [ ] `::testing::_` → `cztest::Any()`
- [ ] 入口点换成 `CZTEST_MAIN()`
- [ ] 处理最后一节列出的特性
- [ ] 跑一遍，确认没有"突然多出来的失败"，逐条确认那是真实问题
