#include "unit_test.h"

#include <array>
#include <cstddef>
#include <span>

extern const dmtcp_test::TestCase dmtcpHeaderTests[];
extern const size_t dmtcpHeaderTestCount;

int
main()
{
  return dmtcp_test::runTests(
    std::span<const dmtcp_test::TestCase>(dmtcpHeaderTests,
                                          dmtcpHeaderTestCount));
}
