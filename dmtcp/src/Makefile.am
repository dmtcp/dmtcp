# set the include path found by configure
INCLUDES= $(all_includes)

# targets:
noinst_PROGRAMS = dmtcphijack.so dmtcp_master dmtcp_checkpoint dmtcp_restart
noinst_LIBRARIES = libdmtcpinternal.a

# headers:
noinst_HEADERS = checkpointcoordinator.h connection.h connectionidentifier.h \
	connectionmanager.h connectionrewirer.h constants.h dmtcpmaster.h \
	dmtcpmessagetypes.h dmtcpworker.h jassert.h jbuffer.h jconvert.h \
	jfilesystem.h jserialize.h jsocket.h jtimer.h kernelbufferdrainer.h \
	mtcpinterface.h nodetable.h protectedfds.h signalmanager.h sockettable.h \
	syscallwrappers.h syslogcheckpointer.h uniquepid.h

libdmtcpinternal_a_SOURCES = checkpointcoordinator.cpp connection.cpp \
	connectionidentifier.cpp connectionmanager.cpp connectionrewirer.cpp dmtcpmessagetypes.cpp \
	jassert.cpp jbuffer.cpp jfilesystem.cpp jserialize.cpp jsocket.cpp jtimer.cpp \
	kernelbufferdrainer.cpp nodetable.cpp protectedfds.cpp signalmanager.cpp sockettable.cpp \
	syscallsreal.c syslogcheckpointer.cpp uniquepid.cpp dmtcpworker.cpp

dmtcphijack_so_SOURCES = dmtcpworker.cpp syscallwrappers.c execwrappers.cpp \
	workerhijack.cpp mtcpinterface.cpp

dmtcp_master_SOURCES = dmtcpmaster.cpp

dmtcp_checkpoint_SOURCES = dmtcpcheckpoint.cpp 

dmtcp_restart_SOURCES = dmtcprestart.cpp

dmtcphijack_so_LDADD = libdmtcpinternal.a -ldl 
dmtcphijack_so_LDFLAGS = -shared 
dmtcp_master_LDADD = libdmtcpinternal.a -ldl
dmtcp_checkpoint_LDADD = libdmtcpinternal.a -ldl
dmtcp_restart_LDADD = libdmtcpinternal.a -ldl

PICFLAGS=-fPIC
AM_CFLAGS = $(PICFLAGS)
AM_CXXFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PICFLAGS)



