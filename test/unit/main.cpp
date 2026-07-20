#include "unit_test.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

extern const dmtcp_test::TestCase dmtcpHeaderTests[];
extern const size_t dmtcpHeaderTestCount;
extern const dmtcp_test::TestCase dmtcpMessageTests[];
extern const size_t dmtcpMessageTestCount;
extern const dmtcp_test::TestCase utilAssertTests[];
extern const size_t utilAssertTestCount;
extern const dmtcp_test::TestCase virtualIdTableTests[];
extern const size_t virtualIdTableTestCount;
extern const dmtcp_test::TestCase pagemapScanTests[];
extern const size_t pagemapScanTestCount;

int
main()
{
  std::vector<dmtcp_test::TestCase> tests;
  tests.insert(tests.end(), dmtcpHeaderTests,
               dmtcpHeaderTests + dmtcpHeaderTestCount);
  tests.insert(tests.end(), dmtcpMessageTests,
               dmtcpMessageTests + dmtcpMessageTestCount);
  tests.insert(tests.end(), utilAssertTests,
               utilAssertTests + utilAssertTestCount);
  tests.insert(tests.end(), virtualIdTableTests,
               virtualIdTableTests + virtualIdTableTestCount);
  tests.insert(tests.end(), pagemapScanTests,
               pagemapScanTests + pagemapScanTestCount);

  return dmtcp_test::runTests(std::span<const dmtcp_test::TestCase>(tests));
}
