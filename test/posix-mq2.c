#include <assert.h>
#include <errno.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

struct msgbuf {
  mqd_t mqdes;
  int expected;
};

void msg_snd(mqd_t mqdes, int i);
void msg_rcv(mqd_t mqdes, int i);
static int msg_notify(mqd_t mqdes, int i);

static void

/* Thread start function */
tfunc(union sigval sv)
{
  struct msgbuf *m = (struct msgbuf *)sv.sival_ptr;

  msg_rcv(m->mqdes, m->expected);

  printf("Server: Notification received for msg: %d\n", m->expected);
  free(m);
}

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
  struct mq_attr attr;

  if (mq_getattr(mqdes, &attr) == -1) {
    perror("mq_getattr");
    exit(1);
  }
  char *buf = malloc(attr.mq_msgsize);

  if (mq_receive(mqdes, buf, attr.mq_msgsize, NULL) == -1) {
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
  free(buf);
}

static int
msg_notify(mqd_t mqdes, int i)
{
  struct msgbuf *m = malloc(sizeof(struct msgbuf));

  m->mqdes = mqdes;
  m->expected = i;
  struct sigevent sev;
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = tfunc;
  sev.sigev_notify_attributes = NULL;
  sev.sigev_value.sival_ptr = m;   /* Arg. to thread func. */
  return mq_notify(mqdes, &sev);
}

void
parent(const char *mqname, const char *mqname2)
{
  mqd_t mqdes = mq_open(mqname, O_RDWR | O_CREAT, 0666, 0);

  // Unfortunately, DMTCP doesn't yet support unlinking while others use it:
  // mq_unlink(mqname); /* parent and child will continue to use mqname */
  if (mqdes == -1) {
    perror("mq_open");
    exit(1);
  }

  mqd_t mqdes2 = mq_open(mqname2, O_RDWR | O_CREAT, 0666, 0);
  if (mqdes == -1) {
    perror("mq_open");
    exit(1);
  }

  int i = 1;
  while (1) {
    int rc;

    // Will call mq_notify, causing a thread to run tfunc and do mq_recv()
    // Notification will only occur after the queue is emptied
    // and a new message arrives.
    do {
      // It's possible for parent to iterate twice before child receives.
      // Keep trying.  We need a notify after each successful receive.
      sleep(1);
      rc = msg_notify(mqdes2, i + 1);

      // printf("Was notified: rc, errno: %d %d\n", rc, errno);
    } while ((rc == -1) && (errno == EBUSY));
    if (rc == -1) {
      perror("mq_notify");
      exit(1);
    }
    printf("Server: sending %d\n", i);
    fflush(stdout);
    msg_snd(mqdes, i);
    sleep(1);
    i += 2;
  }
  exit(0);
}

void
child(const char *mqname, const char *mqname2)
{
  mqd_t mqdes = mq_open(mqname, O_RDWR | O_CREAT, 0666, 0);

  // Unfortunately, DMTCP doesn't yet support unlinking while others use it:
  // mq_unlink(mqname); /* parent and child will continue to use mqname */
  if (mqdes == -1) {
    perror("mq_open");
    exit(1);
  }

  mqd_t mqdes2 = mq_open(mqname2, O_RDWR | O_CREAT, 0666, 0);
  if (mqdes == -1) {
    perror("mq_open");
    exit(1);
  }

  int i = 1;
  while (1) {
    msg_rcv(mqdes, i);
    printf("Client: received %d, sending %d\n", i, i + 1);
    fflush(stdout);
    msg_snd(mqdes2, i + 1);
    i += 2;
  }
  exit(0);
}

int
main(int argc, char **argv)
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
