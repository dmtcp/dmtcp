// msgrcv, msgsnd require _XOPEN_SOURCE
#define _XOPEN_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <unistd.h>

#define SIZE 1024

struct my_msgbuf {
  long mtype;
  long val;
  char buf[1024];
};

void
msg_snd(int msqid, int i)
{
  struct my_msgbuf buf;

  buf.mtype = i;
  buf.val = i;
  if (msgsnd(msqid, (const void *)&buf, sizeof(long), 0) == -1) {
    perror("msgsnd failed");
    fflush(stdout);
    sleep(1);
    exit(1);
  }
}

void
msg_rcv(int msqid, int i)
{
  struct my_msgbuf buf;

  buf.mtype = 0;
  buf.val = 0;
  if (msgrcv(msqid, (void *)&buf, sizeof(buf), i, 0) == -1) {
    perror("msgrcv failed");
    fflush(stdout);
    sleep(1);
    exit(1);
  }
}

void
parent(int msqid)
{
  int i = 1;

  for (i = 1; i < 32000; i += 2) {
    printf("Server: %d\n", i);
    fflush(stdout);
    msg_snd(msqid, i);
    msg_rcv(msqid, i + 1);
    sleep(1);
  }
  exit(0);
}

void
child(int msqid)
{
  int i = 1;

  while (1) {
    msg_rcv(msqid, i);
    printf("Client: %d\n", i);
    fflush(stdout);
    msg_snd(msqid, i + 1);
    i += 2;
  }
  exit(0);
}

int
main(int argc, char **argv)
{
  int msgid;

  msgid = msgget((key_t)9977, IPC_CREAT | 0666);
  if (msgid == -1) {
    perror("semget failed");
    exit(1);
  }

  if (fork() == 0) {
    child(msgid);
  } else {
    parent(msgid);
  }
  return 0;
}
