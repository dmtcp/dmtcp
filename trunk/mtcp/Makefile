#+++2006-05-03
##    Copyright (C) 2006  Mike Rieker, Beverly, MA USA
##    Additions by Gene Cooperman to work with x86-64 architecture
##    EXPECT it to FAIL when someone's HeALTh or PROpeRTy is at RISk
##
##    This program is free software; you can redistribute it and/or modify
##    it under the terms of the GNU General Public License as published by
##    the Free Software Foundation; version 2 of the License.
##
##    This program is distributed in the hope that it will be useful,
##    but WITHOUT ANY WARRANTY; without even the implied warranty of
##    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
##    GNU General Public License for more details.
##
##    You should have received a copy of the GNU General Public License
##    along with this program; if not, write to the Free Software
##    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
##---2006-05-03

##################################################################################################################################
##																##
##  Compile and link the shareable, restore utility and test programs								##
##																##
##  Versions tested with:													##
##    binutils-2.15.92, 2.16.1, 2.18.1, 2.19.1											##
##    glibc-2.3.4, 2.3.6, 2.7.12, 2.7, 2.9											##
##    linux-2.6.9-34, 2.6.9-78, 2.6.10, 2.6.14, 2.6.25, 2.6.26									##
##    gcc-3.4.4, 4.0.2, 4.1.3, 4.3.2												##
##																##
##################################################################################################################################

MTCP_VERSION = 77

# EXPERIMENTAL FEATURE:  checkpointing 32-bit images on 64-bit machines:
# Invoke as "make M32=1" to create 32-bit image on 64-bit machine.
# NOTE:  This also requires a modified HIGHEST_VA in mtcp_internal.h.  WHY?

# If we're running under linux32 on a 64-bit O/S, set mixed mode:  M32=1
ifneq (${shell uname -m},x86_64)
ifeq (${shell setarch x86_64 uname -m 2>/dev/null},x86_64)
M32=1
endif
endif

ifdef M32
# HIGHEST_VA is [vdso] (just above [stack])
CC = gcc -m32 -Wa,--32 -DHIGHEST_VA=0xffffe000
UNAME = linux32 uname
LD_RAW = ld -m32 -melf_i386
LDFLAGS =  -Wl,-m32 -Wl,-melf_i386 -shared
AS = as
else
CC = gcc
UNAME = uname
LD_RAW = ld
LDFLAGS =  -shared
AS = as --32
endif

#Currently sometimes can't link libmtcp.so without CFLAGS=-O0
CFLAGS += -O0 -g
#CFLAGS += -O0 -g -DDEBUG -DTIMING -Wall
#CFLAGS += -O0 -g -DDMTCP_DEBUG -DTIMING -Wall
# Try:  env CFLAGS=-DTEST_FORKED_CHECKPOINTING to CFLAGS  make
#  	 to enable standalone invocation of forked checkpointing.
# Could add something like -Wl,-soname,libmtcp.so.1 if one wants to globally
#  install a copy of libmtcp.so for others to use.

# If ./configure --enable-ptrace-support defined, DMTCP 'make' will set this.
# If calling 'make' from mtcp subdir, you may want to unconditionally set this.
ifdef PTRACE_SUPPORT
  CFLAGS += -DPTRACE
endif

# If ./configure --enable-ptrace-support defined, DMTCP 'make' will set this.
# If calling 'make' from mtcp subdir, you may want to unconditionally set this.
ifdef HBICT_DELTACOMP
  CFLAGS += -DHBICT_DELTACOMP
endif

# If ./configure --enable-fast-ckpt-restart defined, DMTCP 'make' will set this.
# If building MTCP standalone, change the line below to invoke this option.
ifdef FAST_CKPT_RESTART
  CFLAGS += -DFAST_CKPT_RST_VIA_MMAP
endif

LD = ${CC}
ASFLAGS =
MTCP_DIR := $(shell pwd)

# There might be a 32-bit built system running on top of 32/64 bit capable
# kernel. In that case `uname -m` returns 'x86_64' which is not correct.
# Instead, we should deduce the build-type from the build toochain itself.
ifeq (${shell ${LD_RAW} -shared -verbose|grep OUTPUT_FORMAT|cut -d '"' -f2},elf64-x86-64)
  BUILDTYPE=x86_64
else
  BUILDTYPE=i386
endif

# But for testmtcp, don't use CFLAGS if don't want position-independent code
ifeq ($(BUILDTYPE),x86_64)
  CFLAGS += -fPIC -DPIC
  LDFLAGS += -fPIC 
endif

ifeq ($(shell ${CC} -v --help 2>&1 | grep stack-protector|head -1|wc -l), 1)
  CFLAGS+=-fno-stack-protector
endif

all default: build tests readmtcp

readmtcp: readmtcp.c mtcp_internal.h libmtcp.so mtcp_util.o \
	mtcp_printf.o mtcp_state.o mtcp_fastckpt.o mtcp_safemmap.o
	${CC} ${CFLAGS} -o readmtcp readmtcp.c libmtcp.so mtcp_util.o \
	  mtcp_printf.o mtcp_state.o mtcp_fastckpt.o mtcp_safemmap.o

build: libmtcp.so mtcp_restart
tests: build testmtcp testmtcp2 testmtcp3 testmtcp4 testmtcp5 threadtest bigtestmtcp

# Test programs

testmtcp: testmtcp.o libmtcp.so
	env LD_LIBRARY_PATH=. ${CC} $(CFLAGS) -o testmtcp testmtcp.o -Wl,--export-dynamic libmtcp.so
	# libtool --mode=link gcc -o testmtcp testmtcp.o -Wl,--export-dynamic libmtcp.so

bigtestmtcp: bigtestmtcp.o libmtcp.so
	env LD_LIBRARY_PATH=. ${CC} $(CFLAGS) -o bigtestmtcp bigtestmtcp.o -Wl,--export-dynamic libmtcp.so

testgettimeofday: testgettimeofday.o libmtcp.so
	env LD_LIBRARY_PATH=. ${CC} $(CFLAGS) \
	  -o testgettimeofday testgettimeofday.o -Wl,--export-dynamic libmtcp.so
testgettimeofday.o: testgettimeofday.c
	${CC} $(CFLAGS) -c -o testgettimeofday.o testgettimeofday.c
checkgettimeofday: libmtcp.so mtcp_restart testgettimeofday
	(sleep 5; pkill testgettimeofda) &
	env LD_LIBRARY_PATH=. ./testgettimeofday || true
	@ echo ""
	@ echo Successfully killed after checkpoint.  Will now restart.
	sleep 2
	(sleep 5; pkill mtcp_restart) &
	./mtcp_restart testgettimeofday.mtcp || true
	@ echo Kill this program now after being satisfied it still works.
	@ echo ""
	sleep 2
	./mtcp_restart testgettimeofday.mtcp

testmtcp.o: testmtcp.c
	${CC} $(CFLAGS) -c -o testmtcp.o testmtcp.c

testmtcp2: testmtcp2.c libmtcp.so
	${CC} $(CFLAGS) -o testmtcp2 testmtcp2.c libmtcp.so

testmtcp3: testmtcp3.c libmtcp.so
	${CC} $(CFLAGS) -o testmtcp3 testmtcp3.c libmtcp.so -lpthread -Xlinker -Map -Xlinker testmtcp3.map

testmtcp4: testmtcp4.c libmtcp.so
	${CC} $(CFLAGS) -o testmtcp4 testmtcp4.c libmtcp.so -lpthread -Xlinker -Map -Xlinker testmtcp4.map

testmtcp5: testmtcp5.c libmtcp.so
	${CC} $(CFLAGS) -o testmtcp5 testmtcp5.c libmtcp.so 

testmtcp6: testmtcp6.c libmtcp.so
	${CC} $(CFLAGS) -Wl,--export-dynamic -o testmtcp6 testmtcp6.c libmtcp.so \
	  -lreadline -lhistory -lcurses

threadtest: threadtest.c
	${CC} $(CFLAGS) -o threadtest threadtest.c

# This is the command-line utility to restore a process
# NOTE: Compile/build mtcp_restart with -nodefaultlibs. See the following
#       explanation.
# RedHat doesn't provide the static libc in their standard
# repository. We want to have mtcp_restart statically linked for the sake
# of VDSO placement and ASLR. Thus we decided to remove the dependency of
# mtcp_restart on libc by rewriting some glibc function calls and
# replacing the glibc-syscall wrappers with the respective mtcp_sys_XXX()
# version.

mtcp_restart: mtcp_restart.c mtcp_internal.h \
	mtcp_printf.o mtcp_util.o mtcp_maybebpt.o \
	mtcp_safemmap.o mtcp_state.o mtcp_safe_open.o \
	mtcp_check_vdso.o mtcp_fastckpt.o
	${CC} $(CFLAGS) -static -nodefaultlibs \
	 -o mtcp_restart mtcp_restart.c mtcp_maybebpt.o \
	 mtcp_printf.o mtcp_util.o mtcp_safemmap.o \
	 mtcp_state.o mtcp_safe_open.o mtcp_check_vdso.o mtcp_fastckpt.o
	#\
	 #-lpthread

#not used:
# # mtcp_restart.so is used only by dmtcp --- not by mtcp
# mtcp_restart.so: mtcp_restart.c mtcp_internal.h mtcp_maybebpt.o \
#   mtcp_printf.o mtcp_safe_open.o mtcp_util.o mtcp_safemmap.o mtcp_state.o
# 	# ${CC} $(CFLAGS) -shared -Wl,--no-allow-shlib-undefined
# 	${CC} $(CFLAGS) -shared \
# 	 -o mtcp_restart.so mtcp_restart.c mtcp_maybebpt.o \
# 	 mtcp_printf.o mtcp_util.o mtcp_safemmap.o mtcp_state.o \
# 	 mtcp_safe_open.o

# This is the shareable that a user links with the application

# mtcp.t originally generated as below via:  ld -shared --verbose.
# It was modified to include shareable_begin/end sections.
# The patch was then created with:  diff -c mtcp.t mtcp.tnew > mtcp.t.patch
# Do not replace ld by ${LD} ${LDFLAGS} in generating mtcp.t.
mtcp.t: mtcp.t.patch-i386 mtcp.t.patch-x86_64
	rm -f mtcp.t
	${LD_RAW} -shared --verbose > mtcp.t
	sed -i -e '1,/========================/ d' mtcp.t
	sed -i -e '/========================/,$$ d' mtcp.t
	rm -f mtcp.t-fail
	if test ${BUILDTYPE} = x86_64; then \
	  if patch mtcp.t mtcp.t.patch-x86_64; then \
	    :; \
	  else \
	    mv mtcp.t mtcp.t-fail; false; \
	  fi \
	else \
	  if patch mtcp.t mtcp.t.patch-i386; then \
	    :; \
	  else \
	    mv mtcp.t mtcp.t-fail; false; \
	  fi \
	fi

LIBRARY_OBJS = mtcp.o mtcp_restart_nolibc.o \
	mtcp_maybebpt.o mtcp_printf.o mtcp_util.o \
	mtcp_safemmap.o mtcp_safe_open.o \
	mtcp_state.o mtcp_check_vdso.o mtcp_sigaction.o mtcp_ptrace.o \
	mtcp_fastckpt.o

# for libtools -- not used
%.lo : %.c
	libtool --mode=compile gcc -c $(CFLAGS) $<

libmtcp.so: mtcp.t ${LIBRARY_OBJS}
	##ld -shared -Map mtcp.map -o libmtcp.so mtcp.lo mtcp_restart_nolibc.lo --verbose
	#   this gets a default .t file so it can be chopped up
	echo LD $(LD)
	echo LDFLAGS $(LDFLAGS)
	${LD} ${LDFLAGS} -T mtcp.t -Wl,-Map,mtcp.map -o libmtcp.so \
	  ${LIBRARY_OBJS} -ldl -lpthread

mtcp.o: mtcp.c mtcp.h mtcp_internal.h mtcp_ptrace.h mtcp_ptrace.o
	${CC} $(CFLAGS) -Wa,-adhl=mtcp.lis -c -o mtcp.o mtcp.c

# Use uname instead of ${UNAME} below to determine if this is really 64-bit.
mtcp_restart_nolibc.o: mtcp_restart_nolibc.c mtcp.h mtcp_internal.h
	${CC} $(CFLAGS) -c -o mtcp_restart_nolibc.o mtcp_restart_nolibc.c

# mtcp.lis is needed only for debugging.
mtcp.lis: mtcp.c mtcp.h mtcp_internal.h
	${CC} $(CFLAGS) -c -o /dev/null -Wa,-ahls=mtcp.lis mtcp.c

# The resultant mtcp_restart_nolibc.o module should not have undefined symbols
# ... if it did, they would get linked to /lib/libc.so at runtime and 
# ... the restart wouldn't work as /lib/libc.so isn't in memory
#   during the restart
# It also cannot have global symbols like 'read' undefined in one module, defined 
# ... by another, as the loader will also re-direct those references to /lib/libc.so


mtcp_maybebpt.o: mtcp_maybebpt.c mtcp_internal.h
	${CC} $(CFLAGS) -c -o mtcp_maybebpt.o mtcp_maybebpt.c

mtcp_safe_open.o: mtcp_safe_open.c mtcp_internal.h
	${CC} $(CFLAGS) -c -o mtcp_safe_open.o mtcp_safe_open.c

mtcp_printf.o: mtcp_printf.c mtcp_internal.h
	${CC} $(CFLAGS) -c -o mtcp_printf.o mtcp_printf.c

mtcp_util.o: mtcp_util.c mtcp_internal.h
	${CC} $(CFLAGS) -c -o mtcp_util.o mtcp_util.c

mtcp_safemmap.o: mtcp_safemmap.c mtcp_internal.h
	${CC} $(CFLAGS) -c -o mtcp_safemmap.o mtcp_safemmap.c

mtcp_state.o: mtcp_state.c mtcp_internal.h
	${CC} $(CFLAGS) -c -o mtcp_state.o mtcp_state.c

mtcp_state.lis: mtcp_state.c mtcp_internal.h
	${CC} $(CFLAGS) -c -o /dev/null -Wa,-ahls=mtcp_state.lis mtcp_state.c

mtcp_check_vdso.o: mtcp_check_vdso.c
	${CC} $(CFLAGS) -c -o mtcp_check_vdso.o mtcp_check_vdso.c

extractobjectmodule: extractobjectmodule.c
	${CC} $(CFLAGS) -o extractobjectmodule extractobjectmodule.c

mtcp_sigaction.o: mtcp_sigaction.c mtcp_sys.h
	${CC} $(CFLAGS) -c -o mtcp_sigaction.o mtcp_sigaction.c

mtcp_ptrace.o: mtcp_ptrace.c mtcp_internal.h mtcp_ptrace.h
	${CC} $(CFLAGS) -c -o mtcp_ptrace.o mtcp_ptrace.c

mtcp_fastckpt.o: mtcp_fastckpt.c mtcp_internal.h
	${CC} $(CFLAGS) -c -o mtcp_fastckpt.o mtcp_fastckpt.c

dist: distclean
	rm -f /tmp/mtcpv${MTCP_VERSION}
	dir=`pwd`; cd ..; cp -r ./`basename $$dir` /tmp/mtcpv${MTCP_VERSION}; \
	 cd  /tmp; tar czvf mtcpv${MTCP_VERSION}.tgz ./mtcpv${MTCP_VERSION}
	rm -rf /tmp/mtcpv${MTCP_VERSION}
	mv /tmp/mtcpv${MTCP_VERSION}.tgz ../
	ls -l ../mtcpv${MTCP_VERSION}.tgz

# check: default testmtcp
# On recent 32-bit kernels, restart fails in "make", but succeeds manually.
# WHY?
check: libmtcp.so mtcp_restart testmtcp
	@ echo ""
	@ echo Type into program to verify it operates.  Will be killed in 13 s.
	@ echo ""
	(sleep 13; pkill -9 testmtcp) &
	env LD_LIBRARY_PATH=. ./testmtcp || true
	@ echo ""
	@ echo Successfully killed after checkpoint.  Will now restart.
	@ echo Continue typing arbitrary text into program to test that
	@ echo it still works, and that checkpoint of a restart works.
	sleep 5
	./mtcp_restart testmtcp.mtcp
	echo FINISH EARLY FOR NOW
	exit 1
	(sleep 13; pkill -9 mtcp_restart) & ./mtcp_restart testmtcp.tmp || true
	@ echo Kill this program now after being satisfied it still works.
	@ echo ""
	sleep 5
	./mtcp_restart testmtcp.mtcp

distclean: clean
	rm -f *~ mtcp.kdevses mtcp.kdevelop.* testmtcp{,1,2,3,4}.mtcp \
	      *.lis a.out mtcp.t.orig readmtcp

clean:
	rm -f *.o *.map mtcp_restart_noblibc.lis mtcp_sharetemp.c \
	      testmtcp.mtcp libmtcp.so mtcp.t mtcp.t-fail mtcp_restart.so \
	      mtcp_restart extractobjectmodule \
	      x.x zz.out testmtcp testmtcp[0-9] threadtest bigtestmtcp
