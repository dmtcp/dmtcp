CFLAGS=-g3 -O0

test: a.out
	./a.out localhost
	./a.out localhost time

gdb: a.out
	gdb --args ./a.out localhost

a.out: gethostbyname_static.c gethostbyname_proxy
	${CC} ${CFLAGS} -DSTANDALONE $< -o $@

gethostbyname_proxy: gethostbyname_proxy.c
	${CC} ${CFLAGS} $< -o $@

clean:
	rm -f a.out *.o gethostbyname_proxy

dist: clean
	dir=`basename $$PWD` && cd .. && tar zcvf ./$$dir.tar.gz ./$$dir
	dir=`basename $$PWD` && ls -l ../$$dir.tar.gz
