# MPI compiler according to the platform
PLATFORM=${shell echo $$HOST}
include ../Makefile_config

WRAPPERS_SRCDIR = ${PWD}

# By default, your resulting plugin library will have this name.
LIBNAME=libmpiwrappers

LOWER_HALF_SRCDIR=../lower-half
PLUGIN_HALF_SRCDIR=..

# As you add new files to your plugin library, add the object file names here.
LIBWRAPPER_OBJS = mpi_p2p_wrappers.o mpi_collective_wrappers.o \
                  mpi_request_wrappers.o mpi_type_wrappers.o \
                  mpi_comm_wrappers.o mpi_group_wrappers.o \
                  mpi_cart_wrappers.o mpi_op_wrappers.o \
                  mpi_unimplemented_wrappers.o mpi_fortran_wrappers.o \
                  mpi_wrappers.o get_fortran_constants.o fortran_constants.o

# Modify if your DMTCP_ROOT is located elsewhere.
ifndef DMTCP_ROOT
  DMTCP_ROOT=../../..
endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
JALIB_INCLUDE=${DMTCP_ROOT}/jalib

override CFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${PLUGIN_HALF_SRCDIR} \
	                 -I${LOWER_HALF_SRCDIR} -std=gnu11
override CXXFLAGS += -g3 -O0 -fPIC -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} \
                     -I${PLUGIN_HALF_SRCDIR} -I${DMTCP_ROOT}/src \
                     -I${LOWER_HALF_SRCDIR} -std=c++11

# We don't build libmpiwrappers.a.  Instead, we use *.o to build libmana.so.
default: ${LIBNAME}.a libmpidummy.so

${LIBNAME}.a: ${LIBWRAPPER_OBJS}
	ar cr $@ $^

mpi_unimplemented_wrappers.cpp: generate-mpi-unimplemented-wrappers.py \
	mpi_unimplemented_wrappers.txt
	python $^ > $@

.c.o:
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

.cpp.o: ../virtual-ids.h
	${MPICXX} ${CXXFLAGS} -g3 -O0 -c -o $@ $<

fortran_constants.o: fortran_constants.f90
	${MPIFORTRAN} -fPIC -g3 -O0 -c -o $@ $< 

mpi_collective_wrappers.o: mpi_collective_wrappers.cpp ../virtual-ids.h
	${MPICXX} ${CXXFLAGS} -Wall -g3 -O0 -DDEBUG -fPIC -I../../../include \
	-I../../../jalib -I.. -I../../../src -I../lower-half -std=c++11 -c $<

# Some of these functions are also collective; e.g., MPI_Win_allocate_shared
mpi_win_wrappers.o: mpi_win_wrappers.cpp ../virtual-ids.h
	${MPICXX} ${CXXFLAGS} -Wall -g3 -O0 -DDEBUG -fPIC -I../../../include \
	-I../../../jalib -I.. -I../../../src -I../lower-half -std=c++11 -c $<

# We need to write to mpi_dummy_wrappers.c.tmp.$$ first, in case of 'make -j'.
# The parallel make would generate mpi_dummy_wrappers.c 3 times, and overwrite.
mpi_dummy_wrappers.c: generate-mpi-dummy-wrappers.py mpi_dummy_wrappers.txt
	rm -f $@
	tmp=$@.tmp.$$$$ ; \
	printf "%s\n\n" \
	 "// *** THIS FILE IS AUTO-GENERATED! DO 'make' TO UPDATE. ***" >$$tmp;\
	python $^ >> $$tmp && mv -f $$tmp $@ || (rm -f $$tmp && false)

mpi_fortran_wrappers.cpp: generate-mpi-fortran-wrappers.py \
                          mpi_fortran_wrappers.txt
	rm -f $@
	tmp=$@.tmp.$$$$ ; \
	printf "%s\n\n" \
	 "// *** THIS FILE IS AUTO-GENERATED! DO 'make' TO UPDATE. ***" >$$tmp;\
	python $^ >> $$tmp && mv -f $$tmp $@ || (rm -f $$tmp && false)

mpi_dummy_wrappers.o: mpi_dummy_wrappers.c
	${MPICC} ${CFLAGS} -g3 -O0 -c -o $@ $<

libmpidummy.so: mpi_dummy_wrappers.o
	${CC} -fPIC -g3 -O0 $< -shared -o $@

libmpich_intel.so.3.0.1: libmpidummy.so
	ln -sf $< $@

libmpich_intel.so.3: libmpich_intel.so.3.0.1
	ln -sf $< $@

libpmi.so.0.5.0: libmpidummy.so
	ln -sf $< $@

libpmi.so.0: libpmi.so.0.5.0
	ln -sf $< $@

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp
	rm -rf ckpt_rank_*

install: libmpidummy.so
	cp -f libmpidummy.so ${DMTCP_ROOT}/lib/dmtcp/
	cd ${DMTCP_ROOT}/lib/dmtcp && \
		ln -sf libmpidummy.so libmpich_intel.so.3.0.1 && \
		ln -sf libmpich_intel.so.3.0.1 libmpich_intel.so.3
	cd ${DMTCP_ROOT}/lib/dmtcp && \
		ln -sf libmpidummy.so libpmi.so.0.5.0 && \
		ln -sf libpmi.so.0.5.0 libpmi.so.0

clean: tidy
	rm -f ${LIBWRAPPER_OBJS}
	rm -f ${LIBNAME}.a libmpidummy.so
	rm -f ${DMTCP_ROOT}/lib/dmtcp/libmpidummy.so
	rm -f mpi_dummy_wrappers.c
	rm -f mpi_unimplemented_wrappers.cpp
	rm -f mpi_fortran_wrappers.cpp
	rm -f libmpich_intel.so.3.0.1 libmpich_intel.so.3
	rm -f libpmi.so.3.0.1 libpmi.so.0
	cd ${DMTCP_ROOT}/lib/dmtcp && rm -f libmpich_intel.so.* libpmi.so.*

distclean: clean

dist: distclean
	dir=`basename $$PWD` && cd .. && tar czvf $$dir.tgz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tgz

.PHONY: default clean dist distclean tidy install
