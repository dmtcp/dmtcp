AC_INIT([DMTCP],[1.2.1],[],[dmtcp],[http://dmtcp.sourceforge.net])
AC_PROG_MAKE_SET
AC_PROG_LN_S
AC_PROG_MKDIR_P
AC_PROG_CC
AC_PROG_CXX
AC_CONFIG_FILES([Makefile test/Makefile test/testconfig.py])
AC_CONFIG_SUBDIRS([dmtcp])

dnl Autoconf manual says option checking is set to warn ("yes") by
dnl  by default.  But it's actually set to "no".
dnl So, we enforce our own choice ("fatal") if autoconf won't cooperate.
enable_option_checking=fatal
if test -n "$ac_unrecognized_opts"; then
  case $enable_option_checking in
    no) ;;
    fatal) { $as_echo "$as_me: error: unrecognized options: $ac_unrecognized_opts" >&2
   { (exit 1); exit 1; }; } ;;
    *)     $as_echo "$as_me: WARNING: unrecognized options: $ac_unrecognized_opts" >&2 ;;
  esac
fi
AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
                            [enable (very) verbose debug output
                             and write log files to \$DMTCP_TMPDIR (default is
                             disabled)])],
            [use_jassert=$enableval],
            [use_jassert=no])
AC_ARG_ENABLE([quiet],
            [AS_HELP_STRING([--enable-quiet],
                            [disable NOTE and WARNING (default is
                             to print NOTE, WARNING, but no TRACE)])],
            [use_quiet=$enableval],
            [use_quiet=no])
AC_ARG_ENABLE([timing],
            [AS_HELP_STRING([--enable-timing],
                            [record checkpoint/restart timing information
                            to jtimings.csv, in working directory of
                            dmtcp_coordinator, and to stderr.])],
            [use_jtiming=$enableval],
            [use_jtiming=no])
AC_ARG_ENABLE([unique_checkpoint_filenames],
            [AS_HELP_STRING([--enable-unique-checkpoint-filenames],
                            [By default, successive checkpoints are written
                            to the same filename.  Enable if each successive
			    checkpoint should be a unique filename.])],
            [use_unique_checkpoint_filenames=$enableval],
            [use_unique_checkpoint_filenames=no])

AC_ARG_ENABLE([pid_virtualization],
            [AS_HELP_STRING([--disable-pid-virtualization],
                            [disable pid virtualization.  Pid virtualization
			    is needed for checkpointing shells and certain
			    other programs.  Disable it for performance
			    reasons or for diagnosis of bugs.])],
            [use_pid_virt=$enableval],
            [use_pid_virt=yes])

if test "$use_pid_virt" = "yes"; then
  AC_SUBST([PID_VIRTUALIZATION], [yes])
else
  AC_SUBST([PID_VIRTUALIZATION], [no])
fi



AC_ARG_ENABLE([delta_compression],
            [AS_HELP_STRING([--enable-delta_compression],
                            [enable incremental/differential checkpointing using HBICT 
                            (Hash-based incremental checkpointing tool])],
            [use_deltacomp=$enableval],
            [use_deltacomp=no])

if test "$use_deltacomp" = "yes"; then
  AC_SUBST([HBICT_DELTACOMP], [yes])
else
  AC_SUBST([HBICT_DELTACOMP], [no])
fi


AC_ARG_ENABLE([ptrace_support],
            [AS_HELP_STRING([--enable-ptrace-support],
                            [enable ptrace support for gdb, valgrind, etc.
			    (EXPERIMENTAL)])],
            [use_ptrace_support=$enableval],
            [use_ptrace_support=no])
if test "$use_ptrace_support" = "yes"; then
  AC_SUBST([PTRACE_SUPPORT], [yes])
else
  AC_SUBST([PTRACE_SUPPORT], [no])
fi
AC_ARG_ENABLE([condor_support],
            [AS_HELP_STRING([--enable-condor-support],
                            [set configure options to support Condor.])],
            [use_condor_support=$enableval],
            [use_condor_support=no])
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
                             assumed to be external; follow poicy of 
                             external-socket-handling configure option.])],
            [use_stale_socket_handling=$enableval],
            [use_stale_socket_handling=yes])

AC_ARG_ENABLE([forked_checkpointing],
            [AS_HELP_STRING([--enable-forked-checkpointing],
                            [fork a child process to do checkpointing, so that
                            parent sees only minor delay during checkpoint
			    (experimental)])],
            [use_forked_ckpt=$enableval],
            [use_forked_ckpt=no])
AC_ARG_ENABLE([allocator],
            [AS_HELP_STRING([--enable-allocator],
                            [cause DMTCP to use a custom allocator based on mmap 
                             and avoid calling malloc and free (EXPERIMENTAL)])],
            [use_allocator=$enableval],
            [use_allocator=no])

#checkfor -lreadline -lhistory v5, does not require curses
AC_CHECK_LIB([readline], [readline], [linksReadline=yes], [linksReadline=no], [-lhistory -lcurses])
if test "$linksReadline" = "yes"; then
  AC_SUBST([HAS_READLINE], [yes])
else
  AC_SUBST([HAS_READLINE], [no])
fi

# Check if static compilation works.  (Needed for:  gcc -static mtcp_restart.c)
# Consider using AC_COMPILE_IFELSE with CFLAGS=[-static]
#  and AC_MSG_FAILURE in future version.
AC_MSG_CHECKING([if 'gcc -static' works])
AC_LANG_CONFTEST( [AC_LANG_PROGRAM()] )
$CC -static -o conftest$ac_exeext conftest.$ac_ext
AC_CHECK_FILE([conftest$ac_exeext], [], [static_libc=no])
if test x"$static_libc" = xno; then
  AC_MSG_ERROR(['gcc -static hello.c' failed.  No static libc.a found?
  On some distros (e.g. Fedora 11, 12, 13), libc.a is not present
    in default system, and must be added via the glibc-static package.])
fi

#check for dash
AC_CHECK_PROG(HAS_DASH, [dash], [yes], [no], [/bin])

#check for tcsh
AC_CHECK_PROG(HAS_TCSH, [tcsh], [yes], [no], [/bin])

#check for zsh
AC_CHECK_PROG(HAS_ZSH, [zsh], [yes], [no], [/bin])

#check for gcl
AC_CHECK_PROG(HAS_GCL, [gcl], [yes], [no], [$PATH:/usr/bin])
if test "$HAS_GCL" != "no"; then
  AC_PATH_PROG([GCL],  [gcl],        [no], [$PATH:/usr/bin])
fi

#check for script
AC_CHECK_PROG(HAS_SCRIPT, [script], [yes], [no], [/usr/bin])

#check for screen
AC_CHECK_PROG(HAS_SCREEN, [screen], [yes], [no], [/usr/bin])
AC_PATH_PROG([SCREEN],  [screen],        [no], [/usr/bin])

#check for matlab
AC_CHECK_PROG(HAS_MATLAB, [matlab], [yes], [no], [$PATH:/usr/bin])
if test "$HAS_MATLAB" != "no"; then
  AC_PATH_PROG([MATLAB],  [matlab],        [no], [$PATH:/usr/bin])
fi

#check for patch utility
AC_CHECK_PROG(HAS_PATCH, [patch], [yes], [no], [$PATH:/usr/bin:/bin])

if test "$HAS_PATCH" != "yes"; then
   AC_MSG_ERROR(
    [Executable, patch, not found:  consider adding pkg:  'patch'])
fi

#check for g++
AC_CHECK_PROG(HAS_CPP, [g++], [yes], [no], [$PATH:/usr/bin:/bin])

if test "$HAS_CPP" != "yes"; then
   AC_MSG_ERROR(
    [Executable, g++, not found:  consider adding pkg:  'g++'])
fi

#if /usr/include/linux/version.h is missing, give up on configuring.
AC_CHECK_HEADERS([linux/version.h], [], [AC_MSG_ERROR(
     [#include: <linux/version.h> not found: consider adding linux-libc-dev pkg]
    )])

#Add check for libc.a next.  MTCP uses it in build.

#check for gzip utility
AC_CHECK_PROG(HAS_GZIP, [gzip], [yes], [no], [/usr/bin:/bin])

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
  AC_SUBST([PTRACE_SUPPORT], [yes])
  AC_DEFINE([OVERRIDE_GLOBAL_ALLOCATOR],[],[Use a custom allocator based on mmap])
else
  AC_SUBST([RECORD_REPLAY], [no])
fi

#check for mpich2
AC_ARG_WITH([mpich],
            [AS_HELP_STRING([--with-mpich=path],
                            [Compile mpich2 tests in `make check`])],
            [mpich=$withval],
            [mpich=no])

if test "$mpich" != "no"; then
  mpich_path="$mpich:$mpich/bin"
  AC_PATH_PROG([MPICH_MPD],        [mpd],        [no], [$mpich_path])
  AC_PATH_PROG([MPICH_MPDBOOT],    [mpdboot],    [no], [$mpich_path])
  AC_PATH_PROG([MPICH_MPDALLEXIT], [mpdallexit], [no], [$mpich_path])
  AC_PATH_PROG([MPICH_MPDCLEANUP], [mpdcleanup], [no], [$mpich_path])
  AC_PATH_PROG([MPICH_MPICC],      [mpicc],      [no], [$mpich_path])
  AC_PATH_PROG([MPICH_MPIEXEC],    [mpiexec],    [no], [$mpich_path])

  if test "$MPICH_MPD" = "no"        ||\
     test "$MPICH_MPDBOOT" = "no"    ||\
     test "$MPICH_MPDALLEXIT" = "no" ||\
     test "$MPICH_MPDCLEANUP" = "no" ||\
     test "$MPICH_MPICC" = "no"      ||\
     test "$MPICH_MPIEXEC" = "no";
  then
    AC_MSG_ERROR([Invalid mpich path, use --with-mpich=<path>])
  fi

  AC_SUBST(HAS_MPICH, [yes])

else
  AC_SUBST(HAS_MPICH,[no])
fi

AC_ARG_ENABLE([m32],
            [AS_HELP_STRING([--enable-m32],
                            [Compile in 32 bit mode on 64 bit linux.])],
            [use_m32=$enableval],
            [use_m32=no])

if test "$use_m32" = "yes"; then
  AC_SUBST([M32], [1])
  AC_SUBST([HAS_READLINE], [no])
else
  AC_SUBST([M32], [0])
fi


AC_OUTPUT
