// cztest - a modern, header-only C++ test framework with function level mocks.
//
// Usage:
//   #include <cztest/cztest.hpp>
//
//   TEST(Suite, Name) {
//     EXPECT_EQ(1 + 1, 2);
//   }
//
// Compile with the include directory on the search path, e.g.
//   clang++ -std=c++17 -Iinclude tests/*.cpp -o tests
//
// Entry point:
//   * On ELF/Mach-O targets the framework provides a weak main(), so including
//     the header is all that is needed.
//   * On Windows (COFF) add CZTEST_MAIN() to exactly one translation unit:
//
//       #include <cztest/cztest.hpp>
//       CZTEST_MAIN()
//
//     or define CZTEST_IMPLEMENT_MAIN before including the header.
//
// License: MIT
#ifndef CZTEST_CZTEST_HPP
#define CZTEST_CZTEST_HPP

#include <cstdlib>
#include <iostream>

#include "cztest/core.hpp"
#include "cztest/matchers.hpp"
#include "cztest/mock.hpp"

// Explicit main: forwards the command line to the runner.
#define CZTEST_MAIN()                        \
  int main(int argc, char** argv) {         \
    return ::cztest::RunAllTests(argc, argv); \
  }

#if defined(CZTEST_IMPLEMENT_MAIN)

int main(int argc, char** argv) { return ::cztest::RunAllTests(argc, argv); }

#elif !defined(CZTEST_NO_MAIN) && (defined(__ELF__) || defined(__APPLE__) || defined(__MACH__))

// A weak symbol: when several translation units include this header the
// linker keeps exactly one definition.
__attribute__((weak)) int main(int argc, char** argv) {
  return ::cztest::RunAllTests(argc, argv);
}

#endif

#endif  // CZTEST_CZTEST_HPP
