#include <sys/types.h>
#include <mqueue.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct msgbuf {
  mqd_t mqdes;
  int   expected;
};

void msg_snd(mqd_t mqdes, int i);
void msg_rcv(mqd_t mqdes, int i);
static void msg_notify(mqd_t mqdes, int i);

static void                     /* Thread start function */
tfunc(union sigval sv)
{
  struct msgbuf *m = (struct msgbuf *) sv.sival_ptr;

  msg_rcv(m->mqdes, m->expected);

  printf("Server: Notification received for msg: %d\n", m->expected);
  free(m);
  return;
}

void msg_snd(mqd_t mqdes, int i)
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

void msg_rcv(mqd_t mqdes, int i)
{
  struct mq_attr attr;

  if (mq_getattr(mqdes, &attr) == -1) {
    perror("mq_getattr");
    exit(1);
  }
  char *buf = malloc(attr.mq_msgsize);

  if (mq_receive(mqdes, buf, attr.mq_msgsize, NULL) == -1) {
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
  free(buf);
}

static void msg_notify(mqd_t mqdes, int i)
{
  struct msgbuf *m = malloc(sizeof(struct msgbuf));
  m->mqdes = mqdes;
  m->expected = i;
  struct sigevent sev;
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = tfunc;
  sev.sigev_notify_attributes = NULL;
  sev.sigev_value.sival_ptr = m;   /* Arg. to thread func. */
  if (mq_notify(mqdes, &sev) == -1) {
    perror("mq_notify");
    exit(1);
  }
}

void parent(const char *mqname, const char *mqname2)
{
  mqd_t mqdes = mq_open(mqname, O_RDWR | O_CREAT, 0666, 0);
  if (mqdes == -1) {
    perror("mq_open() failed");
    exit(1);
  }

  mqd_t mqdes2 = mq_open(mqname2, O_RDWR | O_CREAT, 0666, 0);
  if (mqdes == -1) {
    perror("mq_open() failed");
    exit(1);
  }

  int i = 1;
  while (1) {
    msg_notify(mqdes2, i+1);
    printf("Server: sending %d\n", i);
    fflush(stdout);
    msg_snd(mqdes, i);
    sleep(1);
    i += 2;
  }
  exit(0);
}

void child(const char *mqname, const char *mqname2)
{
  mqd_t mqdes = mq_open(mqname, O_RDWR | O_CREAT, 0666, 0);
  if (mqdes == -1) {
    perror("mq_open() failed");
    exit(1);
  }

  mqd_t mqdes2 = mq_open(mqname2, O_RDWR | O_CREAT, 0666, 0);
  if (mqdes == -1) {
    perror("mq_open() failed");
    exit(1);
  }

  int i=1;
  while (1) {
    msg_rcv(mqdes, i);
    printf("Client: received %d, sending %d\n", i, i + 1);
    fflush(stdout);
    msg_snd(mqdes2, i + 1);
    i += 2;
  }
  exit(0);
}

int main(int argc, char **argv)
{
  char mqname[256];
  char mqname2[256];
  char *user = getenv("USER");
  sprintf(mqname, "/dmtcp-mq-%s", user == NULL ? "" : user);
  sprintf(mqname2, "/dmtcp-mq-2-%s", user == NULL ? "" : user);

  mq_unlink(mqname);
  mq_unlink(mqname2);
  if (fork() == 0) {
    child(mqname, mqname2);
  } else {
    parent(mqname, mqname2);
  }
  return 0;
}
