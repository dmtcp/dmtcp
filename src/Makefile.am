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

PICFLAGS=-fPIC
AM_CFLAGS = $(PICFLAGS)
AM_CXXFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PICFLAGS)

if CONFIG_M32
d_libdir = $(top_builddir)/lib/$(PACKAGE)/32/lib/$(PACKAGE)
d_bindir = $(top_builddir)/lib/$(PACKAGE)/32/bin
else
d_libdir = $(top_builddir)/lib/$(PACKAGE)
d_bindir = $(top_builddir)/bin
endif

#HEADERS
#set the include path found by configure
AM_CPPFLAGS= -I$(dmtcpincludedir) -I$(jalibdir)

# targets:
noinst_LIBRARIES = libdmtcpinternal.a libsyscallsreal.a libnohijack.a libjalib.a
bin_PROGRAMS = $(d_bindir)/dmtcp_launch \
	       $(d_bindir)/dmtcp_command \
	       $(d_bindir)/dmtcp_coordinator \
	       $(d_bindir)/dmtcp_restart \
	       $(d_bindir)/dmtcp_nocheckpoint
dmtcplib_PROGRAMS = $(d_libdir)/libdmtcp.so
include_HEADERS = $(srcdir)/../include/dmtcp.h

# headers:
nobase_noinst_HEADERS = constants.h \
	$(jalibdir)/jassert.h $(jalibdir)/jalloc.h $(jalibdir)/jalib.h \
	$(jalibdir)/jbuffer.h $(jalibdir)/jconvert.h $(jalibdir)/jfilesystem.h \
	$(jalibdir)/jserialize.h $(jalibdir)/jsocket.h $(jalibdir)/jtimer.h \
	$(dmtcpincludedir)/dmtcpalloc.h $(dmtcpincludedir)/dmtcp.h \
	$(dmtcpincludedir)/protectedfds.h $(dmtcpincludedir)/shareddata.h \
	$(dmtcpincludedir)/trampolines.h $(dmtcpincludedir)/util.h \
	$(dmtcpincludedir)/virtualidtable.h $(dmtcpincludedir)/procmapsarea.h \
	$(dmtcpincludedir)/procselfmaps.h \
	restartscript.h \
	dmtcp_coordinator.h dmtcpmessagetypes.h workerstate.h lookup_service.h \
	dmtcpworker.h threadsync.h coordinatorapi.h \
	barrierinfo.h pluginmanager.h plugininfo.h \
	syscallwrappers.h \
	threadlist.h threadinfo.h siginfo.h \
	uniquepid.h processinfo.h ckptserializer.h \
	mtcp/ldt.h mtcp/restore_libc.h mtcp/tlsutil.h

# Note that libdmtcpinternal.a does not include wrappers.
# dmtcp_launch, dmtcp_command, dmtcp_coordinator, etc.
#   should not need wrappers.
libdmtcpinternal_a_SOURCES = dmtcpmessagetypes.cpp  coordinatorapi.cpp \
			     workerstate.cpp \
			     uniquepid.cpp shareddata.cpp \
			     util_exec.cpp util_misc.cpp util_init.cpp \
			     jalibinterface.cpp processinfo.cpp procselfmaps.cpp

libjalib_a_SOURCES = $(jalibdir)/jalib.cpp $(jalibdir)/jassert.cpp \
		     $(jalibdir)/jbuffer.cpp $(jalibdir)/jfilesystem.cpp \
		     $(jalibdir)/jserialize.cpp $(jalibdir)/jsocket.cpp \
		     $(jalibdir)/jtimer.cpp $(jalibdir)/jalloc.cpp

# FIXME:  Rename libsyscallsreal.a to libhijack.a
# An executable should use either libsyscallsreal.a or libnohijack.a -- not both
libsyscallsreal_a_SOURCES = syscallsreal.c trampolines.cpp
libnohijack_a_SOURCES = nosyscallsreal.c dmtcpnohijackstubs.cpp

__d_bindir__dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp lookup_service.cpp restartscript.cpp

__d_bindir__dmtcp_nocheckpoint_SOURCES = dmtcp_nocheckpoint.c

__d_bindir__dmtcp_restart_SOURCES = dmtcp_restart.cpp util_exec.cpp

__d_bindir__dmtcp_command_SOURCES = dmtcp_command.cpp

__d_libdir__libdmtcp_so_SOURCES = dmtcpworker.cpp threadsync.cpp \
		      coordinatorapi.cpp execwrappers.cpp \
		      signalwrappers.cpp \
		      terminal.cpp \
		      alarm.cpp \
		      threadwrappers.cpp \
		      miscwrappers.cpp ckptserializer.cpp writeckpt.cpp \
		      glibcsystem.cpp \
		      threadlist.cpp siginfo.cpp \
		      dmtcpplugin.cpp popen.cpp syslogwrappers.cpp \
		      dlsym_default.c plugininfo.cpp pluginmanager.cpp

__d_libdir__libdmtcp_so_LDFLAGS = -shared -Xlinker -znow

# Note that an ELF object uses libsyscallsreal.a or libnohijack.a
#  but not both.  libnohijack.a has stub definitions for same symbols.
__d_libdir__libdmtcp_so_LDADD = libdmtcpinternal.a libjalib.a \
				libsyscallsreal.a mtcp/libmtcp.a \
				-ldl -lpthread -lrt

__d_bindir__dmtcp_coordinator_LDADD = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt
__d_bindir__dmtcp_launch_LDADD  = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt -ldl
__d_bindir__dmtcp_restart_LDADD     = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt -ldl
__d_bindir__dmtcp_command_LDADD     = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt -ldl

__d_bindir__dmtcp_launch_SOURCES = dmtcp_launch.cpp

mtcp/libmtcp.a:
	cd mtcp && ${MAKE} libmtcp.a

install-libs: install-dmtcplibPROGRAMS
	(cd mtcp && ${MAKE} install-libs)
	(cd plugin && ${MAKE} install-libs)

uninstall-libs: uninstall-dmtcplibPROGRAMS
	(cd mtcp && ${MAKE} uninstall-libs)
	(cd plugin && ${MAKE} uninstall-libs)

.PHONY: install-libs uninstall-libs
