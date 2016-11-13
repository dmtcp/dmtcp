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
#include <stdarg.h>
#include <time.h>
#include "dmtcp.h"
#include "util.h"

#include "fileconnection.h"
#include "fileconnlist.h"
#include "filewrappers.h"

using namespace dmtcp;

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

  DMTCP_PLUGIN_DISABLE_CKPT();
  PosixMQConnection *con = (PosixMQConnection *)
    FileConnList::instance().getConnection(mqdes);
  con->on_mq_notify(NULL);
  DMTCP_PLUGIN_ENABLE_CKPT();

  start_routine(s);
}

extern "C"
int
mq_notify(mqd_t mqdes, const struct sigevent *sevp)
{
  int res;

  DMTCP_PLUGIN_DISABLE_CKPT();
  if (sevp != NULL && sevp->sigev_notify == SIGEV_THREAD) {
    struct sigevent se;
    struct mqNotifyData *mdata;
    mdata = (struct mqNotifyData *)
      JALLOC_HELPER_MALLOC(sizeof(struct mqNotifyData));
    se = *sevp;
    mdata->start_routine = sevp->sigev_notify_function;
    mdata->sv = sevp->sigev_value;
    mdata->mqdes = mqdes;
    se.sigev_notify_function = mq_notify_thread_start;
    se.sigev_value.sival_ptr = mdata;
    res = _real_mq_notify(mqdes, &se);
  } else {
    res = _real_mq_notify(mqdes, sevp);
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
