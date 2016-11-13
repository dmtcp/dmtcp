#include "../../jalib/jassert.h"

extern "C"
void
warning(const char *warning_part1, const char *warning_part2)
{
  dmtcp::string warning("modify_env.c: ");

  warning = warning + warning_part1 + warning_part2;
  JWARNING(false).Text(warning.c_str());
}
