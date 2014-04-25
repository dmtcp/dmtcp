/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
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
#include <features.h>

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

#ifndef CKPT_SIGNAL
# define CKPT_SIGNAL SIGUSR2
#endif

// This macro (LIBC...) is also defined in ../jalib/jassert.cpp and should
// always be kept in sync with that.
#define LIBC_FILENAME "libc.so.6"
#define LIBPTHREAD_FILENAME "libpthread.so.0"

#define MTCP_FILENAME "libmtcp.so.1"
#define LIBDL_FILENAME "libdl.so.2"
#define CKPT_FILE_PREFIX "ckpt_"
#define CKPT_FILE_SUFFIX ".dmtcp"
#define CKPT_FILE_SUFFIX_LEN strlen(".dmtcp")
#define CKPT_FILES_SUBDIR_PREFIX "ckpt_"
#define CKPT_FILES_SUBDIR_SUFFIX "_files"
/* dmtcp_launch, dmtcp_restart return a unique rc (default: 99) */
#define DMTCP_FAIL_RC \
        (getenv("DMTCP_FAIL_RC") && atoi(getenv("DMTCP_FAIL_RC")) ? \
	 atoi(getenv("DMTCP_FAIL_RC")) : 99)

#define DMTCP_PRGNAME_PREFIX "DMTCP:"

// Not used
//#define X11_LISTENER_PORT_START 6000

// Virtual pids used by coordinator.
#define INITIAL_VIRTUAL_PID  40000
#define MAX_VIRTUAL_PID  400000000

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7779

// Matchup this definition with the one in plugins/ptrace/ptracewrappers.h
#define DMTCP_FAKE_SYSCALL 1023

//this next string can be at most 16 chars long
#define DMTCP_MAGIC_STRING "DMTCP_CKPT_V0\n"

//it should be safe to change any of these names
#define ENV_VAR_NAME_HOST "DMTCP_HOST"
#define ENV_VAR_NAME_PORT "DMTCP_PORT"
#define ENV_VAR_NAME_RESTART_DIR  "DMTCP_RESTART_DIR"
#define ENV_VAR_CKPT_INTR "DMTCP_CHECKPOINT_INTERVAL"
#define ENV_VAR_ORIG_LD_PRELOAD "DMTCP_ORIG_LD_PRELOAD"
#define ENV_VAR_HIJACK_LIBS "DMTCP_HIJACK_LIBS"
#define ENV_VAR_HIJACK_LIBS_M32 "DMTCP_HIJACK_LIBS_M32"
#define ENV_VAR_CHECKPOINT_DIR "DMTCP_CHECKPOINT_DIR"
#define ENV_VAR_TMPDIR "DMTCP_TMPDIR"
#define ENV_VAR_CKPT_OPEN_FILES "DMTCP_CKPT_OPEN_FILES"
#define ENV_VAR_PLUGIN "DMTCP_PLUGIN"
#define ENV_VAR_QUIET "DMTCP_QUIET"
#define ENV_VAR_PREFIX_ID "DMTCP_PREFIX_ID"
#define ENV_VAR_PREFIX_PATH "DMTCP_PREFIX_PATH"
#define ENV_VAR_DMTCP_DUMMY "DMTCP_DUMMY"
// Keep in sync with plugin/pid/pidwrappers.h
#define ENV_VAR_VIRTUAL_PID "DMTCP_VIRTUAL_PID"


// it is not yet safe to change these; these names are hard-wired in the code
#define ENV_VAR_UTILITY_DIR "JALIB_UTILITY_DIR"
#define ENV_VAR_STDERR_PATH "JALIB_STDERR_PATH"
#define ENV_VAR_COMPRESSION "DMTCP_GZIP"
#define ENV_VAR_ALLOC_PLUGIN "DMTCP_ALLOC_PLUGIN"
#define ENV_VAR_DL_PLUGIN "DMTCP_DL_PLUGIN"
#ifdef HBICT_DELTACOMP
  #define ENV_VAR_DELTACOMPRESSION "DMTCP_HBICT"
  #define ENV_DELTACOMPRESSION ENV_VAR_DELTACOMPRESSION
#else
  #define ENV_DELTACOMPRESSION
#endif

#define ENV_VAR_FORKED_CKPT "DMTCP_FORKED_CHECKPOINT"
#define ENV_VAR_SIGCKPT "DMTCP_SIGCKPT"
#define ENV_VAR_SCREENDIR "SCREENDIR"
#define ENV_VAR_DISABLE_UID_CHECKING "DMTCP_DISABLE_UID_CHECKING"

#define GLIBC_BASE_FUNC isalnum

#define LIBDL_BASE_FUNC dlinfo
#define LIBDL_BASE_FUNC_STR "dlinfo"
#define ENV_VAR_DLSYM_OFFSET "DMTCP_DLSYM_OFFSET"
#define ENV_VAR_DLSYM_OFFSET_M32 "DMTCP_DLSYM_OFFSET_M32"

//this list should be kept up to date with all "protected" environment vars
#define ENV_VARS_ALL \
    ENV_VAR_NAME_HOST,\
    ENV_VAR_NAME_PORT,\
    ENV_VAR_CKPT_INTR,\
    ENV_VAR_ORIG_LD_PRELOAD,\
    ENV_VAR_HIJACK_LIBS,\
    ENV_VAR_HIJACK_LIBS_M32,\
    ENV_VAR_PLUGIN, \
    ENV_VAR_CHECKPOINT_DIR,\
    ENV_VAR_TMPDIR,\
    ENV_VAR_CKPT_OPEN_FILES,\
    ENV_VAR_QUIET,\
    ENV_VAR_UTILITY_DIR,\
    ENV_VAR_STDERR_PATH,\
    ENV_VAR_COMPRESSION,\
    ENV_VAR_ALLOC_PLUGIN,\
    ENV_VAR_DL_PLUGIN,\
    ENV_VAR_SIGCKPT,\
    ENV_VAR_PREFIX_ID,\
    ENV_VAR_PREFIX_PATH,\
    ENV_VAR_SCREENDIR, \
    ENV_VAR_DLSYM_OFFSET, \
    ENV_VAR_DLSYM_OFFSET_M32, \
    ENV_VAR_VIRTUAL_PID, \
    ENV_DELTACOMPRESSION

#define DMTCP_RESTART_CMD "dmtcp_restart"

#define RESTART_SCRIPT_BASENAME "dmtcp_restart_script"
#define RESTART_SCRIPT_EXT "sh"

#define DMTCP_FILE_HEADER "DMTCP_CHECKPOINT_IMAGE_v2.0\n"

// #define MIN_SIGNAL 1
// #define MAX_SIGNAL 30

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9)
#define user_desc modify_ldt_ldt_s
#endif

#define DMTCP_VERSION_AND_COPYRIGHT_INFO                                       \
  BINARY_NAME " (DMTCP) " PACKAGE_VERSION "\n"                                 \
  "License LGPLv3+: GNU LGPL version 3 or later\n"                             \
  "    <http://gnu.org/licenses/lgpl.html>.\n"                                 \
  "This program comes with ABSOLUTELY NO WARRANTY.\n"                          \
  "This is free software, and you are welcome to redistribute it\n"            \
  "under certain conditions; see COPYING file for details.\n"

#define HELP_AND_CONTACT_INFO             \
  "Report bugs to: "PACKAGE_BUGREPORT"\n" \
  "DMTCP home page: <"PACKAGE_URL">\n"

#endif
