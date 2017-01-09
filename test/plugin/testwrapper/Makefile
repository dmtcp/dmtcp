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
override CXXFLAGS += -fPIC -I${DMTCP_INCLUDE} -I${DMTCP_ROOT}/jalib
LINK = ${CC}

DEMO_PORT=7781

default: ${LIBNAME}.so

${DMTCP_ROOT}/test/dmtcp1: ${DMTCP_ROOT}/test
	cd ${DMTCP_ROOT}/test; make dmtcp1

tests/grid1: tests/grid1.c
	cd tests; make

check: ${LIBNAME}.so tests/grid1
	# Kill an old coordinator on this port if present, just in case.
	@ ${DMTCP_ROOT}/bin/dmtcp_command --quit --quiet \
	  --coord-port ${DEMO_PORT} 2>/dev/null || true
	@ ${DMTCP_ROOT}/bin/dmtcp_coordinator --quiet --daemon --port ${DEMO_PORT}
	${DMTCP_ROOT}/bin/dmtcp_launch -j -p ${DEMO_PORT} --with-plugin $$PWD/${LIBNAME}.so  mpirun -n 2 -hosts localhost tests/grid1 2 5 10 &
	@ sleep 3
	# Checkpoint
	@ ${DMTCP_ROOT}/bin/dmtcp_command -p ${DEMO_PORT} -c
	# Kill
	@ ${DMTCP_ROOT}/bin/dmtcp_command -p ${DEMO_PORT} -k
	# Restart in record mode
	DMTCP_MPI_START_RECORD=1 ${DMTCP_ROOT}/bin/dmtcp_restart -j -p ${DEMO_PORT} ckpt*.dmtcp &
	@ sleep 3
	@ ${DMTCP_ROOT}/bin/dmtcp_command -p ${DEMO_PORT} -k
	# Restart in replay mode
	DMTCP_MPI_START_REPLAY=1 ${DMTCP_ROOT}/bin/dmtcp_restart -j -p ${DEMO_PORT} ckpt_*.dmtcp
	@ ${DMTCP_ROOT}/bin/dmtcp_command -p ${DEMO_PORT} -q

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
