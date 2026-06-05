#include "unit_test.h"

#include "siginfo.h"

#include <csignal>
#include <cstdio>

namespace {

void parseCkptSignalAcceptsValidSignal()
{
  int signal = -1;

  ASSERT_TRUE(dmtcp::SigInfo::parseCkptSignalText("1", &signal));
  ASSERT_EQ(signal, 1);

  int highestUsableSignal = SIGRTMAX - 1;
  char text[16];
  snprintf(text, sizeof(text), "%d", highestUsableSignal);

  ASSERT_TRUE(dmtcp::SigInfo::parseCkptSignalText(text, &signal));
  ASSERT_EQ(signal, highestUsableSignal);
}

void parseCkptSignalRejectsInvalidText()
{
  int signal = 77;

  ASSERT_TRUE(!dmtcp::SigInfo::parseCkptSignalText("12x", &signal));
  ASSERT_EQ(signal, 77);

  ASSERT_TRUE(!dmtcp::SigInfo::parseCkptSignalText("", &signal));
  ASSERT_EQ(signal, 77);

  ASSERT_TRUE(!dmtcp::SigInfo::parseCkptSignalText(nullptr, &signal));
  ASSERT_EQ(signal, 77);
}

void parseCkptSignalRejectsOutOfRangeSignals()
{
  int signal = 77;
  char text[16];
  snprintf(text, sizeof(text), "%d", SIGRTMAX);

  ASSERT_TRUE(!dmtcp::SigInfo::parseCkptSignalText("0", &signal));
  ASSERT_EQ(signal, 77);

  ASSERT_TRUE(!dmtcp::SigInfo::parseCkptSignalText("-1", &signal));
  ASSERT_EQ(signal, 77);

  ASSERT_TRUE(!dmtcp::SigInfo::parseCkptSignalText(text, &signal));
  ASSERT_EQ(signal, 77);
}

} // namespace

extern const dmtcp_test::TestCase sigInfoTests[] = {
  {"checkpoint signal parser accepts valid signal",
   parseCkptSignalAcceptsValidSignal},
  {"checkpoint signal parser rejects invalid text",
   parseCkptSignalRejectsInvalidText},
  {"checkpoint signal parser rejects out-of-range signals",
   parseCkptSignalRejectsOutOfRangeSignals},
};

extern const size_t sigInfoTestCount =
  sizeof(sigInfoTests) / sizeof(sigInfoTests[0]);
