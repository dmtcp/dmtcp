AC_INIT([DMTCP],[1.2.4],[],[dmtcp],[http://dmtcp.sourceforge.net])
AC_PREREQ([2.60])
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE
AM_CONFIG_HEADER([src/config.h])
#AM_PROG_LIBTOOL

AC_LANG_CPLUSPLUS
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CC_C_O
AC_PROG_RANLIB

AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
                            [enable (very) verbose debug output
                             and write log files to $DMTCP_TMPDIR (default is
                             disabled)])],
            [use_jassert=$enableval],
            [use_jassert=no])

if test "$use_jassert" = "yes"; then
  AC_DEFINE([DEBUG],[],[Verbose debug output and log files in $DMTCP_TMPDIR])
  CFLAGS="-g -O0 -Wall"
  CXXFLAGS="-g -O0 -Wall"
fi

AC_ARG_ENABLE([quiet],
            [AS_HELP_STRING([--enable-quiet],
                            [disable NOTE and WARNING (default is
                             to print NOTE, WARNING, but no TRACE)])],
            [use_quiet=$enableval],
            [use_quiet=no])

if test "$use_quiet" = "yes"; then
  AC_DEFINE([QUIET],[],[No output, not even NOTE and WARNING])
fi

AC_ARG_ENABLE([timing],
            [AS_HELP_STRING([--enable-timing],
                            [record checkpoint/restart timing information
                            to jtimings.csv, in working directory of
                            dmtcp_coordinator, and to stderr.])],
            [use_jtiming=$enableval],
            [use_jtiming=no])

if test "$use_jtiming" = "yes"; then
  AC_DEFINE([TIMING],[],[Record timing information to stderr and jtimings.csv])
fi

AC_ARG_ENABLE([condor_support],
            [AS_HELP_STRING([--enable-condor-support],
                            [set configure options to support Condor.])],
            [use_condor_support=$enableval],
            [use_condor_support=no])

AC_ARG_ENABLE([unique_checkpoint_filenames],
            [AS_HELP_STRING([--enable-unique-checkpoint-filenames],
                            [By default, successive checkpoints are written
                            to the same filename.  Enable if each successive
			    checkpoint should be a unique filename.])],
            [use_unique_checkpoint_filenames=$enableval],
            [use_unique_checkpoint_filenames=no])

if test "$use_unique_checkpoint_filenames" = "yes"; then
  AC_DEFINE([UNIQUE_CHECKPOINT_FILENAMES],[],[Use unique filenames for checkpoint images])
fi

AC_ARG_ENABLE([ptrace_support],
            [AS_HELP_STRING([--enable-ptrace-support],
                            [enable ptrace support for gdb, valgrind, etc.
			    (EXPERIMENTAL)])],
            [use_ptrace_support=$enableval],
            [use_ptrace_support=no])

if test "$use_ptrace_support" = "yes"; then
  AC_DEFINE([PTRACE],[],[Use support for ptrace system call.])
fi

AC_ARG_ENABLE([forked_checkpointing],
            [AS_HELP_STRING([--enable-forked-checkpointing],
                            [fork a child process to do checkpointing, so that
                            parent sees only minor delay during checkpoint.
                            (EXPERIMENTAL)])],
            [use_forked_ckpt=$enableval],
            [use_forked_ckpt=no])

if test "$use_forked_ckpt" = "yes"; then
  AC_DEFINE([FORKED_CHECKPOINTING],[],[Child process does checkpointing])
fi

AC_ARG_ENABLE([delta_compression],
            [AS_HELP_STRING([--enable-delta_compression],
                            [enable incremental/differential checkpointing
			     using HBICT (hash-based incremental checkpointing)
			     tool; disables default gzip compression
			     (EXPERIMENTAL)])],
            [use_deltacomp=$enableval],
            [use_deltacomp=no])

if test "$use_deltacomp" = "yes"; then
  AC_DEFINE([HBICT_DELTACOMP],[],[Use delta compression.])
fi

AC_ARG_ENABLE([experts_only_space1], [], [], [])
AC_ARG_ENABLE([experts_only],
            [AS_HELP_STRING(
             [               === **** NOTE:  EXPERTS ONLY BELOW HERE **** ===],
			    [(Use at your own risk!!)])],
	    [], [])
AC_ARG_ENABLE([experts_only_after], [AS_HELP_STRING([ ], [])], [], [])

AC_ARG_ENABLE([pid_virtualization],
            [AS_HELP_STRING([--disable-pid-virtualization],
                            [disable pid virtualization.  Pid virtualization
                            is _required_ for checkpointing shells and for many
                            programs using fork().  Disable it only for
                            performance reasons or for diagnosis of bugs.])],
            [use_pid_virt=$enableval],
            [use_pid_virt=yes])

AC_ARG_ENABLE([external_socket_handling],
            [AS_HELP_STRING([--enable-external-socket-handling],
                            [wait for connection to external peer to close;
			     expensive if stale-socket-handling
			     also enabled.])],
            [use_external_socket_handling=$enableval],
            [use_external_socket_handling=no])
AC_ARG_ENABLE([stale_socket_handling],
            [AS_HELP_STRING([--disable-stale-socket-handling],
                            [disable stale socket handling; unknown peer
			     assumed to be external; follow policy of
			     external-socket-handling configure option.])],
            [use_stale_socket_handling=$enableval],
            [use_stale_socket_handling=yes])

if test "$use_external_socket_handling" = "yes"; then
  AC_DEFINE([EXTERNAL_SOCKET_HANDLING],[],[Socket with no peer is external; wait until communication finished])
fi
if test "$use_stale_socket_handling" = "yes"; then
  AC_DEFINE([STALE_SOCKET_HANDLING],[],[Socket with no peer is stale (no peer exists); ignore socket])
fi
#Force other config options for Condor
if test "$use_condor_support" = "yes"; then
  AC_DEFINE([EXTERNAL_SOCKET_HANDLING],[yes])
  AC_DEFINE([STALE_SOCKET_HANDLING],[no])
fi

if test "$use_pid_virt" = "yes"; then
  AC_DEFINE([PID_VIRTUALIZATION],[],[Use virtual pids for pid-related system calls.])
fi

AC_ARG_ENABLE([allocator],
            [AS_HELP_STRING([--enable-allocator],
                            [cause DMTCP to use a custom allocator based on
                             mmap and avoid calling malloc and free.
			     (EXPERIMENTAL)])],
            [use_allocator=$enableval],
            [use_allocator=no])

if test "$use_allocator" = "yes"; then
  AC_DEFINE([OVERRIDE_GLOBAL_ALLOCATOR],[],[Use a custom allocator based on mmap])
fi

#check for record-replay capability
# REPLACE BY FOLLOWING WHEN READY TO SUPPORT THIS EXTERNALLY:
#            [AS_HELP_STRING([--with-record-replay],
#                            [support record replay (experimental)])],
AC_ARG_WITH([record-replay],
            [AS_HELP_STRING([--experts-only-flag], [for experts only])],
            [record_replay=$withval],
            [record_replay=no])

if test "$record_replay" != "no"; then
  AC_DEFINE([RECORD_REPLAY],[],[Record to log and then replay.])
  AC_DEFINE([PTRACE],[],[Use support for ptrace system call.])
  AC_DEFINE([OVERRIDE_GLOBAL_ALLOCATOR],[],[Use a custom allocator based on mmap])
else
  AC_SUBST([RECORD_REPLAY], [no])
fi
AM_CONDITIONAL(RECORD_REPLAY, [test "$record_replay" = "yes"])
AM_CONDITIONAL(HAS_I486,
  [test `uname -m` == i686 -o  `uname -m` == i586 -o `uname -m` == i486])

AC_ARG_ENABLE([m32],
            [AS_HELP_STRING([--enable-m32],
                            [Compile in 32 bit mode on 64 bit linux.])],
            [use_m32=$enableval],
            [use_m32=no])

AM_CONDITIONAL(CONFIG_M32, [test "$use_m32" = "yes"])
if test "$use_m32" = "yes"; then
  AC_DEFINE([CONFIG_M32],[],[Compiling in 32 bit mode on 64 bit linux.])
fi
  
  
AC_DEFINE([DMTCP],[],[Always enable this.])

AC_OUTPUT(Makefile src/Makefile)
