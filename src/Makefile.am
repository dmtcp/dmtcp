# We should disable AM_MAINTAINER_MODE since otherwise a re-build by the
#  end-user may require local tools, which may be missing or whose version is
#  incompatible with these tools (e.g. autoheader).  Instead, we run NMI and
#  openSUSE build services to test compatibility with end-user machines.
#  The developers should run autoconf, automake, etc., as needed, but
#  not propagate to the svn-generated tools that may be incompatible
#  with the user's local tools.  - Gene
# See http://www.gnu.org/s/hello/manual/automake/CVS.html#CVS
# AM_MAINTAINER_MDE([disable])

# not a GNU package. You can remove this line, if
# have all needed files, that a GNU package needs
# TODO(kapil): Add 'subdir-objects after automake 1.16 has been released.
AUTOMAKE_OPTIONS = foreign

jalibdir=$(top_srcdir)/jalib
dmtcpincludedir=$(top_srcdir)/include
dmtcplibdir = $(pkglibdir)

SUBDIRS = mtcp plugin

# clang/llmv handles arm64 correctly, but not GNU gcc.  For an overview,
#   see:  https://github.com/llvm/llvm-project/issues/71883
# Automake requires this weird syntax (*_x86_64; *AARCH64) using configure.ac.
# Automake is a hack, halfway betwen GNU extensions to Makefile and autoconf/sh.
if X86_64_HOST
 JALLOC_ATOMIC_128_CXXFLAG_x86_64 = -mx86-64-v2
endif
if AARCH64_GCC_HOST
# For gcc-12:
# See  https://lore.kernel.org/linux-arm-kernel/20240109082647.GJ19790@gate.crashing.org/T/#m2b7753c4dab70f67497dd4e618e80150649c133d
# I suspect this is down to your toolchain enabling -moutline-atomics by default;
# I suspect your # cross-compile toolchain doesn't enable that by default.
# Unfortunately, '-mno-outline' will cause arum64 to use the slower futexes.
# Arguably, this is a bug.
# If this is a problem, then use clang (LLVM) instead of gcc,r or wait and see
# if Debian/GNU gcc.will fix this toolchain misfeature.
# See configure.ac for further discussion.
 JALLOC_ATOMIC_128_CXXFLAG_AARCH64 = -mno-outline-atomics
endif
## risc-v does not appear to support sync_bool_compare_and_swap as of 2024.
## So, jalib/jalloc.cpp will use a futex for risc-v.

PICFLAGS=-fPIC
AM_CFLAGS = $(PICFLAGS)
AM_CXXFLAGS = $(PICFLAGS) $(JALLOC_ATOMIC_128_CXXFLAG_X86_64) $(JALLOC_ATOMIC_128_CXXFLAG_AARCH64)
AM_LDFLAGS = $(PICFLAGS)

if CONFIG_M32
d_libdir = $(top_builddir)/lib/$(PACKAGE)/32/lib/$(PACKAGE)
d_bindir = $(top_builddir)/lib/$(PACKAGE)/32/bin
else
d_libdir = $(top_builddir)/lib/$(PACKAGE)
d_bindir = $(top_builddir)/bin
endif

if FAST_RST_VIA_MMAP
  AM_CXXFLAGS += -DFAST_RST_VIA_MMAP
endif

#HEADERS
#set the include path found by configure
AM_CPPFLAGS= -I$(dmtcpincludedir) -I$(jalibdir)

# targets:
noinst_LIBRARIES = libdmtcpinternal.a 				\
		   libdmtcprestart.a 				\
		   libsyscallsreal.a 				\
		   libnohijack.a 				\
		   libjalib.a

bin_PROGRAMS = $(d_bindir)/dmtcp_command 			\
	       $(d_bindir)/dmtcp_coordinator 			\
	       $(d_bindir)/dmtcp_launch 			\
	       $(d_bindir)/dmtcp_nocheckpoint			\
	       $(d_bindir)/dmtcp_get_libc_offset		\
	       $(d_bindir)/dmtcp_restart

dmtcplib_PROGRAMS = $(d_libdir)/libdmtcp.so

include_HEADERS = $(srcdir)/../include/dmtcp.h 			\
		  $(srcdir)/../include/dmtcp/version.h


# headers:
nobase_noinst_HEADERS =						\
			ckptserializer.h			\
			constants.h 				\
			coordinatorapi.h			\
			dmtcp_coordinator.h			\
			dmtcprestartinternal.h				\
			dmtcpmessagetypes.h			\
			dmtcpworker.h				\
			lookup_service.h			\
			ldt.h					\
			plugininfo.h				\
			pluginmanager.h				\
			processinfo.h				\
			restartscript.h				\
			tls.h					\
			siginfo.h				\
			syscallwrappers.h			\
			threadinfo.h				\
			threadlist.h				\
			threadsync.h				\
			uniquepid.h				\
			workerstate.h

nobase_noinst_HEADERS += $(jalibdir)/jalib.h			\
			 $(jalibdir)/jalloc.h			\
			 $(jalibdir)/jassert.h			\
			 $(jalibdir)/jbuffer.h			\
			 $(jalibdir)/jconvert.h			\
			 $(jalibdir)/jfilesystem.h		\
			 $(jalibdir)/jserialize.h		\
			 $(jalibdir)/jsocket.h			\
			 $(jalibdir)/jtimer.h

nobase_noinst_HEADERS += $(dmtcpincludedir)/dmtcp.h		\
			 $(dmtcpincludedir)/dmtcpalloc.h	\
			 $(dmtcpincludedir)/futex.h		\
			 $(dmtcpincludedir)/procmapsarea.h	\
			 $(dmtcpincludedir)/procselfmaps.h	\
			 $(dmtcpincludedir)/protectedfds.h	\
			 $(dmtcpincludedir)/shareddata.h	\
			 $(dmtcpincludedir)/tokenize.h		\
			 $(dmtcpincludedir)/trampolines.h	\
			 $(dmtcpincludedir)/util.h		\
			 $(dmtcpincludedir)/virtualidtable.h	\
			 $(dmtcpincludedir)/wrapperlock.h	\
			 $(dmtcpincludedir)/dmtcp/version.h

# Note that libdmtcpinternal.a does not include wrappers.
# dmtcp_launch, dmtcp_command, dmtcp_coordinator, etc.
#   should not need wrappers.
libdmtcpinternal_a_SOURCES = coordinatorapi.cpp 		\
			     dmtcpmessagetypes.cpp		\
			     dmtcp_dlsym.cpp 			\
			     jalibinterface.cpp			\
			     mutex.cpp 				\
			     processinfo.cpp 			\
			     procselfmaps.cpp			\
			     rwlock.cpp 			\
			     shareddata.cpp 			\
			     tokenize.cpp			\
			     uniquepid.cpp			\
			     util_exec.cpp			\
			     util_init.cpp 			\
			     util_misc.cpp 			\
			     workerstate.cpp

libjalib_a_SOURCES = $(jalibdir)/jalib.cpp			\
		     $(jalibdir)/jalloc.cpp			\
		     $(jalibdir)/jassert.cpp 			\
		     $(jalibdir)/jbuffer.cpp			\
		     $(jalibdir)/jfilesystem.cpp 		\
		     $(jalibdir)/jserialize.cpp			\
		     $(jalibdir)/jsocket.cpp 			\
		     $(jalibdir)/jtimer.cpp

# This is required for jalib/jalloc.cpp:__sync_bool_compare_and_swap()
libjalib_a_CFLAGS = -march=native

# FIXME:  Rename libsyscallsreal.a to libhijack.a
# An executable should use either libsyscallsreal.a or libnohijack.a -- not both
libsyscallsreal_a_SOURCES = syscallsreal.c 			\
			    trampolines.cpp

libnohijack_a_SOURCES = dmtcpnohijackstubs.cpp			\
			nosyscallsreal.c

__d_libdir__libdmtcp_so_SOURCES = alarm.cpp			\
				  ckptserializer.cpp 		\
				  dlwrappers.cpp 		\
				  dmtcpplugin.cpp 		\
				  dmtcpworker.cpp 		\
				  dmtcp_dlsym_wrappers.cpp 	\
				  execwrappers.cpp 		\
				  glibcsystem.cpp 		\
				  kvdb.cpp			\
				  miscwrappers.cpp 		\
				  plugininfo.cpp 		\
				  pluginmanager.cpp		\
				  popen.cpp 			\
				  tls.cpp			\
				  posix_spawn.cpp 		\
				  rlimitfloatenv.cpp 		\
				  signalwrappers.cpp 		\
				  siginfo.cpp 			\
				  syslogwrappers.cpp 		\
				  terminal.cpp 			\
				  threadlist.cpp 		\
				  threadsync.cpp 		\
				  threadwrappers.cpp 		\
				  wrapperlock.cpp		\
				  wrappers.cpp 			\
				  writeckpt.cpp

__d_libdir__libdmtcp_so_LDFLAGS = -shared -Xlinker -znow

# Note that an ELF object uses libsyscallsreal.a or libnohijack.a
#  but not both.  libnohijack.a has stub definitions for same symbols.
__d_libdir__libdmtcp_so_LDADD = libdmtcpinternal.a 		\
				 libjalib.a 			\
				 libsyscallsreal.a 		\
				 -ldl -lpthread -lrt


__d_bindir__dmtcp_nocheckpoint_SOURCES = dmtcp_nocheckpoint.c

__d_bindir__dmtcp_get_libc_offset_SOURCES = dmtcp_get_libc_offset.c

__d_bindir__dmtcp_get_libc_offset_LDADD = -ldl

__d_bindir__dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp 	\
					lookup_service.cpp 	\
					restartscript.cpp

__d_bindir__dmtcp_coordinator_LDADD = libdmtcpinternal.a 	\
				      libjalib.a 		\
				      libnohijack.a		\
				      -lpthread -lrt -ldl

__d_bindir__dmtcp_launch_SOURCES = dmtcp_launch.cpp

__d_bindir__dmtcp_launch_LDADD  = libdmtcpinternal.a 		\
				  libjalib.a			\
				  libnohijack.a			\
				  -lpthread -lrt -ldl

libdmtcprestart_a_SOURCES = dmtcprestartinternal.cpp
libdmtcprestart_a_LIBADD  = libdmtcpinternal.a 			\
				   libjalib.a 			\
				   libnohijack.a

__d_bindir__dmtcp_restart_SOURCES = dmtcp_restart.cpp

__d_bindir__dmtcp_restart_LDADD  = libdmtcprestart.a 		\
				   libdmtcpinternal.a 		\
				   libjalib.a 			\
				   libnohijack.a		\
				   -lpthread -lrt -ldl

__d_bindir__dmtcp_restart_DEPENDENCIES  = libdmtcprestart.a

__d_bindir__dmtcp_command_SOURCES = dmtcp_command.cpp

__d_bindir__dmtcp_command_LDADD = libdmtcpinternal.a 		\
				  libjalib.a 			\
				  libnohijack.a			\
				  -lpthread -lrt -ldl
if AARCH64_HOST
# FIXME: This depends on configure showing __atomic_* exists, not on aarch64
__d_libdir__libdmtcp_so_LDADD += -latomic
endif

# Refer to configure.ac for the definition of RESTART_PLUGIN.
__d_bindir__dmtcp_restart_LDADD += $(RESTART_PLUGIN)
__d_bindir__dmtcp_restart_DEPENDENCIES += $(RESTART_PLUGIN)

install-libs: install-dmtcplibPROGRAMS
	(cd mtcp && ${MAKE} install-libs)
	(cd plugin && ${MAKE} install-libs)

uninstall-libs: uninstall-dmtcplibPROGRAMS
	(cd mtcp && ${MAKE} uninstall-libs)
	(cd plugin && ${MAKE} uninstall-libs)

.PHONY: install-libs uninstall-libs
