/* Align the test array using the runtime page size supplied by the harness. */
#ifndef SNAPSHOT_PAGE_SIZE
#error "Compile with -DSNAPSHOT_PAGE_SIZE=<runtime page size>"
#endif
const unsigned char snapshot_fixture[8 * 1024 * 1024]
  __attribute__((aligned(SNAPSHOT_PAGE_SIZE))) = { [0] = 42,
                                                   [8 * 1024 * 1024 - 1] = 17 };
