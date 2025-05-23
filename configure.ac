AC_INIT([DMTCP],
        m4_esyscmd([tr -d '\n' < VERSION]),
        [dmtcp-forum@lists.sourceforge.net],
        [dmtcp],
        [https://dmtcp.sourceforge.io/ ; https://github.com/dmtcp/dmtcp])

AC_PREREQ([2.71])
# TODO(kapil): Add 'subdir-objects after automake 1.16 has been released.
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_MAINTAINER_MODE
AM_PROG_AS
AC_CANONICAL_HOST
AC_PROG_INSTALL
# Automake uses 'ar cru' by default.  The 'ar' program then issues a warning.
AC_SUBST([ARFLAGS], [rc])
AC_PROG_RANLIB
AC_PROG_MAKE_SET
AC_PROG_LN_S
AC_PROG_MKDIR_P
dnl Due to a strange misfeature in autotools, AC_PROG_CXX will succeed if there
dnl is no CXX compiler and if AC_PROG_CXX is invoked _after_ AC_PROG_CC.
dnl So, we are invoking AC_PROG_CXX _before_ AC_PROG_CC, to take advantage
dnl of an undocumented autotools feature.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP

AC_CONFIG_MACRO_DIR([m4])
AX_CXX_COMPILE_STDCXX([14], [noext], [mandatory])

AX_CHECK_COMPILE_FLAG([-std=gnu11],
  [AX_APPEND_FLAG([-std=gnu11])],
  [AC_MSG_ERROR([-std=gnu11 not supported, you may have to set CFLAGS to enable C11 support.])])

dnl This sets OPENMP_CFLAGS
AC_OPENMP
dnl This sets OPENMP_CXXFLAGS
AC_LANG_PUSH([C++])
AC_OPENMP
AC_LANG_POP([C++])
AC_CONFIG_HEADERS([include/config.h])
AC_CONFIG_FILES([Makefile \
                 contrib/Makefile \
                 contrib/ckptfile/Makefile \
                 plugin/Makefile \
                 src/Makefile \
                 src/mtcp/Makefile \
                 src/plugin/Makefile \
                 include/dmtcp/version.h \
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
    fatal) { AS_ECHO(["error: unrecognized options: $ac_unrecognized_opts"]) >&2
   { (exit 1); exit 1; }; } ;;
    *)     AS_ECHO(["$as_me: WARNING: unrecognized options: $ac_unrecognized_opts"]) >&2 ;;
  esac
fi

AC_ARG_ENABLE([logging],
            [AS_HELP_STRING([--enable-logging],
                            [enable (very) verbose logging output
                             and write log files to $DMTCP_TMPDIR (default is
                             disabled)])],
            [use_logging=$enableval],
            [use_logging=no])

if test "$use_logging" = "yes"; then
  AC_SUBST([LOGGING], [yes])
  AC_DEFINE([LOGGING],[1],[Verbose trace output and log files in $DMTCP_TMPDIR])
  CFLAGS="$CFLAGS -DLOGGING"
  CPPFLAGS="$CPPFLAGS -DLOGGING"
  CXXFLAGS="$CXXFLAGS -DLOGGING"
else
  AC_SUBST([LOGGING], [no])
fi

AC_ARG_ENABLE([debug],
            [AS_HELP_STRING([--enable-debug],
                            [Use debugging flags "-Wall -g3 -O0" on DMTCP libs
                             (default is
                             disabled); also, see --enable-logging])],
            [use_debug=$enableval],
            [use_debug=no])

if test "$use_debug" = "yes"; then
  AC_SUBST([DEBUG], [yes])
  AC_DEFINE([DEBUG],[1],[Use debugging flags "-Wall -g3 -O0"])
  CFLAGS="$CFLAGS -Wall -g3 -O0 -DDEBUG"
  CPPFLAGS="$CPPFLAGS -Wall -g3 -O0 -DDEBUG"
  CXXFLAGS="$CXXFLAGS -Wall -g3 -O0 -DDEBUG"
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
  AC_SUBST([FAST_RST_VIA_MMAP], [yes])
else
  AC_SUBST([FAST_RST_VIA_MMAP], [no])
fi
AM_CONDITIONAL([FAST_RST_VIA_MMAP], [test x$use_fast_restart = xyes])

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
  old_CFLAGS="$CFLAGS"
  CFLAGS="-g -O0"
  old_LIBS="$LIBS"
  LIBS="-lrt $LIBS"
  AC_RUN_IFELSE([AC_LANG_SOURCE([[
              #include <fcntl.h>           /* For O_* constants */
              #include <sys/stat.h>        /* For mode constants */
              #include <mqueue.h>
              #include <stdlib.h>
              int main()
              {
                char *mqname = "/dmtcp-mq-conf-test";
                mq_unlink(mqname);
                if (mq_open(mqname, O_RDWR | O_CREAT, 0666, 0) == (mqd_t)-1) {
                  return 1;
                }
                mq_unlink(mqname);
                return 0;
              }
              ]])],[posix_mq='yes'],[posix_mq='no'],[])
  CFLAGS="$old_CFLAGS"
  LIBS="$old_LIBS"
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

AC_ARG_ENABLE([fsgsbase-override],
            [AS_HELP_STRING([--enable-fsgsbase-override],
                            [Defines HAS_FSGSBASE for cross-configure; The
                             target computer must have the FSGSBASE
                             kernel patch.])],
            [use_fsgsbase_override=$enableval],
            [use_fsgsbase_override=no])
AM_CONDITIONAL(ENABLE_FSGSBASE_OVERRIDE,
               [test "$use_fsgsbase_override" = "yes"])
if test "$use_fsgsbase_override" = "yes"; then
  AC_DEFINE([ENABLE_FSGSBASE_OVERRIDE],[1],[Override to use FSGSBASE
                                            kernel patch])
fi

dnl --enable-m32 will use _32 variant, but --enable_multilib uses _64 variant
CFLAGS_64="$CFLAGS"
CXXFLAGS_64="$CXXFLAGS"
LDFLAGS_64="$LDFLAGS"
CFLAGS_32="$CFLAGS -m32 -march=i686 -Wa,--32"
CXXFLAGS_32="$CXXFLAGS -m32 -march=i686 -Wa,--32"
LDFLAGS_32="$LDFLAGS -m32 -march=i686 -Wl,-m32 -Wl,-melf_i386 -Wa,--32"

AC_ARG_ENABLE([m32],
            [AS_HELP_STRING([--enable-m32],
                            [Compile in 320bit mode on 64-bit Linux. (Works on
                             most, but not all programs, due to restrictions of
                             32-bit mode.)])],
            [use_m32=$enableval],
            [use_m32=no])

AM_CONDITIONAL(CONFIG_M32, [test "$use_m32" = "yes"])
if test "$use_m32" = "yes"; then
  AC_SUBST([M32], [1])
  AC_SUBST([HAS_READLINE], [no])
  AC_DEFINE([CONFIG_M32],[1],[Compiling in 32-bit mode on 64-bit Linux.])

  CFLAGS="$CFLAGS_32"
  CXXFLAGS="$CXXFLAGS_32"
  LDFLAGS="$LDFLAGS_32"
else
  AC_SUBST([M32], [0])
fi

dnl If --enable-multilib is invoked, then the top-level 'Makefile' will
dnl   create config.status-multilib-m32.
dnl Invoking ./config.status-multilib-m32 configures as if --enable-m32 called.
dnl Invoking ./config.status configures as if --enable-m32 was not called.
AC_ARG_ENABLE([multilib],
            [AS_HELP_STRING([--enable-multilib],
                            [Compile for mixed 32-bit and 64-bit mode.
                             (Requires gcc-multilib/g++-multilib packages.)])],
            [use_multilib=$enableval],
            [use_multilib=no])

AM_CONDITIONAL(CONFIG_MULTILIB, [test "$use_multilib" = "yes"])
if test "$use_multilib" = "yes"; then
  AC_SUBST([MULTILIB], [1])
  AC_SUBST([HAS_READLINE], [no])
  AC_DEFINE([CONFIG_MULTILIB],[1],[Compiling in 32-bit/64-bit mixed mode.])
else
  AC_SUBST([MULTILIB], [0])
fi

if test "$use_m32" = "yes" -o "$use_multilib" = "yes"; then
  AC_MSG_CHECKING([whether multilib $CC compiles using -m32])
  CFLAGS="$CFLAGS_32"
  CXXFLAGS="$CXXFLAGS_32"
  LDFLAGS="$LDFLAGS_32"

  AC_LANG_PUSH([C])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <stdio.h>
  #include <features.h>  /* This often causes a problem. */
  ]], [[
  printf("Hello, world!\n");
  ]])], [cc_m32='yes'], [cc_m32='no'])
  AC_LANG_POP([C])
  AC_MSG_RESULT([$cc_m32])
  if test "$cc_m32" = "no"; then
    AC_MSG_FAILURE([[Failed to build C program with -m32
    Typically, this means that 32-bit glibc include files were not found.
    Consider installing gcc-multilib g++-multilib libc6-dev-i386 (Debian/Ubuntu)
     or glibc-devel.i686 glibc-devel libstdc++-devel.i686(Red Hat/Fedora/CentOS)
     or glibc-devel-32bit (SUSE/OpenSUSE)
     or the equivalent packages for your Linux distro.]])
  fi

  AC_MSG_CHECKING([whether multilib $CXX compiles using -m32])
  AC_LANG_PUSH([C++])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <stdio.h>
  #include <features.h>  /* This often causes a problem. */
  ]], [[
  printf("Hello, world!\n");
  ]])], [cplusplus_m32='yes'], [cplusplus_m32='no'])
  AC_LANG_POP([C++])
  AC_MSG_RESULT([$cplusplus_m32])
  if test "$cplusplus_m32" = "no"; then
    AC_MSG_FAILURE([[Failed to build $CXX program with -m32
    Typically, this means that 32-bit libstdc++.so was not found
    Consider installing gcc-multilib g++-multilib libc6-dev-i386 (Debian/Ubuntu)
     or glibc-devel.i686 glibc-devel ibstdc++-devel.i686 (Red Hat/Fedora/CentOS)
     or glibc-devel-32bit (SUSE/OpenSUSE)
     or the equivalent packages for your Linux distro.]])
  fi
  if test "$use_multilib" = "yes"; then
    CFLAGS="$CFLAGS_64"
    CXXFLAGS="$CXXFLAGS_64"
    LDFLAGS="$LDFLAGS_64"
  fi
fi

AC_ARG_ENABLE([static_libstdcxx],
            [AS_HELP_STRING([--enable-static-libstdcxx],
                            [Compile $CXX with -static-libstdc++.
                             This allows DMTCP executables compiled on later
                             distro to run correctly on earlier distros.)])],
            [use_static_libstdcxx=$enableval],
            [use_static_libstdcxx=no])

if test "$use_static_libstdcxx" = "yes"; then
  CXXFLAGS="$CXXFLAGS -static-libstdc++"

  AC_MSG_CHECKING([whether $CXX compiles using -static-libstdc++])
  AC_LANG_PUSH([C++])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
  #include <stdio.h>
  #include <features.h>  /* This often causes a problem. */
  ]], [[
  printf("Hello, world!\n");
  ]])], [cplusplus_static_libstdcxx='yes'], [cplusplus_static_libstdcxx='no'])
  AC_LANG_POP([C++])
  AC_MSG_RESULT([$cplusplus_static_libstdcxx])
  if test "$cplusplus_static_libstdcxx" = "no"; then
    AC_MSG_FAILURE([[Failed to build $CXX program with -static-libstdc++
    Typically, this means that you are missing a static package.
    Consider installing libstdc++-static (Red Hat/Fedora/CentOS)
     or the equivalent packages for your Linux distro.]])
  fi

  # We do this hack for cross-distro compatibility only if we already
  #   requested the other hack, --enable-static-libstdcxx
  OLDPATH="$PATH"
  if test "$MKDIR_P" = "/usr/bin/mkdir -p" && \
     test -x /bin/mkdir && test -x /usr/bin/mkdir && \
     diff /bin/mkdir /usr/bin/mkdir > /dev/null; then
    # Fedora prefers /usr/bin/mkdir to /bin/mkdir.  But on shared filesystems,
    #   this creates a problem for Ubuntu, which supports only /bin/mkdir.
    MKDIR_P="/bin/mkdir -p"
    AC_MSG_NOTICE(
      [--enable-static-libstdcxx: thread-safe "mkdir -p": $MKDIR_P])
  fi
fi

AC_ARG_ENABLE([dlsym_wrapper],
            [AS_HELP_STRING([--disable-dlsym-wrapper],
                            [disables dlsym wrappers to avoid incompatibility
                            with RTLD_NEXT.])],
            [use_dlsym_wrapper=$enableval],
            [use_dlsym_wrapper=yes])

if test "$use_dlsym_wrapper" = "yes"; then
  AC_DEFINE([ENABLE_DLSYM_WRAPPER],[1],[Enable dlsym wrapper in libdmtcp.so])
fi

AC_ARG_ENABLE([mtcp_fixed_textaddr],
            [AS_HELP_STRING([--enable-mtcp-fixed-textaddr],
                            [Loads mtcp_restart at a fixed text addr, as
                             required by MANA.  WARNING:   In some cases,
                             the heap assigned by the kernel is at a lower
                             address than this mtcp_restart fixed address,
                             causing sbrk calls to fail when they attempt to
                             shrink heap.])],
            [mtcp_fixed_textaddr=$enableval],
            [mtcp_fixed_textaddr=no])

if test "$mtcp_fixed_textaddr" = "yes"; then
  AC_SUBST([MTCP_FIXED_TEXTADDR], [1])
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
  aarch64*) is_aarch64_host=yes ;;
  i?86) is_i486_host=yes ;;
  x86_64) is_x86_64_host=yes ;;
esac

AM_CONDITIONAL(X86_64_HOST, [test "$is_x86_64_host" == "yes"])
if test "$is_x86_64_host" == "yes"; then
  AC_SUBST([X86_64_HOST], [yes])
else
  AC_SUBST([X86_64_HOST], [no])
fi

AM_CONDITIONAL(ARM_HOST, [test "$is_arm_host" == "yes"])
if test "$is_arm_host" == "yes"; then
  AC_SUBST([ARM_HOST], [yes])
else
  AC_SUBST([ARM_HOST], [no])
fi

AM_CONDITIONAL(AARCH64_HOST, [test "$is_aarch64_host" == "yes"])
if test "$is_aarch64_host" == "yes"; then
  AC_SUBST([AARCH64_HOST], [yes])
else
  AC_SUBST([AARCH64_HOST], [no])
fi

AC_MSG_CHECKING([whether ARM64/aarch64 and g++ (if we need to use -mno-outline)])
has_aarch64_gcc='no'
if test "$is_aarch64_host" == "yes" -a "$CC" == "gcc"; then
  # For gcc-12:
  # See  https://lore.kernel.org/linux-arm-kernel/20240109082647.GJ19790@gate.crashing.org/T/#m2b7753c4dab70f67497dd4e618e80150649c133d
  # I suspect this is down to your toolchain enabling -moutline-atomics by default;
  # I suspect your # cross-compile toolchain doesn't enable that by default.
  # Unfortunately, '-mno-outline' will cause arum64 to use the slower futexes.
  # Arguably, this is a bug.
  # If this is a problem, then use clang (LLVM) instead of gcc,r or wait and see
  # if Debian/GNU gcc.will fix this toolchain misfeature.
  # See configure.ac for further discussion.
  AX_CHECK_COMPILE_FLAG([-mno-outline-atomics],
                        [AX_APPEND_FLAG([-mno-outline-atomics])],
                        [AC_MSG_WARN([-mno-outline-atomics not supported.])])
  has_aarch64_gcc='yes'
fi
AC_MSG_RESULT([$has_aarch64_gcc])

AC_MSG_CHECKING([whether tid offset for glibc is correct])
CFLAGS_orig="$CFLAGS"
CFLAGS="$CFLAGS -pthread"
AC_RUN_IFELSE(
   [AC_LANG_SOURCE([`cat util/check-pthread-tid-offset.c`])],
   [AC_MSG_RESULT([yes])],
   [AC_MSG_RESULT([no])]
   [AC_MSG_ERROR([util/check-pthread-tid-offset.c failed; Read for explanation])]
)
CFLAGS="$CFLAGS"
AC_OUTPUT

AC_MSG_CHECKING([if this is WSL (Windows Subystem for Linux)])
if uname -r | grep -i Microsoft > /dev/null; then
  is_wsl='yes'
  wsl_version=`uname -r`
else
  is_wsl='no'
fi
if test "$is_wsl" == "yes"; then
  AC_SUBST([WSL], [yes])
  AC_DEFINE_UNQUOTED([WSL],[$wsl_version],[This is WSL (Windows Subsystem for Linux.])
fi
AC_MSG_RESULT([$is_wsl])

AC_MSG_CHECKING([whether GNU_HASH is supported for ELF])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
           #include <stdio.h>
           #include <elf.h>
           int main() {
             printf("(DT_GNU_HASH: %d) ", DT_GNU_HASH);
             return 0;
           }
           ]])],[has_gnu_hash='yes'],[has_gnu_hash='no'],[])
if test "$has_gnu_hash" == "yes"; then
  AC_DEFINE([HAS_GNU_HASH],[1],[DT_GNU_HASH is defined (e.g., in elf.h).])
fi
AC_MSG_RESULT([$has_gnu_hash])

AC_MSG_CHECKING([whether PR_SET_PTRACER is an option for prctl()])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
           #include <sys/prctl.h>
           int main() {
             prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
             prctl(PR_SET_PTRACER, 0, 0, 0, 0);
             return 0;
           }
           ]])],[has_pr_set_ptracer='yes'],[has_pr_set_ptracer='no'],[])
if test "$has_pr_set_ptracer" == "yes"; then
  AC_DEFINE([HAS_PR_SET_PTRACER],[1],[Allow 'gdb attach' when DMTCP_RESTART_PAUSE_WHILE is defined.])
fi
AC_MSG_RESULT([$has_pr_set_ptracer])

dnl CMA (Cross Memory Attach) brought in with  Linux 3.2.0 and glibc 2.15
dnl Around Feb., 2018, Docker forbids process_vm_readv even though it's in
dnl   libc.so.  So, AC_CHECK_FUNC is not sufficient.  Use AC_RUN_IFELSE([AC_LANG_SOURCE([[]])],[],[],[]).
AC_MSG_CHECKING([if process_vm_readv/process_vm_writev (CMA) available])
dnl AC_CHECK_FUNC(process_vm_readv, [has_cma='yes'], [has_cma='no'])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
       #define _GNU_SOURCE
       #include <sys/types.h>
       #include <unistd.h>
       #include <sys/wait.h>
       #include <sys/uio.h>

       int
       main(void)
       {
           struct iovec local[2];
           struct iovec remote[1];
           char buf1[10];
           char buf2[10];
           char remote_buf[100];
           ssize_t nread;
           pid_t childpid = fork();             /* PID of remote process */
           if (childpid > 0) { // if parent
             int status;
             int rc = waitpid(childpid, &status, 0);
             return WEXITSTATUS(status); // return child's return status
           }
           // else child
           int pid = getppid();

           local[0].iov_base = buf1;
           local[0].iov_len = 10;
           local[1].iov_base = buf2;
           local[1].iov_len = 10;
           remote[0].iov_base = (void *) remote_buf;
           remote[0].iov_len = 20;

           nread = process_vm_writev(pid, local, 2, remote, 1, 0);
           if (nread != 20)
               return 1;
           else
               return 0;
       }
       ]])],[has_cma='yes'],[has_cma='no'],[])
if test "$has_cma" == "yes"; then
  AC_DEFINE([HAS_CMA],[1],[Define to 1 if you have process_vm_readv and process_vm_writev.])
  AC_SUBST([HAS_CMA], [yes])
else
  AC_SUBST([HAS_CMA], [no])
fi
AC_MSG_RESULT([$has_cma])

dnl The Linux FSGSBASE commits were pushed in with Linux 5.9.0.
dnl This allows x86_64 assembly "rdfsbase" to be called in user space.
dnl However, some earlier Linux kernels
dnl have backported this feature (e.g., HPE Cray in Linux 5.3.18).
dnl So, testing the kernel version does not suffice.  Here, we use AC_RUN_IFELSE([AC_LANG_SOURCE([[]])],[],[],[]).
AC_MSG_CHECKING([if FSGSBASE for x86_64 (setting fs in user space) is available])
dnl AC_CANONICAL_HOST
dnl HOST_OS=""
dnl AS_CASE([$host_cpu], [x86_64], [ HOST_OS="x86_64" ])
has_fsgsbase='no'
if test `uname -m` == x86_64; then
  AC_RUN_IFELSE([AC_LANG_SOURCE([[
             #include <sys/auxv.h>
             #include <elf.h>

             /* Will be eventually in asm/hwcap.h */
             #ifndef HWCAP2_FSGSBASE
             # define HWCAP2_FSGSBASE (1 << 1)
             #endif

             int main() {
               unsigned val = getauxval(AT_HWCAP2);
               if (val & HWCAP2_FSGSBASE) {
                 return 0; /* success */
               } else {
                 return 1; /* failure */
               }
             }
           /* If FSGSBASE is enabled, then the following user-space versions:
            *   unsigned long int fsbase;
            *   asm volatile("rex.W\n rdfsbase %0" : "=r" (fsbase) :: "memory");
            *   asm volatile("rex.W\n wrfsbase %0" :: "r" (fsbase) : "memory");
            * can replace:
            *   unsigned long int fsbase;
            *   syscall(SYS_arch_prctl, ARCH_GET_FS, fsbase);
            *   syscall(SYS_arch_prctl, ARCH_SET_FS, fsbase);
            */
       ]])],[has_fsgsbase='yes'],[has_fsgsbase='no'],[])
fi
if test "$has_fsgsbase" == "yes"; then
  AC_DEFINE([HAS_FSGSBASE],[1],[Define to 1 if you have FSGSBASE in kernel.])
  AC_SUBST([HAS_FSGSBASE], [yes])
else
  AC_SUBST([HAS_FSGSBASE], [no])
fi
AC_MSG_RESULT([$has_fsgsbase])

AC_MSG_CHECKING([if epoll_create1 available])
AC_CHECK_FUNC(epoll_create1, [has_epoll_create1='yes'], [has_epoll_create1='no'])
if test "$has_epoll_create1" == "yes"; then
  AC_DEFINE([HAS_EPOLL_CREATE1],[1],[Define to 1 if you have epoll_create1.])
  AC_SUBST([HAS_EPOLL_CREATE1], [yes])
else
  AC_SUBST([HAS_EPOLL_CREATE1], [no])
fi

#check for -lreadline -lhistory v5; does not require curses
AC_CHECK_LIB([readline], [readline], [linksReadline=yes], [linksReadline=no], [-lhistory -lcurses])
includeReadline=yes
AC_CHECK_HEADERS([readline/readline.h readline/history.h], [], [includeReadline=no],)
if test "$linksReadline" = "yes" -a "$includeReadline" != "no"; then
  AC_SUBST([HAS_READLINE], [yes])
else
  AC_SUBST([HAS_READLINE], [no])
fi

#check for -lreadline -lhistory v5; does not require curses
AC_CHECK_LIB([selinux], [getcon], [linksSelinux=yes], [linksSelinux=no], [-lselinux])
AC_CHECK_HEADERS([selinux/selinux.h], [includeSelinux=yes], [includeSelinux=no],)
if test "$linksSelinux" = "yes" -a "$includeSelinux" = "yes"; then
  AC_SUBST([HAS_SELINUX], [yes])
else
  AC_SUBST([HAS_SELINUX], [no])
fi

#check for ps
AC_CHECK_PROG(HAS_PS, [ps], [yes], [no], [/usr/bin:/bin])

#check for python
AC_CHECK_PROG(HAS_PYTHON, [python], [yes], [no], [/usr/bin])

#check for python3
AC_CHECK_PROG(HAS_PYTHON3, [python3], [yes], [no], [/usr/bin])

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

#if /usr/include/linux/version.h is missing, give up on configuring.
AC_CHECK_HEADERS([linux/version.h], [], [AC_MSG_ERROR(
     [#include: <linux/version.h> not found: consider adding linux-libc-dev pkg]
    )])

#check for gzip utility
AC_CHECK_PROG(HAS_GZIP, [gzip], [yes], [no], [/usr/bin:/bin])

echo -n "Checking for password-less 'ssh localhost'... "
if ssh -o BatchMode=yes localhost true 2>/dev/null; then
  echo "yes"
  AC_SUBST([HAS_SSH_LOCALHOST], [yes])
else
  echo "no"
  AC_SUBST([HAS_SSH_LOCALHOST], [no])
fi

#check for mpich3
AC_ARG_WITH([mpich],
            [AS_HELP_STRING([--with-mpich=mpich_path_prefix],
                            [Compile mpich3 tests in `make check`])],
            [mpich_path_prefix=$withval],
            [mpich_path_prefix=no])

mpich_path="no"
if test "$mpich_path_prefix" != "no"; then
  mpich_path="$mpich_path_prefix:$mpich_path_prefix/bin"
else
  # Let's see if we can find mpicc in the default path
  mpiccpath=`which mpicc 2>/dev/null`
  if test ! -z "$mpiccpath"; then
    mpicclink=`readlink -f $mpiccpath`
    if test "`basename $mpicclink`" = mpicc.mpich; then
      mpich_path=`dirname $mpiccpath`
    fi
  fi
fi
if test "mpich_path" != "no"; then
  AC_PATH_PROG([MPICH_MPICC],      [mpicc],      [no], [$mpich_path])
  AC_PATH_PROG([MPICH_MPIRUN],     [mpirun],     [no], [$mpich_path])
  AC_PATH_PROG([MPICH_MPIEXEC],    [mpiexec],    [no], [$mpich_path])
  if test "$MPICH_MPIRUN" = "no"     ||\
     test "$MPICH_MPICC" = "no"      ||\
     test "$MPICH_MPIEXEC" = "no";
  then
    mpich_path="no"
  fi
fi
if test "$mpich_path" = "no"; then
  if test "$mpich_path_prefix" = "no"; then
    AC_SUBST(HAS_MPICH,[no])
  else
    AC_MSG_ERROR([invalid mpich path, use --with-mpich=<mpich_path_prefix>])
  fi
else
  AC_SUBST(HAS_MPICH, [yes])
  if test "$mpich_path_prefix" = "no"; then
    mpiccpath=`which mpicc`
  else
    PATH=$mpich_path mpiccpath=`which mpicc`
  fi
  mpich_path=`dirname $mpiccpath`
  AC_SUBST(MPICH_PATH, [$mpich_path])
fi

#check for Open MPI
#Debian uses /usr/bin/mpicc.openmpi.  Red Hat/SuSe stores it elsewhere.
MPI_PATH=$PATH:/usr/bin:/opt/openmpi/bin/:/usr/lib64/openmpi/bin:/usr/lib/openmpi/bin
MPI_PATH=$MPI_PATH:/usr/lib64/mpi/gcc/openmpi/bin:/usr/lib/mpi/gcc/openmpi/bin
AC_PATH_PROGS([OPENMPI_MPICC],  [mpicc.openmpi mpicc], [no], [$MPI_PATH])
AC_PATH_PROGS([OPENMPI_MPIRUN],  [mpirun.openmpi mpirun], [no], [$MPI_PATH])
if test "$OPENMPI_MPICC" = "no" -o "$OPENMPI_MPIRUN" = "no"; then
  AC_SUBST(HAS_OPENMPI, [no])
else
  if $OPENMPI_MPIRUN --help 2>&1 | grep -i 'open mpi' > /dev/null;  then
    AC_SUBST(HAS_OPENMPI, [yes])
  else
    AC_SUBST(HAS_OPENMPI, [no])
  fi
fi

dnl ****** TODO: THIS IS STALE CODE.  WE WILL DELETE IT (or updateit?)
dnl ****** AFTER WE COMMIT THE PR FOR THE NEW, PLANNED RESTART PLUGINS.
dnl AC_ARG_WITH([mana_helper_dir],
dnl             [AS_HELP_STRING([--with-mana-helper-dir=manadir],
dnl                             [Directory containing additional mana-specific helpers.])],
dnl             [mana_helper_dir=$withval],
dnl             [mana_helper_dir=no])
dnl ***** TODO: When finally removing this, also remove references
dnl             in src/Makefile.am and src/mtcp/Makefile.in

dnl ****** TODO: SEE ABOVE: KEEP THIS UNTIL THE NEW RESTART PLUGIN
# NOTE FO FUTURE:  We should avoid AM_CONDITIONAL.
# automake-2.71 has a bug in AM_CONDITIONAL.  When the if statment is
#  _not_ triggered.  They seem to assume that you _must_ define
# the conditional case for 'then' and for 'else', even if 'else'
# means creating a differently named file that is empty.
# Otherwise, automake tries to do dependency checking on the
#   file specified in 'then', even though we onky triggered the 'if'.
# FLAME ON: automake is a separate, poorly documented, inflexible
#   language that loses the clarity and simplicity of an ordinary
#   Makefile.in.  For example, it's not possible to have a per-file CLFAGS.
# *** BUG IN ORIGINAL DESIGN WITH automake:  It's not clear if
#   --with-mana-helper-dir ever worked.  We were using the default only.
mana_helper_dir=no
if test "$mana_helper_dir" != "no"; then
  # This is the buggy case.
  RESTART_PLUGIN=`realpath $mana_helper_dir`/dmtcp_restart_plugin.$(OBJEXT)
  MANA_HELPER_DIR=`realpath $mana_helper_dir`
else
  RESTART_PLUGIN=
  MANA_HELPER_DIR=
fi
AC_SUBST([RESTART_PLUGIN])
AC_SUBST([MANA_HELPER_DIR])

if test "$use_m32" = "yes"; then
  interp=/lib/ld-linux.so.2
elif test -x /usr/bin/readelf -a -r /usr/bin/readelf; then
  interp=`/usr/bin/readelf -aW /usr/bin/readelf | grep 'interpreter.*]' | \
          sed -e 's^.* interpreter: \(.*\)]^\1^'`
elif test `uname -m` == x86_64; then
  interp=/lib64/ld-linux-x86-64.so.2
else
  # FIXME: ARM (and others?) can use different interpreter.
  interp=/lib/ld-linux.so.2
fi

AC_DEFINE_UNQUOTED([ELF_INTERPRETER],["$interp"],[Generated by readelf -aW | grep interpreter])

AC_CHECK_HEADERS([sys/epoll.h sys/eventfd.h sys/signalfd.h sys/inotify.h])

dnl atomic builtins are required for jalloc support.
AC_MSG_CHECKING(for $CXX __sync_bool_compare_and_swap builtins)
dnl Since we'll be using these atomic builtins in C++ files we should test
dnl the C++ compiler.
AC_LANG_PUSH([C++])
dnl C++ standard set for C++14.  If standalone, use: -std=c++14 -march=native
dnl Apparently, x86_64 requires -march=native, but aarch64 assumes that
dnl that all such architectures support __sync_bool_compare_and_swap().
CXXFLAGS_orig="$CXXFLAGS"
CXXFLAGS="$CXXFLAGS -march=native"
AC_LINK_IFELSE(
[
dnl NOTE:  x86_64 and clang do sync_book_compare_and_swap fine.
dnl        Unfortunately, the Debian-12 release (and others??)
dnl        have a gcc-12 that by default uses 'gcc -noutline',
dnl        which assumes __aarch64_cas16_sync is defined, and gcc defines it
dnl        _only_ if this is the base executable, and not a .o or .so file
  AC_LANG_SOURCE([[
    #include <assert.h>
    #include <stdlib.h>
    int main() {
      __int128 dest = static_cast<__int128>(0x1234567812345678);
      __int128 oldValue=0x7ffffffff098;
      __int128 newValue=0x7ffffffff088;
      bool result = __sync_bool_compare_and_swap(&dest, oldValue, newValue);
      assert(result == false && dest != newValue);
      dest = oldValue;
      result = __sync_bool_compare_and_swap(&dest, oldValue, newValue);
      assert(result == true && dest == newValue);
      return 0;
    }
  ]])
], [gccSyncBuiltins='yes'], [gccSyncBuiltins='no'])
CXXFLAGS="$CXXFLAGS_orig"
AC_LANG_POP([C++])
AC_MSG_RESULT([$gccSyncBuiltins])
if test "$gccSyncBuiltins" = "yes"; then
  AC_DEFINE([HAS_128_SYNC_BOOL],[1],[Define to 1 if __sync_bool_compare_and_swap is supported])
fi

dnl NOTE:  Using __atomic_* requires linking with -latomic.
dnl        If libatomic not available, then we must switch to __sync_*.
dnl        __atomic_compare_exchange_16 is the 16-byte (128-bit) version.
if test "$is_arm_host" == "yes" -o "$is_i486_host" == "yes" ; then
  AC_CHECK_LIB([atomic], [__atomic_compare_exchange_8])
else
  AC_CHECK_LIB([atomic], [__atomic_compare_exchange_16])
fi
if test ${ac_cv_lib_atomic_+y}; then
  printf %s "*** libatomic.so not found; In jalib/jalloc.cpp need to"
  printf %s "***   replace __atomic_* by __sync_*."
fi

dnl atomic builtins are required for jalloc support.
AC_MSG_CHECKING(for $CXX __atomic_compare_exchange builtins using -latomic)
dnl Since we'll be using these atomic builtins in C++ files we should test
dnl the C++ compiler.
AC_LANG_PUSH([C++])
# It seems to be undocumented that this needs '-latomic' as of 2024.
LDFLAGS_orig="$LDFLAGS"
LDFLAGS="$LDFLAGS -latomic"
AC_LINK_IFELSE(
[
  AC_LANG_SOURCE([[
    #include <atomic>
    #include <assert.h>
    #include <stdlib.h>
    int main() {
      typedef unsigned __int128 uint128_t;
      uint128_t *dst = static_cast<uint128_t*>(malloc(128));
      uint128_t *oldValue = static_cast<uint128_t*>(malloc(128));
      uint128_t *newValue = static_cast<uint128_t*>(malloc(128));
      *dst = static_cast<uint128_t>(1);
      *oldValue = static_cast<uint128_t>(1);
      *newValue = static_cast<uint128_t>(2);
      bool result = __atomic_compare_exchange((uint128_t*)dst,
                                         (uint128_t*)oldValue,
                                         (uint128_t*)newValue, 0,
                                          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
      assert(result == true);
      assert(*dst == *newValue);
      return 0;
    }
  ]])
], [gccAtomicBuiltins='yes'], [gccAtomicBuiltins='no'])
LDFLAGS="$LDFLAGS_orig"
AC_LANG_POP([C++])
AC_MSG_RESULT([$gccAtomicBuiltins])

if test "$gccAtomicBuiltins" = "yes"; then
  AC_DEFINE([HAS_128_ATOMIC],[1],[Define to 1 if 'gcc test.c -latomic' works for __atomic_compare_exchange])
fi
if test "$gccAtomicBuiltins" = "no"; then
  AC_MSG_FAILURE([Using C++14, which should support both sync and atomic
                  built-ins.  Will default to using slower mutexes in jalloc.cpp])
fi

AC_MSG_CHECKING([value of page size (sysconf(_SC_PAGESIZE))])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
           #include <stdio.h>
           #include <unistd.h>
           int main() {
             fprintf(stderr, "%d", sysconf(_SC_PAGESIZE));
             return 0;
           }
           ]])],[sysconf_pagesize=$(./conftest$EXEEXT 2>&1)],
                [sysconf_pagesize='unknown'],[])
if test "$sysconf_pagesize" = "unknown"; then
  AC_MSG_FAILURE([Page size not found; Manually change include/config.h])
fi
AC_DEFINE_UNQUOTED([MTCP_PAGE_SIZE], $sysconf_pagesize,
                   [Page size (sysconf(_SC_PAGESIZE))])

dnl TODO:  __WAIT_STATUS is defined in include/config.h to be 'int *'
dnl        POSIX says 'int'.  Why do we need __WAIT_STATUS for DMTCP,
dnl        and then do AC_DEFINE to define it??
dnl        It was part of the glibc lxstat support, but it was deprecated,
dnl        and then removed as of glibcgccAtomicBuiltins" = "no"; then
AC_MSG_RESULT([$sysconf_pagesize])
AC_DEFINE_UNQUOTED([MTCP_PAGE_SIZE], $sysconf_pagesize,
                   [Page size (sysconf(_SC_PAGESIZE))])

dnl TODO:  __WAIT_STATUS is defined in include/config.h to be 'int *'
dnl        POSIX says 'int'.  Why do we need __WAIT_STATUS for DMTCP,
dnl        and then do AC_DEFINE to define it??
dnl        It was part of the glibc lxstat support, but it was deprecated,
dnl        and then removed as of glibc-2.24 in 2016.
dnl        DMTCP should also drop support for __WAIT_STATUS.
dnl Check whether __WAIT_STATUS is available.
AC_MSG_CHECKING(for __WAIT_STATUS type)
AC_LINK_IFELSE(
[
  AC_LANG_SOURCE([[
    #include <sys/wait.h>
    #include <sys/types.h>
    #include <stdlib.h>
    int main() {
      __WAIT_STATUS status __attribute__((unused));
      return 0;
    }
  ]])
], [wait_status='yes'], [wait_status='no'])
AC_MSG_RESULT([$wait_status])

if test "$wait_status" = "no"; then
  AC_DEFINE([__WAIT_STATUS],[int *],
            [can use __WAIT_STATUS as wait status type])
fi
dnl __WAIT_STATUS entered at commit 7d1344 (2009) and bdcac4 (2016)
AC_MSG_NOTICE([*** glibc-2.24 removed __WAIT_STATUS; This check will go away in the future.])

AC_OUTPUT
