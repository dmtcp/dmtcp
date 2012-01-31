#ifndef REMOTE_EXEC_WRAPPERS_H
#define REMOTE_EXEC_WRAPPERS_H

#include "dmtcpalloc.h"

void processSshCommand(dmtcp::string programName,
                              dmtcp::vector<dmtcp::string>& args);

#endif