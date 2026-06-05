#include "util.h"

#include "unit_test.h"

#include <string_view>

namespace {

void stringViewStartsWithUsesExactPrefix()
{
  using namespace std::literals;

  ASSERT_TRUE(dmtcp::Util::strStartsWith("checkpoint-image"sv,
                                         "checkpoint"sv));
  ASSERT_TRUE(!dmtcp::Util::strStartsWith("checkpoint-image"sv, "restart"sv));
  ASSERT_TRUE(dmtcp::Util::strStartsWith("checkpoint-image"sv, ""sv));
  ASSERT_TRUE(!dmtcp::Util::strStartsWith("ckpt"sv, "checkpoint"sv));
}

void stringViewEndsWithUsesExactSuffix()
{
  using namespace std::literals;

  ASSERT_TRUE(dmtcp::Util::strEndsWith("ckpt_worker.dmtcp"sv, ".dmtcp"sv));
  ASSERT_TRUE(!dmtcp::Util::strEndsWith("ckpt_worker.dmtcp"sv, ".gz"sv));
  ASSERT_TRUE(dmtcp::Util::strEndsWith("ckpt_worker.dmtcp"sv, ""sv));
  ASSERT_TRUE(!dmtcp::Util::strEndsWith("gz"sv, ".dmtcp"sv));
}

} // namespace

extern const dmtcp_test::TestCase utilTests[] = {
  {"string_view starts-with uses exact prefix",
   stringViewStartsWithUsesExactPrefix},
  {"string_view ends-with uses exact suffix",
   stringViewEndsWithUsesExactSuffix},
};

extern const size_t utilTestCount = sizeof(utilTests) / sizeof(utilTests[0]);
