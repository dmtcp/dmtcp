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
noinst_LIBRARIES = libdmtcpinternal.a libsyscallsreal.a libnohijack.a
pkglib_PROGRAMS = dmtcp_checkpoint dmtcp_command dmtcp_coordinator \
	dmtcp_restart dmtcphijack.so dmtcp_inspector dmtcp_nocheckpoint \
	libdmtcpaware.so.1.0.0
include_HEADERS = dmtcpaware.h
lib_LIBRARIES = libdmtcpaware.a

if RECORD_REPLAY
if HAS_I486
# This -march=i486 is needed to compile synchronizationlogging.cpp
CXXFLAGS += -march=i486
endif
pkglib_PROGRAMS += fred_read_log
fred_read_log_SOURCES = fred_read_log.cpp
fred_read_log_LDADD   = libdmtcpinternal.a libnohijack.a -lpthread
endif

# headers:
nobase_noinst_HEADERS = connectionstate.h connection.h connectionidentifier.h \
	connectionmanager.h connectionrewirer.h constants.h \
	dmtcp_coordinator.h dmtcpmessagetypes.h \
	dmtcpworker.h dmtcpcoordinatorapi.h ../jalib/jassert.h \
	../jalib/jbuffer.h ../jalib/jconvert.h ../jalib/jfilesystem.h \
	../jalib/jserialize.h ../jalib/jsocket.h ../jalib/jtimer.h
	kernelbufferdrainer.h mtcpinterface.h nodetable.h protectedfds.h \
	sockettable.h syscallwrappers.h syslogcheckpointer.h \
	uniquepid.h virtualpidtable.h util.h sysvipc.h ../jalib/jalloc.h \
	synchronizationlogging.h ptracewrapper.h log.h dmtcpmodule.h \
	lookup_service.h trampolines.h

# Note that libdmtcpinternal.a does not include wrappers.
# dmtcp_checkpoint, dmtcp_command, dmtcp_coordinator, etc.
#   should not need wrappers.
# FIXME:  Move syscallsreal.c from here to dmtcphijack.so
#          and also add it explicitly to dmtcp_restart_SOURCES.
#  Move/split connection.cpp (1.0 MB binary) and kernelbufferdrainer.cpp (.5 MB)
#   and others(?) to dmtcphijack.so or dmtcp_restart.  Stop the replication.
#  Also eventually modify dmtcpworker.cpp so it apears _only_ in dmtcphijack.so
libdmtcpinternal_a_SOURCES = connectionstate.cpp connection.cpp \
	connectionidentifier.cpp connectionmanager.cpp connectionrewirer.cpp \
	dmtcpmessagetypes.cpp ../jalib/jassert.cpp ../jalib/jbuffer.cpp \
	../jalib/jfilesystem.cpp ../jalib/jserialize.cpp ../jalib/jsocket.cpp \
	../jalib/jtimer.cpp kernelbufferdrainer.cpp nodetable.cpp \
	protectedfds.cpp sockettable.cpp \
	syslogcheckpointer.cpp uniquepid.cpp \
	dmtcpcoordinatorapi.cpp virtualpidtable.cpp sysvipc.cpp \
	util_exec.cpp util_gen.cpp util_init.cpp \
	../jalib/jalloc.cpp synchronizationlogging.cpp log.cpp
# FIXME:  Rename libsyscallsreal.a to libhijack.a
# An executable should use either libsyscallsreal.a or libnohijack.a -- not both
libsyscallsreal_a_SOURCES = syscallsreal.c trampolines.cpp
libnohijack_a_SOURCES = nosyscallsreal.c dmtcpnohijackstubs.cpp

dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp lookup_service.cpp

dmtcp_checkpoint_SOURCES = dmtcp_checkpoint.cpp

dmtcp_nocheckpoint_SOURCES = dmtcp_nocheckpoint.c

dmtcp_restart_SOURCES = dmtcp_restart.cpp

dmtcp_command_SOURCES = dmtcp_command.cpp

dmtcphijack_so_SOURCES = dmtcpawareapi.cpp dmtcpworker.cpp \
	dmtcpcoordinatorapi.cpp \
	execwrappers.cpp mtcpinterface.cpp signalwrappers.cpp \
	socketwrappers.cpp workerhijack.cpp \
	pidwrappers.cpp miscwrappers.cpp glibcsystem.cpp sysvipc.cpp \
	filewrappers.cpp mallocwrappers.cpp ptracewrapper.cpp dmtcpmodule.cpp
# This should be a superset of all optional files.
EXTRA_dmtcphijack_so_SOURCES = pthreadwrappers.cpp synchronizationlogging.cpp log.cpp

dmtcp_inspector_SOURCES = dmtcp_inspector.cpp

dmtcphijack_so_LDFLAGS = -shared -module

#dmtcp_nocheckpoint_LDFLAGS = -static

# pthreadwrappers.cpp and synchronizationlogging.cpp are EXTRA files.
# To compile the EXTRA files conditionally, replace them
#  by @dmtcp_module@ (or other name), and define dmtcp_module in configure.ac
# Note that an ELF object uses libsyscallsreal.a or libnohijack.a
#  but not both.  libnohijack.a has stub definitions for same symbols.
dmtcphijack_so_LDADD    = pthreadwrappers.o synchronizationlogging.o log.o \
	libdmtcpinternal.a libsyscallsreal.a  -ldl -lpthread
dmtcp_coordinator_LDADD = libdmtcpinternal.a libnohijack.a -lpthread
dmtcp_checkpoint_LDADD  = libdmtcpinternal.a libnohijack.a -ldl -lpthread
dmtcp_restart_LDADD     = libdmtcpinternal.a libnohijack.a -lpthread
dmtcp_command_LDADD     = libdmtcpinternal.a libnohijack.a -lpthread
dmtcp_inspector_LDADD   = libdmtcpinternal.a libnohijack.a -lpthread

libdmtcpaware_a_SOURCES = dmtcpaware.c

libdmtcpaware_so_1_0_0_SOURCES = dmtcpaware.c
libdmtcpaware_so_1_0_0_LDFLAGS = -shared -Wl,-soname,libdmtcpaware.so.1 -module

# dmtcpaware
#pkglib_HEADERS = dmtcpaware.h dmtcpaware.c
#pkglib_LIBRARIES = libdmtcpaware.a
#libdmtcpaware_a_LDFLAGS = -avoid-version -module
#libdmtcpaware_a_LDFLAGS = -avoid-version

#libdmtcpaware_so_LDFLAGS = -shared -Wl,-soname,libdmtcpaware.so.0
#libdmtcpaware_so_SOURCES = dmtcpaware.c
#libdmtcpaware_a_SOURCES  = dmtcpaware.c

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

