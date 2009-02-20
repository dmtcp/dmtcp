#HEADERS 
#set the include path found by configure
INCLUDES= $(all_includes)

# targets:
noinst_LIBRARIES = libdmtcpinternal.a
pkglib_PROGRAMS = dmtcp_checkpoint dmtcp_command dmtcp_coordinator \
	dmtcp_restart dmtcp_inspector dmtcphijack.so

# headers:
nobase_noinst_HEADERS = connectionstate.h connection.h connectionidentifier.h \
	connectionmanager.h connectionrewirer.h constants.h dmtcp_coordinator.h \
	dmtcpmessagetypes.h dmtcpworker.h ../jalib/jassert.h \
	../jalib/jbuffer.h ../jalib/jconvert.h ../jalib/jfilesystem.h \
	../jalib/jserialize.h ../jalib/jsocket.h ../jalib/jtimer.h
	kernelbufferdrainer.h mtcpinterface.h nodetable.h protectedfds.h \
	signalmanager.h sockettable.h syscallwrappers.h syslogcheckpointer.h \
	uniquepid.h virtualpidtable.h

libdmtcpinternal_a_SOURCES = connectionstate.cpp connection.cpp \
	connectionidentifier.cpp connectionmanager.cpp connectionrewirer.cpp \
	dmtcpmessagetypes.cpp ../jalib/jassert.cpp ../jalib/jbuffer.cpp \
	../jalib/jfilesystem.cpp ../jalib/jserialize.cpp ../jalib/jsocket.cpp \
	../jalib/jtimer.cpp kernelbufferdrainer.cpp nodetable.cpp \
	protectedfds.cpp signalmanager.cpp sockettable.cpp \
	syscallsreal.c syslogcheckpointer.cpp uniquepid.cpp dmtcpworker.cpp\
  virtualpidtable.cpp

dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp

dmtcp_checkpoint_SOURCES = dmtcp_checkpoint.cpp 

dmtcp_restart_SOURCES = dmtcp_restart.cpp

dmtcp_command_SOURCES = dmtcp_command.cpp

dmtcphijack_so_SOURCES = dmtcpapi.cpp dmtcpworker.cpp execwrappers.cpp \
	mtcpinterface.cpp signalwrapers.cpp syscallwrappers.c workerhijack.cpp\
  pidwrappers.cpp

dmtcp_inspector_SOURCES = dmtcpinspector.cpp

dmtcphijack_so_LDFLAGS = -shared 

dmtcphijack_so_LDADD    = libdmtcpinternal.a -ldl -lpthread
dmtcp_coordinator_LDADD = libdmtcpinternal.a -ldl -lpthread
dmtcp_checkpoint_LDADD  = libdmtcpinternal.a -ldl -lpthread
dmtcp_restart_LDADD     = libdmtcpinternal.a -ldl -lpthread
dmtcp_command_LDADD     = libdmtcpinternal.a -ldl -lpthread
dmtcp_inspector_LDADD = libdmtcpinternal.a -ldl -lpthread

# dmtcpaware
pkglib_HEADERS = dmtcpaware.h dmtcpaware.c
pkglib_LTLIBRARIES = libdmtcpaware.la
libdmtcpaware_la_SOURCES = dmtcpaware.c 
#libdmtcpaware_la_LDFLAGS = -avoid-version -module
libdmtcpaware_la_LDFLAGS = -avoid-version 

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

