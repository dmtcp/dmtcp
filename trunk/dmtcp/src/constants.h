/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/


#define LIBC_FILENAME "libc.so.6"
#define MTCP_FILENAME "mtcp.so"
#define CHECKPOINT_FILE_PREFIX "ckpt_"

#define DEFAULT_PORT 7779

#define RESTORE_PORT_START 9777
#define RESTORE_PORT_STOP 9977

//this next string can be at most 16 chars long
#define DMTCP_MAGIC_STRING "DMTCP_CKPT_V0\n"

//it should be safe to change any of these names
#define ENV_VAR_NAME_ADDR "DMTCP_HOST"
#define ENV_VAR_NAME_PORT "DMTCP_PORT"
#define ENV_VAR_NAME_CKPT_INTR "DMTCP_CHECKPOINT_INTERVAL"
#define ENV_VAR_SERIALFILE_INITIAL "DMTCP_INITSOCKTBL"
#define ENV_VAR_HIJACK_LIB "DMTCP_HIJACK_LIB"
#define ENV_VAR_CHECKPOINT_DIR "DMTCP_CHECKPOINT_DIR"

// it is not yet safe to change thse
#define ENV_VAR_UTILITY_DIR "JALIB_UTILITY_DIR"
#define ENV_VAR_STDERR_PATH "JALIB_STDERR_PATH"
#define ENV_VAR_COMPRESSION "DMTCP_GZIP"
#define ENV_VAR_SIGCKPT "DMTCP_SIGCKPT"

//this list should be kept up to data with all "protected" environment vars
#define ENV_VARS_ALL \
    ENV_VAR_NAME_ADDR,\
    ENV_VAR_NAME_PORT,\
    ENV_VAR_NAME_CKPT_INTR,\
    ENV_VAR_SERIALFILE_INITIAL,\
    ENV_VAR_HIJACK_LIB,\
    ENV_VAR_CHECKPOINT_DIR,\
    ENV_VAR_UTILITY_DIR,\
    ENV_VAR_STDERR_PATH,\
    ENV_VAR_COMPRESSION,\
    ENV_VAR_SIGCKPT

#define DRAINER_CHECK_FREQ 0.1

#define DRAINER_WARNING_FREQ 10

#define SOCKET_DRAIN_MAGIC_COOKIE_STR "[dmtcp{v0<DRAIN!"

#define DMTCP_CHECKPOINT_CMD "dmtcp_checkpoint"

#define DMTCP_RESTART_CMD "dmtcp_restart"

#define RESTART_SCRIPT_NAME "dmtcp_restart_script.sh"

#define PROTECTED_FD_START 820
#define PROTECTED_FD_COUNT 8

#define CONNECTION_ID_START 99000

// #define MIN_SIGNAL 1
// #define MAX_SIGNAL 30

//at least one of these must be enabled:
#define HANDSHAKE_ON_CONNECT    0
#define HANDSHAKE_ON_CHECKPOINT 1

