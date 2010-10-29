/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef CONSTANTS_H
#define CONSTANTS_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "linux/version.h"


// This macro (LIBC...) is also defined in ../jalib/jassert.cpp and should
// always be kept in sync with that.
#define LIBC_FILENAME "libc.so.6"
#define LIBPTHREAD_FILENAME "libpthread.so.0"

#define MTCP_FILENAME "libmtcp.so"
#define CKPT_FILE_PREFIX "ckpt_"
#define CKPT_FILE_SUFFIX ".dmtcp"
#define CKPT_FILES_SUBDIR_PREFIX "ckpt_"
#define CKPT_FILES_SUBDIR_SUFFIX "_files"
#define DELETED_FILE_SUFFIX " (deleted)"

#define X11_LISTENER_PORT_START 6000

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7779

#define RESTORE_PORT_START 9777
#define RESTORE_PORT_STOP 9977

//#define ENABLE_DLOPEN
//#define ENABLE_MALLOC_WRAPPER

//this next string can be at most 16 chars long
#define DMTCP_MAGIC_STRING "DMTCP_CKPT_V0\n"

//it should be safe to change any of these names
#define ENV_VAR_NAME_ADDR "DMTCP_HOST"
#define ENV_VAR_NAME_PORT "DMTCP_PORT"
#define ENV_VAR_NAME_RESTART_DIR  "DMTCP_RESTART_DIR"
#define ENV_VAR_CKPT_INTR "DMTCP_CHECKPOINT_INTERVAL"
#define ENV_VAR_SERIALFILE_INITIAL "DMTCP_INITSOCKTBL"
#define ENV_VAR_PIDTBLFILE_INITIAL "DMTCP_INITPIDTBL"
#define ENV_VAR_HIJACK_LIB "DMTCP_HIJACK_LIB"
#define ENV_VAR_CHECKPOINT_DIR "DMTCP_CHECKPOINT_DIR"
#define ENV_VAR_TMPDIR "DMTCP_TMPDIR"
#define ENV_VAR_CKPT_OPEN_FILES "DMTCP_CKPT_OPEN_FILES"
#define ENV_VAR_QUIET "DMTCP_QUIET"
#define ENV_VAR_ROOT_PROCESS "DMTCP_ROOT_PROCESS"


// it is not yet safe to change these; these names are hard-wired in the code
#define ENV_VAR_UTILITY_DIR "JALIB_UTILITY_DIR"
#define ENV_VAR_STDERR_PATH "JALIB_STDERR_PATH"
#define ENV_VAR_COMPRESSION "DMTCP_GZIP"
#define ENV_VAR_FORKED_CKPT "MTCP_FORKED_CHECKPOINT"
#define ENV_VAR_SIGCKPT "DMTCP_SIGCKPT"
#define ENV_VAR_LIBC_FUNC_OFFSETS "DMTCP_LIBC_FUNC_OFFSETS"

#define GLIBC_BASE_FUNC isalnum

//this list should be kept up to date with all "protected" environment vars
#define ENV_VARS_ALL \
    ENV_VAR_NAME_ADDR,\
    ENV_VAR_NAME_PORT,\
    ENV_VAR_CKPT_INTR,\
    ENV_VAR_SERIALFILE_INITIAL,\
    ENV_VAR_PIDTBLFILE_INITIAL,\
    ENV_VAR_HIJACK_LIB,\
    ENV_VAR_CHECKPOINT_DIR,\
    ENV_VAR_TMPDIR,\
    ENV_VAR_CKPT_OPEN_FILES,\
    ENV_VAR_QUIET,\
    ENV_VAR_UTILITY_DIR,\
    ENV_VAR_STDERR_PATH,\
    ENV_VAR_COMPRESSION,\
    ENV_VAR_SIGCKPT,\
    ENV_VAR_ROOT_PROCESS,\
    ENV_VAR_LIBC_FUNC_OFFSETS


#define DRAINER_CHECK_FREQ 0.1

#define DRAINER_WARNING_FREQ 10

#define SOCKET_DRAIN_MAGIC_COOKIE_STR "[dmtcp{v0<DRAIN!"

#define DMTCP_CHECKPOINT_CMD "dmtcp_checkpoint"

#define DMTCP_RESTART_CMD "dmtcp_restart"

#define RESTART_SCRIPT_BASENAME "dmtcp_restart_script"
#define RESTART_SCRIPT_EXT ".sh"

#define DMTCP_FILE_HEADER "DMTCP_CHECKPOINT_IMAGE_v1.10\n"

#define PROTECTED_FD_START 820
#define PROTECTED_FD_COUNT 14

#define CONNECTION_ID_START 99000

// Fix dlclose segfault bug
//#define MAX_DLCLOSE_MTCP_CALLS 10
#define MAX_DLCLOSE_MTCP_CALLS 1

// #define MIN_SIGNAL 1
// #define MAX_SIGNAL 30

//at least one of these must be enabled:
#define HANDSHAKE_ON_CONNECT    0
#define HANDSHAKE_ON_CHECKPOINT 1

#ifndef PID_VIRTUALIZATION
#define _real_getpid getpid
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9)
#define user_desc modify_ldt_ldt_s
#endif

#endif

