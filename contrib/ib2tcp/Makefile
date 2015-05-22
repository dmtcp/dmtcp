LIBNAME=libdmtcp_ib2tcp.so
LIBOBJS = ibwrappers.o ib2tcp.o
HEADERS = ib2tcp.h ibwrappers.h

# This assumes infiniband is a subdirectory of dmtcp/src
ifndef DMTCP_ROOT
	DMTCP_ROOT=../..
endif
INCLUDE_FLAGS=-I${DMTCP_ROOT}/include -I${DMTCP_ROOT}/jalib

CFLAGS += ${INCLUDE_FLAGS} -DDMTCP -fPIC -c -g -O0
CPPFLAGS += ${INCLUDE_FLAGS} -DDMTCP -fPIC -c -g -O0

default: check-compat ${LIBNAME}

check-compat:
	@ grep "INFINIBAND_SUPPORT='yes'" ${DMTCP_ROOT}/config.log > /dev/null \
	  || \
	  (echo && \
	  echo DMTCP not configured with --enable-infiniband-support. && \
	  echo Infiniband plugin cannot work. Execute the following && \
	  echo to reconfigure DMTCP before proceeding: && \
	  echo './configure --enable-infiniband-support; make clean; make' && \
	  false)

${LIBNAME}: ${LIBOBJS}
	${CXX} -shared -fPIC -o $@ $^

%.o: %.c ${HEADERS}
	${CC} ${CFLAGS} -o $@ $<

%.o: %.cpp ${HEADERS}
	${CXX} ${CPPFLAGS} -o $@ $<

tidy:
	rm -rf ckpt_*.dmtcp core.* dmtcp_restart_script*

clean:
	rm -f ${LIBOBJS} ${LIBNAME}

distclean: clean

dist: distclean
	dir=`basename $$PWD`; cd ..; tar czvf $$dir.tar.gz ./$$dir
	dir=`basename $$PWD`; ls -l ../$$dir.tar.gz

.PHONY: default distclean clean tidy
