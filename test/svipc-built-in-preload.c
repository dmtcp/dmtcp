/*
 * Runtime preload probe for the built-in SysV IPC wrappers.
 *
 * Run under bin/dmtcp_launch.  The probe validates both the launch-time
 * environment and the post-exec environment reconstructed by execwrappers.cpp,
 * and it exercises SysV shared memory, semaphore, and message queue wrappers.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _SEM_SEMUN_UNDEFINED
union semun {
  int val;
  struct semid_ds *buf;
  unsigned short *array;
  struct seminfo *__buf;
};
#endif

#define ENV_HIJACK_LIBS "DMTCP_HIJACK_LIBS"
#define ENV_DISABLE_ALL_PLUGINS "DMTCP_DISABLE_ALL_PLUGINS"
#define ENV_AFTER_EXEC "DMTCP_SVIPC_BUILT_IN_PRELOAD_AFTER_EXEC"
#define LIB_DMTCP "libdmtcp.so"
#define LIB_DMTCP_SVIPC "libdmtcp_svipc.so"

#define EXIT_ENV 2
#define EXIT_IPC 3
#define EXIT_EXEC 4

struct probe_msg {
  long mtype;
  char mtext[32];
};

static int
fail_errno(const char *phase, const char *operation)
{
  fprintf(stderr, "%s: %s failed: %s\n", phase, operation, strerror(errno));
  return EXIT_IPC;
}

static void
remember_failure(int *status, int rc)
{
  if (*status == 0 && rc != 0) {
    *status = rc;
  }
}

static int
require_env_contains(const char *phase, const char *env_name,
                     const char *token)
{
  const char *value = getenv(env_name);
  if (value == NULL || strstr(value, token) == NULL) {
    fprintf(stderr,
            "%s: %s missing required token '%s'; value=%s\n",
            phase, env_name, token, value == NULL ? "<unset>" : value);
    return EXIT_ENV;
  }
  return 0;
}

static int
require_env_omits(const char *phase, const char *env_name, const char *token)
{
  const char *value = getenv(env_name);
  if (value != NULL && strstr(value, token) != NULL) {
    fprintf(stderr,
            "%s: %s contains unexpected token '%s'; value=%s\n",
            phase, env_name, token, value);
    return EXIT_ENV;
  }
  return 0;
}

static int
check_ld_preload_env(const char *phase)
{
  const char *value = getenv("LD_PRELOAD");

  if (value == NULL) {
    fprintf(stderr,
            "%s: LD_PRELOAD missing; expected '%s' or DMTCP-hidden empty "
            "value; value=<unset>\n",
            phase, LIB_DMTCP);
    return EXIT_ENV;
  }

  if (strstr(value, LIB_DMTCP_SVIPC) != NULL) {
    fprintf(stderr,
            "%s: LD_PRELOAD contains unexpected token '%s'; value=%s\n",
            phase, LIB_DMTCP_SVIPC, value);
    return EXIT_ENV;
  }

  if (value[0] != '\0' && strstr(value, LIB_DMTCP) == NULL) {
    fprintf(stderr,
            "%s: LD_PRELOAD missing required token '%s'; value=%s\n",
            phase, LIB_DMTCP, value);
    return EXIT_ENV;
  }

  return 0;
}

static int
check_loaded_libraries(const char *phase)
{
  FILE *maps;
  char line[4096];
  int found_libdmtcp = 0;
  int found_svipc = 0;

  maps = fopen("/proc/self/maps", "r");
  if (maps == NULL) {
    fprintf(stderr,
            "%s: /proc/self/maps could not be read while checking preload "
            "state: %s\n",
            phase, strerror(errno));
    return EXIT_ENV;
  }

  while (fgets(line, sizeof(line), maps) != NULL) {
    if (strstr(line, LIB_DMTCP) != NULL) {
      found_libdmtcp = 1;
    }
    if (strstr(line, LIB_DMTCP_SVIPC) != NULL) {
      found_svipc = 1;
    }
  }
  fclose(maps);

  if (found_svipc) {
    fprintf(stderr,
            "%s: loaded libraries contain unexpected '%s'; %s=%s; "
            "LD_PRELOAD=%s\n",
            phase, LIB_DMTCP_SVIPC, ENV_HIJACK_LIBS,
            getenv(ENV_HIJACK_LIBS) == NULL ?
            "<unset>" : getenv(ENV_HIJACK_LIBS),
            getenv("LD_PRELOAD") == NULL ?
            "<unset>" : getenv("LD_PRELOAD"));
    return EXIT_ENV;
  }

  if (!found_libdmtcp) {
    fprintf(stderr,
            "%s: loaded libraries missing '%s'; %s=%s; LD_PRELOAD=%s\n",
            phase, LIB_DMTCP, ENV_HIJACK_LIBS,
            getenv(ENV_HIJACK_LIBS) == NULL ?
            "<unset>" : getenv(ENV_HIJACK_LIBS),
            getenv("LD_PRELOAD") == NULL ?
            "<unset>" : getenv("LD_PRELOAD"));
    return strcmp(phase, "after-exec") == 0 ? EXIT_EXEC : EXIT_ENV;
  }

  return 0;
}

static int
check_preload_env(const char *phase)
{
  int rc;

  rc = require_env_contains(phase, ENV_HIJACK_LIBS, LIB_DMTCP);
  if (rc != 0) {
    return rc;
  }
  rc = require_env_omits(phase, ENV_HIJACK_LIBS, LIB_DMTCP_SVIPC);
  if (rc != 0) {
    return rc;
  }
  rc = check_ld_preload_env(phase);
  if (rc != 0) {
    return rc;
  }
  rc = check_loaded_libraries(phase);
  if (rc != 0) {
    return rc;
  }

  return 0;
}

static int
cleanup_shm(const char *phase, int shmid)
{
  if (shmid != -1 && shmctl(shmid, IPC_RMID, NULL) == -1) {
    fprintf(stderr, "%s: shmctl(IPC_RMID) cleanup failed for shmid=%d: %s\n",
            phase, shmid, strerror(errno));
    return EXIT_IPC;
  }
  return 0;
}

static int
cleanup_sem(const char *phase, int semid)
{
  if (semid != -1 && semctl(semid, 0, IPC_RMID) == -1) {
    fprintf(stderr,
            "%s: semctl(IPC_RMID no-arg) cleanup failed for semid=%d: %s\n",
            phase, semid, strerror(errno));
    return EXIT_IPC;
  }
  return 0;
}

static int
cleanup_msg(const char *phase, int msqid)
{
  if (msqid != -1 && msgctl(msqid, IPC_RMID, NULL) == -1) {
    fprintf(stderr, "%s: msgctl(IPC_RMID) cleanup failed for msqid=%d: %s\n",
            phase, msqid, strerror(errno));
    return EXIT_IPC;
  }
  return 0;
}

static int
exercise_shm_ipc_private(const char *phase)
{
  int status = 0;
  int shmid1 = -1;
  int shmid2 = -1;
  void *addr1 = (void *)-1;
  void *addr2 = (void *)-1;

  shmid1 = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
  if (shmid1 == -1) {
    return fail_errno(phase, "shmget(IPC_PRIVATE)#1");
  }

  shmid2 = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
  if (shmid2 == -1) {
    status = fail_errno(phase, "shmget(IPC_PRIVATE)#2");
    goto out;
  }

  if (shmid1 == shmid2) {
    fprintf(stderr,
            "%s: shmget(IPC_PRIVATE) returned duplicate IDs: %d and %d\n",
            phase, shmid1, shmid2);
    status = EXIT_IPC;
    goto out;
  }

  addr1 = shmat(shmid1, NULL, 0);
  if (addr1 == (void *)-1) {
    status = fail_errno(phase, "shmat#1");
    goto out;
  }
  addr2 = shmat(shmid2, NULL, 0);
  if (addr2 == (void *)-1) {
    status = fail_errno(phase, "shmat#2");
    goto out;
  }

  ((char *)addr1)[0] = 'A';
  ((char *)addr2)[0] = 'B';
  if (((char *)addr1)[0] != 'A' || ((char *)addr2)[0] != 'B') {
    fprintf(stderr, "%s: shared memory write/read sanity check failed\n", phase);
    status = EXIT_IPC;
    goto out;
  }

out:
  if (addr2 != (void *)-1) {
    if (shmdt(addr2) == -1) {
      fprintf(stderr, "%s: shmdt#2 cleanup failed: %s\n",
              phase, strerror(errno));
      remember_failure(&status, EXIT_IPC);
    }
  }
  if (addr1 != (void *)-1) {
    if (shmdt(addr1) == -1) {
      fprintf(stderr, "%s: shmdt#1 cleanup failed: %s\n",
              phase, strerror(errno));
      remember_failure(&status, EXIT_IPC);
    }
  }
  remember_failure(&status, cleanup_shm(phase, shmid2));
  remember_failure(&status, cleanup_shm(phase, shmid1));
  return status;
}

static int
exercise_sem(const char *phase)
{
  int status = 0;
  int semid = -1;
  union semun arg;
  struct sembuf op;

  memset(&arg, 0, sizeof(arg));
  memset(&op, 0, sizeof(op));

  semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
  if (semid == -1) {
    return fail_errno(phase, "semget(IPC_PRIVATE)");
  }

  arg.val = 0;
  if (semctl(semid, 0, SETVAL, arg) == -1) {
    status = fail_errno(phase, "semctl(SETVAL)");
    goto out;
  }

  op.sem_num = 0;
  op.sem_op = 1;
  op.sem_flg = 0;
  if (semop(semid, &op, 1) == -1) {
    status = fail_errno(phase, "semop(+1)");
    goto out;
  }

  op.sem_num = 0;
  op.sem_op = -1;
  op.sem_flg = IPC_NOWAIT;
  if (semop(semid, &op, 1) == -1) {
    status = fail_errno(phase, "semop(-1|IPC_NOWAIT)");
    goto out;
  }

  op.sem_num = 0;
  op.sem_op = 1;
  op.sem_flg = 0;
  if (semtimedop(semid, &op, 1, NULL) == -1) {
    status = fail_errno(phase, "semtimedop(+1)");
    goto out;
  }

  if (semctl(semid, 0, GETPID) == -1) {
    status = fail_errno(phase, "semctl(GETPID no-arg)");
    goto out;
  }

out:
  remember_failure(&status, cleanup_sem(phase, semid));
  return status;
}

static int
exercise_msg(const char *phase)
{
  int status = 0;
  int msqid = -1;
  struct probe_msg send_msg;
  struct probe_msg recv_msg;
  ssize_t nread;
  const char *payload = "svipc-built-in";

  memset(&send_msg, 0, sizeof(send_msg));
  memset(&recv_msg, 0, sizeof(recv_msg));

  msqid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
  if (msqid == -1) {
    return fail_errno(phase, "msgget(IPC_PRIVATE)");
  }

  send_msg.mtype = 1;
  snprintf(send_msg.mtext, sizeof(send_msg.mtext), "%s", payload);
  if (msgsnd(msqid, &send_msg, strlen(send_msg.mtext) + 1, 0) == -1) {
    status = fail_errno(phase, "msgsnd");
    goto out;
  }

  nread = msgrcv(msqid, &recv_msg, sizeof(recv_msg.mtext), 1, 0);
  if (nread == -1) {
    status = fail_errno(phase, "msgrcv");
    goto out;
  }
  if (strcmp(recv_msg.mtext, payload) != 0) {
    fprintf(stderr,
            "%s: msgrcv payload mismatch; expected='%s' actual='%s' bytes=%zd\n",
            phase, payload, recv_msg.mtext, nread);
    status = EXIT_IPC;
    goto out;
  }

out:
  remember_failure(&status, cleanup_msg(phase, msqid));
  return status;
}

static int
exercise_sysv_ipc(const char *phase)
{
  int rc;

  rc = exercise_shm_ipc_private(phase);
  if (rc != 0) {
    return rc;
  }
  rc = exercise_sem(phase);
  if (rc != 0) {
    return rc;
  }
  rc = exercise_msg(phase);
  if (rc != 0) {
    return rc;
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  const char *after_exec = getenv(ENV_AFTER_EXEC);
  const char *phase = after_exec != NULL && strcmp(after_exec, "1") == 0 ?
                      "after-exec" : "launch";
  int rc;

  (void)argc;

  rc = check_preload_env(phase);
  if (rc != 0) {
    return rc;
  }

  rc = exercise_sysv_ipc(phase);
  if (rc != 0) {
    return rc;
  }

  if (strcmp(phase, "launch") == 0) {
    if (setenv(ENV_AFTER_EXEC, "1", 1) == -1) {
      fprintf(stderr,
              "launch: %s could not be set before exec: %s\n",
              ENV_AFTER_EXEC, strerror(errno));
      return EXIT_EXEC;
    }
    fflush(NULL);
    execvp(argv[0], argv);
    fprintf(stderr,
            "launch: exec reconstruction probe failed for argv[0]=%s: %s\n",
            argv[0], strerror(errno));
    return EXIT_EXEC;
  }

  printf("PASS svipc-built-in-preload: launch and exec preload state valid%s\n",
         getenv(ENV_DISABLE_ALL_PLUGINS) != NULL &&
         strcmp(getenv(ENV_DISABLE_ALL_PLUGINS), "1") == 0 ?
         " (disable-all)" : "");
  return EXIT_SUCCESS;
}
