LIBOBJS = infinibandreals.o infinibandwrappers.o ibvctx.o lib/list.o ibv_trampolines.o
TRACEOBJS =  infinibandtrace.o infinibandreals.o
DMTCP_PORT=7783

# This assumes infiniband is a subdirectory of dmtcp/src
ifndef DMTCP_ROOT
	DMTCP_ROOT=../../
endif
DMTCP_SRC=${DMTCP_ROOT}/dmtcp/include
ifndef DMTCP_BIN
  DMTCP_BIN = ${DMTCP_ROOT}/bin
endif
ifndef HOST0
	HOST0 = compute-0-5
endif
ifndef HOST1
	HOST1 = compute-0-0
endif
ifndef HOST2
	HOST2 = compute-0-1
endif

CFLAGS += -I${DMTCP_SRC} -DDMTCP -fPIC -c --std=gnu99 -g
CPPFLAGS += -I${DMTCP_SRC} -DDMTCP -fPIC -c -g

check-compat:
	@ grep "INFINIBAND_SUPPORT='yes'" ${DMTCP_ROOT}/config.log || \
	  (echo && \
	  echo DMTCP not configured with --enable-infiniband-support. && \
	  echo Infiniband plugin cannot work. Execute the following && \
	  echo to reconfigure DMTCP before proceeding: && \
	  echo ./configure --enable-infiniband-support; make clean; make && \
	  false)

default: check-compat libinfiniband.so

trace: check-compat libibtrace.so

libibtrace.so: ${TRACEOBJS}
	${CC} -shared -fPIC -o $@ $^

libinfiniband.so: ${LIBOBJS}
	${CXX} -shared -fPIC -o $@ $^

get_qp_from_pointer.ic: get_XX_from_pointer.ic
	sed -e 's%XX%qp%g' get_XX_from_pointer.ic > get_qp_from_pointer.ic
get_cq_from_pointer.ic: get_XX_from_pointer.ic
	sed -e 's%XX%cq%g' get_XX_from_pointer.ic > get_cq_from_pointer.ic
ibv_wr_ops_send.ic: ibv_wr_ops.ic
	sed -e 's%SENDRECV%send%g' ibv_wr_ops.ic > ibv_wr_ops_send.ic
ibv_wr_ops_recv.ic: ibv_wr_ops.ic
	sed -e 's%SENDRECV%recv%g' ibv_wr_ops.ic > ibv_wr_ops_recv.ic
ibvctx.c: ibv_wr_ops_send.ic ibv_wr_ops_recv.ic get_qp_from_pointer.ic get_cq_from_pointer.ic

.c.o:
	${CC} ${CFLAGS} -o $@ $<
.cpp.o:
	${CXX} ${CPPFLAGS} -o $@ $<

examples/orig:
	cd examples && ${MAKE} default

rdma:
	cd examples/rdma && ${MAKE}

check: examples/orig
	- pkill -9 dmtcp_coord
	${DMTCP_BIN}/dmtcp_coordinator -p ${DMTCP_PORT} --background
	export dir=$$PWD && ssh ${HOST1} "cd $$dir && \
	  ${DMTCP_BIN}/dmtcp_launch -h ${HOST0} -p ${DMTCP_PORT} --with-plugin $$dir/libinfiniband.so ./examples/orig" &
	export dir=$$PWD && ssh ${HOST2} "cd $$dir && \
	  ${DMTCP_BIN}/dmtcp_launch -h ${HOST0} -p ${DMTCP_PORT} --with-plugin $$dir/libinfiniband.so ./examples/orig ${HOST1}" &
	@ sleep 15 && ${DMTCP_BIN}/dmtcp_command -p ${DMTCP_PORT} --checkpoint
	sleep 20 ; ${DMTCP_BIN}/dmtcp_command -p ${DMTCP_PORT} --quit

tidy:
	rm -rf ckpt_*.dmtcp core.* dmtcp_restart_script_*
clean:
	rm -f ${LIBOBJS} libinfiniband.so
	rm -f ${TRACEOBJS} libibtrace.so
	rm -f ibv_wr_ops_send.ic ibv_wr_ops_recv.ic get_qp_from_pointer.ic get_cq_from_pointer.ic
distclean: clean

dist: distclean
	dir=`basename $$PWD`; cd ..; tar czvf $$dir.tar.gz ./$$dir
	dir=`basename $$PWD`; ls -l ../$$dir.tar.gz

.PHONY: default
