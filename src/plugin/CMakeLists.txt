
########### install files ###############




#original Makefile.am contents follow:

## not a GNU package. You can remove this line, if
## have all needed files, that a GNU package needs
#AUTOMAKE_OPTIONS = foreign
#
#PICFLAGS=-fPIC
#AM_CFLAGS = $(PICFLAGS)
#AM_CXXFLAGS = $(PICFLAGS)
#AM_LDFLAGS = $(PICFLAGS)
#
##HEADERS
##set the include path found by configure
#AM_CPPFLAGS  = -I$(top_srcdir)/include
#AM_CPPFLAGS += -I$(top_srcdir)/jalib
#
#dmtcp_ldflags = -shared -Xlinker -znow -Wl,--unresolved-symbols=ignore-all
#LDADD = -ldl -lpthread
#
#libdmtcpdir = $(pkglibdir)
#
#bin_PROGRAMS =
#bin_SCRIPTS =
#CLEANFILES =
#dist_bin_SCRIPTS =
#libdmtcp_PROGRAMS =
#noinst_LIBRARIES =
#
##
## Alloc plugin
##
#libdmtcp_PROGRAMS += libdmtcp_alloc.so
#libdmtcp_alloc_so_SOURCES =                                \
#	alloc/alloc.h                                                  \
#	alloc/mallocwrappers.cpp                                       \
#	alloc/mmapwrappers.cpp
#libdmtcp_alloc_so_LDFLAGS = $(dmtcp_ldflags)
#libdmtcp_alloc_so_LDADD = $(LDADD)
#
##
## Dl plugin
##
#libdmtcp_PROGRAMS += libdmtcp_dl.so
#libdmtcp_dl_so_SOURCES =                                   \
#	dl/dlwrappers.cpp
#libdmtcp_dl_so_LDFLAGS = $(dmtcp_ldflags)
#libdmtcp_dl_so_LDADD = $(LDADD)
#
##
## IPC plugin
## TODO(kapil): Split SSH plugin into its own.
##
#noinst_LIBRARIES += libssh.a
#libssh_a_SOURCES =                                                     \
#	ipc/ssh/util_ssh.cpp                                           \
#	ipc/ssh/util_ssh.h
#libssh_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
#libssh_a_SHORTNAME = i
#
#libdmtcp_PROGRAMS += libdmtcp_ipc.so
#libdmtcp_ipc_so_SOURCES =                                  \
#	ipc/connection.cpp                                             \
#	ipc/connection.h                                               \
#	ipc/connectionidentifier.cpp                                   \
#	ipc/connectionidentifier.h                                     \
#	ipc/connectionlist.cpp                                         \
#	ipc/connectionlist.h                                           \
#	ipc/ipc.cpp                                                    \
#	ipc/ipc.h                                                      \
#	ipc/event/eventconnection.cpp                                  \
#	ipc/event/eventconnection.h                                    \
#	ipc/event/eventconnlist.cpp                                    \
#	ipc/event/eventconnlist.h                                      \
#	ipc/event/eventwrappers.cpp                                    \
#	ipc/event/eventwrappers.h                                      \
#	ipc/event/util_descriptor.cpp                                  \
#	ipc/event/util_descriptor.h                                    \
#	ipc/file/fileconnection.cpp                                    \
#	ipc/file/fileconnection.h                                      \
#	ipc/file/fileconnlist.cpp                                      \
#	ipc/file/fileconnlist.h                                        \
#	ipc/file/filewrappers.cpp                                      \
#	ipc/file/filewrappers.h                                        \
#	ipc/file/posixipcwrappers.cpp                                  \
#	ipc/socket/connectionrewirer.cpp                               \
#	ipc/socket/connectionrewirer.h                                 \
#	ipc/socket/kernelbufferdrainer.cpp                             \
#	ipc/socket/kernelbufferdrainer.h                               \
#	ipc/socket/socketconnection.cpp                                \
#	ipc/socket/socketconnection.h                                  \
#	ipc/socket/socketconnlist.cpp                                  \
#	ipc/socket/socketconnlist.h                                    \
#	ipc/socket/socketwrappers.cpp                                  \
#	ipc/socket/socketwrappers.h                                    \
#	ipc/ssh/ssh.cpp                                                \
#	ipc/ssh/sshdrainer.cpp                                         \
#	ipc/ssh/sshdrainer.h                                           \
#	ipc/ssh/ssh.h
#libdmtcp_ipc_so_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
#libdmtcp_ipc_so_LDFLAGS = $(dmtcp_ldflags)
#libdmtcp_ipc_so_LDADD = $(LDADD) -lrt
#libdmtcp_ipc_so_SHORTNAME = i
#
#bin_PROGRAMS += dmtcp_ssh
#dmtcp_ssh_SOURCES =                                        \
#	ipc/ssh/dmtcp_ssh.cpp
#dmtcp_ssh_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
#dmtcp_ssh_LDADD = libssh.a $(LDADD)
#dmtcp_ssh_SHORTNAME = i
#
#bin_PROGRAMS += dmtcp_sshd
#dmtcp_sshd_SOURCES =                                       \
#	ipc/ssh/dmtcp_sshd.cpp
#dmtcp_sshd_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
#dmtcp_sshd_LDADD = libssh.a $(LDADD)
#dmtcp_sshd_SHORTNAME = i
#
##
## Pid plugin
##
#libdmtcp_PROGRAMS += libdmtcp_pid.so
#libdmtcp_pid_so_SOURCES =                                  \
#	pid/pid.cpp                                                    \
#	pid/pid_filewrappers.cpp                                       \
#	pid/pid.h                                                      \
#	pid/pid_miscwrappers.cpp                                       \
#	pid/pid_syscallsreal.c                                         \
#	pid/pidwrappers.cpp                                            \
#	pid/pidwrappers.h                                              \
#	pid/virtualpidtable.cpp                                        \
#	pid/virtualpidtable.h
#libdmtcp_pid_so_LDFLAGS = $(dmtcp_ldflags)
#
##
## SysV Plugin
##
#libdmtcp_PROGRAMS += libdmtcp_svipc.so
#libdmtcp_svipc_so_SOURCES =                                \
#	svipc/sysvipc.cpp                                              \
#	svipc/sysvipc.h                                                \
#	svipc/sysvipcwrappers.cpp                                      \
#	svipc/sysvipcwrappers.h
#libdmtcp_svipc_so_LDFLAGS = $(dmtcp_ldflags)
#
##
## Timer plugin
##
#libdmtcp_PROGRAMS += libdmtcp_timer.so
#libdmtcp_timer_so_SOURCES =                                \
#	timer/timer_create.cpp                                         \
#	timer/timerlist.cpp                                            \
#	timer/timerlist.h                                              \
#	timer/timerwrappers.cpp                                        \
#	timer/timerwrappers.h
#libdmtcp_timer_so_LDFLAGS = $(dmtcp_ldflags)
