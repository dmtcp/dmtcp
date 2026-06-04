#include "unit_test.h"

#include <array>
#include <cstddef>
#include <span>

extern const dmtcp_test::TestCase dmtcpHeaderTests[];
extern const size_t dmtcpHeaderTestCount;
extern const dmtcp_test::TestCase utilAssertTests[];
extern const size_t utilAssertTestCount;
extern const dmtcp_test::TestCase threadInfoTests[];
extern const size_t threadInfoTestCount;

int
main()
{
  int status = dmtcp_test::runTests(
    std::span<const dmtcp_test::TestCase>(dmtcpHeaderTests,
                                          dmtcpHeaderTestCount));
  status |= dmtcp_test::runTests(
    std::span<const dmtcp_test::TestCase>(utilAssertTests,
                                          utilAssertTestCount));
  status |= dmtcp_test::runTests(
    std::span<const dmtcp_test::TestCase>(threadInfoTests,
                                          threadInfoTestCount));
  return status;
}
