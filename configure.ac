AC_INIT([DMTCP],[1.2.3],[],[dmtcp],[http://dmtcp.sourceforge.net])
AC_PREREQ([2.60])
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE
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

if test "$use_jassert" = "yes"; then
  AC_SUBST([DEBUG], [yes])
else
  AC_SUBST([DEBUG], [no])
fi

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

AC_ARG_ENABLE([forked_checkpointing],
            [AS_HELP_STRING([--enable-forked-checkpointing],
                            [fork a child process to do checkpointing, so that
                            parent sees only minor delay during checkpoint.
			    (EXPERIMENTAL)])],
            [use_forked_ckpt=$enableval],
            [use_forked_ckpt=no])

AC_ARG_ENABLE([fast_ckpt_restart],
            [AS_HELP_STRING([--enable-fast-ckpt-restart],
                            [uses tricks to mmap from checkpoint image file
			    (EXPERIMENTAL)])],
            [use_fast_ckpt_restart=$enableval],
            [use_fast_ckpt_restart=default])
if test "$use_fast_ckpt_restart" = "yes"; then
  AC_SUBST([FAST_CKPT_RESTART], [yes])
else
  AC_SUBST([FAST_CKPT_RESTART], [no])
fi

AC_ARG_ENABLE([delta_compression],
            [AS_HELP_STRING([--enable-delta-compression],
                            [enable incremental/differential checkpointing
			     using HBICT (hash-based incremental checkpointing)
			     tool; disables default gzip compression
			     (EXPERIMENTAL)])],
            [use_deltacomp=$enableval],
            [use_deltacomp=no])

if test "$use_deltacomp" = "yes"; then
  AC_SUBST([HBICT_DELTACOMP], [yes])
else
  AC_SUBST([HBICT_DELTACOMP], [no])
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

if test "$use_pid_virt" = "yes"; then
  AC_SUBST([PID_VIRTUALIZATION], [yes])
else
  AC_SUBST([PID_VIRTUALIZATION], [no])
fi

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
                             external-socket-handling configure option.
			     (OBSOLETE: Do not use)])],
            [use_stale_socket_handling=$enableval],
            [use_stale_socket_handling=yes])

AC_ARG_ENABLE([allocator],
            [AS_HELP_STRING([--enable-allocator],
                            [cause DMTCP to use a custom allocator based on mmap 
                             and avoid calling malloc and free (EXPERIMENTAL)])],
            [use_allocator=$enableval],
            [use_allocator=no])

#checkfor -lreadline -lhistory v5, does not require curses
AC_CHECK_LIB([readline], [readline], [linksReadline=yes], [linksReadline=no], [-lhistory -lcurses])
AC_CHECK_HEADERS([readline/readline.h readline/history.h], [includeReadline=no],)
if test "$linksReadline" = "yes" -a "$includeReadline" != "no"; then
  AC_SUBST([HAS_READLINE], [yes])
else
  AC_SUBST([HAS_READLINE], [no])
fi

# Check if static compilation works.  (Needed for:  gcc -static mtcp_restart.c)
# Consider using AC_COMPILE_IFELSE with CFLAGS=[-static]
#  and AC_MSG_FAILURE in future version.
#AC_MSG_CHECKING([if 'gcc -static' works])
#AC_LANG_CONFTEST( [AC_LANG_PROGRAM()] )
#$CC -static -o conftest$ac_exeext conftest.$ac_ext
#AC_CHECK_FILE([conftest$ac_exeext], [], [static_gcc=no])
#if test x"$static_gcc" = xno; then
#  AC_MSG_ERROR(['gcc -static hello.c' failed.
#fi

#check for python
AC_CHECK_PROG(HAS_PYTHON, [python], [yes], [no], [/usr/bin])

#check for dash
AC_CHECK_PROG(HAS_DASH, [dash], [yes], [no], [/bin])

#check for tcsh
AC_CHECK_PROG(HAS_TCSH, [tcsh], [yes], [no], [/bin])

#check for zsh
AC_CHECK_PROG(HAS_ZSH, [zsh], [yes], [no], [/bin])

#check for vim
AC_CHECK_PROG(HAS_VIM, [vim], [yes], [no], [/usr/bin:/bin])
AC_PATH_PROG([VIM],    [vim],        [no], [/usr/bin:/bin])

#check for emacs
AC_CHECK_PROG(HAS_EMACS, [emacs], [yes], [no], [/usr/bin])

#check for script
AC_CHECK_PROG(HAS_SCRIPT, [script], [yes], [no], [/usr/bin])

#check for screen
AC_CHECK_PROG(HAS_SCREEN, [screen], [yes], [no], [/usr/bin])
AC_PATH_PROG([SCREEN],    [screen],        [no], [/usr/bin])

#check for strace
AC_CHECK_PROG(HAS_STRACE, [strace], [yes], [no], [/usr/bin])

#check for gdb
AC_CHECK_PROG(HAS_GDB, [gdb], [yes], [no], [/usr/bin])

#check for gcl
AC_CHECK_PROG(HAS_GCL, [gcl], [yes], [no], [$PATH:/usr/bin])
AC_PATH_PROG([GCL],    [gcl],        [no], [$PATH:/usr/bin])

#check for matlab
AC_CHECK_PROG(HAS_MATLAB, [matlab], [yes], [no], [$PATH:/usr/bin])
AC_PATH_PROG([MATLAB],    [matlab],        [no], [$PATH:/usr/bin])

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

#check for gzip utility
AC_CHECK_PROG(HAS_GZIP, [gzip], [yes], [no], [/usr/bin:/bin])

#check for record-replay capability
# REPLACE BY FOLLOWING WHEN READY TO SUPPORT THIS EXTERNALLY:
#            [AS_HELP_STRING([--with-record-replay],
#                            [support record replay (EXPERIMENTAL)
#			      forces fast_ckpt_restart unless --disable-fast-ckpt-restart specified])],
AC_ARG_WITH([record-replay],
            [AS_HELP_STRING([--experts-only-flag], [for experts only])],
            [record_replay=$withval],
            [record_replay=no])

if test "$record_replay" != "no"; then
  AC_DEFINE([RECORD_REPLAY],[],[Record to log and then replay.])
  AC_DEFINE([PTRACE],[],[Use support for ptrace system call.])
  AC_SUBST([PTRACE_SUPPORT], [yes])
  dnl *** THIS CODE WOULD --enable-fast-ckpt-restart IF --with-record-replay
  dnl *** THIS WORKED WHEN FReD LOOKED ONLY AT libc.so IN /proc/*/mmap
  dnl *** FReD NOW WANTS TO SEE test_list IN /proc/*/mmap
  dnl *** IN FUTURE, FReD WILL GET THOSE ADDRSSES AT CKPT TIME (NOT RESTART),
  dnl ***    AND WE CAN THEN RE-ENABLE THIS SECTION.
  dnl if test "$use_fast_ckpt_restart" != "no"; then
  dnl   dnl FAST_CKPT_RESTART is based on mmap
  dnl   AC_SUBST([FAST_CKPT_RESTART], [yes])
  dnl else
  dnl   AC_SUBST([FAST_CKPT_RESTART], [no])
  dnl fi
  AC_DEFINE([OVERRIDE_GLOBAL_ALLOCATOR],[],[Use a custom allocator based on mmap])
else
  AC_SUBST([RECORD_REPLAY], [no])
fi
echo "use_fast_ckpt_restart": $use_fast_ckpt_restart

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

#check for OpenMPI
#Debian uses /usr/bin/mpicc.openmpi.  Red Hat/SuSe store it elsewhere.
MPI_PATH=/usr/bin:/opt/openmpi/bin/:/usr/lib64/openmpi/bin:/usr/lib/openmpi/bin
MPI_PATH=$MPI_PATH:/usr/lib64/mpi/gcc/openmpi/bin:/usr/lib/mpi/gcc/openmpi/bin
AC_PATH_PROGS([OPENMPI_MPICC],  [mpicc.openmpi mpicc], [no], [$MPI_PATH])
AC_PATH_PROGS([OPENMPI_MPIRUN],  [mpirun.openmpi mpirun], [no], [$MPI_PATH])
if test "$OPENMPI_MPICC" = "no" -o "$OPENMPI_MPIRUN" = "no"; then
  AC_SUBST(HAS_OPENMPI, [no])
else
  AC_MSG_CHECKING([whether mpicc works for OpenMPI])
  if $OPENMPI_MPICC -o ./test_openmpi test/hellompi.c 2>/dev/null 1>&2; then
    AC_SUBST(HAS_OPENMPI, [yes])
    AC_MSG_RESULT([yes])
    AC_MSG_CHECKING([whether mpirun invokes orted for OpenMPI])
    rm -f ./uses_openmpi_orted
    /bin/sh -c "$OPENMPI_MPIRUN -np 4 ./test_openmpi 2>/dev/null 1>&2 & \
      ps auxw | grep $USER | grep -v grep | grep -q orted && \
      touch ./uses_openmpi_orted" 2>/dev/null
    dnl Extra [...] needed to protect inner [...] from m4
    /bin/kill -9 [`ps -eo pid,comm | grep test_openmpi | \
      sed -e 's%\([0-9]\) .*$%\1%'`]
    if test -r ./uses_openmpi_orted; then
      rm -f ./uses_openmpi_orted
      AC_SUBST(USES_OPENMPI_ORTED, [yes])
      AC_MSG_RESULT([yes])
    else
      AC_SUBST(USES_OPENMPI_ORTED, [no])
      AC_MSG_RESULT([no])
    fi
  else
    AC_SUBST(HAS_OPENMPI, [no])
    AC_MSG_RESULT([no])
  fi
  rm -f ./test_openmpi
fi

AC_ARG_ENABLE([m32],
            [AS_HELP_STRING([--enable-m32],
                            [Compile in 32 bit mode on 64 bit linux. (Works on most, but not all programs, due to restrictions of 32-bit mode.)])],
            [use_m32=$enableval],
            [use_m32=no])

if test "$use_m32" = "yes"; then
  AC_SUBST([M32], [1])
  AC_SUBST([HAS_READLINE], [no])
else
  AC_SUBST([M32], [0])
fi


AC_OUTPUT
