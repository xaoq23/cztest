#!/usr/bin/env python3
"""Measures the cost of the test frameworks: compile time, binary size, run time.

The numbers answer a simple question: what does a test framework cost when a
project has many tests?  Three frameworks are measured with the same tests:

  * cztest   - the framework of this repository
  * gtest    - GoogleTest with GoogleMock (the one everybody compares to)
  * doctest  - the light header only competitor

Every framework gets the same tests:

  * "plain"   - one empty test per case
  * "mocks"   - one test per case, each with a mocked method and an
                expectation (doctest has no mocking, so it is left out)

The measurement is the wall clock of the compiler for the single translation
unit that holds all the tests, the size of the linked binary and the wall
clock of the test run.

Usage:
  python benchmarks/run.py                  # 1000 cases, 3 runs
  python benchmarks/run.py --cases 200 --runs 5
  python benchmarks/run.py --compiler clang++

The frameworks of the others are fetched into benchmarks/.deps (nothing is
installed on the machine) and are built there.
"""

from __future__ import annotations

import argparse
import os
import shutil
import statistics
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
# The dependencies are built per compiler: the libraries of one compiler do
# not link against another one.
DEPS = HERE / ".deps"
WORK = HERE / ".work"

GTEST_VERSION = "v1.15.2"
DOCTEST_VERSION = "2.4.11"

# ---------------------------------------------------------------------------
# The tests that every framework gets.
# ---------------------------------------------------------------------------

PLAIN_TESTS = {
    "cztest": """
#include <cztest/cztest.hpp>

{tests}

CZTEST_MAIN()
""",
    "gtest": """
#include <gtest/gtest.h>

{tests}
""",
    "doctest": """
#include <doctest/doctest.h>

{tests}
""",
}

MOCK_TESTS = {
    "cztest": """
#include <cztest/cztest.hpp>

struct IStore {{
  virtual ~IStore() = default;
  virtual int Get(int key) = 0;
}};

class MockStore : public IStore, public cztest::MockBase {{
 public:
  CZTEST_MOCK_INTERFACE(IStore);
  CZTEST_MOCK_METHOD(int, Get, int);
}};
CZTEST_MOCK_COMPLETE(MockStore);

{tests}

CZTEST_MAIN()
""",
    "gtest": """
#include <gmock/gmock.h>

struct IStore {{
  virtual ~IStore() = default;
  virtual int Get(int key) = 0;
}};

class MockStore : public IStore {{
 public:
  MOCK_METHOD(int, Get, (int), (override));
}};

{tests}
""",
}


def plain_cases(framework: str, cases: int) -> str:
    if framework == "doctest":
        return "\n".join(
            f'TEST_CASE("Suite.Case{n}") {{ CHECK_EQ(1, 1); }}' for n in range(cases)
        )
    return "\n".join(
        f"TEST(Suite, Case{n}) {{ EXPECT_EQ(1, 1); }}" for n in range(cases)
    )


def mock_cases(framework: str, cases: int) -> str:
    if framework == "gtest":
        return "\n".join(
            f"""TEST(Mocks, Case{n}) {{
  MockStore mock;
  EXPECT_CALL(mock, Get(1)).Times(1).WillOnce(::testing::Return(7));
  EXPECT_EQ(mock.Get(1), 7);
}}"""
            for n in range(cases)
        )
    return "\n".join(
        f"""TEST(Mocks, Case{n}) {{
  MockStore mock;
  EXPECT_CALL(mock, Get(1)).Times(1).WillOnce(cztest::Return(7));
  EXPECT_EQ(mock.Get(1), 7);
}}"""
        for n in range(cases)
    )


# ---------------------------------------------------------------------------
# The other frameworks.
# ---------------------------------------------------------------------------

def fetch_gtest(compiler: str) -> Path:
    """Fetches GoogleTest and builds it.  Returns the directory of the libs.

    The libraries are built directly from the sources of GoogleTest, so that
    they use the same compiler as the tests.
    """
    target = DEPS / compiler.replace("+", "p") / "googletest"
    if not target.exists():
        print("  fetching googletest ...")
        subprocess.run(
            ["git", "clone", "--depth", "1", "--branch", GTEST_VERSION,
             "https://github.com/google/googletest.git", str(target)],
            check=True, stdout=subprocess.DEVNULL,
        )

    libs = target / "lib"
    if not (libs / "libgmock.a").exists():
        print("  building googletest ...")
        libs.mkdir(parents=True, exist_ok=True)
        gtest = target / "googletest"
        gmock = target / "googlemock"
        common = ["-std=c++17", "-O2",
                  f"-I{gtest / 'include'}", f"-I{gtest}",
                  f"-I{gmock / 'include'}", f"-I{gmock}"]
        sources = {
            "libgtest.a": [gtest / "src" / "gtest-all.cc"],
            "libgtest_main.a": [gtest / "src" / "gtest_main.cc"],
            "libgmock.a": [gmock / "src" / "gmock-all.cc"],
        }
        for name, files in sources.items():
            objects = []
            for source in files:
                obj = libs / (source.stem + ".o")
                run_timed([compiler, *common, "-c", str(source), "-o", str(obj)])
                objects.append(str(obj))
            run_timed(["ar", "rcs", str(libs / name), *objects])
    return libs


def fetch_doctest() -> Path:
    """Fetches the single header of doctest.  Returns the include directory."""
    target = DEPS / "doctest"
    header = target / "doctest" / "doctest.h"
    if not header.exists():
        print("  fetching doctest ...")
        header.parent.mkdir(parents=True, exist_ok=True)
        url = (f"https://github.com/doctest/doctest/releases/download/"
               f"v{DOCTEST_VERSION}/doctest.h")
        with urllib.request.urlopen(url) as response:
            header.write_bytes(response.read())
    return target


# ---------------------------------------------------------------------------
# The measurement.
# ---------------------------------------------------------------------------

class Result:
    def __init__(self, name: str, kind: str):
        self.name = name
        self.kind = kind
        self.compile_seconds = 0.0
        self.size_kb = 0
        self.run_ms = 0.0


def run_timed(command: list[str]) -> float:
    start = time.perf_counter()
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    return time.perf_counter() - start


def measure(framework: str, kind: str, cases: int, runs: int, compiler: str,
            gtest_lib: Path | None, doctest_inc: Path | None) -> Result | None:
    """Builds one variant and measures it."""
    result = Result(f"{framework} / {kind}", kind)

    work = WORK / framework / kind
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    if kind == "plain":
        source = PLAIN_TESTS[framework].format(tests=plain_cases(framework, cases))
    else:
        source = MOCK_TESTS[framework].format(tests=mock_cases(framework, cases))

    main = work / "main.cpp"
    obj = work / "tests.o"
    binary = work / "tests.exe" if os.name == "nt" else work / "tests"
    main.write_text(source)

    includes: list[str] = []
    link: list[str] = []

    if framework == "cztest":
        includes = ["-I", str(ROOT / "include")]
    elif framework == "gtest":
        base = DEPS / compiler.replace("+", "p") / "googletest"
        includes = ["-I", str(base / "googletest" / "include"),
                    "-I", str(base / "googlemock" / "include")]
        link = [f"-L{gtest_lib}", "-lgmock", "-lgtest", "-lgtest_main"]
    elif framework == "doctest":
        includes = ["-I", str(doctest_inc)]

    standard = "-std=c++20" if framework == "cztest" else "-std=c++17"

    # The compile of the translation unit that holds the tests is the number
    # that matters: it is the part that grows with the number of tests.
    compile_command = [compiler, standard, "-O2", *includes, "-c", str(main), "-o", str(obj)]
    times = [run_timed(compile_command) for _ in range(runs)]
    result.compile_seconds = statistics.median(times)

    link_command = [compiler, str(obj), *includes, *link, "-o", str(binary)]
    if framework == "doctest":
        # doctest needs its main in a separate file.
        impl = work / "impl.cpp"
        impl.write_text('#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include <doctest/doctest.h>\n')
        link_command = [compiler, str(obj), str(impl), *includes, *link, "-o", str(binary)]
    run_timed(link_command)

    result.size_kb = binary.stat().st_size // 1024

    # The run is measured a few times as well: the start of the process
    # costs about as much as the tests themselves.
    times = []
    for _ in range(runs):
        start = time.perf_counter()
        subprocess.run([str(binary)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        times.append((time.perf_counter() - start) * 1000.0)
    result.run_ms = statistics.median(times)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases", type=int, default=1000,
                        help="how many tests every variant gets (default 1000)")
    parser.add_argument("--runs", type=int, default=3,
                        help="how often the compiler is run (default 3, the median is used)")
    parser.add_argument("--compiler", default="g++", help="the compiler to use")
    parser.add_argument("--keep", action="store_true",
                        help="keep the generated files")
    args = parser.parse_args()

    if shutil.which(args.compiler) is None:
        print(f"error: the compiler '{args.compiler}' was not found")
        return 1

    DEPS.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)

    print(f"cases: {args.cases}, runs: {args.runs}, compiler: {args.compiler}")
    gtest_lib = fetch_gtest(args.compiler)
    doctest_inc = fetch_doctest()

    results: list[Result] = []
    for framework in ("cztest", "gtest", "doctest"):
        results.append(measure(framework, "plain", args.cases, args.runs,
                                args.compiler, gtest_lib, doctest_inc))
        if framework != "doctest":
            results.append(measure(framework, "mocks", args.cases, args.runs,
                                    args.compiler, gtest_lib, doctest_inc))

    print()
    print("| variant | compile time | binary size | run time |")
    print("| --- | --- | --- | --- |")
    for result in results:
        print(f"| {result.name} | {result.compile_seconds:.2f} s | "
              f"{result.size_kb} kB | {result.run_ms:.0f} ms |")

    if not args.keep:
        shutil.rmtree(WORK, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
