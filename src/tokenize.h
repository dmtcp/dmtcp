#ifndef __DMTCP_TOKENIZE_CPP__
#define __DMTCP_TOKENIZE_CPP__

#include <string>
#include <string_view>
#include <vector>

#include "dmtcpalloc.h"

namespace dmtcp
{
vector<string> tokenizeString(std::string_view s,
                              std::string_view delims,
                              bool allowEmptyTokens = false);
} // namespace dmtcp
#endif // #ifndef __DMTCP_TOKENIZE_CPP__
