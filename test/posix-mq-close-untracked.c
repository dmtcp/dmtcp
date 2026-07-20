#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static void
run_forever(void)
{
  while (1) {
    printf("posix-mq-close-untracked ok\n");
    fflush(stdout);
    sleep(1);
  }
}

int
main(void)
{
#ifndef SYS_mq_open
  run_forever();
#else
  char mqname[128];
  snprintf(mqname, sizeof(mqname), "/dmtcp-mq-close-untracked-%ld",
           (long)getpid());
  mq_unlink(mqname);

  struct mq_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.mq_maxmsg = 4;
  attr.mq_msgsize = 64;

  /*
   * Bypass mq_open(3), and therefore the DMTCP mq_open wrapper, so mq_close()
   * sees a valid kernel descriptor that is absent from FileConnList.
   */
  mqd_t mqdes = (mqd_t)syscall(SYS_mq_open, mqname + 1,
                               O_CREAT | O_RDWR | O_CLOEXEC,
                               S_IRUSR | S_IWUSR, &attr);
  if (mqdes == (mqd_t)-1) {
    perror("syscall(SYS_mq_open)");
    return 1;
  }

  if (mq_close(mqdes) == -1) {
    perror("mq_close");
    mq_unlink(mqname);
    return 1;
  }

  if (mq_unlink(mqname) == -1) {
    perror("mq_unlink");
    return 1;
  }
  run_forever();
#endif
}
