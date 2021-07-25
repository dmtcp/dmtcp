PLATFORM=${shell echo $$HOST}
ifeq ($(findstring cori,$(PLATFORM)),cori)
  MPICC = cc
  MPICXX = CC
  CFLAGS?= -g3 -O0 -std=c99 -fPIC
  CXXFLAGS?= -g3 -O0 -std=c99 -fPIC
  LDFLAGS?= -dynamic
else
  MPICC = mpicc
  MPICXX = mpic++
  CFLAGS?= -g3 -O0 -std=c99 -fPIC
  CXXFLAGS?= -g3 -O0 -std=c99 -fPIC
  LDFLAGS?=
endif

ifndef DMTCP_ROOT
  DMTCP_ROOT=../../../
  endif
DMTCP_INCLUDE=${DMTCP_ROOT}/include
LIBNAME=libdmtcp_mpi-proxy
DEMO_PORT=7787

# FILES=mpi_hello_world Abort_test Allreduce_test Alltoall_test \
#       Alltoallv_test Barrier_test Bcast_test Comm_split_test Reduce_test \
#       send_recv send_recv_many ping_pong Comm_dup_test Sendrecv_test \
#       Waitall_test Allgather_test Group_size_rank Type_commit_contiguous
FILES=ping_pong

OBJS=$(addsuffix .o, ${FILES})

TESTS_DUMMY=$(addsuffix .dummy.exe, ${FILES})
TESTS=$(addsuffix .exe, ${FILES})
LDFLAGS_DUMMY=-L../mpi-wrappers -lmpidummy

default: ${TESTS} ${TESTS_DUMMY}

integrated_dmtcp_test: integrated_dmtcp_test.c
	${MPICC}  -I${DMTCP_INCLUDE} -I${JALIB_INCLUDE} -DDMTCP -fPIC -g3 -O0 -o $@ $<

check: tidy integrated_dmtcp_test
	${DMTCP_ROOT}/bin/dmtcp_launch --port ${DEMO_PORT}\
    --with-plugin $$PWD/../${LIBNAME}.so ./integrated_dmtcp_test && \
      echo "Restarting..." && \
        ${DMTCP_ROOT}/bin/dmtcp_restart --port ${DEMO_PORT} \
          ckpt*.dmtcp

%.o: %.c
	 $(MPICC)  -fPIC -g3 -O0 -c -o $@ $< $(CFLAGS)

%.exe: %.o
	 $(MPICC)  -o $@ $< ${LDFLAGS}

%.dummy.exe: %.o
	 $(MPICC)  -o $@ $< ${LDFLAGS} ${LDFLAGS_DUMMY}

%.exe: %.cpp
	$(MPICXX)  -fPIC -g3 -O0 -o $@ $< $(CXXFLAGS)

tidy:
	rm -f ckpt_*.dmtcp dmtcp_restart_script* \
	dmtcp-shared-memory.* dmtcp-test-typescript.tmp core*
	rm -rf ckpt_*

clean: tidy
	rm -f $(TESTS) ${OBJS} ${TESTS_DUMMY} *.pyc *.so
