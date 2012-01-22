AUTOMAKE_OPTIONS = foreign

DMTCP_SRC_PATH=$(DMTCP_ROOT)/dmtcp/src
JALIB_PATH=$(DMTCP_ROOT)/dmtcp/jalib

#HEADERS
#set the include path found by configure
INCLUDES= $(all_includes) -I$(JALIB_PATH) -I$(DMTCP_SRC_PATH)

pkglib_PROGRAMS = ptracehijack.so

# headers:
nobase_noinst_HEADERS = $(DMTCP_SRC_PATH)/util.h $(DMTCP_SRC_PATH)/dmtcpmodule.h \
			mtcp_ptrace.h ptrace.h ptracewrappers.h \
			$(JALIB_PATH)/jassert.h $(JALIB_PATH)/jalloc.h \
			$(JALIB_PATH)/jalib.h $(JALIB_PATH)/jbuffer.h \
			$(JALIB_PATH)/jconvert.h $(JALIB_PATH)/jfilesystem.h \
			$(JALIB_PATH)/jserialize.h $(JALIB_PATH)/jsocket.h

ptracehijack_so_SOURCES = mtcp_ptrace.c ptrace.cpp ptracewrappers.cpp \
			 ptrace_syscallsreal.c

ptracehijack_so_LDFLAGS = -shared -module

ptracehijack_so_LDADD   = $(DMTCP_SRC_PATH)/dmtcphijack.so -ldl -lpthread

PICFLAGS=-fPIC
AM_CFLAGS = $(PICFLAGS)
AM_CXXFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PICFLAGS)

