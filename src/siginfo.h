#ifndef SIGINFO_H
#define SIGINFO_H

#include <signal.h>
#include <string_view>

#include "util.h"

namespace dmtcp
{
namespace SigInfo
{
inline bool
parseCkptSignalText(const char *text, int *signal)
{
  if (text == nullptr || signal == nullptr) {
    return false;
  }

  int parsedSignal = 0;
  if (!Util::parseInteger(std::string_view(text), &parsedSignal) ||
      parsedSignal < 1 ||
      parsedSignal >= SIGRTMAX) {
    return false;
  }

  *signal = parsedSignal;
  return true;
}

int ckptSignal();
void setupCkptSigHandler(sighandler_t handler);
void saveSigHandlers();
void restoreSigHandlers();
}
}
#endif // ifndef SIGINFO_H
