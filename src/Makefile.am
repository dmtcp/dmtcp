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

SUBDIRS = mtcp

# clang/llmv handles arm64 correctly, but not GNU gcc.  For an overview,
#   see:  https://github.com/llvm/llvm-project/issues/71883
# Automake requires this weird syntax (*_x86_64; *AARCH64) using configure.ac.
# Automake is a hack, halfway betwen GNU extensions to Makefile and autoconf/sh.
if X86_64_HOST
 JALLOC_ATOMIC_128_CXXFLAG_x86_64 = -mx86-64-v2
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

stale_folded_plugin_dsos = \
	$(d_libdir)/libdmtcp_alloc.so \
	$(d_libdir)/libdmtcp_dl.so \
	$(d_libdir)/libdmtcp_ipc.so \
	$(d_libdir)/libdmtcp_pid.so \
	$(d_libdir)/libdmtcp_svipc.so \
	$(d_libdir)/libdmtcp_timer.so \
	$(d_libdir)/libdmtcp_unique-ckpt.so

CLEANFILES = $(stale_folded_plugin_dsos)

if FAST_RST_VIA_MMAP
  AM_CXXFLAGS += -DFAST_RST_VIA_MMAP
endif

#HEADERS
#set the include path found by configure
AM_CPPFLAGS= -I$(dmtcpincludedir) -I$(jalibdir) -I$(srcdir) \
	     -I$(srcdir)/plugin -I$(srcdir)/plugin/timer \
	     -I$(srcdir)/plugin/pid

# targets:
noinst_LIBRARIES = libdmtcpinternal.a 				\
		   libdmtcprestart.a 				\
		   libsyscallsreal.a 				\
		   libnohijack.a 				\
		   libjalib.a					\
		   libssh.a					\
		   libdmtcp_alloc_internal.a			\
		   libdmtcp_dl_internal.a			\
		   libdmtcp_event_internal.a			\
		   libdmtcp_file_internal.a			\
		   libdmtcp_pty_internal.a			\
		   libdmtcp_socket_internal.a			\
		   libdmtcp_ssh_internal.a			\
		   libdmtcp_svipc_internal.a			\
		   libdmtcp_timer_internal.a			\
		   libdmtcp_pid_internal.a			\
		   libdmtcp_unique_ckpt_internal.a

bin_PROGRAMS = $(d_bindir)/dmtcp_command 			\
	       $(d_bindir)/dmtcp_coordinator 			\
	       $(d_bindir)/dmtcp_launch 			\
	       $(d_bindir)/dmtcp_nocheckpoint			\
	       $(d_bindir)/dmtcp_get_libc_offset		\
	       $(d_bindir)/dmtcp_restart			\
	       $(d_bindir)/dmtcp_ssh				\
	       $(d_bindir)/dmtcp_sshd

dmtcplib_PROGRAMS = $(d_libdir)/libdmtcp.so

include_HEADERS = $(srcdir)/../include/dmtcp.h 			\
		  $(srcdir)/../include/dmtcp/version.h


# headers:
nobase_noinst_HEADERS =						\
			ckptserializer.h			\
			constants.h 				\
			coordinatorapi.h			\
			coordinatorplugin.h			\
			dmtcp_coordinator.h			\
			dmtcprestartinternal.h			\
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
			workerstate.h				\
			plugin/alloc/alloc.h

nobase_noinst_HEADERS += $(jalibdir)/jalib.h			\
			 $(jalibdir)/jalloc.h			\
			 $(jalibdir)/jassert.h			\
			 $(jalibdir)/jbuffer.h			\
			 $(jalibdir)/jconvert.h			\
			 $(jalibdir)/jfilesystem.h		\
			 $(jalibdir)/jserialize.h		\
			 $(jalibdir)/jsocket.h

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
		     $(jalibdir)/jsocket.cpp

# This is required for jalib/jalloc.cpp:__sync_bool_compare_and_swap()
libjalib_a_CFLAGS = -march=native

# FIXME:  Rename libsyscallsreal.a to libhijack.a
# An executable should use either libsyscallsreal.a or libnohijack.a -- not both
libsyscallsreal_a_SOURCES = syscallsreal.c 			\
			    trampolines.cpp

libnohijack_a_SOURCES = dmtcpnohijackstubs.cpp			\
			nosyscallsreal.c

#
# Built-in plugin libraries.
#
libssh_a_SOURCES =							\
	plugin/ssh/util_ssh.cpp					\
	plugin/ssh/util_ssh.h
libssh_a_CPPFLAGS = $(AM_CPPFLAGS)
libssh_a_SHORTNAME = i

libdmtcp_alloc_internal_a_SOURCES =					\
	plugin/alloc/alloc.h						\
	plugin/alloc/mallocwrappers.cpp				\
	plugin/alloc/mmapwrappers.cpp
libdmtcp_alloc_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_alloc_internal_a_SHORTNAME = i

libdmtcp_dl_internal_a_SOURCES =					\
	plugin/dl/dlwrappers.cpp
libdmtcp_dl_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_dl_internal_a_SHORTNAME = i

libdmtcp_event_internal_a_SOURCES =					\
	plugin/event/eventconnection.cpp				\
	plugin/event/eventconnection.h				\
	plugin/event/eventconnlist.cpp				\
	plugin/event/eventconnlist.h				\
	plugin/event/eventwrappers.cpp				\
	plugin/event/eventwrappers.h				\
	plugin/event/util_descriptor.cpp				\
	plugin/event/util_descriptor.h
libdmtcp_event_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_event_internal_a_SHORTNAME = i

libdmtcp_file_internal_a_SOURCES =					\
	plugin/connection.cpp					\
	plugin/connection.h						\
	plugin/connectionidentifier.cpp				\
	plugin/connectionidentifier.h				\
	plugin/connectionlist.cpp					\
	plugin/connectionlist.h					\
	plugin/file/fileconnection.cpp				\
	plugin/file/fileconnection.h				\
	plugin/file/fileconnlist.cpp				\
	plugin/file/fileconnlist.h					\
	plugin/file/filewrappers.cpp				\
	plugin/file/filewrappers.h					\
	plugin/file/openwrappers.cpp				\
	plugin/file/posixipcwrappers.cpp
libdmtcp_file_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_file_internal_a_SHORTNAME = i

libdmtcp_pty_internal_a_SOURCES =					\
	plugin/file/ptyconnection.cpp				\
	plugin/file/ptyconnection.h					\
	plugin/file/ptyconnlist.cpp					\
	plugin/file/ptyconnlist.h					\
	plugin/file/ptywrappers.cpp					\
	plugin/file/ptywrappers.h
libdmtcp_pty_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_pty_internal_a_SHORTNAME = i

libdmtcp_socket_internal_a_SOURCES =					\
	plugin/socket/connectionmessage.h				\
	plugin/socket/connectionrewirer.cpp				\
	plugin/socket/connectionrewirer.h				\
	plugin/socket/kernelbufferdrainer.cpp			\
	plugin/socket/kernelbufferdrainer.h				\
	plugin/socket/socketconnection.cpp				\
	plugin/socket/socketconnection.h				\
	plugin/socket/socketconnlist.cpp				\
	plugin/socket/socketconnlist.h				\
	plugin/socket/socketwrappers.cpp				\
	plugin/socket/socketwrappers.h
libdmtcp_socket_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_socket_internal_a_SHORTNAME = i

libdmtcp_ssh_internal_a_SOURCES =					\
	plugin/ssh/ssh.cpp						\
	plugin/ssh/sshdrainer.cpp					\
	plugin/ssh/sshdrainer.h					\
	plugin/ssh/ssh.h
libdmtcp_ssh_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_ssh_internal_a_SHORTNAME = i

libdmtcp_svipc_internal_a_SOURCES =					\
	plugin/svipc/sysvipc.cpp					\
	plugin/svipc/sysvipc.h						\
	plugin/svipc/sysvipcwrappers.cpp				\
	plugin/svipc/sysvipcwrappers.h
libdmtcp_svipc_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_svipc_internal_a_SHORTNAME = i

libdmtcp_timer_internal_a_SOURCES =					\
	plugin/timer/timer_create.cpp					\
	plugin/timer/timerlist.cpp					\
	plugin/timer/timerlist.h					\
	plugin/timer/timerwrappers.cpp					\
	plugin/timer/timerwrappers.h
libdmtcp_timer_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_timer_internal_a_SHORTNAME = i

libdmtcp_pid_internal_a_SOURCES =					\
	plugin/pid/glibc_pthread.cpp					\
	plugin/pid/glibc_pthread.h					\
	plugin/pid/pid.cpp						\
	plugin/pid/pid_filewrappers.cpp					\
	plugin/pid/pid.h						\
	plugin/pid/pidhelpers.h						\
	plugin/pid/pidwrappers.cpp					\
	plugin/pid/pidwrappers.h					\
	plugin/pid/sched_wrappers.cpp					\
	plugin/pid/virtualpidtable.cpp					\
	plugin/pid/virtualpidtable.h
libdmtcp_pid_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_pid_internal_a_SHORTNAME = i

libdmtcp_unique_ckpt_internal_a_SOURCES =				\
	plugin/unique-ckpt/unique-ckpt.cpp
libdmtcp_unique_ckpt_internal_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)
libdmtcp_unique_ckpt_internal_a_SHORTNAME = i

__d_libdir__libdmtcp_so_SOURCES = alarm.cpp			\
				  ckptserializer.cpp 		\
				  dmtcpplugin.cpp 		\
				  dmtcpworker.cpp 		\
				  dmtcp_dlsym_wrappers.cpp 	\
				  execwrappers.cpp 		\
				  glibcsystem.cpp 		\
				  kvdb.cpp			\
				  miscwrappers.cpp 		\
				  plugin_pathtranslator.cpp     \
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

__d_libdir__libdmtcp_so_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/plugin -I$(srcdir)/plugin/timer -I$(srcdir)/plugin/pid

__d_libdir__libdmtcp_so_LDFLAGS = -shared -Xlinker -znow
dmtcp_dash = $(subst x,,-x)
dmtcp_comma = $(subst x,,x,)
dmtcp_plugin_whole_archive = $(dmtcp_dash)Wl$(dmtcp_comma)--whole-archive
dmtcp_plugin_no_whole_archive = $(dmtcp_dash)Wl$(dmtcp_comma)--no-whole-archive

# Note that an ELF object uses libsyscallsreal.a or libnohijack.a
#  but not both.  libnohijack.a has stub definitions for same symbols.
# Folded plugin archives define interposition-only wrappers that may not
# satisfy unresolved references during a normal archive scan.  Keep them whole
# so their wrapper objects are always linked into libdmtcp.so.
# The matching no-whole-archive marker is intentionally placed before the
# shared core archives; otherwise the linker would pull every object from
# libdmtcpinternal.a, libjalib.a, and libsyscallsreal.a too.
__d_libdir__libdmtcp_so_LDADD = $(dmtcp_plugin_whole_archive)	\
				 libdmtcp_ssh_internal.a		\
				 libdmtcp_event_internal.a		\
				 libdmtcp_file_internal.a		\
				 libdmtcp_pty_internal.a		\
				 libdmtcp_socket_internal.a		\
				 libdmtcp_svipc_internal.a		\
				 libdmtcp_timer_internal.a		\
				 libdmtcp_pid_internal.a		\
				 libdmtcp_unique_ckpt_internal.a	\
				 libdmtcp_alloc_internal.a		\
				 libdmtcp_dl_internal.a			\
				 $(dmtcp_plugin_no_whole_archive)	\
				 libdmtcpinternal.a 		\
				 libjalib.a 			\
				 libsyscallsreal.a 		\
				 -ldl -lpthread -lrt

__d_libdir__libdmtcp_so_DEPENDENCIES = libdmtcp_ssh_internal.a \
				 libdmtcp_event_internal.a		\
				 libdmtcp_file_internal.a		\
				 libdmtcp_pty_internal.a		\
				 libdmtcp_socket_internal.a		\
				 libdmtcp_svipc_internal.a		\
				 libdmtcp_timer_internal.a		\
				 libdmtcp_pid_internal.a		\
				 libdmtcp_unique_ckpt_internal.a	\
				 libdmtcp_alloc_internal.a		\
				 libdmtcp_dl_internal.a			\
				 libdmtcpinternal.a 		\
				 libjalib.a 			\
				 libsyscallsreal.a

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

__d_bindir__dmtcp_ssh_SOURCES =					\
	plugin/ssh/dmtcp_ssh.cpp
__d_bindir__dmtcp_ssh_CPPFLAGS = $(AM_CPPFLAGS)
__d_bindir__dmtcp_ssh_LDADD = libssh.a -ldl -lpthread
__d_bindir__dmtcp_ssh_SHORTNAME = i

__d_bindir__dmtcp_sshd_SOURCES =					\
	plugin/ssh/dmtcp_sshd.cpp
__d_bindir__dmtcp_sshd_CPPFLAGS = $(AM_CPPFLAGS)
__d_bindir__dmtcp_sshd_LDADD = libssh.a -ldl -lpthread
__d_bindir__dmtcp_sshd_SHORTNAME = i
if AARCH64_HOST
# FIXME: This depends on configure showing __atomic_* exists, not on aarch64
__d_libdir__libdmtcp_so_LDADD += -latomic
endif

# Refer to configure.ac for the definition of RESTART_PLUGIN.
__d_bindir__dmtcp_restart_LDADD += $(RESTART_PLUGIN)
__d_bindir__dmtcp_restart_DEPENDENCIES += $(RESTART_PLUGIN)

install-libs: install-dmtcplibPROGRAMS
	(cd mtcp && ${MAKE} install-libs)

uninstall-libs: uninstall-dmtcplibPROGRAMS
	rm -f $(stale_folded_plugin_dsos)
	(cd mtcp && ${MAKE} uninstall-libs)

.PHONY: install-libs uninstall-libs
