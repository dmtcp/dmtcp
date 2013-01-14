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

#HEADERS
#set the include path found by configure
INCLUDES= $(all_includes)

# targets:
noinst_LIBRARIES = libdmtcpinternal.a libsyscallsreal.a libnohijack.a \
		   libsysvipc.a libjalib.a libconnection.a
bin_PROGRAMS = dmtcp_checkpoint dmtcp_command dmtcp_coordinator \
	       dmtcp_restart dmtcp_nocheckpoint
pkglibexec_PROGRAMS = dmtcphijack.so libdmtcpaware.so.1.0.0
include_HEADERS = dmtcpaware.h
lib_LIBRARIES = libdmtcpaware.a

# headers:
nobase_noinst_HEADERS = constants.h \
	../jalib/jassert.h ../jalib/jalloc.h ../jalib/jalib.h \
	../jalib/jbuffer.h ../jalib/jconvert.h ../jalib/jfilesystem.h \
	../jalib/jserialize.h ../jalib/jsocket.h ../jalib/jtimer.h \
	sysvipc/sysvipc.h con/connection.h \
	con/connectionmanager.h con/connectionrewirer.h \
	con/kernelbufferdrainer.h con/util_descriptor.h\
	dmtcp_coordinator.h dmtcpmessagetypes.h \
	dmtcpworker.h threadsync.h coordinatorapi.h \
	mtcpinterface.h  protectedfds.h connectionidentifier.h \
	syscallwrappers.h syslogwrappers.h \
	uniquepid.h processinfo.h util.h virtualidtable.h \
	dmtcpplugin.h lookup_service.h trampolines.h shareddata.h

# Note that libdmtcpinternal.a does not include wrappers.
# dmtcp_checkpoint, dmtcp_command, dmtcp_coordinator, etc.
#   should not need wrappers.
libdmtcpinternal_a_SOURCES = dmtcpmessagetypes.cpp protectedfds.cpp \
			     uniquepid.cpp shareddata.cpp \
			     coordinatorapi.cpp connectionidentifier.cpp \
			     util_exec.cpp util_gen.cpp util_init.cpp \
			     jalibinterface.cpp processinfo.cpp

libconnection_a_SOURCES = con/con.cpp con/connection.cpp \
			  con/connectionmanager.cpp con/connectionrewirer.cpp \
			  con/kernelbufferdrainer.cpp con/util_descriptor.cpp

libsysvipc_a_SOURCES = sysvipc/sysvipc.cpp sysvipc/sysvipcwrappers.cpp

libjalib_a_SOURCES = ../jalib/jalib.cpp ../jalib/jassert.cpp \
		     ../jalib/jbuffer.cpp ../jalib/jfilesystem.cpp \
		     ../jalib/jserialize.cpp ../jalib/jsocket.cpp \
		     ../jalib/jtimer.cpp ../jalib/jalloc.cpp

# FIXME:  Rename libsyscallsreal.a to libhijack.a
# An executable should use either libsyscallsreal.a or libnohijack.a -- not both
libsyscallsreal_a_SOURCES = syscallsreal.c trampolines.cpp
libnohijack_a_SOURCES = nosyscallsreal.c dmtcpnohijackstubs.cpp

dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp lookup_service.cpp

dmtcp_checkpoint_SOURCES = dmtcp_checkpoint.cpp

dmtcp_nocheckpoint_SOURCES = dmtcp_nocheckpoint.c

dmtcp_restart_SOURCES = dmtcp_restart.cpp util_exec.cpp

dmtcp_command_SOURCES = dmtcp_command.cpp

dmtcphijack_so_SOURCES = dmtcpawareapi.cpp dmtcpworker.cpp threadsync.cpp \
			 coordinatorapi.cpp execwrappers.cpp \
		         mtcpinterface.cpp signalwrappers.cpp \
			 workerhijack.cpp threadwrappers.cpp \
			 miscwrappers.cpp \
			 glibcsystem.cpp mallocwrappers.cpp \
			 dmtcpplugin.cpp popen.cpp syslogwrappers.cpp \
			 con/eventwrappers.cpp con/filewrappers.cpp \
			 con/socketwrappers.cpp con/posixipcwrappers.cpp \
			 sysvipc/sysvipcwrappers.cpp

dmtcphijack_so_LDFLAGS = -shared

#dmtcp_nocheckpoint_LDFLAGS = -static

# Note that an ELF object uses libsyscallsreal.a or libnohijack.a
#  but not both.  libnohijack.a has stub definitions for same symbols.
dmtcphijack_so_LDADD    = libdmtcpinternal.a libsysvipc.a libconnection.a \
			  libjalib.a libsyscallsreal.a  -ldl -lpthread -lrt \
			  ../../mtcp/libmtcp.so
dmtcp_coordinator_LDADD = libdmtcpinternal.a libconnection.a libjalib.a \
			  libnohijack.a -lpthread -lrt
dmtcp_checkpoint_LDADD  = libdmtcpinternal.a libconnection.a libjalib.a \
			  libnohijack.a -ldl -lpthread -lrt
dmtcp_restart_LDADD     = libdmtcpinternal.a libconnection.a libjalib.a \
			  libnohijack.a -lpthread -lrt -ldl
dmtcp_command_LDADD     = libdmtcpinternal.a libconnection.a libjalib.a \
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
