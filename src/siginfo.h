#ifndef SIGINFO_H
#define SIGINFO_H

#include <signal.h>

namespace dmtcp
{
namespace SigInfo
{
int ckptSignal();
void setupCkptSigHandler(sighandler_t handler);
void saveSigHandlers();
void restoreSigHandlers();
}
}
#endif // ifndef SIGINFO_H
