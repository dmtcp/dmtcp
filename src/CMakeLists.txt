add_subdirectory(mtcp)
add_subdirectory(plugin)


########### next target ###############

SET(dmtcpinternal_STAT_SRCS
   dmtcpmessagetypes.cpp
   coordinatorapi.cpp
   uniquepid.cpp
   shareddata.cpp
   util_exec.cpp
   util_misc.cpp
   util_init.cpp
   jalibinterface.cpp
   processinfo.cpp
   procselfmaps.cpp
)

add_library(dmtcpinternal STATIC ${dmtcpinternal_STAT_SRCS})


########### next target ###############

SET(syscallsreal_STAT_SRCS
   syscallsreal.c
   trampolines.cpp
)

add_library(syscallsreal STATIC ${syscallsreal_STAT_SRCS})


########### next target ###############

SET(nohijack_STAT_SRCS
   nosyscallsreal.c
   dmtcpnohijackstubs.cpp
)

add_library(nohijack STATIC ${nohijack_STAT_SRCS})


########### next target ###############

SET(jalib_STAT_SRCS
   $(jalibdir)/jalib.cpp
   $(jalibdir)/jassert.cpp
   $(jalibdir)/jbuffer.cpp
   $(jalibdir)/jfilesystem.cpp
   $(jalibdir)/jserialize.cpp
   $(jalibdir)/jsocket.cpp
   $(jalibdir)/jtimer.cpp
   $(jalibdir)/jalloc.cpp
)

add_library(jalib STATIC ${jalib_STAT_SRCS})


########### next target ###############

SET(dmtcp_launch_SRCS
   dmtcp_launch.cpp
)

add_executable(dmtcp_launch ${dmtcp_launch_SRCS})

target_link_libraries(dmtcp_launch dmtcpinternal jalib nohijack dl pthread rt)

install(TARGETS dmtcp_launch DESTINATION bin)


########### next target ###############

SET(dmtcp_command_SRCS
   dmtcp_command.cpp
)

add_executable(dmtcp_command ${dmtcp_command_SRCS})

target_link_libraries(dmtcp_command dmtcpinternal jalib nohijack pthread rt dl)

install(TARGETS dmtcp_command DESTINATION bin)


########### next target ###############

SET(dmtcp_coordinator_SRCS
   dmtcp_coordinator.cpp
   lookup_service.cpp
)

add_executable(dmtcp_coordinator ${dmtcp_coordinator_SRCS})

target_link_libraries(dmtcp_coordinator dmtcpinternal jalib nohijack pthread rt)

install(TARGETS dmtcp_coordinator DESTINATION bin)


########### next target ###############

SET(dmtcp_restart_SRCS
   dmtcp_restart.cpp
   util_exec.cpp
)

add_executable(dmtcp_restart ${dmtcp_restart_SRCS})

target_link_libraries(dmtcp_restart dmtcpinternal jalib nohijack pthread rt dl)

install(TARGETS dmtcp_restart DESTINATION bin)


########### next target ###############

SET(dmtcp_nocheckpoint_SRCS
   dmtcp_nocheckpoint.c
)

add_executable(dmtcp_nocheckpoint ${dmtcp_nocheckpoint_SRCS})

target_link_libraries(dmtcp_nocheckpoint)

install(TARGETS dmtcp_nocheckpoint DESTINATION bin)


########### install files ###############

install(FILES  $(srcdir)/../include/dmtcp.h DESTINATION include)



#original Makefile.am contents follow:

## We should disable AM_MAINTAINER_MODE since otherwise a re-build by the
##  end-user may require local tools, which may be missing or whose version is
##  incompatible with these tools (e.g. autoheader).  Instead, we run NMI and
##  openSUSE build services to test compatibility with end-user machines.
##  The developers should run autoconf, automake, etc., as needed, but
##  not propagate to the svn-generated tools that may be incompatible
##  with the user's local tools.  - Gene
## See http://www.gnu.org/s/hello/manual/automake/CVS.html#CVS
## AM_MAINTAINER_MDE([disable])
#
## not a GNU package. You can remove this line, if
## have all needed files, that a GNU package needs
## TODO(kapil): Add 'subdir-objects after automake 1.16 has been released.
#AUTOMAKE_OPTIONS = foreign
#
#jalibdir=$(top_srcdir)/jalib
#dmtcpincludedir=$(top_srcdir)/include
#dmtcplibdir = $(pkglibdir)
#
#SUBDIRS = mtcp plugin
#
#PICFLAGS=-fPIC
#AM_CFLAGS = $(PICFLAGS)
#AM_CXXFLAGS = $(PICFLAGS)
#AM_LDFLAGS = $(PICFLAGS)
#
#if CONFIG_M32
#targetdir = $(top_builddir)/lib/$(PACKAGE)/32
#else
#targetdir = $(top_builddir)
#endif
#
##HEADERS
##set the include path found by configure
#AM_CPPFLAGS= -I$(dmtcpincludedir) -I$(jalibdir)
#
## targets:
#noinst_LIBRARIES = libdmtcpinternal.a libsyscallsreal.a libnohijack.a libjalib.a
#bin_PROGRAMS = dmtcp_launch \
#	       dmtcp_command \
#	       dmtcp_coordinator \
#	       dmtcp_restart \
#	       dmtcp_nocheckpoint
#dmtcplib_PROGRAMS = libdmtcp.so
#include_HEADERS = $(srcdir)/../include/dmtcp.h
#
## headers:
#nobase_noinst_HEADERS = constants.h \
#	$(jalibdir)/jassert.h $(jalibdir)/jalloc.h $(jalibdir)/jalib.h \
#	$(jalibdir)/jbuffer.h $(jalibdir)/jconvert.h $(jalibdir)/jfilesystem.h \
#	$(jalibdir)/jserialize.h $(jalibdir)/jsocket.h $(jalibdir)/jtimer.h \
#	$(dmtcpincludedir)/dmtcpalloc.h $(dmtcpincludedir)/dmtcp.h \
#	$(dmtcpincludedir)/protectedfds.h $(dmtcpincludedir)/shareddata.h \
#	$(dmtcpincludedir)/trampolines.h $(dmtcpincludedir)/util.h \
#	$(dmtcpincludedir)/virtualidtable.h $(dmtcpincludedir)/procmapsarea.h \
#	$(dmtcpincludedir)/procselfmaps.h \
#	dmtcp_coordinator.h dmtcpmessagetypes.h lookup_service.h \
#	dmtcpworker.h threadsync.h coordinatorapi.h \
#	mtcpinterface.h syscallwrappers.h \
#	threadlist.h threadinfo.h siginfo.h \
#	uniquepid.h processinfo.h ckptserializer.h \
#	mtcp/ldt.h mtcp/restore_libc.h mtcp/tlsutil.h
#
## Note that libdmtcpinternal.a does not include wrappers.
## dmtcp_launch, dmtcp_command, dmtcp_coordinator, etc.
##   should not need wrappers.
#libdmtcpinternal_a_SOURCES = dmtcpmessagetypes.cpp  coordinatorapi.cpp \
#			     uniquepid.cpp shareddata.cpp \
#			     util_exec.cpp util_misc.cpp util_init.cpp \
#			     jalibinterface.cpp processinfo.cpp procselfmaps.cpp
#
#libjalib_a_SOURCES = $(jalibdir)/jalib.cpp $(jalibdir)/jassert.cpp \
#		     $(jalibdir)/jbuffer.cpp $(jalibdir)/jfilesystem.cpp \
#		     $(jalibdir)/jserialize.cpp $(jalibdir)/jsocket.cpp \
#		     $(jalibdir)/jtimer.cpp $(jalibdir)/jalloc.cpp
#
## FIXME:  Rename libsyscallsreal.a to libhijack.a
## An executable should use either libsyscallsreal.a or libnohijack.a -- not both
#libsyscallsreal_a_SOURCES = syscallsreal.c trampolines.cpp
#libnohijack_a_SOURCES = nosyscallsreal.c dmtcpnohijackstubs.cpp
#
#dmtcp_coordinator_SOURCES = dmtcp_coordinator.cpp lookup_service.cpp
#
#dmtcp_nocheckpoint_SOURCES = dmtcp_nocheckpoint.c
#
#dmtcp_restart_SOURCES = dmtcp_restart.cpp util_exec.cpp
#
#dmtcp_command_SOURCES = dmtcp_command.cpp
#
#libdmtcp_so_SOURCES = dmtcpworker.cpp threadsync.cpp \
#		      coordinatorapi.cpp execwrappers.cpp \
#		      mtcpinterface.cpp signalwrappers.cpp \
#		      procname.cpp terminal.cpp \
#		      alarm.cpp \
#		      threadwrappers.cpp \
#		      miscwrappers.cpp ckptserializer.cpp writeckpt.cpp \
#		      glibcsystem.cpp \
#		      threadlist.cpp siginfo.cpp \
#		      dmtcpplugin.cpp popen.cpp syslogwrappers.cpp \
#		      dlsym_default.c
#
#libdmtcp_so_LDFLAGS = -shared -Xlinker -znow
#
##dmtcp_nocheckpoint_LDFLAGS = -static
#
## Note that an ELF object uses libsyscallsreal.a or libnohijack.a
##  but not both.  libnohijack.a has stub definitions for same symbols.
#libdmtcp_so_LDADD = libdmtcpinternal.a libjalib.a \
#					   libsyscallsreal.a mtcp/libmtcp.a \
#					   -ldl -lpthread -lrt
#
#dmtcp_coordinator_LDADD = libdmtcpinternal.a libjalib.a \
#			  libnohijack.a -lpthread -lrt
#dmtcp_launch_LDADD  = libdmtcpinternal.a libjalib.a \
#			  libnohijack.a -ldl -lpthread -lrt
#dmtcp_restart_LDADD     = libdmtcpinternal.a libjalib.a \
#			  libnohijack.a -lpthread -lrt -ldl
#dmtcp_command_LDADD     = libdmtcpinternal.a libjalib.a \
#			  libnohijack.a -lpthread -lrt -ldl
#
#dmtcp_launch_SOURCES = dmtcp_launch.cpp
#
#mtcp/libmtcp.a:
#	cd mtcp && ${MAKE} libmtcp.a
#
#libs: $(dmtcplib_PROGRAMS)
#	(cd mtcp && ${MAKE} libs)
#	(cd plugin && ${MAKE} libs)
#
#install-libs: install-dmtcplibPROGRAMS
#	(cd mtcp && ${MAKE} install-libs)
#	(cd plugin && ${MAKE} install-libs)
#
#uninstall-libs: uninstall-dmtcplibPROGRAMS
#	(cd mtcp && ${MAKE} uninstall-libs)
#	(cd plugin && ${MAKE} uninstall-libs)
#
#.PHONY: libs install-libs uninstall-libs
