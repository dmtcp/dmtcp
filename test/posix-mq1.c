#include <assert.h>
#include <errno.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

void
msg_snd(mqd_t mqdes, int i)
{
  char buf[16];

  sprintf(buf, "%d", i);

  errno = 0;
  if (mq_send(mqdes, buf, strlen(buf) + 1, 0) == -1) {
    perror("mq_send failed");
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
    perror("mq_receive failed");
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
    perror("mq_open() failed");
    exit(1);
  }

  struct mq_attr attr;
  mq_getattr(mqdes, &attr);

  printf("mq_flags: %ld, mq_maxmsg: %ld, mq_msgsize: %ld, mq_curmsgs: %ld\n",
         attr.mq_flags, attr.mq_maxmsg, attr.mq_msgsize, attr.mq_curmsgs);
  fflush(stdout);

  int i = 1;
  while (1) {
    printf("Server: %d\n", i);
    fflush(stdout);
    msg_snd(mqdes, i);
    sleep(1);
    i++;
  }
  exit(0);
}

void
child(const char *mqname)
{
  mqd_t mqdes = mq_open(mqname, O_RDWR | O_CREAT, 0666, 0);

  if (mqdes == -1) {
    perror("mq_open() failed");
    exit(1);
  }

  int i = 1;
  while (1) {
    msg_rcv(mqdes, i);
    /* We know that the parent has already called mq_open, so it's safe to
     * unlink it now.
     */
    if (i == 1) {
      mq_unlink(mqname);
    }

    printf("Client: %d\n", i);
    fflush(stdout);
    i++;
  }
  exit(0);
}

int
main(int argc, char **argv)
{
  char mqname[256];
  char *user = getenv("USER");

  sprintf(mqname, "/dmtcp-mq-%s", user == NULL ? "" : user);
  mq_unlink(mqname);
  if (fork() == 0) {
    child(mqname);
  } else {
    parent(mqname);
  }
  return 0;
}
