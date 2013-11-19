AC_INIT([DMTCP],[2.0],[],[dmtcp],[http://dmtcp.sourceforge.net])
AC_PREREQ([2.60])
AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE
AM_CONFIG_HEADER([include/config.h])
#AM_PROG_LIBTOOL

AC_LANG_CPLUSPLUS
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CC_C_O
AC_PROG_RANLIB
AC_PROG_LN_S

AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
                            [enable (very) verbose debug output
                             and write log files to $DMTCP_TMPDIR (default is
                             disabled)])],
            [use_jassert=$enableval],
            [use_jassert=no])

if test "$use_jassert" = "yes"; then
  AC_DEFINE([DEBUG],[1],[Verbose debug output and log files in $DMTCP_TMPDIR])
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

AC_ARG_ENABLE([ptrace_support],
            [AS_HELP_STRING([--enable-ptrace-support],
                            [enable ptrace support for gdb, valgrind, etc.
			    (EXPERIMENTAL)])],
            [use_ptrace_support=$enableval],
            [use_ptrace_support=no])

if test "$use_ptrace_support" = "yes"; then
  AC_DEFINE([PTRACE],[1],[Use support for ptrace system call.])
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

AC_ARG_ENABLE([delta_compression],
            [AS_HELP_STRING([--enable-delta_compression],
                            [enable incremental/differential checkpointing
			     using HBICT (hash-based incremental checkpointing)
			     tool; disables default gzip compression
			     (EXPERIMENTAL)])],
            [use_deltacomp=$enableval],
            [use_deltacomp=no])

if test "$use_deltacomp" = "yes"; then
  AC_DEFINE([HBICT_DELTACOMP],[1],[Use delta compression.])
fi

AC_ARG_ENABLE([experts_only_space1], [], [], [])
AC_ARG_ENABLE([experts_only],
            [AS_HELP_STRING(
             [               === **** NOTE:  EXPERTS ONLY BELOW HERE **** ===],
			    [(Use at your own risk!!)])],
	    [], [])
AC_ARG_ENABLE([experts_only_after], [AS_HELP_STRING([ ], [])], [], [])

AC_ARG_ENABLE([run-as-root],
            [AS_HELP_STRING([--enable-run-as-root],
                            [allow DMTCP to run as root (normally forbidden)])],
            [use_run_as_root=$enableval],
            [use_run_as_root=no])

if test "$use_run_as_root" = "yes"; then
  AC_SUBST([RUN_AS_ROOT], [yes])
else
  AC_SUBST([RUN_AS_ROOT], [no])
fi

AM_CONDITIONAL(RUN_AS_ROOT, [test "$use_run_as_root" = "yes"])
if test "$use_run_as_root" = "yes"; then
  AC_DEFINE([RUN_AS_ROOT],[1],[Allow DMTCP to run as root])
fi

AC_ARG_ENABLE([pid_virtualization],
            [AS_HELP_STRING([--disable-pid-virtualization],
                            [disable pid virtualization.  Pid virtualization
                            is _required_ for checkpointing shells and for many
                            programs using fork().  Disable it only for
                            performance reasons or for diagnosis of bugs.])],
            [use_pid_virt=$enableval],
            [use_pid_virt=yes])

if test "$use_pid_virt" = "yes"; then
  AC_DEFINE([PID_VIRTUALIZATION],[1],[Use virtual pids for pid-related system calls.])
fi

AC_ARG_ENABLE([allocator],
            [AS_HELP_STRING([--enable-allocator],
                            [cause DMTCP to use a custom allocator based on
                             mmap and avoid calling malloc and free.
			     (EXPERIMENTAL)])],
            [use_allocator=$enableval],
            [use_allocator=no])

if test "$use_allocator" = "yes"; then
  AC_DEFINE([OVERRIDE_GLOBAL_ALLOCATOR],[1],[Use a custom allocator based on mmap])
fi

AM_CONDITIONAL(HAS_I486,
  [test `uname -m` == i686 -o  `uname -m` == i586 -o `uname -m` == i486])

AC_ARG_ENABLE([m32],
            [AS_HELP_STRING([--enable-m32],
                            [Compile in 32 bit mode on 64 bit linux.])],
            [use_m32=$enableval],
            [use_m32=no])

AM_CONDITIONAL(CONFIG_M32, [test "$use_m32" = "yes"])
if test "$use_m32" = "yes"; then
  AC_DEFINE([CONFIG_M32],[1],[Compiling in 32 bit mode on 64 bit linux.])
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

AC_OUTPUT(Makefile src/Makefile)
