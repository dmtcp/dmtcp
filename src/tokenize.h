#ifndef __DMTCP_TOKENIZE_CPP__
#define __DMTCP_TOKENIZE_CPP__

#include <string>
#include <vector>

#include "dmtcpalloc.h"

namespace dmtcp
{
vector<string> tokenizeString(const string &s,
                              const string &delims,
                              bool allowEmptyTokens = false);
} // namespace dmtcp
#endif // #ifndef __DMTCP_TOKENIZE_CPP__
