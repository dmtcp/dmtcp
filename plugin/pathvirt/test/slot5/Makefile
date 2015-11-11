DMTCP_ROOT = ../../../..
PATHVIRT = $(realpath $(PWD)/..)

pv-test: pv-test.cpp
	g++ -g -O0 -o pv-test $<


check: clean pv-test
	DMTCP_PATH_PREFIX=$(PATHVIRT)/slot5/bin5:$(PATHVIRT)/slot5/doc5:$(PATHVIRT)/slot5/lib5 \
		${DMTCP_ROOT}/bin/dmtcp_launch -i4 --with-plugin ${DMTCP_ROOT}/lib/dmtcp/libdmtcp_pathvirt.so ./pv-test
		# dmtcp_launch -i2 --with-plugin ../dmtcp_pathvirthijack.so /usr/bin/python pv-test.py

vi: pv-test.cpp
	vim pv-test.cpp

gdb: clean pv-test
	DMTCP_PATH_PREFIX=$(PATHVIRT)/slot5/bin5:$(PATHVIRT)/slot5/doc5:$(PATHVIRT)/slot5/lib5 \
		gdb --args ${DMTCP_ROOT}/bin/dmtcp_launch --with-plugin ${DMTCP_ROOT}/lib/dmtcp/libdmtcp_pathvirt.so ./pv-test

check-python:  clean
	DMTCP_PATH_PREFIX=$(PATHVIRT)/slot5/bin5:$(PATHVIRT)/slot5/doc5:$(PATHVIRT)/slot5/lib5 \
		${DMTCP_ROOT}/bin/dmtcp_launch -i4 --with-plugin ${DMTCP_ROOT}/lib/dmtcp/libdmtcp_pathvirt.so /usr/bin/python pv-test.py

restart:
	DMTCP_PATH_PREFIX=$(PATHVIRT)/misc/slot7/bin7:$(PATHVIRT)/misc/slot7/doc7:$(PATHVIRT)/misc/slot7/lib7 \
		./dmtcp_restart_script.sh

# second restart test just to show it works more than once
restart2:
	DMTCP_PATH_PREFIX=$(PATHVIRT)/misc/slot9/bin9:$(PATHVIRT)/misc/slot9/doc9:$(PATHVIRT)/misc/slot9/lib9 \
		./dmtcp_restart_script.sh

clean:
	rm -f pv-test ckpt* dmtcp*
	> ../misc/slot7/bin7/pv-test.txt
	> ../misc/slot7/doc7/pv-test.txt
	> ../misc/slot7/lib7/pv-test.txt
	> ../misc/slot9/bin9/pv-test.txt
	> ../misc/slot9/doc9/pv-test.txt
	> ../misc/slot9/lib9/pv-test.txt
	> bin5/pv-test.txt
	> doc5/pv-test.txt
	> lib5/pv-test.txt
	
test:
	cat bin5/pv-test.txt
	cat doc5/pv-test.txt
	cat lib5/pv-test.txt
	cat ../misc/slot7/bin7/pv-test.txt
	cat ../misc/slot7/doc7/pv-test.txt
	cat ../misc/slot7/lib7/pv-test.txt
	cat ../misc/slot9/bin9/pv-test.txt
	cat ../misc/slot9/doc9/pv-test.txt
	cat ../misc/slot9/lib9/pv-test.txt
