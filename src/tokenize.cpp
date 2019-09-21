#include "tokenize.h"

namespace dmtcp
{
// Tokenizes the string using the delimiters.
// Empty tokens will not be included in the result.

vector<string>
tokenizeString(const string &s, const string &delims, bool allowEmptyTokens)
{
  size_t offset = -1;

  vector<string>tokens;

  do {
    offset += 1;
    size_t j = s.find_first_of(delims, offset);

    string token = s.substr(offset, j - offset);

    if (allowEmptyTokens || token.length() > 0) {
      tokens.push_back(token);
    }

    offset = j;
  } while (offset != string::npos);

  return tokens;
}
} // namespace dmtcp
