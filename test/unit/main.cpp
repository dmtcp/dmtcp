#include "unit_test.h"

#include <array>
#include <cstddef>
#include <span>

extern const dmtcp_test::TestCase dmtcpHeaderTests[];
extern const size_t dmtcpHeaderTestCount;
extern const dmtcp_test::TestCase utilAssertTests[];
extern const size_t utilAssertTestCount;
extern const dmtcp_test::TestCase utilTests[];
extern const size_t utilTestCount;
extern const dmtcp_test::TestCase sigInfoTests[];
extern const size_t sigInfoTestCount;
extern const dmtcp_test::TestCase threadInfoTests[];
extern const size_t threadInfoTestCount;
extern const dmtcp_test::TestCase virtualIdTableTests[];
extern const size_t virtualIdTableTestCount;

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
    std::span<const dmtcp_test::TestCase>(utilTests, utilTestCount));
  status |= dmtcp_test::runTests(
    std::span<const dmtcp_test::TestCase>(sigInfoTests, sigInfoTestCount));
  status |= dmtcp_test::runTests(
    std::span<const dmtcp_test::TestCase>(threadInfoTests,
                                          threadInfoTestCount));
  status |= dmtcp_test::runTests(
    std::span<const dmtcp_test::TestCase>(virtualIdTableTests,
                                          virtualIdTableTestCount));
  return status;
}
