#ifndef SIGINFO_H
#define SIGINFO_H

namespace dmtcp {
  namespace SigInfo {
    int ckptSignal();
    void setupCkptSigHandler(sighandler_t handler);
    void saveSigHandlers();
    void restoreSigHandlers();
  };
};

#endif
