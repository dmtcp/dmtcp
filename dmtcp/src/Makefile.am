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

jalibdir=$(srcdir)/../jalib/
dmtcpincludedir=$(srcdir)/../include
dmtcplibdir = $(pkglibdir)

#HEADERS
#set the include path found by configure
AM_CPPFLAGS= $(all_includes) -I$(dmtcpincludedir) -I$(jalibdir)

# targets:
noinst_LIBRARIES = libdmtcpinternal.a libsyscallsreal.a libnohijack.a libjalib.a
bin_PROGRAMS = dmtcp_launch dmtcp_command dmtcp_coordinator \
	       dmtcp_restart dmtcp_nocheckpoint
dmtcplib_PROGRAMS = libdmtcp.so libdmtcpaware.so.1.0.0
include_HEADERS = dmtcpaware.h $(srcdir)/../include/dmtcpplugin.h
lib_LIBRARIES = libdmtcpaware.a

# headers:
nobase_noinst_HEADERS = constants.h \
	$(jalibdir)/jassert.h $(jalibdir)/jalloc.h $(jalibdir)/jalib.h \
	$(jalibdir)/jbuffer.h $(jalibdir)/jconvert.h $(jalibdir)/jfilesystem.h \
	$(jalibdir)/jserialize.h $(jalibdir)/jsocket.h $(jalibdir)/jtimer.h \
	$(dmtcpincludedir)/dmtcpalloc.h $(dmtcpincludedir)/dmtcpplugin.h \
	$(dmtcpincludedir)/protectedfds.h $(dmtcpincludedir)/shareddata.h \
	$(dmtcpincludedir)/trampolines.h $(dmtcpincludedir)/util.h \
	$(dmtcpincludedir)/virtualidtable.h \
	dmtcp_coordinator.h dmtcpmessagetypes.h lookup_service.h \
	dmtcpworker.h threadsync.h coordinatorapi.h \
	mtcpinterface.h syscallwrappers.h syslogwrappers.h \
	uniquepid.h processinfo.h ckptserializer.h

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

dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp lookup_service.cpp

dmtcp_launch_SOURCES = dmtcp_launch.cpp

dmtcp_nocheckpoint_SOURCES = dmtcp_nocheckpoint.c

dmtcp_restart_SOURCES = dmtcp_restart.cpp util_exec.cpp ckptserializer.cpp

dmtcp_command_SOURCES = dmtcp_command.cpp

libdmtcp_so_SOURCES = dmtcpawareapi.cpp dmtcpworker.cpp threadsync.cpp \
		      coordinatorapi.cpp execwrappers.cpp \
		      mtcpinterface.cpp signalwrappers.cpp \
		      threadwrappers.cpp \
		      miscwrappers.cpp ckptserializer.cpp \
		      glibcsystem.cpp \
		      dmtcpplugin.cpp popen.cpp syslogwrappers.cpp

# Add the following three paths to rpath:
# 1. ../                : Useful when installed with 'make DESTDIR=xxx install'
# 2. $ORIGIN/../../mtcp : Useful when running from build directory
# 3. $(libdir)          : Useful when installed with 'make install'
libdmtcp_so_LDFLAGS = -shared -L../../mtcp \
		      -Xlinker -rpath -Xlinker ../ \
		      -Xlinker -rpath -Xlinker '$$ORIGIN/../../mtcp' \
		      -Xlinker -rpath -Xlinker $(libdir)

#dmtcp_nocheckpoint_LDFLAGS = -static

# Note that an ELF object uses libsyscallsreal.a or libnohijack.a
#  but not both.  libnohijack.a has stub definitions for same symbols.
libdmtcp_so_LDADD	= libdmtcpinternal.a libjalib.a libsyscallsreal.a \
			  -ldl -lpthread -lrt -lmtcp
dmtcp_coordinator_LDADD = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt
dmtcp_launch_LDADD  = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -ldl -lpthread -lrt
dmtcp_restart_LDADD     = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt -ldl
dmtcp_command_LDADD     = libdmtcpinternal.a libjalib.a \
			  libnohijack.a -lpthread -lrt

libdmtcpaware_a_SOURCES = dmtcpaware.c

libdmtcpaware_so_1_0_0_SOURCES = dmtcpaware.c
libdmtcpaware_so_1_0_0_LDFLAGS = -shared -Wl,-soname,libdmtcpaware.so.1

PICFLAGS=-fPIC
if CONFIG_M32
AM_CFLAGS = -m32 -Wa,--32 $(PICFLAGS)
AM_CXXFLAGS = -m32 -Wa,--32 $(PICFLAGS)
AM_LDFLAGS =  -m32 -Wl,-m32 -Wl,-melf_i386 -Wa,--32 $(PICFLAGS)
else
AM_CFLAGS = $(PICFLAGS)
AM_CXXFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PICFLAGS)
endif

install-exec-hook:
	cd $(DESTDIR)$(bindir) && $(LN_S) -f dmtcp_launch dmtcp_checkpoint

install-data-hook:
	cd $(DESTDIR)$(libdir) && mv dmtcp/libdmtcpaware.so.1.0.0 .
	cd $(DESTDIR)$(libdir) && \
	  $(LN_S) -f libdmtcpaware.so.1.0.0 libdmtcpaware.so.1
	cd $(DESTDIR)$(libdir) && $(LN_S) -f libdmtcpaware.so.1 libdmtcpaware.so

uninstall-hook:
	rm -f $(DESTDIR)$(bindir)/dmtcp_checkpoint
	rm -f $(DESTDIR)$(libdir)/libdmtcpaware.so*
