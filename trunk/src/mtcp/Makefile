ARM_EXTRAS = libc-do-syscall-arm.S

../../bin/mtcp_restart: mtcp_restart.o
	${CC} -fPIC -g -O2 -nodefaultlibs ${ARM_EXTRAS} -o $@ $<

mtcp_restart.o: mtcp_restart.c mtcp_util.ic \
	        mtcp_sys.h mtcp_util.h \
	        ../membarrier.h ../../include/procmapsarea.h
	${CC} -I. -I.. -I../../include -DPIC -fPIC -g -O0 -fno-stack-protector \
	    -c $<

# The stupid 'automake' can't be told to just call "make" without all.
# Put this in, to keep it happy.
all: ../../bin/mtcp_restart
	echo "'automake' just called 'all'" in mtcp subdirectory

clean:
	rm -f *.o
