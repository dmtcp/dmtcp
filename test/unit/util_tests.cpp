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

void parseIntegerPrefixParsesLeadingDecimalText()
{
  using namespace std::literals;

  int value = 0;
  size_t parsedLength = 0;
  ASSERT_TRUE(dmtcp::Util::parseIntegerPrefix("123/rest"sv,
                                              &value,
                                              &parsedLength));
  ASSERT_EQ(value, 123);
  ASSERT_EQ(parsedLength, static_cast<size_t>(3));

  ASSERT_TRUE(dmtcp::Util::parseIntegerPrefix("-42abc"sv,
                                              &value,
                                              &parsedLength));
  ASSERT_EQ(value, -42);
  ASSERT_EQ(parsedLength, static_cast<size_t>(3));
}

void parseIntegerPrefixRejectsMissingDigitsWithoutChangingOutput()
{
  using namespace std::literals;

  int value = 77;
  size_t parsedLength = 88;
  ASSERT_TRUE(!dmtcp::Util::parseIntegerPrefix("abc"sv,
                                               &value,
                                               &parsedLength));
  ASSERT_EQ(value, 77);
  ASSERT_EQ(parsedLength, static_cast<size_t>(88));

  ASSERT_TRUE(!dmtcp::Util::parseIntegerPrefix("-"sv,
                                               &value,
                                               &parsedLength));
  ASSERT_EQ(value, 77);
  ASSERT_EQ(parsedLength, static_cast<size_t>(88));

  ASSERT_TRUE(!dmtcp::Util::parseIntegerPrefix(""sv,
                                               &value,
                                               &parsedLength));
  ASSERT_EQ(value, 77);
  ASSERT_EQ(parsedLength, static_cast<size_t>(88));
}

void parsePortNumberAcceptsValidPortRange()
{
  using namespace std::literals;

  int port = -1;
  ASSERT_TRUE(dmtcp::Util::parsePortNumber("0"sv, &port));
  ASSERT_EQ(port, 0);

  ASSERT_TRUE(dmtcp::Util::parsePortNumber("65535"sv, &port));
  ASSERT_EQ(port, 65535);
}

void parsePortNumberRejectsInvalidPortText()
{
  using namespace std::literals;

  int port = 1234;
  ASSERT_TRUE(!dmtcp::Util::parsePortNumber("-1"sv, &port));
  ASSERT_EQ(port, 1234);

  ASSERT_TRUE(!dmtcp::Util::parsePortNumber("65536"sv, &port));
  ASSERT_EQ(port, 1234);

  ASSERT_TRUE(!dmtcp::Util::parsePortNumber("12x"sv, &port));
  ASSERT_EQ(port, 1234);
}

void parseNumericFlagAcceptsStrictDecimalZeroAndNonzero()
{
  using namespace std::literals;

  bool enabled = true;
  ASSERT_TRUE(dmtcp::Util::parseNumericFlag("0"sv, &enabled));
  ASSERT_TRUE(!enabled);

  ASSERT_TRUE(dmtcp::Util::parseNumericFlag("1"sv, &enabled));
  ASSERT_TRUE(enabled);

  ASSERT_TRUE(dmtcp::Util::parseNumericFlag("-1"sv, &enabled));
  ASSERT_TRUE(enabled);

  ASSERT_TRUE(dmtcp::Util::parseNumericFlag("2"sv, &enabled));
  ASSERT_TRUE(enabled);
}

void parseNumericFlagRejectsMalformedTextWithoutChangingOutput()
{
  using namespace std::literals;

  bool enabled = true;
  ASSERT_TRUE(!dmtcp::Util::parseNumericFlag("12x"sv, &enabled));
  ASSERT_TRUE(enabled);

  ASSERT_TRUE(!dmtcp::Util::parseNumericFlag(""sv, &enabled));
  ASSERT_TRUE(enabled);

  ASSERT_TRUE(!dmtcp::Util::parseNumericFlag("0x1"sv, &enabled));
  ASSERT_TRUE(enabled);

  ASSERT_TRUE(!dmtcp::Util::parseNumericFlag(" 1"sv, &enabled));
  ASSERT_TRUE(enabled);
}

void parseMeminfoKilobytesAcceptsFieldWithWhitespace()
{
  using namespace std::literals;

  unsigned long kilobytes = 0;
  ASSERT_TRUE(dmtcp::Util::parseMeminfoKilobytes(
    "MemTotal:       17123456 kB\nMemFree: 1234 kB\n"sv,
    "MemTotal"sv,
    &kilobytes));
  ASSERT_EQ(kilobytes, 17123456ul);

  ASSERT_TRUE(dmtcp::Util::parseMeminfoKilobytes(
    "MemTotal:\t42\n"sv,
    "MemTotal"sv,
    &kilobytes));
  ASSERT_EQ(kilobytes, 42ul);
}

void parseMeminfoKilobytesRejectsMalformedField()
{
  using namespace std::literals;

  unsigned long kilobytes = 99;
  ASSERT_TRUE(!dmtcp::Util::parseMeminfoKilobytes(
    "MemAvailable: 17123456 kB\n"sv,
    "MemTotal"sv,
    &kilobytes));
  ASSERT_EQ(kilobytes, 99ul);

  ASSERT_TRUE(!dmtcp::Util::parseMeminfoKilobytes(
    "MemTotalExtra: 17123456 kB\n"sv,
    "MemTotal"sv,
    &kilobytes));
  ASSERT_EQ(kilobytes, 99ul);

  ASSERT_TRUE(!dmtcp::Util::parseMeminfoKilobytes(
    "MemTotal: kB\n"sv,
    "MemTotal"sv,
    &kilobytes));
  ASSERT_EQ(kilobytes, 99ul);
}

void parseDottedVersionPrefixAcceptsMajorMinorAndSuffix()
{
  using namespace std::literals;

  int major = -1;
  int minor = -1;
  ASSERT_TRUE(dmtcp::Util::parseDottedVersionPrefix("2.39"sv,
                                                   &major,
                                                   &minor));
  ASSERT_EQ(major, 2);
  ASSERT_EQ(minor, 39);

  ASSERT_TRUE(dmtcp::Util::parseDottedVersionPrefix("2.39-ubuntu"sv,
                                                   &major,
                                                   &minor));
  ASSERT_EQ(major, 2);
  ASSERT_EQ(minor, 39);

  ASSERT_TRUE(dmtcp::Util::parseDottedVersionPrefix("2.39.1"sv,
                                                   &major,
                                                   &minor));
  ASSERT_EQ(major, 2);
  ASSERT_EQ(minor, 39);
}

void parseDottedVersionPrefixRejectsMalformedPrefixes()
{
  using namespace std::literals;

  int major = 7;
  int minor = 8;
  ASSERT_TRUE(!dmtcp::Util::parseDottedVersionPrefix("2"sv, &major, &minor));
  ASSERT_EQ(major, 7);
  ASSERT_EQ(minor, 8);

  ASSERT_TRUE(!dmtcp::Util::parseDottedVersionPrefix("2."sv, &major, &minor));
  ASSERT_EQ(major, 7);
  ASSERT_EQ(minor, 8);

  ASSERT_TRUE(!dmtcp::Util::parseDottedVersionPrefix(".39"sv, &major, &minor));
  ASSERT_EQ(major, 7);
  ASSERT_EQ(minor, 8);

  ASSERT_TRUE(!dmtcp::Util::parseDottedVersionPrefix("x.39"sv,
                                                    &major,
                                                    &minor));
  ASSERT_EQ(major, 7);
  ASSERT_EQ(minor, 8);
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
  {"parseIntegerPrefix parses leading decimal text",
   parseIntegerPrefixParsesLeadingDecimalText},
  {"parseIntegerPrefix rejects missing digits without changing output",
   parseIntegerPrefixRejectsMissingDigitsWithoutChangingOutput},
  {"parsePortNumber accepts valid port range",
   parsePortNumberAcceptsValidPortRange},
  {"parsePortNumber rejects invalid port text",
   parsePortNumberRejectsInvalidPortText},
  {"parseNumericFlag accepts strict decimal zero and nonzero",
   parseNumericFlagAcceptsStrictDecimalZeroAndNonzero},
  {"parseNumericFlag rejects malformed text without changing output",
   parseNumericFlagRejectsMalformedTextWithoutChangingOutput},
  {"parseMeminfoKilobytes accepts field with whitespace",
   parseMeminfoKilobytesAcceptsFieldWithWhitespace},
  {"parseMeminfoKilobytes rejects malformed field",
   parseMeminfoKilobytesRejectsMalformedField},
  {"parseDottedVersionPrefix accepts major minor and suffix",
   parseDottedVersionPrefixAcceptsMajorMinorAndSuffix},
  {"parseDottedVersionPrefix rejects malformed prefixes",
   parseDottedVersionPrefixRejectsMalformedPrefixes},
};

extern const size_t utilTestCount = sizeof(utilTests) / sizeof(utilTests[0]);
