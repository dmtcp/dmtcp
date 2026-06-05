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

void parseIntegerParsesStrictDecimalText()
{
  using namespace std::literals;

  int value = 0;
  ASSERT_TRUE(dmtcp::Util::parseInteger("123"sv, &value));
  ASSERT_EQ(value, 123);

  ASSERT_TRUE(dmtcp::Util::parseInteger("-42"sv, &value));
  ASSERT_EQ(value, -42);
}

void parseIntegerRejectsPartialEmptyAndOverflowText()
{
  using namespace std::literals;

  int value = 77;
  ASSERT_TRUE(!dmtcp::Util::parseInteger("123x"sv, &value));
  ASSERT_EQ(value, 77);

  ASSERT_TRUE(!dmtcp::Util::parseInteger(""sv, &value));
  ASSERT_EQ(value, 77);

  ASSERT_TRUE(!dmtcp::Util::parseInteger("999999999999999999999"sv, &value));
  ASSERT_EQ(value, 77);
}

} // namespace

extern const dmtcp_test::TestCase utilTests[] = {
  {"string_view starts-with uses exact prefix",
   stringViewStartsWithUsesExactPrefix},
  {"string_view ends-with uses exact suffix",
   stringViewEndsWithUsesExactSuffix},
  {"parseInteger parses strict decimal text",
   parseIntegerParsesStrictDecimalText},
  {"parseInteger rejects partial empty and overflow text",
   parseIntegerRejectsPartialEmptyAndOverflowText},
};

extern const size_t utilTestCount = sizeof(utilTests) / sizeof(utilTests[0]);
