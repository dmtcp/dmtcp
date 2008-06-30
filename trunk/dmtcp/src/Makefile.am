#
#set the include path found by configure
INCLUDES= $(all_includes)

# targets:
noinst_PROGRAMS = dmtcp_checkpoint dmtcp_command dmtcp_coordinator \
	dmtcp_restart dmtcphijack.so libdmtcpaware.so
noinst_LIBRARIES = libdmtcpinternal.a libdmtcpaware.a

# headers:
noinst_HEADERS = checkpointcoordinator.h connection.h connectionidentifier.h \
	connectionmanager.h connectionrewirer.h constants.h dmtcpcoordinator.h \
	dmtcpmessagetypes.h dmtcpworker.h jassert.h jbuffer.h jconvert.h \
	jfilesystem.h jserialize.h jsocket.h jtimer.h kernelbufferdrainer.h \
	mtcpinterface.h nodetable.h protectedfds.h signalmanager.h sockettable.h \
	syscallwrappers.h syslogcheckpointer.h uniquepid.h dmtcpaware.h

libdmtcpinternal_a_SOURCES = checkpointcoordinator.cpp connection.cpp \
	connectionidentifier.cpp connectionmanager.cpp connectionrewirer.cpp dmtcpmessagetypes.cpp \
	jassert.cpp jbuffer.cpp jfilesystem.cpp jserialize.cpp jsocket.cpp jtimer.cpp \
	kernelbufferdrainer.cpp nodetable.cpp protectedfds.cpp signalmanager.cpp sockettable.cpp \
	syscallsreal.c syslogcheckpointer.cpp uniquepid.cpp dmtcpworker.cpp

dmtcphijack_so_SOURCES = dmtcpworker.cpp execwrappers.cpp mtcpinterface.cpp \
	syscallwrappers.c workerhijack.cpp dmtcpapi.cpp

dmtcp_coordinator_SOURCES = dmtcpcoordinator.cpp

dmtcp_checkpoint_SOURCES = dmtcpcheckpoint.cpp 

dmtcp_restart_SOURCES = dmtcprestart.cpp

dmtcp_command_SOURCES = dmtcp_command.cpp

dmtcphijack_so_LDFLAGS = -shared 

dmtcphijack_so_LDADD    = libdmtcpinternal.a -ldl -lpthread
dmtcp_coordinator_LDADD = libdmtcpinternal.a -ldl -lpthread
dmtcp_checkpoint_LDADD  = libdmtcpinternal.a -ldl -lpthread
dmtcp_restart_LDADD     = libdmtcpinternal.a -ldl -lpthread
dmtcp_command_LDADD     = libdmtcpinternal.a -ldl -lpthread

libdmtcpaware_so_LDFLAGS = -shared 
libdmtcpaware_so_SOURCES = dmtcpaware.c
libdmtcpaware_a_SOURCES  = dmtcpaware.c

PICFLAGS=-fPIC
AM_CFLAGS = $(PICFLAGS)
AM_CXXFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PICFLAGS)


