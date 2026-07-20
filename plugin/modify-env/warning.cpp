#define DMTCP_LOG_COMPONENT "modify-env"
#include "../../src/dmtcp_assert.h"

extern "C"
void
warning(const char *warning_part1, const char *warning_part2)
{
  WARN(false, "modify_env.c: {}{}", warning_part1, warning_part2);
}
