/****************************************************************************
 *   Copyright (C) 2012 by Kapil Arya(kapil@ccs.neu.edu)                   *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <mqueue.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "dmtcp.h"
#include "ipc.h"
#include "util.h"

#include "fileconnection.h"
#include "fileconnlist.h"
#include "filewrappers.h"

using namespace dmtcp;

typedef pid_t (*PidTranslateFn)(pid_t pid);

static PidTranslateFn
ipcPidVirtualToRealFn()
{
  static PidTranslateFn nextFn = NULL;
  static bool nextFnResolved = false;

  if (dmtcp_virtual_to_real_pid != NULL) {
    return dmtcp_virtual_to_real_pid;
  }

  if (!nextFnResolved && dmtcp_dlsym != NULL) {
    nextFn = (PidTranslateFn)dmtcp_dlsym(RTLD_NEXT,
                                         "dmtcp_virtual_to_real_pid");
    nextFnResolved = true;
  }

  return nextFn;
}

static pid_t
virtualToRealPidIfEnabled(pid_t pid)
{
  if (!dmtcp_ipc_wrappers_enabled()) {
    return pid;
  }

  PidTranslateFn fn = ipcPidVirtualToRealFn();
  return fn != NULL ? fn(pid) : pid;
}

extern "C"
mqd_t
mq_open(const char *name, int oflag, ...)
{
  mode_t mode = 0;
  struct mq_attr *attr = NULL;

  // Handling the variable number of arguments
  if (oflag & O_CREAT) {
    va_list arg;
    va_start(arg, oflag);
    mode = va_arg(arg, mode_t);
    attr = va_arg(arg, struct mq_attr *);
    va_end(arg);
  }

  if (!dmtcp_ipc_wrappers_enabled()) {
    return _real_mq_open(name, oflag, mode, attr);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  int res = _real_mq_open(name, oflag, mode, attr);
  if (res != -1) {
    PosixMQConnection *pcon = new PosixMQConnection(name, oflag,
                                                    mode, attr);
    FileConnList::instance().add(res, pcon);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return res;
}

extern "C"
int
mq_close(mqd_t mqdes)
{
  if (!dmtcp_ipc_wrappers_enabled()) {
    return _real_mq_close(mqdes);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  int res = _real_mq_close(mqdes);
  if (res != -1) {
    PosixMQConnection *con = (PosixMQConnection *)
      FileConnList::instance().getConnection(mqdes);
    con->on_mq_close();
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return res;
}

struct mqNotifyData {
  void (*start_routine) (union sigval);
  union sigval sv;
  mqd_t mqdes;
};

static pthread_once_t mq_notify_helper_once = PTHREAD_ONCE_INIT;
static int mq_notify_sock = -1;

static void mq_notify_reset_on_fork(void);
static void *mq_notify_helper_thread(void *arg);
static void start_mq_notify_helper(void);
static int mq_notify_sigev_thread(mqd_t mqdes, const struct sigevent *sevp);

static void
mq_notify_reset_on_fork(void)
{
  if (mq_notify_sock != -1) {
    _real_close(mq_notify_sock);
  }
  mq_notify_helper_once = PTHREAD_ONCE_INIT;
  mq_notify_sock = -1;
}

static void
mq_notify_thread_start(union sigval sv)
{
  // FIXME: Possible race:
  // If some other thread in this process calls mq_notify() before we
  // make a call to con->on_mq_notify(NULL), the notify request won't be
  // restored on restart. However, this case is rather unusual.
  struct mqNotifyData *m = (struct mqNotifyData *)sv.sival_ptr;

  void (*start_routine) (union sigval) = m->start_routine;
  union sigval s = m->sv;
  mqd_t mqdes = m->mqdes;

  JALLOC_HELPER_FREE(m);

  if (dmtcp_ipc_wrappers_enabled()) {
    DMTCP_PLUGIN_DISABLE_CKPT();
    PosixMQConnection *con = (PosixMQConnection *)
      FileConnList::instance().getConnection(mqdes);
    con->on_mq_notify(NULL);
    DMTCP_PLUGIN_ENABLE_CKPT();
  }

  start_routine(s);
}

// Linux implements SIGEV_THREAD mqueue notifications by asking the kernel to
// deliver a copy of the sigevent over a netlink socket.  We keep that socket
// and helper thread under DMTCP control instead of relying on glibc's internal
// helper state, which can point at a stale socket after restart.
static void *
mq_notify_helper_thread(void *arg)
{
  int sock = (int)(intptr_t)arg;

  while (1) {
    struct sigevent se;
    ssize_t ret = recv(sock, &se, sizeof(se), MSG_WAITALL | MSG_NOSIGNAL);
    if (ret == -1 && errno == EINTR) {
      continue;
    }
    if (ret != (ssize_t)sizeof(se)) {
      break;
    }

    // Run the callback on this tracked helper thread.  Creating a fresh
    // pthread here can collide with DMTCP checkpoint/restart state transitions.
    union sigval sv;
    sv.sival_ptr = se.sigev_value.sival_ptr;
    mq_notify_thread_start(sv);
  }

  _real_close(sock);
  return NULL;
}

static void
start_mq_notify_helper(void)
{
  mq_notify_sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (mq_notify_sock == -1) {
    return;
  }

  pthread_attr_t attr;
  pthread_t thread;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int rc = pthread_create(&thread, &attr, mq_notify_helper_thread,
                          (void *)(intptr_t)mq_notify_sock);
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    _real_close(mq_notify_sock);
    mq_notify_sock = -1;
    return;
  }

  pthread_atfork(NULL, NULL, mq_notify_reset_on_fork);
}

static int
mq_notify_sigev_thread(mqd_t mqdes, const struct sigevent *sevp)
{
  pthread_once(&mq_notify_helper_once, start_mq_notify_helper);
  if (mq_notify_sock == -1) {
    errno = EAGAIN;
    return -1;
  }

  struct mqNotifyData *mdata = (struct mqNotifyData *)
    JALLOC_HELPER_MALLOC(sizeof(struct mqNotifyData));
  if (mdata == NULL) {
    errno = EAGAIN;
    return -1;
  }

  struct sigevent se = *sevp;
  mdata->start_routine = sevp->sigev_notify_function;
  mdata->sv = sevp->sigev_value;
  mdata->mqdes = mqdes;
  se.sigev_signo = mq_notify_sock;
  se.sigev_value.sival_ptr = mdata;
  se.sigev_notify_function = mq_notify_thread_start;
  se.sigev_notify_attributes = NULL;

  int ret = (int)_real_syscall(SYS_mq_notify, mqdes, &se);
  if (ret == -1) {
    JALLOC_HELPER_FREE(mdata);
  }
  return ret;
}

extern "C"
int
mq_notify(mqd_t mqdes, const struct sigevent *sevp)
{
  int res;
  struct sigevent translatedSev;
  const struct sigevent *sevpForKernel = sevp;

  if (!dmtcp_ipc_wrappers_enabled()) {
    return _real_mq_notify(mqdes, sevp);
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (sevpForKernel != NULL &&
      sevpForKernel->sigev_notify == SIGEV_THREAD_ID) {
    translatedSev = *sevpForKernel;
    translatedSev._sigev_un._tid =
      virtualToRealPidIfEnabled(translatedSev._sigev_un._tid);
    sevpForKernel = &translatedSev;
  }

  if (sevpForKernel != NULL && sevpForKernel->sigev_notify == SIGEV_THREAD) {
    res = mq_notify_sigev_thread(mqdes, sevpForKernel);
  } else {
    res = _real_mq_notify(mqdes, sevpForKernel);
  }

  if (res != -1) {
    PosixMQConnection *con = (PosixMQConnection *)
      FileConnList::instance().getConnection(mqdes);
    con->on_mq_notify(sevp);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return res;
}

extern "C"
int
mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned msg_prio)
{
  if (!dmtcp_ipc_wrappers_enabled()) {
    return _real_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
  }

  int res;
  struct timespec ts;

  do {
    JASSERT(clock_gettime(CLOCK_REALTIME, &ts) != -1);
    ts.tv_sec += 1000;
    res = mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, &ts);
  } while (res == -1 && errno == ETIMEDOUT);
  return res;
}

extern "C"
ssize_t
mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned *msg_prio)
{
  if (!dmtcp_ipc_wrappers_enabled()) {
    return _real_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
  }

  ssize_t res;
  struct timespec ts;

  do {
    JASSERT(clock_gettime(CLOCK_REALTIME, &ts) != -1);
    ts.tv_sec += 1000;
    res = mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio, &ts);
  } while (res == -1 && errno == ETIMEDOUT);
  return res;
}

static struct timespec ts_100ms = { 0, 100 * 1000 * 1000 };
extern "C"
int
mq_timedsend(mqd_t mqdes,
             const char *msg_ptr,
             size_t msg_len,
             unsigned msg_prio,
             const struct timespec *abs_timeout)
{
  if (!dmtcp_ipc_wrappers_enabled()) {
    return _real_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, abs_timeout);
  }

  struct timespec ts;
  int ret = -1;

  do {
    DMTCP_PLUGIN_DISABLE_CKPT();
    JASSERT(clock_gettime(CLOCK_REALTIME, &ts) != -1);
    if (TIMESPEC_CMP(&ts, abs_timeout, <=)) {
      TIMESPEC_ADD(&ts, &ts_100ms, &ts);
    }
    ret = _real_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, &ts);
    DMTCP_PLUGIN_ENABLE_CKPT();

    if (ret != -1 || (ret == -1 && errno != ETIMEDOUT)) {
      return ret;
    }
  } while (TIMESPEC_CMP(&ts, abs_timeout, <));

  errno = ETIMEDOUT;
  return ret;
}

extern "C"
ssize_t
mq_timedreceive(mqd_t mqdes,
                char *msg_ptr,
                size_t msg_len,
                unsigned *msg_prio,
                const struct timespec *abs_timeout)
{
  if (!dmtcp_ipc_wrappers_enabled()) {
    return _real_mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio, abs_timeout);
  }

  struct timespec ts;
  int ret = -1;

  do {
    DMTCP_PLUGIN_DISABLE_CKPT();
    JASSERT(clock_gettime(CLOCK_REALTIME, &ts) != -1);
    if (TIMESPEC_CMP(&ts, abs_timeout, <=)) {
      TIMESPEC_ADD(&ts, &ts_100ms, &ts);
    }
    ret = _real_mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio, &ts);
    DMTCP_PLUGIN_ENABLE_CKPT();

    if (ret != -1 || (ret == -1 && errno != ETIMEDOUT)) {
      return ret;
    }
  } while (TIMESPEC_CMP(&ts, abs_timeout, <));

  errno = ETIMEDOUT;
  return ret;
}
