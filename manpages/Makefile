MANPAGES = ${addsuffix .gz,${wildcard *.1}}

default:

## FIXME:  Many .1 files were updates without updating the .latex2man
## Uncomment this rule when the .latex2man have been updated.
# %.1: %.latex2man
# 	./latex2man -t ./macros.trans $< $@

%.1.gz: %.1
	gzip --keep $<

install: ${MANPAGES}

clean:
	rm -f *.1.gz
