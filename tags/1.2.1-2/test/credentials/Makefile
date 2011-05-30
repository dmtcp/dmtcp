
TARGETS = conf3 conf4 conf5 conf6 conf7 grouptest1 grouptest2 grouptest3 grouptest4

all: $(TARGETS)

clean:
	rm -f ckpt_* dmtcp_* p[1234567890]*
	rm -f $(TARGETS)


conf3: conf3.c
	-$(CC) -o $@ $< $(CFLAGS)

conf4: conf4.c
	-$(CC) -o $@ $< $(CFLAGS)

conf5: conf5.c
	-$(CC) -o $@ $< $(CFLAGS)

conf6: conf6.c
	-$(CC) -o $@ $< $(CFLAGS)
	
conf7: conf7.c
	-$(CC) -o $@ $< $(CFLAGS)

grouptest1: grouptest1.c
	-$(CC) -o $@ $< $(CFLAGS)

grouptest2: grouptest2.c
	-$(CC) -o $@ $< $(CFLAGS)

grouptest3: grouptest3.c
	-$(CC) -o $@ $< $(CFLAGS)

grouptest4: grouptest4.c
	-$(CC) -o $@ $< $(CFLAGS)

%: %.c
	$(CC) -o $@ $< $(CFLAGS)

%: %.cpp
	$(CXX) -o $@ $< $(CXXFLAGS)
