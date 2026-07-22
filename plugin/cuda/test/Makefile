CC=gcc
CXX=g++

FILES=counter

default: counter

counter: counter.cu
	nvcc -g -O0 -o $@ $^ -cudart=shared

tidy:
	rm -f *~ .*.swp dmtcp_restart_script*.sh ckpt_*.dmtcp

clean: tidy
	rm -f $(FILES)

distclean: clean
	rm -f dmtcp_restart_script*.sh ckpt_*.dmtcp

.PHONY: default tidy clean distclean
