# To demonstrate, do:  make check    [Checkpoints every 5 seconds]

# The name will be the same as the current directory name.
NAME=${shell basename $$PWD}

# By default, your resulting library will have this name.
LIBNAME=dmtcp_${NAME}hijack

# As you add new files to your hijack library, add the object file names here.
LIBOBJS = ${NAME}.o

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../..
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/dmtcp/include

CFLAGS += -I${DMTCP_INCLUDE} -DDMTCP -fPIC -c --std=gnu99 -g
CXXFLAGS += -I${DMTCP_INCLUDE} -DDMTCP -fPIC -c -g

DEMO_PORT=7781

default: ${LIBNAME}.so

${DMTCP_ROOT}/test/dmtcp1: ${DMTCP_ROOT}/test
	cd ${DMTCP_ROOT}/test; make dmtcp1

# We link the library using C++ for compatibility with the main libdmtcp.so
${LIBNAME}.so: ${LIBOBJS}
	${CXX} -shared -fPIC -o $@ $^

.c.o:
	${CC} ${CFLAGS} -o $@ $<
.cpp.o:
	${CXX} ${CXXFLAGS} -o $@ $<

# -fPIC and -ldl needed for test to recognize presence of DMTCP
test: test.c
	${CC} -fPIC -I${DMTCP_INCLUDE} -o $@ $< -ldl

check: ${LIBNAME}.so test
	${DMTCP_ROOT}/bin/dmtcp_launch --quiet --port ${DEMO_PORT} \
	  --with-plugin $$PWD/${LIBNAME}.so ./test
	./dmtcp_restart_script.sh

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

clean:
	rm -f ${LIBOBJS} ${LIBNAME}.so test

distclean: clean
	rm -f ${LIBNAME}.so *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

dist: distclean
	dir=`basename $$PWD`; cd ..; \
	  tar czvf $$dir.tar.gz --exclude-vcs ./$$dir
	dir=`basename $$PWD`; ls -l ../$$dir.tar.gz

.PHONY: default clean dist distclean
