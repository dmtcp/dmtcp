# not a GNU package. You can remove this line, if
# have all needed files, that a GNU package needs
AUTOMAKE_OPTIONS = foreign

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
AM_CPPFLAGS  = -I$(top_srcdir)/include
AM_CPPFLAGS += -I$(top_srcdir)/jalib

dmtcp_ldflags = -shared -Xlinker -znow -Wl,--unresolved-symbols=ignore-all
LDADD = -ldl -lpthread

libdmtcpdir = $(pkglibdir)

bin_PROGRAMS =
bin_SCRIPTS =
CLEANFILES =
dist_bin_SCRIPTS =
libdmtcp_PROGRAMS =
noinst_LIBRARIES =

#
# Alloc plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_alloc.so
__d_libdir__libdmtcp_alloc_so_SOURCES =                                \
	alloc/alloc.h                                                  \
	alloc/mallocwrappers.cpp                                       \
	alloc/mmapwrappers.cpp
__d_libdir__libdmtcp_alloc_so_LDFLAGS = $(dmtcp_ldflags)
__d_libdir__libdmtcp_alloc_so_LDADD = $(LDADD)

#
# Dl plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_dl.so
__d_libdir__libdmtcp_dl_so_SOURCES =                                   \
	dl/dlwrappers.cpp
__d_libdir__libdmtcp_dl_so_LDFLAGS = $(dmtcp_ldflags)
__d_libdir__libdmtcp_dl_so_LDADD = $(LDADD)

#
# IPC plugin
# TODO(kapil): Split SSH plugin into its own.
#
noinst_LIBRARIES += libssh.a
libssh_a_SOURCES =                                                     \
	ipc/ssh/util_ssh.cpp                                           \
	ipc/ssh/util_ssh.h
libssh_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
libssh_a_SHORTNAME = i

libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_ipc.so
__d_libdir__libdmtcp_ipc_so_SOURCES =                                  \
	ipc/connection.cpp                                             \
	ipc/connection.h                                               \
	ipc/connectionidentifier.cpp                                   \
	ipc/connectionidentifier.h                                     \
	ipc/connectionlist.cpp                                         \
	ipc/connectionlist.h                                           \
	ipc/ipc.cpp                                                    \
	ipc/ipc.h                                                      \
	ipc/event/eventconnection.cpp                                  \
	ipc/event/eventconnection.h                                    \
	ipc/event/eventconnlist.cpp                                    \
	ipc/event/eventconnlist.h                                      \
	ipc/event/eventwrappers.cpp                                    \
	ipc/event/eventwrappers.h                                      \
	ipc/event/util_descriptor.cpp                                  \
	ipc/event/util_descriptor.h                                    \
	ipc/file/fileconnection.cpp                                    \
	ipc/file/fileconnection.h                                      \
	ipc/file/fileconnlist.cpp                                      \
	ipc/file/fileconnlist.h                                        \
	ipc/file/filewrappers.cpp                                      \
	ipc/file/filewrappers.h                                        \
	ipc/file/openwrappers.cpp                                      \
	ipc/file/posixipcwrappers.cpp                                  \
	ipc/file/ptyconnection.cpp                                     \
	ipc/file/ptyconnection.h                                       \
	ipc/file/ptyconnlist.cpp                                       \
	ipc/file/ptyconnlist.h                                         \
	ipc/file/ptywrappers.cpp                                       \
	ipc/file/ptywrappers.h                                         \
	ipc/socket/connectionmessage.h                                 \
	ipc/socket/connectionrewirer.cpp                               \
	ipc/socket/connectionrewirer.h                                 \
	ipc/socket/kernelbufferdrainer.cpp                             \
	ipc/socket/kernelbufferdrainer.h                               \
	ipc/socket/socketconnection.cpp                                \
	ipc/socket/socketconnection.h                                  \
	ipc/socket/socketconnlist.cpp                                  \
	ipc/socket/socketconnlist.h                                    \
	ipc/socket/socketwrappers.cpp                                  \
	ipc/socket/socketwrappers.h                                    \
	ipc/ssh/ssh.cpp                                                \
	ipc/ssh/sshdrainer.cpp                                         \
	ipc/ssh/sshdrainer.h                                           \
	ipc/ssh/ssh.h
__d_libdir__libdmtcp_ipc_so_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
__d_libdir__libdmtcp_ipc_so_LDFLAGS = $(dmtcp_ldflags)
__d_libdir__libdmtcp_ipc_so_LDADD = $(LDADD) -lrt
__d_libdir__libdmtcp_ipc_so_SHORTNAME = i

bin_PROGRAMS += $(d_bindir)/dmtcp_ssh
__d_bindir__dmtcp_ssh_SOURCES =                                        \
	ipc/ssh/dmtcp_ssh.cpp
__d_bindir__dmtcp_ssh_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
__d_bindir__dmtcp_ssh_LDADD = libssh.a $(LDADD)
__d_bindir__dmtcp_ssh_SHORTNAME = i

bin_PROGRAMS += $(d_bindir)/dmtcp_sshd
__d_bindir__dmtcp_sshd_SOURCES =                                       \
	ipc/ssh/dmtcp_sshd.cpp
__d_bindir__dmtcp_sshd_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
__d_bindir__dmtcp_sshd_LDADD = libssh.a $(LDADD)
__d_bindir__dmtcp_sshd_SHORTNAME = i

#
# Pid plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_pid.so
__d_libdir__libdmtcp_pid_so_SOURCES =                                  \
	pid/pid.cpp                                                    \
	pid/pid_filewrappers.cpp                                       \
	pid/pid.h                                                      \
	pid/pid_miscwrappers.cpp                                       \
	pid/pid_syscallsreal.c                                         \
	pid/pidwrappers.cpp                                            \
	pid/pidwrappers.h                                              \
	pid/sched_wrappers.cpp                                         \
	pid/virtualpidtable.cpp                                        \
	pid/virtualpidtable.h
if ENABLE_PTHREAD_MUTEX_WRAPPERS
__d_libdir__libdmtcp_pid_so_SOURCES +=                                 \
	pid/pid_mutexwrappers.cpp
endif

__d_libdir__libdmtcp_pid_so_LDFLAGS = $(dmtcp_ldflags)

#
# SysV Plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_svipc.so
__d_libdir__libdmtcp_svipc_so_SOURCES =                                \
	svipc/sysvipc.cpp                                              \
	svipc/sysvipc.h                                                \
	svipc/sysvipcwrappers.cpp                                      \
	svipc/sysvipcwrappers.h
__d_libdir__libdmtcp_svipc_so_LDFLAGS = $(dmtcp_ldflags)

#
# Timer plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_timer.so
__d_libdir__libdmtcp_timer_so_SOURCES =                                \
	timer/timer_create.cpp                                         \
	timer/timerlist.cpp                                            \
	timer/timerlist.h                                              \
	timer/timerwrappers.cpp                                        \
	timer/timerwrappers.h
__d_libdir__libdmtcp_timer_so_LDFLAGS = $(dmtcp_ldflags)

install-libs: install-libdmtcpPROGRAMS

uninstall-libs: uninstall-libdmtcpPROGRAMS

.PHONY: install-libs uninstall-libs
