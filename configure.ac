AC_INIT([DMTCP],
        m4_esyscmd([tr -d '\n' < VERSION]),
        [dmtcp-forum@lists.sourceforge.net],
        [dmtcp],
        [http://dmtcp.sourceforge.net])

AC_PREREQ([2.60])
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE
AC_CANONICAL_HOST
AC_PROG_INSTALL
AC_PROG_RANLIB
AC_PROG_MAKE_SET
AC_PROG_LN_S
AC_PROG_MKDIR_P
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
dnl This sets OPENMP_CFLAGS
AC_OPENMP
dnl This sets OPENMP_CXXFLAGS
AC_LANG_PUSH([C++])
AC_OPENMP
AC_LANG_POP([C++])
AM_CONFIG_HEADER([include/config.h])
AC_CONFIG_FILES([Makefile \
                 contrib/Makefile \
                 contrib/ckptfile/Makefile \
                 contrib/infiniband/Makefile \
                 plugin/Makefile \
                 plugin/batch-queue/Makefile \
                 plugin/modify-env/Makefile \
                 plugin/ptrace/Makefile \
                 plugin/unique-ckpt/Makefile \
                 src/Makefile \
                 src/mtcp/Makefile \
                 src/plugin/Makefile \
                 src/plugin/alloc/Makefile \
                 src/plugin/dl/Makefile \
                 src/plugin/ipc/Makefile \
                 src/plugin/pid/Makefile \
                 test/Makefile \
                 test/autotest_config.py \
                 test/plugin/Makefile])
#AC_CONFIG_FILES([test/credentials/Makefile])

#AC_CONFIG_SUBDIRS([dmtcp])

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
                             and write log files to $DMTCP_TMPDIR (default is
                             disabled)])],
            [use_jassert=$enableval],
            [use_jassert=no])

if test "$use_jassert" = "yes"; then
  AC_SUBST([DEBUG], [yes])
  AC_DEFINE([DEBUG],[1],[Verbose debug output and log files in $DMTCP_TMPDIR])
  CFLAGS="-g -O0 -Wall"
  CXXFLAGS="-g -O0 -Wall"
else
  AC_SUBST([DEBUG], [no])
fi

AC_ARG_ENABLE([quiet],
            [AS_HELP_STRING([--enable-quiet],
                            [disable NOTE and WARNING (default is
                             to print NOTE, WARNING, but no TRACE)])],
            [use_quiet=$enableval],
            [use_quiet=no])

if test "$use_quiet" = "yes"; then
  AC_DEFINE([QUIET],[1],[No output, not even NOTE and WARNING])
fi

AC_ARG_ENABLE([timing],
            [AS_HELP_STRING([--enable-timing],
                            [record checkpoint/restart timing information
                            to jtimings.csv, in working directory of
                            dmtcp_coordinator, and to stderr.])],
            [use_jtiming=$enableval],
            [use_jtiming=no])

if test "$use_jtiming" = "yes"; then
  AC_DEFINE([TIMING],[1],[Record timing information to stderr and jtimings.csv])
fi

AC_ARG_ENABLE([realtime_ckpt_signal],
            [AS_HELP_STRING([--enable-realtime-ckpt-signal],
                            [Use a realtime signal (SIGRTMAX - 2) instead of
                             the default SIGUSR2 signal.  The signal can be
                             overridden with the --ckpt-signal flag for
                             dmtcp_launch.])],
            [use_realtime_ckpt_signal=$enableval],
            [use_realtime_ckpt_signal=no])

if test "$use_realtime_ckpt_signal" = "yes"; then
  AC_DEFINE([CKPT_SIGNAL],[(SIGRTMAX - 2)],[Checkpoint signal to be used])
fi

AC_ARG_ENABLE([unique_checkpoint_filenames],
            [AS_HELP_STRING([--enable-unique-checkpoint-filenames],
                            [By default, successive checkpoints are written
                            to the same filename.  Enable if each successive
			    checkpoint should be a unique filename.])],
            [use_unique_checkpoint_filenames=$enableval],
            [use_unique_checkpoint_filenames=no])

if test "$use_unique_checkpoint_filenames" = "yes"; then
  AC_DEFINE([UNIQUE_CHECKPOINT_FILENAMES],[1],[Use unique filenames for checkpoint images])
fi

AC_ARG_ENABLE([infiniband_support],
            [AS_HELP_STRING([--enable-infiniband-support],
                            [Turn on flags needed to support Infiniband.
			    (EXPERIMENTAL)])],
            [use_infiniband_support=$enableval],
            [use_infiniband_support=no])

if test "$use_infiniband_support" = "yes"; then
  AC_CHECK_HEADERS([infiniband/verbs.h], [],
                   [AC_MSG_ERROR([#include: <infiniband/verbs.h> not found: consider adding devel package for libibverbs]
    )])
  AC_SUBST([INFINIBAND_SUPPORT], [yes])
else
  AC_SUBST([INFINIBAND_SUPPORT], [no])
fi

AC_ARG_ENABLE([forked_checkpointing],
            [AS_HELP_STRING([--enable-forked-checkpointing],
                            [fork a child process to do checkpointing, so that
                            parent sees only minor delay during checkpoint.
			    (EXPERIMENTAL)])],
            [use_forked_ckpt=$enableval],
            [use_forked_ckpt=no])

if test "$use_forked_ckpt" = "yes"; then
  AC_DEFINE([FORKED_CHECKPOINTING],[1],[Child process does checkpointing])
fi

AC_ARG_ENABLE([fast_restart],
            [AS_HELP_STRING([--enable-fast-restart],
                            [uses tricks to mmap from checkpoint image file;
                             disables all kinds of compression
			    (EXPERIMENTAL)])],
            [use_fast_restart=$enableval],
            [use_fast_restart=default])
if test "$use_fast_restart" = "yes"; then
  AC_SUBST([FAST_RESTART], [yes])
  AC_SUBST([HBICT_DELTACOMP], [no])
else
  AC_SUBST([FAST_RESTART], [no])
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
  AC_DEFINE([HBICT_DELTACOMP],[1],[Use delta compression.])
  AC_SUBST([HBICT_DELTACOMP], [yes])
else
  AC_SUBST([HBICT_DELTACOMP], [no])
fi

AC_ARG_ENABLE([test_suite],
            [AS_HELP_STRING([--disable-test-suite],
                            [disables "make check"; target apps for testing
                             (e.g. java) will not be invoked])],
            [use_test_suite=$enableval],
            [use_test_suite=yes])
if test "$use_test_suite" = "yes"; then
  AC_SUBST([USE_TEST_SUITE], [yes])
  AC_MSG_CHECKING([whether posix mq_open works]) 
  AC_LANG_PUSH([C])
  old_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS -lrt"
  AC_TRY_RUN([
              #include <fcntl.h>           /* For O_* constants */
              #include <sys/stat.h>        /* For mode constants */
              #include <mqueue.h>
              #include <stdlib.h>
              int main()
              {
                char *mqname = "/dmtcp-mq-conf-test";
                mq_unlink(mqname);
                if (mq_open(mqname, O_RDWR | O_CREAT, 0666, 0) == -1) {
                  exit(1);
                }
                mq_unlink(mqname);
                exit(0);
              }
              ], [posix_mq='yes'], [posix_mq='no'])
  LDFLAGS="$old_LDFLAGS"
  AC_LANG_POP([C])
  AC_MSG_RESULT([$posix_mq]) 
  if test "$posix_mq" = "yes"; then
    AC_SUBST([TEST_POSIX_MQ], [yes])
  else
    AC_SUBST([TEST_POSIX_MQ], [no])
  fi
else
  AC_SUBST([USE_TEST_SUITE], [no])
fi

AC_ARG_ENABLE([experts_only_space1], [], [], [])
AC_ARG_ENABLE([experts_only],
            [AS_HELP_STRING(
             [               === **** NOTE:  EXPERTS ONLY BELOW HERE **** ===],
			    [(Use at your own risk!!)])],
	    [], [])
AC_ARG_ENABLE([experts_only_after], [AS_HELP_STRING([ ], [])], [], [])

AC_ARG_ENABLE([m32],
            [AS_HELP_STRING([--enable-m32],
                            [Compile in 32 bit mode on 64 bit linux. (Works on
                             most, but not all programs, due to restrictions of
                             32-bit mode.)])],
            [use_m32=$enableval],
            [use_m32=no])

AM_CONDITIONAL(CONFIG_M32, [test "$use_m32" = "yes"])
if test "$use_m32" = "yes"; then
  AC_SUBST([M32], [1])
  AC_SUBST([HAS_READLINE], [no])
  AC_DEFINE([CONFIG_M32],[1],[Compiling in 32 bit mode on 64 bit linux.])

  AC_MSG_CHECKING([whether multilib $CC compiles using -m32]) 
  AC_LANG_PUSH([C])
  old_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS -m32"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <stdio.h>
  #include <features.h>  /* This often causes a problem. */
  ]], [[
  printf("Hello, world!\n");
  ]])], [cc_m32='yes'], [cc_m32='no'])
  CFLAGS="$old_CFLAGS"
  AC_LANG_POP([C])
  AC_MSG_RESULT([$cc_m32]) 
  if test "$cc_m32" = "no"; then
    AC_MSG_FAILURE([[Failed to build with -m32
    Consider installing gcc-multilib (and g++-multilib) or
      the equivalent packages for your Linux distro.]])
  fi

  AC_MSG_CHECKING([whether multilib $CXX compiles using -m32]) 
  AC_LANG_PUSH([C++])
  old_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS -m32"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <stdio.h>
  #include <features.h>  /* This often causes a problem. */
  ]], [[
  printf("Hello, world!\n");
  ]])], [cplusplus_m32='yes'], [cplusplus_m32='no'])
  CXXFLAGS="$old_CXXFLAGS"
  AC_LANG_POP([C++])
  AC_MSG_RESULT([$cplusplus_m32]) 
  if test "$cplusplus_m32" = "no"; then
    AC_MSG_FAILURE([[Failed to build with -m32
    Typically, this means that 32-bit libstdc++.so was not found
    Consider installing g++-multilib or equivalent pkg for your Linux distro.]])
  fi
else
  AC_SUBST([M32], [0])
fi

#AC_ARG_ENABLE([allocator],
#            [AS_HELP_STRING([--enable-allocator],
#                            [cause DMTCP to use a custom allocator based on mmap
#                             and avoid calling malloc and free (EXPERIMENTAL)])],
#            [use_allocator=$enableval],
#            [use_allocator=no])
#
#if test "$use_allocator" = "yes"; then
#  AC_DEFINE([OVERRIDE_GLOBAL_ALLOCATOR],[1],[Use a custom allocator based on mmap])
#fi

case "$host_cpu" in
  arm*) is_arm_host=yes ;;
  i?86) is_i486_host=yes ;;
esac
AM_CONDITIONAL(ARM_HOST, [test "$is_arm_host" == "yes"])
if test "$is_arm_host" == "yes"; then
  AC_SUBST([ARM_HOST], [yes])
else
  AC_SUBST([ARM_HOST], [no])
fi

#check for -lreadline -lhistory v5; does not require curses
AC_CHECK_LIB([readline], [readline], [linksReadline=yes], [linksReadline=no], [-lhistory -lcurses])
AC_CHECK_HEADERS([readline/readline.h readline/history.h], [includeReadline=no],)
if test "$linksReadline" = "yes" -a "$includeReadline" != "no"; then
  AC_SUBST([HAS_READLINE], [yes])
else
  AC_SUBST([HAS_READLINE], [no])
fi

#check for ps
AC_CHECK_PROG(HAS_PS, [ps], [yes], [no], [/usr/bin:/bin])

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
AC_CHECK_PROG(HAS_EMACS_NOX, [emacs-nox], [yes], [no], [/usr/bin])

#check for script
AC_CHECK_PROG(HAS_SCRIPT, [script], [yes], [no], [/usr/bin])

#check for screen
AC_CHECK_PROG(HAS_SCREEN, [screen], [yes], [no], [/usr/bin])
AC_PATH_PROG([SCREEN],    [screen],        [no], [/usr/bin])

#check for strace
AC_CHECK_PROG(HAS_STRACE, [strace], [yes], [no], [/usr/bin])

#check for gdb
AC_CHECK_PROG(HAS_GDB, [gdb], [yes], [no], [/usr/bin])

#check for java
AC_CHECK_PROG(HAS_JAVA, [java], [yes], [no], [/usr/local/bin:/usr/bin])

#check for javac
AC_CHECK_PROG(HAS_JAVAC, [javac], [yes], [no], [/usr/local/bin:/usr/bin])

#checking later if java dialect is Sun/Oracle/OpenJDK, in test/autotest.py.in

#check for cilk
AC_CHECK_PROG(HAS_CILK, [cilkc], [yes], [no], [$PATH:/usr/local/bin:/usr/bin])
AC_PATH_PROG([CILKC],   [cilkc],        [no], [$PATH:/usr/bin])

#check for gcl
AC_CHECK_PROG(HAS_GCL, [gcl], [yes], [no], [$PATH:/usr/local/bin:/usr/bin])
AC_PATH_PROG([GCL],    [gcl],        [no], [$PATH:/usr/local/bin:/usr/bin])

#check for matlab
AC_CHECK_PROG(HAS_MATLAB, [matlab], [yes], [no], [$PATH:/usr/local/bin:/usr/bin])
AC_PATH_PROG([MATLAB],    [matlab],        [no], [$PATH:/usr/local/bin:/usr/bin])

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

echo -n "Checking for password-less 'ssh localhost'... "
if ssh -o BatchMode=yes localhost true 2>/dev/null; then
  echo "yes"
  AC_SUBST([HAS_SSH], [yes])
else
  echo "no"
  AC_SUBST([HAS_SSH], [no])
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

#check for Open MPI
#Debian uses /usr/bin/mpicc.openmpi.  Red Hat/SuSe store it elsewhere.
MPI_PATH=/usr/bin:/opt/openmpi/bin/:/usr/lib64/openmpi/bin:/usr/lib/openmpi/bin
MPI_PATH=$MPI_PATH:/usr/lib64/mpi/gcc/openmpi/bin:/usr/lib/mpi/gcc/openmpi/bin
AC_PATH_PROGS([OPENMPI_MPICC],  [mpicc.openmpi mpicc], [no], [$MPI_PATH])
AC_PATH_PROGS([OPENMPI_MPIRUN],  [mpirun.openmpi mpirun], [no], [$MPI_PATH])
if test "$OPENMPI_MPICC" = "no" -o "$OPENMPI_MPIRUN" = "no"; then
  AC_SUBST(HAS_OPENMPI, [no])
else
  AC_SUBST(HAS_OPENMPI, [yes])
fi

if test "$use_m32" = "yes"; then
  interp=/lib/ld-linux.so.2
elif test -x /usr/bin/readelf -a -r /usr/bin/readelf; then
  interp=`/usr/bin/readelf -aW /usr/bin/readelf | grep interpreter | \
          sed -e 's^.* interpreter: \(.*\)]^\1^'`
elif test `uname -m` == x86_64; then
  interp=/lib64/ld-linux-x86-64.so.2
else
  # FIXME: ARM (and others?) can use different interpreter.
  interp=/lib/ld-linux.so.2
fi

AC_DEFINE_UNQUOTED([ELF_INTERPRETER],["$interp"],[Generated by readelf -aW | grep interpreter])

AC_CHECK_HEADERS_ONCE([sys/epoll.h sys/eventfd.h sys/signalfd.h sys/inotify.h])

AC_DEFINE([DMTCP],[1],[Always enable this.])


AC_OUTPUT
