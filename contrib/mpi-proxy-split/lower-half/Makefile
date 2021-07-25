# MPI compiler according to the platform
PLATFORM=${shell echo $$HOST}
include ../Makefile_config

PROXY_BIN = lh_proxy
PROXY_OBJS = lh_proxy.o
LIBPROXY = libproxy
LIBPROXY_OBJS = libproxy.o procmapsutils.o sbrk.o mmap64.o munmap.o \
		shmat.o shmget.o

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../../..
endif

ifndef PLUGIN_ROOT
  PLUGIN_ROOT=..
endif

DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib
PLUGIN_INCLUDE=${PLUGIN_ROOT}

override CFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -std=gnu11
override CXXFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} \
                     -I${DMTCP_ROOT}/src -std=c++11

PROXY_LD_FLAGS=-static -Wl,-Ttext-segment -Wl,0xE000000 -Wl,--wrap -Wl,__munmap -Wl,--wrap -Wl,shmat -Wl,--wrap -Wl,shmget

default: ${PROXY_BIN}

ifeq ($(findstring cori,$(PLATFORM)),cori) 
  STATIC_GETHOSTBYNAME=
else
  STATIC_GETHOSTBYNAME=gethostbyname-static/gethostbyname_static.o
${STATIC_GETHOSTBYNAME}: gethostbyname-static/gethostbyname_static.c \
                         gethostbyname-static/gethostbyname_proxy.c
	cd gethostbyname-static && make gethostbyname_static.o
	cd gethostbyname-static && make gethostbyname_proxy
	cp gethostbyname-static/gethostbyname_proxy -f ${DMTCP_ROOT}/bin/
endif

.c.o:
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

# CAVEATS:
# If the Makefile complains about a missing libxml2.a, it can be because of
#   a bad MPICH configuration.  If MPICH discovers libxml2.so, then it
#   configures itself to require libxml2, even though '-static libxml2.a' fails.
#   The solution is to install the distro package libxml2-dev .
# Ubuntu mpich package adds '-lcr' flag that's incompatible with 'mpicc -static'
# MPICH-3.3 'mpicc -static' adds spurious duplicate -lunwind; we remove that.
# *** NOTE:  This will fail unless you have configured MPICH with:
# ***        ./configure --disable-xml2 --disable-libxml2 ...
# ***        or unless you installed the necessary libaries.
# ***        See ./README.mpich-static here, for how to install the libaries
${PROXY_BIN}: ${PROXY_OBJS} ${LIBPROXY}.a ${STATIC_GETHOSTBYNAME}
	if ${MPICC} -v 2>&1 | grep -q 'MPICH version'; then \
	  rm -f tmp.sh; \
	  ${MPICC} -show ${PROXY_LD_FLAGS} -o $@ -Wl,-start-group \
	    $^ ${MPI_LD_FLAG} -lrt -lpthread -lc -Wl,-end-group | \
	    sed -e 's^-lunwind ^ ^'> tmp.sh; \
	  sh tmp.sh; \
	  rm -f tmp.sh; \
	else \
	  ${MPICC} ${PROXY_LD_FLAGS} -o $@ -Wl,-start-group \
            $^ ${MPI_LD_FLAG} -lrt -lpthread -lc -Wl,-end-group; \
	fi

${LIBPROXY}.a: ${LIBPROXY_OBJS}
	ar cr $@ $^

install: ${PROXY_BIN}
	cp -f $< ${DMTCP_ROOT}/bin/

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*

clean: tidy
	rm -f ${PROXY_BIN} ${LIBPROXY}.a ${PROXY_OBJS} ${LIBPROXY_OBJS}
	if test -d gethostbyname-static; then \
	  cd gethostbyname-static && make clean; \
	fi

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean install
