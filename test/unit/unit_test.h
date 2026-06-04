#ifndef DMTCP_TEST_UNIT_TEST_H
#define DMTCP_TEST_UNIT_TEST_H

#include <exception>
#include <iostream>
#include <sstream>
#include <span>
#include <string>

namespace dmtcp_test {

struct TestCase {
  const char *name;
  void (*fn)();
};

class TestFailure : public std::exception {
 public:
  explicit TestFailure(std::string message) : message_(std::move(message)) {}

  const char *what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

inline std::string location(const char *file, int line)
{
  std::ostringstream out;
  out << file << ':' << line << ": ";
  return out.str();
}

template <typename Lhs, typename Rhs>
void assertEqual(const Lhs& lhs,
                 const Rhs& rhs,
                 const char *lhsExpr,
                 const char *rhsExpr,
                 const char *file,
                 int line)
{
  if (lhs == rhs) {
    return;
  }

  std::ostringstream out;
  out << location(file, line)
      << "expected " << lhsExpr << " == " << rhsExpr
      << ", got " << lhs << " and " << rhs;
  throw TestFailure(out.str());
}

inline void assertTrue(bool value,
                       const char *expr,
                       const char *file,
                       int line)
{
  if (value) {
    return;
  }

  throw TestFailure(location(file, line) + "expected true: " + expr);
}

inline int runTests(std::span<const TestCase> tests)
{
  int failures = 0;

  for (const TestCase& test : tests) {
    try {
      test.fn();
      std::cout << "PASS " << test.name << '\n';
    } catch (const std::exception& ex) {
      ++failures;
      std::cerr << "FAIL " << test.name << ": " << ex.what() << '\n';
    } catch (...) {
      ++failures;
      std::cerr << "FAIL " << test.name << ": unknown exception\n";
    }
  }

  std::cout << tests.size() - failures << " of " << tests.size()
            << " unit tests passed\n";
  return failures == 0 ? 0 : 1;
}

} // namespace dmtcp_test

#define ASSERT_TRUE(expr) \
  ::dmtcp_test::assertTrue((expr), #expr, __FILE__, __LINE__)

#define ASSERT_EQ(lhs, rhs) \
  ::dmtcp_test::assertEqual((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)

#endif // DMTCP_TEST_UNIT_TEST_H
