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

// IBV is for the Infiniband module. The overhead in DMTCP is essentially zero.
// Undefine this if for some reason it should interfere.
#define IBV

// Turn on coordinator NameService by default. In future, we will replace the
// logic in dmtcp_coordinator.cpp and dmtcp_worker.cpp to allow the coordinator
// to automatically detect when a worker wants to use NameService. If it does,
// the worker will go through two extra barriers in the coordinator (REGISTER
// and QUERY).
// Every worker that uses the NameService, must register _some_ data with the
// coordinator before it can do a query.
#define COORD_NAMESERVICE

#define FALSE 0
#define TRUE 1

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
/* dmtcp_checkpoint, dmtcp_restart return a unique rc (default: 99) */
#define DMTCP_FAIL_RC \
        (getenv("DMTCP_FAIL_RC") && atoi(getenv("DMTCP_FAIL_RC")) ? \
	 atoi(getenv("DMTCP_FAIL_RC")) : 99)

#define DMTCP_PRGNAME_PREFIX "DMTCP:"

#define X11_LISTENER_PORT_START 6000

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7779

#define RESTORE_PORT_START 9777
#define RESTORE_PORT_STOP 9977

#define ENABLE_MALLOC_WRAPPER

//this next string can be at most 16 chars long
#define DMTCP_MAGIC_STRING "DMTCP_CKPT_V0\n"

//it should be safe to change any of these names
#define ENV_VAR_NAME_HOST "DMTCP_HOST"
#define ENV_VAR_NAME_PORT "DMTCP_PORT"
#define ENV_VAR_NAME_RESTART_DIR  "DMTCP_RESTART_DIR"
#define ENV_VAR_CKPT_INTR "DMTCP_CHECKPOINT_INTERVAL"
#define ENV_VAR_SERIALFILE_INITIAL "DMTCP_INITSOCKTBL"
#define ENV_VAR_PIDTBLFILE_INITIAL "DMTCP_INITPIDTBL"
#define ENV_VAR_HIJACK_LIB "DMTCP_HIJACK_LIB"
#define ENV_VAR_CHECKPOINT_DIR "DMTCP_CHECKPOINT_DIR"
#define ENV_VAR_TMPDIR "DMTCP_TMPDIR"
#define ENV_VAR_CKPT_OPEN_FILES "DMTCP_CKPT_OPEN_FILES"
#define ENV_VAR_MODULE "DMTCP_MODULE"
#define ENV_VAR_QUIET "DMTCP_QUIET"
#define ENV_VAR_ROOT_PROCESS "DMTCP_ROOT_PROCESS"
#define ENV_VAR_PREFIX_ID "DMTCP_PREFIX_ID"
#define ENV_VAR_PREFIX_PATH "DMTCP_PREFIX_PATH"
#define ENV_VAR_DMTCP_DUMMY "DMTCP_DUMMY"
#define ENV_VAR_VIRTUAL_PID "DMTCP_VIRTUAL_PID"


// it is not yet safe to change these; these names are hard-wired in the code
#define ENV_VAR_UTILITY_DIR "JALIB_UTILITY_DIR"
#define ENV_VAR_STDERR_PATH "JALIB_STDERR_PATH"
#define ENV_VAR_COMPRESSION "DMTCP_GZIP"
#ifdef HBICT_DELTACOMP
  #define ENV_VAR_DELTACOMPRESSION "DMTCP_HBICT"
  #define ENV_DELTACOMPRESSION , ENV_VAR_DELTACOMPRESSION
#else
  #define ENV_DELTACOMPRESSION
#endif
#define ENV_VAR_FORKED_CKPT "MTCP_FORKED_CHECKPOINT"
#define ENV_VAR_SIGCKPT "DMTCP_SIGCKPT"
#define ENV_VAR_SCREENDIR "SCREENDIR"

#define GLIBC_BASE_FUNC isalnum

#define LIBDL_BASE_FUNC dlinfo
#define LIBDL_BASE_FUNC_STR "dlinfo"
#define ENV_VAR_DLSYM_OFFSET "DMTCP_DLSYM_OFFSET"

//this list should be kept up to date with all "protected" environment vars
#define ENV_VARS_ALL \
    ENV_VAR_NAME_HOST,\
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
    ENV_VAR_PREFIX_ID,\
    ENV_VAR_PREFIX_PATH,\
    ENV_VAR_SCREENDIR, \
    ENV_VAR_DLSYM_OFFSET, \
    ENV_VAR_VIRTUAL_PID, \
    ENV_DELTACOMPRESSION

#define DRAINER_CHECK_FREQ 0.1

#define DRAINER_WARNING_FREQ 10

#define SOCKET_DRAIN_MAGIC_COOKIE_STR "[dmtcp{v0<DRAIN!"

#define DMTCP_CHECKPOINT_CMD "dmtcp_checkpoint"

#define DMTCP_RESTART_CMD "dmtcp_restart"

#define RESTART_SCRIPT_BASENAME "dmtcp_restart_script"
#define RESTART_SCRIPT_EXT ".sh"

#define DMTCP_FILE_HEADER "DMTCP_CHECKPOINT_IMAGE_v1.10\n"

#define PROTECTED_FD_START 820
#define PROTECTED_FD_COUNT 15

#define CONNECTION_ID_START 99000

// Fix dlclose segfault bug
//#define MAX_DLCLOSE_MTCP_CALLS 10
#define MAX_DLCLOSE_MTCP_CALLS 1

// #define MIN_SIGNAL 1
// #define MAX_SIGNAL 30

//at least one of these must be enabled:
#define HANDSHAKE_ON_CONNECT    0
#define HANDSHAKE_ON_CHECKPOINT 1

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9)
#define user_desc modify_ldt_ldt_s
#endif

#define LIB_PRIVATE __attribute__ ((visibility ("hidden")))

#define DMTCP_VERSION_AND_COPYRIGHT_INFO                                        \
  BINARY_NAME " (DMTCP + MTCP) " PACKAGE_VERSION "\n"                           \
  "Copyright (C) 2006-2011  Jason Ansel, Michael Rieker, Kapil Arya, and\n"     \
  "                                                       Gene Cooperman\n"     \
  "This program comes with ABSOLUTELY NO WARRANTY.\n"                           \
  "This is free software, and you are welcome to redistribute it\n"             \
  "under certain conditions; see COPYING file for details.\n"

#define DMTCP_BANNER                                                            \
  DMTCP_VERSION_AND_COPYRIGHT_INFO                                              \
  "(Use flag \"-q\" to hide this message.)\n\n"

#endif
