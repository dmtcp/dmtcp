#include "unit_test.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

extern const dmtcp_test::TestCase dmtcpHeaderTests[];
extern const size_t dmtcpHeaderTestCount;
extern const dmtcp_test::TestCase dmtcpMessageTests[];
extern const size_t dmtcpMessageTestCount;

int
main()
{
  std::vector<dmtcp_test::TestCase> tests;
  tests.insert(tests.end(), dmtcpHeaderTests,
               dmtcpHeaderTests + dmtcpHeaderTestCount);
  tests.insert(tests.end(), dmtcpMessageTests,
               dmtcpMessageTests + dmtcpMessageTestCount);

  return dmtcp_test::runTests(std::span<const dmtcp_test::TestCase>(tests));
}
