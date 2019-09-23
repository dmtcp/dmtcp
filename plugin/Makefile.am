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
# Batch queue plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_batch-queue.so
__d_libdir__libdmtcp_batch_queue_so_SOURCES =                          \
	batch-queue/rm_main.cpp                                        \
	batch-queue/rm_main.h                                          \
	batch-queue/rm_torque.cpp                                      \
	batch-queue/rm_torque.h                                        \
	batch-queue/rm_slurm.cpp                                       \
	batch-queue/rm_slurm.h                                         \
	batch-queue/rm_utils.cpp                                       \
	batch-queue/rm_utils.h                                         \
	batch-queue/rm_pmi.cpp                                         \
	batch-queue/rm_pmi.h                                           \
	batch-queue/slurm_helper.cpp
__d_libdir__libdmtcp_batch_queue_so_LDFLAGS = $(dmtcp_ldflags)
__d_libdir__libdmtcp_batch_queue_so_LDADD = $(LDADD)

bin_PROGRAMS += $(d_bindir)/dmtcp_discover_rm
__d_bindir__dmtcp_discover_rm_SOURCES =                                \
	batch-queue/discover_dmtcpinput.cpp                            \
	batch-queue/discover_resources.cpp                             \
	batch-queue/discover_slurm.cpp                                 \
	batch-queue/discover_torque.cpp                                \
	batch-queue/dmtcp_discover_rm.cpp

bin_PROGRAMS += $(d_bindir)/dmtcp_srun_helper
__d_bindir__dmtcp_srun_helper_SOURCES =                                \
	batch-queue/rm_main.h                                          \
	batch-queue/rm_slurm.h                                         \
	batch-queue/slurm_helper.cpp                                   \
	batch-queue/slurm_helper.h                                     \
	batch-queue/dmtcp_srun_helper.cpp

dist_bin_SCRIPTS += batch-queue/dmtcp_rm_loclaunch

build_rm_loclaunch:
	cp batch-queue/dmtcp_rm_loclaunch $(top_builddir)/bin/

clean_rm_loclaunch:
	rm $(top_builddir)/bin/dmtcp_rm_loclaunch


#
# Modify env plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_modify-env.so
__d_libdir__libdmtcp_modify_env_so_SOURCES =                           \
	modify-env/modify-env.c modify-env/warning.cpp
__d_libdir__libdmtcp_modify_env_so_LDFLAGS = $(dmtcp_ldflags)
__d_libdir__libdmtcp_modify_env_so_LDADD = $(LDADD)

#
# Pathvirt plugin
#
libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_pathvirt.so
__d_libdir__libdmtcp_pathvirt_so_SOURCES =                           \
	pathvirt/pathvirt.cpp
__d_libdir__libdmtcp_pathvirt_so_LDFLAGS = $(dmtcp_ldflags)
__d_libdir__libdmtcp_pathvirt_so_LDADD = $(LDADD)

# TODO(kapil): Re-enable Ptrace plugin after reworking the logic for new plugin
# API.
#
# # Ptrace plugin
# #
# 
# # FIXME:  ptrace plugin not yet working for aarch64
# if !AARCH64_HOST
# libdmtcp_PROGRAMS += $(d_libdir)/libdmtcp_ptrace.so
# __d_libdir__libdmtcp_ptrace_so_SOURCES =                               \
# 	ptrace/ptrace.cpp                                              \
# 	ptrace/ptrace.h                                                \
# 	ptrace/ptraceinfo.cpp                                          \
# 	ptrace/ptraceinfo.h                                            \
# 	ptrace/ptracewrappers.cpp
# __d_libdir__libdmtcp_ptrace_so_LDFLAGS = $(dmtcp_ldflags)
# __d_libdir__libdmtcp_ptrace_so_LDADD = $(LDADD)
# endif

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

all-local: build_rm_loclaunch

clean-local: clean_rm_loclaunch

.PHONY: install-libs uninstall-libs
