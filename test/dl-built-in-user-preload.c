/* Tiny user LD_PRELOAD helper for the DL built-in runtime gate. */

static volatile int dmtcp_dl_built_in_user_preload_seen = 0;

void __attribute__((constructor))
dmtcp_dl_built_in_user_preload_constructor(void)
{
  dmtcp_dl_built_in_user_preload_seen = 4242;
}

int
dmtcp_dl_built_in_user_preload_marker(void)
{
  return dmtcp_dl_built_in_user_preload_seen;
}
