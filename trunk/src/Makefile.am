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
AUTOMAKE_OPTIONS = foreign

jalibdir=$(top_srcdir)/jalib
dmtcpincludedir=$(top_srcdir)/include
dmtcplibdir = $(pkglibdir)

SUBDIRS = mtcp plugin

PICFLAGS=-fPIC
if CONFIG_M32
AM_CFLAGS = -m32 -Wa,--32 $(PICFLAGS)
AM_CXXFLAGS = -m32 -Wa,--32 $(PICFLAGS)
AM_LDFLAGS =  -m32 -Wl,-m32 -Wl,-melf_i386 -Wa,--32 $(PICFLAGS)
targetdir = $(top_builddir)/lib/$(PACKAGE)/32
else
AM_CFLAGS = $(PICFLAGS)
AM_CXXFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PICFLAGS)
targetdir = $(top_builddir)
endif

#HEADERS
#set the include path found by configure
AM_CPPFLAGS= -I$(dmtcpincludedir) -I$(jalibdir)

# targets:
noinst_LIBRARIES = libdmtcpinternal.a libsyscallsreal.a libnohijack.a libjalib.a
bin_PROGRAMS = $(targetdir)/bin/dmtcp_launch \
	       $(targetdir)/bin/dmtcp_command \
	       $(targetdir)/bin/dmtcp_coordinator \
	       $(targetdir)/bin/dmtcp_restart \
	       $(targetdir)/bin/dmtcp_nocheckpoint
dmtcplib_PROGRAMS = $(targetdir)/lib/dmtcp/libdmtcp.so
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
	dmtcp_coordinator.h dmtcpmessagetypes.h lookup_service.h \
	dmtcpworker.h threadsync.h coordinatorapi.h \
	mtcpinterface.h syscallwrappers.h syslogwrappers.h \
	threadlist.h threadinfo.h siginfo.h \
	uniquepid.h processinfo.h ckptserializer.h \
	mtcp/ldt.h mtcp/restore_libc.h mtcp/tlsutil.h

# Note that libdmtcpinternal.a does not include wrappers.
# dmtcp_launch, dmtcp_command, dmtcp_coordinator, etc.
#   should not need wrappers.
libdmtcpinternal_a_SOURCES = dmtcpmessagetypes.cpp  coordinatorapi.cpp \
			     uniquepid.cpp shareddata.cpp \
			     util_exec.cpp util_gen.cpp util_init.cpp \
			     jalibinterface.cpp processinfo.cpp

libjalib_a_SOURCES = $(jalibdir)/jalib.cpp $(jalibdir)/jassert.cpp \
		     $(jalibdir)/jbuffer.cpp $(jalibdir)/jfilesystem.cpp \
		     $(jalibdir)/jserialize.cpp $(jalibdir)/jsocket.cpp \
		     $(jalibdir)/jtimer.cpp $(jalibdir)/jalloc.cpp

# FIXME:  Rename libsyscallsreal.a to libhijack.a
# An executable should use either libsyscallsreal.a or libnohijack.a -- not both
libsyscallsreal_a_SOURCES = syscallsreal.c trampolines.cpp
libnohijack_a_SOURCES = nosyscallsreal.c dmtcpnohijackstubs.cpp

__targetdir__bin_dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp lookup_service.cpp

__targetdir__bin_dmtcp_nocheckpoint_SOURCES = dmtcp_nocheckpoint.c

__targetdir__bin_dmtcp_restart_SOURCES = dmtcp_restart.cpp util_exec.cpp ckptserializer.cpp

__targetdir__bin_dmtcp_command_SOURCES = dmtcp_command.cpp

__targetdir__lib_dmtcp_libdmtcp_so_SOURCES = dmtcpworker.cpp threadsync.cpp \
		      coordinatorapi.cpp execwrappers.cpp \
		      mtcpinterface.cpp signalwrappers.cpp \
		      threadwrappers.cpp \
		      miscwrappers.cpp ckptserializer.cpp writeckpt.cpp \
		      glibcsystem.cpp \
		      threadlist.cpp siginfo.cpp \
		      dmtcpplugin.cpp popen.cpp syslogwrappers.cpp \
		      dlsym_default.c

__targetdir__lib_dmtcp_libdmtcp_so_LDFLAGS = -shared -Xlinker -znow

#dmtcp_nocheckpoint_LDFLAGS = -static

# Note that an ELF object uses libsyscallsreal.a or libnohijack.a
#  but not both.  libnohijack.a has stub definitions for same symbols.
__targetdir__lib_dmtcp_libdmtcp_so_LDADD = libdmtcpinternal.a libjalib.a \
					   libsyscallsreal.a mtcp/libmtcp.a \
					   -ldl -lpthread -lrt

__targetdir__bin_dmtcp_coordinator_LDADD = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt
__targetdir__bin_dmtcp_launch_LDADD  = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -ldl -lpthread -lrt
__targetdir__bin_dmtcp_restart_LDADD     = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt -ldl
__targetdir__bin_dmtcp_command_LDADD     = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt -ldl

__targetdir__bin_dmtcp_launch_SOURCES = dmtcp_launch.cpp

mtcp/libmtcp.a:
	cd mtcp && ${MAKE} libmtcp.a

install-exec-hook:
	cd $(DESTDIR)$(bindir) && $(LN_S) -f dmtcp_launch dmtcp_checkpoint

uninstall-hook:
	rm -f $(DESTDIR)$(bindir)/dmtcp_checkpoint

libs: $(dmtcplib_PROGRAMS)
	(cd mtcp && ${MAKE} libs)
	(cd plugin && ${MAKE} libs)

install-libs: install-dmtcplibPROGRAMS
	(cd mtcp && ${MAKE} install-libs)
	(cd plugin && ${MAKE} install-libs)

uninstall-libs: uninstall-dmtcplibPROGRAMS
	(cd mtcp && ${MAKE} uninstall-libs)
	(cd plugin && ${MAKE} uninstall-libs)

.PHONY: libs install-libs uninstall-libs
