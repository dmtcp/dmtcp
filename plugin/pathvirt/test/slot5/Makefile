DMTCP_ROOT = ../../../..
PATHVIRT = $(realpath $(PWD)/..)

pv-test: pv-test.cpp
	g++ -g -O0 -o pv-test $<

file-test: file-test.c
	gcc -g3 -fPIC -I../../../../include $< -o file-test

file-test2: file-test2.c
	gcc -g3 -fPIC -I../../../../include -I${PATHVIRT}/../ $< -o file-test2

file-test3: file-test3.c
	gcc -g3 -fPIC -I../../../../include -I${PATHVIRT}/../ $< -o file-test3

file-test4: file-test4.c
	gcc -g3 -fPIC -I../../../../include -I${PATHVIRT}/../ $< -o file-test4

check: clean pv-test
	DMTCP_ORIGINAL_PATH_PREFIX=$(PATHVIRT)/slot5/bin5:$(PATHVIRT)/slot5/doc5:$(PATHVIRT)/slot5/lib5 \
		${DMTCP_ROOT}/bin/dmtcp_launch --pathvirt -i4 ./pv-test
		# dmtcp_launch -i2 --pathvirt /usr/bin/python pv-test.py

newtest: clean file-test
	DMTCP_ORIGINAL_PATH_PREFIX=/tmp ${DMTCP_ROOT}/bin/dmtcp_launch --checkpoint-open-files --pathvirt ./file-test
	@sleep 1
	DMTCP_NEW_PATH_PREFIX=$(PATHVIRT) ${DMTCP_ROOT}/bin/dmtcp_restart ckpt_file-test*.dmtcp

newtest2: clean file-test
	${DMTCP_ROOT}/bin/dmtcp_launch --checkpoint-open-files --pathvirt ./file-test
	@sleep 1
	DMTCP_ORIGINAL_PATH_PREFIX=/tmp DMTCP_NEW_PATH_PREFIX=$(PATHVIRT) ${DMTCP_ROOT}/bin/dmtcp_restart ckpt_file-test*.dmtcp

newtest3: clean file-test2
	${DMTCP_ROOT}/bin/dmtcp_launch --checkpoint-open-files --pathvirt ./file-test2
	@sleep 1
	DMTCP_NEW_PATH_PREFIX=$(PATHVIRT) ${DMTCP_ROOT}/bin/dmtcp_restart ckpt_file-test*.dmtcp

newtest4: clean file-test3 file-test4
	DMTCP_ORIGINAL_PATH_PREFIX=/tmp ${DMTCP_ROOT}/bin/dmtcp_launch --checkpoint-open-files --pathvirt ./file-test3 ./file-test4
	DMTCP_NEW_PATH_PREFIX=$(PATHVIRT) ${DMTCP_ROOT}/bin/dmtcp_restart ckpt_file-test*.dmtcp

vi: pv-test.cpp
	vim pv-test.cpp

gdb: clean pv-test
	DMTCP_ORIGINAL_PATH_PREFIX=$(PATHVIRT)/slot5/bin5:$(PATHVIRT)/slot5/doc5:$(PATHVIRT)/slot5/lib5 \
		gdb --args ${DMTCP_ROOT}/bin/dmtcp_launch --pathvirt ./pv-test

check-python:  clean
	DMTCP_ORIGINAL_PATH_PREFIX=$(PATHVIRT)/slot5/bin5:$(PATHVIRT)/slot5/doc5:$(PATHVIRT)/slot5/lib5 \
		${DMTCP_ROOT}/bin/dmtcp_launch -i4 --pathvirt /usr/bin/python pv-test.py

restart:
	DMTCP_NEW_PATH_PREFIX=$(PATHVIRT)/misc/slot7/bin7:$(PATHVIRT)/misc/slot7/doc7:$(PATHVIRT)/misc/slot7/lib7 \
	./dmtcp_restart_script.sh

# second restart test just to show it works more than once
restart2:
	DMTCP_NEW_PATH_PREFIX=$(PATHVIRT)/misc/slot9/bin9:$(PATHVIRT)/misc/slot9/doc9:$(PATHVIRT)/misc/slot9/lib9 \
	./dmtcp_restart_script.sh

tidy:
	rm -f ckpt*.dmtcp* dmtcp_restart_script*.sh

clean: tidy
	rm -rf pv-test file-test file-test2
	rm -f /tmp/msg $(PATHVIRT)/msg $(PATHVIRT)/restart_process
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
