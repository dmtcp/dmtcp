# File with TLA+ model
FILE=2pcmodelv4
# File with model checking configuration
MC=${FILE}mc
# Path to TLA install dir
TLA_PATH=/Users/rgarg/software/tla/tla/
# Path to Java
JAVA=/usr/bin/java
# Num. of worker threads for model checking
TLC_OPTIONS=-workers 8

default: build

build: ${FILE}.tla
	CLASSPATH=${TLA_PATH} ${JAVA} pcal.trans $<

vi vim: ${FILE}.tla
	vim $<

run: build
	CLASSPATH=${TLA_PATH} ${JAVA} tlc2.TLC ${TLC_OPTIONS} ${MC} > error.trace

clean:
	rm -rf states *.old

tidy:
	rm -f error.trace

dist: clean
	(dir=`basename $$PWD` && cd .. && tar zcvf $$dir.tgz $$dir)
	(dir=`basename $$PWD` && ls -l ../$$dir.tgz)

.PHONY: default build run clean vi vim tidy dist
