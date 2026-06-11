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
#include <fcntl.h>
#include "pluginmanager.h"
#include "dmtcp.h"
#include "util.h"

#include "fileconnection.h"
#include "fileconnlist.h"
#include "filewrappers.h"
#include "util_assert.h"
#include "wrapperlock.h"

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

  if (!internalPluginEnabled(INTERNAL_PLUGIN_FILE)) {
    return _real_mq_open(name, oflag, mode, attr);
  }

  WrapperLock wrapperLock;
  int res = _real_mq_open(name, oflag, mode, attr);
  if (res != -1) {
    PosixMQConnection *pcon = new PosixMQConnection(name, oflag,
                                                    mode, attr);
    FileConnList::instance().add(res, pcon);
  }
  return res;
}

extern "C"
int
mq_close(mqd_t mqdes)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_FILE)) {
    return _real_mq_close(mqdes);
  }

  WrapperLock wrapperLock;
  int res = _real_mq_close(mqdes);
  if (res != -1) {
    PosixMQConnection *con = (PosixMQConnection *)
      FileConnList::instance().getConnection(mqdes);
    if (con != NULL) {
      con->on_mq_close();
    }
  }
  return res;
}

extern "C"
int
mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned msg_prio)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_FILE)) {
    return _real_mq_send(mqdes, msg_ptr, msg_len, msg_prio);
  }

  int res;
  struct timespec ts;

  do {
    ASSERT_NE(-1, clock_gettime(CLOCK_REALTIME, &ts),
                 "clock_gettime(CLOCK_REALTIME) failed before mq_timedsend");
    ts.tv_sec += 1000;
    res = mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, &ts);
  } while (res == -1 && errno == ETIMEDOUT);
  return res;
}

extern "C"
ssize_t
mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned *msg_prio)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_FILE)) {
    return _real_mq_receive(mqdes, msg_ptr, msg_len, msg_prio);
  }

  ssize_t res;
  struct timespec ts;

  do {
    ASSERT_NE(-1, clock_gettime(CLOCK_REALTIME, &ts),
                 "clock_gettime(CLOCK_REALTIME) failed before mq_timedreceive");
    ts.tv_sec += 1000;
    res = mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio, &ts);
  } while (res == -1 && errno == ETIMEDOUT);
  return res;
}

static struct timespec ts_100ms = { 0, 100 * 1000 * 1000 };

static void
advanceTimeoutUpToDeadline(struct timespec *ts,
                           const struct timespec *abs_timeout)
{
  struct timespec candidate;
  TIMESPEC_ADD(ts, &ts_100ms, &candidate);
  if (TIMESPEC_CMP(&candidate, abs_timeout, >)) {
    *ts = *abs_timeout;
  } else {
    *ts = candidate;
  }
}

extern "C"
int
mq_timedsend(mqd_t mqdes,
             const char *msg_ptr,
             size_t msg_len,
             unsigned msg_prio,
             const struct timespec *abs_timeout)
{
  if (!internalPluginEnabled(INTERNAL_PLUGIN_FILE)) {
    return _real_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, abs_timeout);
  }

  struct timespec ts;
  int ret = -1;

  do {
    {
      WrapperLock wrapperLock;
      ASSERT_NE(-1, clock_gettime(CLOCK_REALTIME, &ts),
                   "clock_gettime(CLOCK_REALTIME) failed in mq_timedsend");
      if (TIMESPEC_CMP(&ts, abs_timeout, <=)) {
        advanceTimeoutUpToDeadline(&ts, abs_timeout);
      }
      ret = _real_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, &ts);
    }

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
  if (!internalPluginEnabled(INTERNAL_PLUGIN_FILE)) {
    return _real_mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio,
                                 abs_timeout);
  }

  struct timespec ts;
  int ret = -1;

  do {
    {
      WrapperLock wrapperLock;
      ASSERT_NE(-1, clock_gettime(CLOCK_REALTIME, &ts),
                   "clock_gettime(CLOCK_REALTIME) failed in mq_timedreceive");
      if (TIMESPEC_CMP(&ts, abs_timeout, <=)) {
        advanceTimeoutUpToDeadline(&ts, abs_timeout);
      }
      ret = _real_mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio, &ts);
    }

    if (ret != -1 || (ret == -1 && errno != ETIMEDOUT)) {
      return ret;
    }
  } while (TIMESPEC_CMP(&ts, abs_timeout, <));

  errno = ETIMEDOUT;
  return ret;
}
