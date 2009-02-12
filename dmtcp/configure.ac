AC_INIT([dmtcp],[1.01])

AM_CONFIG_HEADER([src/config.h])
AM_INIT_AUTOMAKE([dmtcp], [1.01])
AM_PROG_LIBTOOL

AC_LANG_CPLUSPLUS
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CC_C_O
AC_PROG_RANLIB

AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
                            [enable (very) verbose debug printouts
                             and write log files to /tmp (default is
                             disabled)])],
            [use_jassert=$enableval],
            [use_jassert=no])

if test "$use_jassert" = "yes"; then
  AC_DEFINE([DEBUG],[],[Verbose debug printouts and log files in /tmp])
  CFLAGS="-g -O0"
  CXXFLAGS="-g -O0"
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

AC_ARG_ENABLE([pid_virtualization],
            [AS_HELP_STRING([--disable-pid-virtualization],
                            [disable pid virtualization.  Pid virtualization
                            is needed for checkpointing shells and certain
                            other programs.  Disable it for performance
                            reasons or for diagnosis of bugs.])],
            [use_pid_virt=$enableval],
            [use_pid_virt=yes])

if test "$use_pid_virt" = "yes"; then
  AC_DEFINE([PID_VIRTUALIZATION],[],[Use virtual pids for pid-related system calls])
fi

AC_ARG_ENABLE([m32],
            [AS_HELP_STRING([--enable-m32],
                            [Compile in 32 bit mode on 64 bit linux.])],
            [use_m32=$enableval],
            [use_m32=no])

AM_CONDITIONAL(CONFIG_M32, [test "$use_m32" = "yes"])
  
AC_DEFINE([DMTCP],[],[Always enable this.])

AC_OUTPUT(Makefile src/Makefile)

