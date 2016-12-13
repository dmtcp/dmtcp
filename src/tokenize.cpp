#include "tokenize.h"

namespace dmtcp
{
// Tokenizes the string using the delimiters.
// Empty tokens will not be included in the result.

vector<string>
tokenizeString(const string &s, const string &delims)
{
  size_t offset = 0;

  vector<string>tokens;

  while (true) {
    size_t i = s.find_first_not_of(delims, offset);
    if (i == string::npos) {
      break;
    }

    size_t j = s.find_first_of(delims, i);
    if (j == string::npos) {
      tokens.push_back(s.substr(i));
      offset = s.length();
      continue;
    }

    tokens.push_back(s.substr(i, j - i));
    offset = j;
  }
  return tokens;
}
} // namespace dmtcp
