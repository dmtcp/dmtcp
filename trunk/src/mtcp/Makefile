ARM_EXTRAS = libc-do-syscall-arm.S

../../bin/mtcp_restart: mtcp_restart.o
	${CC} -fPIC -g -O2 -nodefaultlibs ${ARM_EXTRAS} -o $@ $<

mtcp_restart.o: mtcp_restart.c mtcp_util.ic \
	        mtcp_sys.h mtcp_util.h \
	        ../membarrier.h ../../include/procmapsarea.h
	${CC} ${MTCP_CFLAGS} -I. -I.. -I../../include \
	    -DPIC -fPIC -g -O0 -fno-stack-protector -c $<

# The stupid 'automake' can't be told to just call "make" without all.
# Put this in, to keep it happy.
all: ../../bin/mtcp_restart
	echo "'automake' just called 'all'" in mtcp subdirectory

gdb: build_mtcp_restart_debug gdb_run
build_mtcp_restart_debug:
	rm -f mtcp_restart.o
	make MTCP_CFLAGS=-DDEBUG mtcp_restart.o
gdb_run: ../../bin/mtcp_restart ckpt_dmtcp1_test.dmtcp
	offset=`./text_offset.sh ../../bin/mtcp_restart` ; \
	  gdb --args ../../bin/mtcp_restart \
	    --use-gdb --text-offset $$offset ckpt_dmtcp1_test.dmtcp
ckpt_dmtcp1_test.dmtcp: ../../test/dmtcp1
	../../bin/dmtcp_launch -i3 ../../test/dmtcp1 &
	sleep 5 && ../../bin/dmtcp_command --quit
	rm -f `ls -t dmtcp_restart_script*.sh`
	mv `ls -t ckpt_dmtcp1_*.dmtcp | head -1` ckpt_dmtcp1_test.dmtcp.gz
	gunzip ckpt_dmtcp1_test.dmtcp.gz


clean:
	rm -f *.o

install:

uninstall:

distclean:
