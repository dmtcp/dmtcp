#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PID_MISM  0x01
#define PPID_MISM 0x02
#define PGRP_MISM 0x04
#define SID_MISM  0x08

int eq_sid = 0, eq_pgrp = 0, eq_ppid = 0;

void
parse_credentials(int ret,
                  char *name,
                  pid_t pid,
                  pid_t ppid,
                  pid_t pgrp,
                  pid_t sid)
{
  char buf[256];
  int len = 0;
  int cnt = 0;

  len += sprintf(buf,
                 "%s: mismatch in (err=%02x,eq_ppid=%d,eq_sid=%d): ",
                 name,
                 ret,
                 eq_ppid,
                 eq_sid);
  if ((ret & PID_MISM)) {
    cnt++;
    len += sprintf(buf + len, "PID(%d,%d) ", pid, getpid());
  }
  if ((ret & PPID_MISM) && eq_ppid) {
    cnt++;
    len += sprintf(buf + len, "PPID(%d,%d) ", ppid, getppid());
  }
  if (ret & PGRP_MISM && eq_pgrp) {
    cnt++;
    len += sprintf(buf + len, "PGRP(%d,%d) ", pgrp, getpgrp());
  }
  if (ret & SID_MISM && eq_sid) {
    cnt++;
    len += sprintf(buf + len, "SID(%d,%d) ", sid, getsid(0));
  }
  if (cnt) {
    printf("%s\n", buf);
  }
}

int
check_credentials(pid_t pid, pid_t ppid, pid_t pgrp, pid_t sid)
{
  int ret = 0;

  if (pid != getpid()) {
    ret |= PID_MISM;
  }
  if (ppid != getppid()) {
    ret |= PPID_MISM;
  }
  if (pgrp != getpgrp()) {
    ret |= PGRP_MISM;
  }
  if (sid != getsid(0)) {
    ret |= SID_MISM;
  }
  return ret;
}

void
process(char *name, pid_t *cids, int cnum)
{
  pid_t pid = getpid();
  pid_t ppid = getppid();
  pid_t pgrp = getpgrp();
  pid_t sid = getsid(0);

  FILE *fp;

  fp = fopen(name, "w");
  fprintf(fp, "pid/spid\tppid/sppid\tsid/ssid\n");
  fclose(fp);
  while (1) {
    int ret = 0, i;

    fp = fopen(name, "a");
    fprintf(fp, "%d/%d\t%d/%d\t%d/%d\n",
            getpid(), pid,
            getppid(), ppid,
            getsid(0), sid);
    fclose(fp);

    ret = check_credentials(pid, ppid, pgrp, sid);
    if (ret) {
      parse_credentials(ret, name, pid, ppid, pgrp, sid);
    }
    for (i = 0; i < cnum; i++) {
      if (kill(cids[i], 0) < 0) {
        printf("%s: no child #%d\n", name, i);
      }
    }
    sleep(1);
  }
}

int
main()
{
  pid_t p1_cids[3];
  int ret;

  if (!(p1_cids[0] = fork())) {
    eq_ppid = 1;             // after restart we should find thi same PPID
    process("p11", NULL, 0);
  }

  if (!(p1_cids[1] = fork())) {
    eq_ppid = 1;             // after restart we should find thi same PPID
    process("p12", NULL, 0);
  }


  if (!(ret = fork())) {
    if (!fork()) {
      pid_t p13_cids[3];

      if (!(p13_cids[0] = fork())) {
        eq_ppid = 1;                         // after restart we should find thi
                                             // same PPID
        process("p131", NULL, 0);
      }
      if (!(p13_cids[1] = fork())) {
        eq_ppid = 1;                         // after restart we should find thi
                                             // same PPID
        process("p132", NULL, 0);
      }

      setsid();

      eq_ppid = 1;
      eq_sid = 1;

      if (!(p13_cids[2] = fork())) {
        eq_ppid = 1;                         // after restart we should find thi
                                             // same PPID
        process("p133", NULL, 0);
      }
      process("p13", p13_cids, 3);
    }
    _exit(0);
  }
  waitpid(ret, NULL, 0);
  process("p1", p1_cids, 1);
  return 0;
}
