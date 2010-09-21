#HEADERS
#set the include path found by configure
INCLUDES= $(all_includes)

# targets:
noinst_LIBRARIES = libdmtcpinternal.a libsyscallsreal.a libnosyscallsreal.a
pkglib_PROGRAMS = dmtcp_checkpoint dmtcp_command dmtcp_coordinator \
	dmtcp_restart dmtcp_inspector dmtcphijack.so dmtcp_nocheckpoint

# headers:
nobase_noinst_HEADERS = connectionstate.h connection.h connectionidentifier.h \
	connectionmanager.h connectionrewirer.h constants.h \
	dmtcp_coordinator.h dmtcpmessagetypes.h \
	dmtcpworker.h dmtcpcoordinatorapi.h ../jalib/jassert.h \
	../jalib/jbuffer.h ../jalib/jconvert.h ../jalib/jfilesystem.h \
	../jalib/jserialize.h ../jalib/jsocket.h ../jalib/jtimer.h
	kernelbufferdrainer.h mtcpinterface.h nodetable.h protectedfds.h \
	signalmanager.h sockettable.h syscallwrappers.h syslogcheckpointer.h \
	uniquepid.h virtualpidtable.h ../jalib/jalloc.h

# Note that libdmtcpinternal.a does not include wrappers.
# dmtcp_checkpoint, dmtcp_restart, dmtcp_command, dmtcp_coordinator, etc.
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
	protectedfds.cpp signalmanager.cpp sockettable.cpp \
	syslogcheckpointer.cpp uniquepid.cpp dmtcpworker.cpp \
	dmtcpcoordinatorapi.cpp virtualpidtable.cpp ../jalib/jalloc.cpp
libsyscallsreal_a_SOURCES = syscallsreal.c
libnosyscallsreal_a_SOURCES = nosyscallsreal.c

dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp

dmtcp_checkpoint_SOURCES = dmtcp_checkpoint.cpp

dmtcp_nocheckpoint_SOURCES = dmtcp_nocheckpoint.c

dmtcp_restart_SOURCES = dmtcp_restart.cpp

dmtcp_command_SOURCES = dmtcp_command.cpp

dmtcphijack_so_SOURCES = dmtcpawareapi.cpp dmtcpworker.cpp \
	dmtcpcoordinatorapi.cpp \
	execwrappers.cpp mtcpinterface.cpp signalwrappers.cpp \
	socketwrappers.cpp workerhijack.cpp \
	pidwrappers.cpp miscwrappers.cpp glibcsystem.cpp

dmtcp_inspector_SOURCES = dmtcp_inspector.cpp

dmtcphijack_so_LDFLAGS = -shared -avoid-version -module

dmtcp_nocheckpoint_LDFLAGS = -static

dmtcphijack_so_LDADD    = libdmtcpinternal.a libsyscallsreal.a -ldl -lpthread
dmtcp_coordinator_LDADD = libdmtcpinternal.a libnosyscallsreal.a
dmtcp_checkpoint_LDADD  = libdmtcpinternal.a libnosyscallsreal.a -ldl -lpthread
dmtcp_restart_LDADD     = libdmtcpinternal.a libnosyscallsreal.a -lpthread
dmtcp_command_LDADD     = libdmtcpinternal.a libnosyscallsreal.a -lpthread
dmtcp_inspector_LDADD   = libdmtcpinternal.a libnosyscallsreal.a

# dmtcpaware
pkglib_HEADERS = dmtcpaware.h dmtcpaware.c
pkglib_LIBRARIES = libdmtcpaware.a
libdmtcpaware_a_SOURCES = dmtcpaware.c
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

