/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
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

#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <limits.h>
#include "constants.h"
#include "syscallwrappers.h"
#include "util.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"

#ifdef RECORD_REPLAY
#include "synchronizationlogging.h"
#include <sys/mman.h>
#include <sys/syscall.h>

static __thread bool ok_to_log_getpwnam = false;
static __thread bool ok_to_log_getpwuid = false;
static __thread bool ok_to_log_getgrnam = false;
static __thread bool ok_to_log_getgrgid = false;

extern "C" int getsockname(int sockfd, struct sockaddr *addr,
                           socklen_t *addrlen)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  WRAPPER_HEADER_CKPT_DISABLED(int, getsockname, _real_getsockname,
                               sockfd, addr, addrlen);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(getsockname);
    if (retval != -1) {
      *addr = GET_FIELD(currentLogEntry, getsockname, ret_addr);
      *addrlen = GET_FIELD(currentLogEntry, getsockname, ret_addrlen);
    }
    WRAPPER_REPLAY_END(getsockname);
  } else if (SYNC_IS_RECORD) {
    retval = _real_getsockname(sockfd, addr, addrlen);
    if (retval != -1) {
      SET_FIELD2(my_entry, getsockname, ret_addr, *addr);
      SET_FIELD2(my_entry, getsockname, ret_addrlen, *addrlen);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" int getpeername(int sockfd, struct sockaddr *addr,
                           socklen_t *addrlen)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  WRAPPER_HEADER_CKPT_DISABLED(int, getpeername, _real_getpeername,
                               sockfd, addr, addrlen);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(getpeername);
    if (retval != -1) {
      *addr = GET_FIELD(currentLogEntry, getpeername, ret_addr);
      *addrlen = GET_FIELD(currentLogEntry, getpeername, ret_addrlen);
    }
    WRAPPER_REPLAY_END(getpeername);
  } else if (SYNC_IS_RECORD) {
    retval = _real_getpeername(sockfd, addr, addrlen);
    if (retval != -1) {
      SET_FIELD2(my_entry, getpeername, ret_addr, *addr);
      SET_FIELD2(my_entry, getpeername, ret_addrlen, *addrlen);
    }
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

extern "C" struct passwd *getpwnam(const char *name)
{
  static __thread char buf[4096 * 4];
  static __thread struct passwd pwd;
  static __thread struct passwd *result;

  if (!SYNC_IS_NOOP) ok_to_log_getpwnam = true;
  int res = getpwnam_r(name, &pwd, buf, sizeof(buf), &result);
  ok_to_log_getpwnam = false;

  if (res == 0) {
    return result;
  }

  return NULL;
}

extern "C" struct passwd *getpwuid(uid_t uid)
{
  static __thread char buf[4096 * 4];
  static __thread struct passwd pwd;
  static __thread struct passwd *result;

  if (!SYNC_IS_NOOP) ok_to_log_getpwuid = true;
  int res = getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);
  ok_to_log_getpwuid = false;

  if (res == 0) {
    return &pwd;
  }

  return NULL;
}

extern "C" struct group *getgrnam(const char *name)
{
  static __thread char buf[4096 * 4];
  static __thread struct group grp;
  static __thread struct group *result;

  if (!SYNC_IS_NOOP) ok_to_log_getgrnam = true;
  int res = getgrnam_r(name, &grp, buf, sizeof(buf), &result);
  ok_to_log_getgrnam = false;

  if (res == 0) {
    return &grp;
  }

  return NULL;
}

extern "C" struct group *getgrgid(gid_t gid)
{
  static __thread char buf[4096 * 4];
  static __thread struct group grp;
  static __thread struct group *result;

  if (!SYNC_IS_NOOP) ok_to_log_getgrgid = true;
  int res = getgrgid_r(gid, &grp, buf, sizeof(buf), &result);
  ok_to_log_getgrgid = false;

  if (res == 0) {
    return &grp;
  }

  return NULL;
}

extern "C" int getpwnam_r(const char *name, struct passwd *pwd,
                          char *buf, size_t buflen, struct passwd **result)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if ((!shouldSynchronize(return_addr) ||
       jalib::Filesystem::GetProgramName() == "gdb") &&
      !ok_to_log_getpwnam) {
    return _real_getpwnam_r(name, pwd, buf, buflen, result);
  }
  int retval;
  log_entry_t my_entry = create_getpwnam_r_entry(my_clone_id,
      getpwnam_r_event, name, pwd, buf, buflen, result);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(getpwnam_r);
    if (retval == 0 &&
        GET_FIELD(currentLogEntry, getpwnam_r, ret_result) != NULL) {
      *pwd = GET_FIELD(currentLogEntry, getpwnam_r, ret_pwd);
      WRAPPER_REPLAY_READ_FROM_READ_LOG(getpwnam_r, buf, buflen);
    }
    *result = GET_FIELD(currentLogEntry, getpwnam_r, ret_result);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_getpwnam_r(name, pwd, buf, buflen, result);
    isOptionalEvent = false;
    if (retval == 0 && result != NULL) {
      SET_FIELD2(my_entry, getpwnam_r, ret_pwd, *pwd);
      WRAPPER_LOG_WRITE_INTO_READ_LOG(getpwnam_r, buf, buflen);
    }
    SET_FIELD2(my_entry, getpwnam_r, ret_result, *result);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int getpwuid_r(uid_t uid, struct passwd *pwd,
                          char *buf, size_t buflen, struct passwd **result)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if ((!shouldSynchronize(return_addr) ||
       jalib::Filesystem::GetProgramName() == "gdb") &&
      !ok_to_log_getpwuid) {
    return _real_getpwuid_r(uid, pwd, buf, buflen, result);
  }
  int retval;
  log_entry_t my_entry = create_getpwuid_r_entry(my_clone_id,
      getpwuid_r_event, uid, pwd, buf, buflen, result);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(getpwuid_r);
    if (retval == 0 &&
        GET_FIELD(currentLogEntry, getpwuid_r, ret_result) != NULL) {
      *pwd = GET_FIELD(currentLogEntry, getpwuid_r, ret_pwd);
      WRAPPER_REPLAY_READ_FROM_READ_LOG(getpwuid_r, buf, buflen);
    }
    *result = GET_FIELD(currentLogEntry, getpwuid_r, ret_result);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_getpwuid_r(uid, pwd, buf, buflen, result);
    isOptionalEvent = false;
    if (retval == 0 && result != NULL) {
      SET_FIELD2(my_entry, getpwuid_r, ret_pwd, *pwd);
      WRAPPER_LOG_WRITE_INTO_READ_LOG(getpwuid_r, buf, buflen);
    }
    SET_FIELD2(my_entry, getpwuid_r, ret_result, *result);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int getgrnam_r(const char *name, struct group *grp,
                          char *buf, size_t buflen, struct group **result)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if ((!shouldSynchronize(return_addr) ||
       jalib::Filesystem::GetProgramName() == "gdb") &&
      !ok_to_log_getgrnam) {
    return _real_getgrnam_r(name, grp, buf, buflen, result);
  }
  int retval;
  log_entry_t my_entry = create_getgrnam_r_entry(my_clone_id,
      getgrnam_r_event, name, grp, buf, buflen, result);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(getgrnam_r);
    if (retval == 0 &&
        GET_FIELD(currentLogEntry, getgrnam_r, ret_result) != NULL) {
      *grp = GET_FIELD(currentLogEntry, getgrnam_r, ret_grp);
      WRAPPER_REPLAY_READ_FROM_READ_LOG(getgrnam_r, buf, buflen);
    }
    *result = GET_FIELD(currentLogEntry, getgrnam_r, ret_result);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_getgrnam_r(name, grp, buf, buflen, result);
    isOptionalEvent = false;
    if (retval == 0 && result != NULL) {
      SET_FIELD2(my_entry, getgrnam_r, ret_grp, *grp);
      WRAPPER_LOG_WRITE_INTO_READ_LOG(getgrnam_r, buf, buflen);
    }
    SET_FIELD2(my_entry, getgrnam_r, ret_result, *result);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" int getgrgid_r(gid_t gid, struct group *grp,
                          char *buf, size_t buflen, struct group **result)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if ((!shouldSynchronize(return_addr) ||
       jalib::Filesystem::GetProgramName() == "gdb") &&
      !ok_to_log_getgrgid) {
    return _real_getgrgid_r(gid, grp, buf, buflen, result);
  }
  int retval;
  log_entry_t my_entry = create_getgrgid_r_entry(my_clone_id,
      getgrgid_r_event, gid, grp, buf, buflen, result);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY(getgrgid_r);
    if (retval == 0 &&
        GET_FIELD(currentLogEntry, getgrgid_r, ret_result) != NULL) {
      *grp = GET_FIELD(currentLogEntry, getgrgid_r, ret_grp);
      WRAPPER_REPLAY_READ_FROM_READ_LOG(getgrgid_r, buf, buflen);
    }
    *result = GET_FIELD(currentLogEntry, getgrgid_r, ret_result);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_getgrgid_r(gid, grp, buf, buflen, result);
    isOptionalEvent = false;
    if (retval == 0 && result != NULL) {
      SET_FIELD2(my_entry, getgrgid_r, ret_grp, *grp);
      WRAPPER_LOG_WRITE_INTO_READ_LOG(getgrgid_r, buf, buflen);
    }
    SET_FIELD2(my_entry, getgrgid_r, ret_result, *result);
    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

#define ADDRINFO_MAX_RES 32
struct addrinfo_extended {
  struct addrinfo *_addrinfo_p;
  struct addrinfo _addrinfo;
  struct sockaddr _sockaddr;
  char canonname[256];
};

extern "C" int getaddrinfo(const char *node, const char *service,
                           const struct addrinfo *hints, struct addrinfo **res)
{
  struct addrinfo_extended addrinfo_res[ADDRINFO_MAX_RES];
  struct addrinfo *rp;
  int numResults = 0;

  WRAPPER_HEADER(int, getaddrinfo, _real_getaddrinfo, node, service, hints,
                 res);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(getaddrinfo);
    if (retval == 0) {
      *res = GET_FIELD(currentLogEntry, getaddrinfo, ret_res);
      numResults = GET_FIELD(currentLogEntry, getaddrinfo, num_res);

      WRAPPER_REPLAY_READ_FROM_READ_LOG(getaddrinfo, (void*) addrinfo_res,
                                        (numResults *
                                         sizeof (struct addrinfo_extended)));
      for (int i = 0; i < numResults; i++) {
        struct addrinfo_extended *ext_info = &addrinfo_res[i];
        struct addrinfo *_addrinfo = &(addrinfo_res[i]._addrinfo);
        struct sockaddr *_sockaddr = &(addrinfo_res[i]._sockaddr);
        memcpy(ext_info->_addrinfo_p, _addrinfo, sizeof(struct addrinfo));
        memcpy(_addrinfo->ai_addr, _sockaddr, _addrinfo->ai_addrlen);
        if (_addrinfo->ai_canonname != NULL) {
          strncpy(_addrinfo->ai_canonname, ext_info->canonname,
                  sizeof(ext_info->canonname));
        }
      }
    }
    WRAPPER_REPLAY_END(getaddrinfo);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_getaddrinfo(node, service, hints, res);
    isOptionalEvent = false;

    if (retval == 0) {
      SET_FIELD2(my_entry, getaddrinfo, ret_res, *res);
      for (rp = *res; rp != NULL; rp = rp->ai_next) {
        JASSERT(numResults < ADDRINFO_MAX_RES);
        struct addrinfo_extended *ext_info = &addrinfo_res[numResults];
        struct addrinfo *_addrinfo = &(addrinfo_res[numResults]._addrinfo);
        struct sockaddr *_sockaddr = &(addrinfo_res[numResults]._sockaddr);
        ext_info->_addrinfo_p = rp;
        memcpy(_addrinfo, rp, sizeof (struct addrinfo));
        memcpy(_sockaddr, rp->ai_addr, rp->ai_addrlen);
        if (rp->ai_canonname != NULL) {
          strncpy(ext_info->canonname, rp->ai_canonname,
                  sizeof(ext_info->canonname));
        }
        numResults++;
      }
      WRAPPER_LOG_WRITE_INTO_READ_LOG(getaddrinfo, (void*) addrinfo_res,
                                      (numResults *
                                       sizeof (struct addrinfo_extended)));
    }
    SET_FIELD2(my_entry, getaddrinfo, num_res, numResults);

    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;
}

extern "C" void freeaddrinfo(struct addrinfo *res)
{
  WRAPPER_HEADER_VOID(freeaddrinfo, _real_freeaddrinfo, res);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_VOID(freeaddrinfo);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    _real_freeaddrinfo(res);
    isOptionalEvent = false;
    WRAPPER_LOG_WRITE_ENTRY_VOID(my_entry);
  }
}

extern "C" int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                           char *host, socklen_t hostlen,
                           char *serv, socklen_t servlen, unsigned int flags)
{
  WRAPPER_HEADER(int, getnameinfo, _real_getnameinfo, sa, salen, host, hostlen,
                 serv, servlen, flags);
  if (SYNC_IS_REPLAY) {
    WRAPPER_REPLAY_START(getnameinfo);
    if (retval == 0 && host != NULL) {
      strncpy(host, GET_FIELD(currentLogEntry, getnameinfo, ret_host), hostlen);
    }
    if (retval == 0 && host != NULL) {
      strncpy(serv, GET_FIELD(currentLogEntry, getnameinfo, ret_serv), servlen);
    }
    WRAPPER_REPLAY_END(getnameinfo);
  } else if (SYNC_IS_RECORD) {
    isOptionalEvent = true;
    retval = _real_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
    isOptionalEvent = false;

    if (retval == 0 && host != NULL) {
      strncpy(GET_FIELD(my_entry, getnameinfo, ret_host), host, hostlen);
    }
    if (retval == 0 && host != NULL) {
      strncpy(GET_FIELD(my_entry, getnameinfo, ret_serv), serv, servlen);
    }

    WRAPPER_LOG_WRITE_ENTRY(my_entry);
  }
  return retval;

}
#endif
