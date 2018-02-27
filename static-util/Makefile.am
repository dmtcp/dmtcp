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
# TODO(kapil): Add 'subdir-objects after automake 1.16 has been released.
AUTOMAKE_OPTIONS = foreign

dmtcpincludedir=$(top_srcdir)/include

PICFLAGS=
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
AM_CPPFLAGS=

# targets:
#bin_SCRIPTS = $(d_bindir)/dmtcp_wrapper_symbols $(d_bindir)/dmtcp_static_gen $(d_bindir)/dmtcp_lib_symbols
#bin_SCRIPTS = dmtcp_wrapper_symbols dmtcp_static_gen dmtcp_lib_symbols
bin_PROGRAMS = $(d_bindir)/dmtcp_replace_symbols_elf

# headers:
nobase_noinst_HEADERS = util.h

__d_bindir__dmtcp_replace_symbols_elf_SOURCES = replace_symbols_elf.c util.c
__d_bindir__dmtcp_replace_symbols_elf_LDADD =
#__d_bindir__dmtcp_wrapper_symbols_SOURCES = dmtcp_wrapper_symbols
#__d_bindir__dmtcp_static_gen_SOURCES = dmtcp_static_gen
#__d_bindir__dmtcp_lib_symbols_SOURCES = dmtcp_lib_symbols
