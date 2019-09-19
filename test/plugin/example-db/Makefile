# To demonstrate, do:  make check    [Checkpoints every 5 seconds]

# The name will be the same as the current directory name.
NAME=${shell basename $$PWD}

# By default, your resulting plugin library will have this name.
LIBNAME=libdmtcp_${NAME}

# As you add new files to your plugin library, add the object file names here.
LIBOBJS = ${NAME}.o

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../../..
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include

override CFLAGS += -fPIC -I${DMTCP_INCLUDE}
override CXXFLAGS += -fPIC -I${DMTCP_INCLUDE}
LINK = ${CC}

# if version.h not found:
ifeq (,$(wildcard ${DMTCP_INCLUDE}/dmtcp/version.h))
  override CFLAGS += -DDMTCP_PACKAGE_VERSION='"3.0.0"'
endif

DEMO_PORT=7781

default: ${LIBNAME}.so

# We run two processes, dmtcp1, with keys set to 1 for one, and 2 for the other
${DMTCP_ROOT}/test/dmtcp1: ${DMTCP_ROOT}/test
	cd ${DMTCP_ROOT}/test; make dmtcp1
check: ${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1
	# Kill an old coordinator on this port if present, just in case.
	@ ${DMTCP_ROOT}/bin/dmtcp_command --quit --quiet \
	  --coord-port ${DEMO_PORT} 2>/dev/null || true
	# Begin test
	${DMTCP_ROOT}/bin/dmtcp_coordinator --coord-port ${DEMO_PORT} \
	  --daemon --exit-on-last -i 5
	EXAMPLE_DB_KEY=1 EXAMPLE_DB_KEY_OTHER=2 \
	  ${DMTCP_ROOT}/bin/dmtcp_launch --coord-port ${DEMO_PORT} --join -i 5\
	  --with-plugin $$PWD/${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1 &
	EXAMPLE_DB_KEY=2 EXAMPLE_DB_KEY_OTHER=1 \
	  ${DMTCP_ROOT}/bin/dmtcp_launch --coord-port ${DEMO_PORT} --join -i 5\
	  --with-plugin $$PWD/${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1 &
	sleep 20; ${DMTCP_ROOT}/bin/dmtcp_command --coord-port ${DEMO_PORT} --quit

${LIBNAME}.so: ${LIBOBJS}
	${LINK} -shared -fPIC -o $@ $^

.c.o:
	${CC} ${CFLAGS} -c -o $@ $<
.cpp.o:
	${CXX} ${CXXFLAGS} -c -o $@ $<

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

clean: tidy
	rm -f ${LIBOBJS} ${LIBNAME}.so

distclean: clean
	rm -f ${LIBNAME}.so *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

dist: distclean
	dir=`basename $$PWD`; cd ..; \
	  tar czvf $$dir.tar.gz --exclude-vcs ./$$dir
	dir=`basename $$PWD`; ls -l ../$$dir.tar.gz

.PHONY: default clean dist distclean
