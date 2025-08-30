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
libdmtcp_PROGRAMS =
dist_bin_SCRIPTS =
bin_SCRIPTS =
CLEANFILES =

#
# Modify env plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_modify-env.so
__d_libdir__libdmtcp_modify_env_so_SOURCES =                           \
	modify-env/modify-env.c modify-env/warning.cpp
__d_libdir__libdmtcp_modify_env_so_LDFLAGS = $(dmtcp_ldflags)
__d_libdir__libdmtcp_modify_env_so_LDADD = $(LDADD)

#
# Unique ckpt plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_unique-ckpt.so
__d_libdir__libdmtcp_unique_ckpt_so_SOURCES =                          \
	unique-ckpt/unique-ckpt.cpp
__d_libdir__libdmtcp_unique_ckpt_so_LDFLAGS = $(dmtcp_ldflags)
__d_libdir__libdmtcp_unique_ckpt_so_LDADD = $(LDADD)

install-libs: install-libdmtcpPROGRAMS

uninstall-libs: uninstall-libdmtcpPROGRAMS

.PHONY: install-libs uninstall-libs
