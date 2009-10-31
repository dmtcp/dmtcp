AC_INIT([DMTCP],[1.1.0],[],[dmtcp],[http://dmtcp.sourceforge.net])
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


#check for gcl
AC_CHECK_PROG(HAS_GCL, [gcl], [yes], [no], [/usr/bin])

#check for patch utility
AC_CHECK_PROG(HAS_PATCH, [patch], [yes], [no], [/usr/bin:/bin])

#check for gzip utility
AC_CHECK_PROG(HAS_GZIP, [gzip], [yes], [no], [/usr/bin:/bin])

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
