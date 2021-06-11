MANPAGES = ${addsuffix .gz,${wildcard *.1}}
GENERATED_PAGES = ${addsuffix .tex,${wildcard *.1}} \
                  ${addsuffix .html,${wildcard *.1}}

default: ${MANPAGES}

html-manpages:
	echo "\setVersion{`cat ../VERSION`}" > $(vtmp)
	echo "\setDate{`date +"%B, %Y"`}" >> $(vtmp)
	for i in $(targets); do \
	  (cat $(hdr) $(vtmp) $$i.latex2man $(ftr) > $(tmp) && \
	   ${LATEX2MAN} -H -t $(macros) $(tmp) $$i.html); \
	done
	rm $(vtmp) $(tmp)

latex-manpages:
	echo "\setVersion{`cat ../VERSION`}" > $(vtmp)
	echo "\setDate{`date +"%B, %Y"`}" >> $(vtmp)
	for i in $(targets); do \
	  (cat $(hdr) $(vtmp) $$i.latex2man $(ftr) > $(tmp) && \
	   ${LATEX2MAN} -L $(tmp) $$i.tex) && \
	   echo '\\end{document}' >> $$i.tex; \
	done
	rm $(vtmp) $(tmp)

## FIXME:  Many .1 files were updates without updating the .latex2man
## Uncomment this rule when the .latex2man have been updated.
# %.1: %.latex2man
# 	./latex2man -t ./macros.trans $< $@

# 'gzip --keep' introduced only in gzip-1.6
%.1.gz: %.1
	gzip < $< > $@

install: ${MANPAGES}

clean:
	rm -f ${GENERATED_PAGES}

distclean: clean
	rm -f *.1.gz

.PHONY: default clean distclean
