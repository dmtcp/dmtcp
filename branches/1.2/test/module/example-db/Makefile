# To demonstrate, do:  make check    [Checkpoints every 5 seconds]

# The name will be the same as the current directory name.
NAME=${shell basename $$PWD}

# By default, your resulting library will have this name.
LIBNAME=dmtcp_${NAME}hijack

# As you add new files to your hijack library, add the object file names here.
LIBOBJS = ${NAME}.o

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../../..
endif
DMTCP_SRC=${DMTCP_ROOT}/dmtcp/src

CFLAGS += -I${DMTCP_SRC} -DDMTCP -fPIC -c --std=gnu99 -g
CXXFLAGS += -I${DMTCP_SRC} -DDMTCP -fPIC -c -g

DEMO_PORT=7781

default: ${LIBNAME}.so

# We run two processes, dmtcp1, with keys set to 1 for one, and 2 for the other
${DMTCP_ROOT}/test/dmtcp1: ${DMTCP_ROOT}/test
	cd ${DMTCP_ROOT}/test; make dmtcp1
check: ${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1
	${DMTCP_ROOT}/bin/dmtcp_coordinator --port ${DEMO_PORT} \
	  --background --exit-on-last -i 5
	EXAMPLE_DB_KEY=1 EXAMPLE_DB_KEY_OTHER=2 \
	  ${DMTCP_ROOT}/bin/dmtcp_checkpoint --port ${DEMO_PORT} --join -i 5 \
	  --with-module $$PWD/${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1 &
	EXAMPLE_DB_KEY=2 EXAMPLE_DB_KEY_OTHER=1 \
	  ${DMTCP_ROOT}/bin/dmtcp_checkpoint --port ${DEMO_PORT} --join -i 5 \
	  --with-module $$PWD/${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1 &
	sleep 20; ${DMTCP_ROOT}/bin/dmtcp_command --port ${DEMO_PORT} --quit

# We link the library using C++ for compatibility with the main dmtcphijack.so
${LIBNAME}.so: ${LIBOBJS}
	${CXX} -shared -fPIC -o $@ $^

.c.o:
	${CC} ${CFLAGS} -o $@ $<
.cpp.o:
	${CXX} ${CXXFLAGS} -o $@ $<

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

clean:
	rm -f ${LIBOBJS}

distclean: clean
	rm -f ${LIBNAME}.so *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

dist: distclean
	dir=`basename $$PWD`; cd ..; \
	  tar czvf $$dir.tar.gz --exclude-vcs ./$$dir
	dir=`basename $$PWD`; ls -l ../$$dir.tar.gz

.PHONY: default clean dist distclean
