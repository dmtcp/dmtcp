# To demonstrate, do:  make check    [Checkpoints every 5 seconds]

# The name will be the same as the current directory name.
NAME=${shell basename $$PWD}

# By default, your resulting library will have this name.
LIBNAME=dmtcp_${NAME}

# As you add new files to your hijack library, add the object file names here.
LIBOBJS = ${NAME}.o

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../../
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib/

CFLAGS += -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -DDMTCP -fPIC -c --std=gnu99 -g -O0
CXXFLAGS += -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -DDMTCP -fPIC -c -g -O0

# enable this for debugging the plugin
# CFLAGS += -DCKPTTIMER_PLUGIN_DEBUG
# CXXFLAGS += -DCKPTTIMER_PLUGIN_DEBUG

DEMO_PORT=7781

default: ${LIBNAME}.so

${DMTCP_ROOT}/test/dmtcp1: ${DMTCP_ROOT}/test
	cd ${DMTCP_ROOT}/test; make dmtcp1
check: ${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1
	# Note that full path of plugin (using $$PWD in this case) is required.
	${DMTCP_ROOT}/bin/dmtcp_launch --port ${DEMO_PORT} -i 5 \
	  --with-plugin $$PWD/${LIBNAME}.so ${DMTCP_ROOT}/test/dmtcp1

# We link the library using C++ for compatibility with the main libdmtcp.so
${LIBNAME}.so: ${LIBOBJS}
	${CXX} -shared -fPIC -o $@ $^ -lrt

.c.o:
	${CC} ${CFLAGS} -o $@ $<
.cpp.o:
	${CXX} ${CXXFLAGS} -o $@ $<

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

clean:
	rm -f ${LIBOBJS} ${LIBNAME}.so

distclean: clean
	rm -f ${LIBNAME}.so *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

dist: distclean
	dir=`basename $$PWD`; cd ..; \
	  tar czvf $$dir.tar.gz --exclude-vcs ./$$dir
	dir=`basename $$PWD`; ls -l ../$$dir.tar.gz

.PHONY: default clean dist distclean
