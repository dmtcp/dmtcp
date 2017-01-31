#include <assert.h>
#include <errno.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

// Note:  /dev/mqueue shows the currently allocated message queues

void
msg_snd(mqd_t mqdes, int i)
{
  char buf[16];

  sprintf(buf, "%d", i);

  errno = 0;
  if (mq_send(mqdes, buf, strlen(buf) + 1, 0) == -1) {
    perror("mq_send");
    fflush(stdout);
    sleep(1);
    exit(1);
  }
}

void
msg_rcv(mqd_t mqdes, int i)
{
  char buf[10000];

  errno = 0;
  if (mq_receive(mqdes, buf, sizeof(buf), NULL) == -1) {
    perror("mq_receive");
    fflush(stdout);
    sleep(1);
    exit(1);
  }
  if (i != atoi(buf)) {
    printf("Msg mismatch: expected: %d, got: %s\n", i, buf);
    fflush(stdout);
    sleep(1);
    exit(1);
  }
}

void
parent(const char *mqname)
{
  mqd_t mqdes = mq_open(mqname, O_RDWR | O_CREAT, 0666, 0);
  if (mqdes == -1) {
    perror("mq_open (in parent)");
    exit(1);
  }

  struct mq_attr attr;
  mq_getattr(mqdes, &attr);

  printf("mq_flags: %ld, mq_maxmsg: %ld, mq_msgsize: %ld, mq_curmsgs: %ld\n",
         attr.mq_flags, attr.mq_maxmsg, attr.mq_msgsize, attr.mq_curmsgs);
  fflush(stdout);

  int i = 1;
  static int unlinked = 0;
  while (1) {
    printf("Server: %d\n", i);
    fflush(stdout);
    msg_snd(mqdes, i);
    sleep(1);
    if (!unlinked) {
      mq_unlink(mqname); /* parent and child will continue to use mqname */
      unlinked = 1;
    }
    i++;
  }
  mq_close(mqdes);
  exit(0);
}

void
child(const char *mqname)
{
  mqd_t mqdes = mq_open(mqname, O_RDWR | O_CREAT, 0666, 0);

  // Unfortunately, DMTCP doesn't yet support unlinking in child
  // while others use it:  But this seems to work fine in the parent.
  // mq_unlink(mqname); /* parent and child will continue to use mqname */
  if (mqdes == -1) {
    perror("mq_open (in child)");
    exit(1);
  }

  int i = 1;
  while (1) {
    msg_rcv(mqdes, i);
    printf("Client: %d\n", i);
    fflush(stdout);
    i++;
  }
  mq_close(mqdes);
  exit(0);
}

int
main(int argc, char **argv)
{
  char mqname[256];
  char *user = getenv("USER");

  sprintf(mqname, "/dmtcp-mq1-%s", user == NULL ? "" : user);
  mq_unlink(mqname);
  if (fork() == 0) {
    child(mqname);
  } else {
    parent(mqname);
  }
  return 0;
}
