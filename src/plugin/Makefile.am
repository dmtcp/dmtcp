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
# IPC plugin
# TODO(kapil): Split SSH plugin into its own.
#
noinst_LIBRARIES += libssh.a
libssh_a_SOURCES =                                                     \
	ipc/ssh/util_ssh.cpp                                           \
	ipc/ssh/util_ssh.h
libssh_a_CPPFLAGS = $(AM_CPPFLAGS) -I$(srcdir)/ipc
libssh_a_SHORTNAME = i

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

install-libs: install-libdmtcpPROGRAMS

uninstall-libs: uninstall-libdmtcpPROGRAMS

.PHONY: install-libs uninstall-libs
