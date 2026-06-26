CC=gcc
CXX=g++

LIBNAME=libdmtcp_cuda
LIBOBJS = cuda-ckpt.o

DMTCP_INCLUDE_FLAGS=-I../../include -I../../jalib -I../../src

# Find CUDA path
NVCC_PATH := $(shell which nvcc 2>/dev/null)
ifdef NVCC_PATH
  CUDA_HOME := $(realpath $(dir $(NVCC_PATH))/..)
endif
CUDA_HOME ?= $(or $(CUDA_PATH),/usr/local/cuda)
CUDA_INCLUDE_FLAGS=-I$(CUDA_HOME)/include

override CFLAGS   += -g3 -O0 -fPIC $(DMTCP_INCLUDE_FLAGS) $(CUDA_INCLUDE_FLAGS)
override CXXFLAGS += -g3 -O0 -fPIC $(DMTCP_INCLUDE_FLAGS) $(CUDA_INCLUDE_FLAGS)

LINK = ${CXX}

default: ${LIBNAME}.so tests

# End-to-end checkpoint/restart test: launch ./test/counter (which increments a
# GPU __device__ counter and prints it once per second), checkpoint it, kill it,
# then restart from the ckpt image. The test PASSES only if the GPU counter
# resumes from its checkpointed value instead of resetting to 0 -- i.e. the first
# value printed after restart (M) is greater than the first value printed by the
# original run (N). Comparing first-values (not max-values) is correct: the
# original may print past the checkpoint before being killed (e.g. ckpt at 3, then
# 4 5, killed), and restart resumes from 4 -- so 4 < 5 is still a success.
# SETTLE multiplies the wait times for slow machines: make check SETTLE=2
SETTLE ?= 1
check: ${LIBNAME}.so tests
	@if ! nvidia-smi -L >/dev/null 2>&1; then \
	  echo "SKIP: no CUDA GPU available"; exit 0; fi
	@../../bin/dmtcp_command --quit 2>/dev/null || true
	@rm -f ckpt_*.dmtcp ckpt_*.dmtcp.temp dmtcp_restart_script*.sh run1.log run2.log
	@echo "Launching ./test/counter" ; \
	../../bin/dmtcp_launch --with-plugin $$PWD/${LIBNAME}.so ./test/counter > run1.log 2>/dev/null & \
	LAUNCH_PID=$$! ; \
	sleep $$((4 * $(SETTLE))) ; \
	echo "Checkpointing" ; \
	../../bin/dmtcp_command --checkpoint ; \
	echo "Waiting for checkpoint image to finalize" ; \
	for i in $$(seq 1 $$((30 * $(SETTLE)))); do \
	  ls ckpt_*.dmtcp >/dev/null 2>&1 && break ; \
	  sleep 1 ; \
	done ; \
	echo "Stopping original process" ; \
	../../bin/dmtcp_command --quit 2>/dev/null || true ; \
	kill -9 $$LAUNCH_PID 2>/dev/null || true ; \
	wait $$LAUNCH_PID 2>/dev/null || true ; \
	echo "Restarting" ; \
	../../bin/dmtcp_restart ckpt_*.dmtcp > run2.log 2>/dev/null & \
	RESTART_PID=$$! ; \
	sleep $$((6 * $(SETTLE))) ; \
	../../bin/dmtcp_command --quit 2>/dev/null || true ; \
	kill -9 $$RESTART_PID 2>/dev/null || true ; \
	wait $$RESTART_PID 2>/dev/null || true ; \
	N=$$(grep '\.\.\.' run1.log | head -1 | grep -oE '[0-9]+' | head -1) ; \
	M=$$(grep '\.\.\.' run2.log | head -1 | grep -oE '[0-9]+' | head -1) ; \
	echo "First value before checkpoint: N=$$N ; first value after restart: M=$$M" ; \
	rm -f ckpt_*.dmtcp ckpt_*.dmtcp.temp dmtcp_restart_script*.sh run1.log run2.log ; \
	if [ -z "$$N" ] || [ -z "$$M" ]; then \
	  echo "FAIL: counter produced no output (N='$$N' M='$$M')"; exit 1; fi ; \
	if [ "$$M" -gt "$$N" ]; then \
	  echo "PASS: CUDA counter resumed after restart ($$M > $$N)"; \
	else \
	  echo "FAIL: CUDA counter did not continue after restart ($$M not > $$N)"; exit 1; fi

${LIBNAME}.so: ${LIBOBJS}
	${LINK} -shared -fPIC -o $@ $^ -lcuda -ldl

.c.o:
	${CC} ${CFLAGS} -c -o $@ $<
.cpp.o:
	${CXX} ${CXXFLAGS} -c -o $@ $<

tests:
	cd test && $(MAKE)

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

clean: tidy
	rm -f ${LIBOBJS} ${LIBNAME}.so
	cd test && $(MAKE) clean

distclean: clean
	rm -f ${LIBNAME}.so *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

dist: distclean
	dir=`basename $$PWD`; cd ..; \
	  tar czvf $$dir.tar.gz --exclude-vcs ./$$dir
	dir=`basename $$PWD`; ls -l ../$$dir.tar.gz

.PHONY: default check tests tidy clean distclean dist
