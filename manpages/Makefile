# This is the version of latex2man of 2017/10/03, with bug fixes from the author
# Since this will be invoked only by developers, delete local latex2man
#   when the author's version makes its way into our current distro.
LATEX2MAN=./latex2man
hdr=header.tex
ftr=footer.tex
macros=macros.trans
tmp=tmp.tex
vtmp=vtmp.tex

targets = dmtcp_command dmtcp_coordinator dmtcp_launch dmtcp_restart dmtcp_srun_helper

default:
	echo "\setVersion{`cat ../VERSION`}" > $(vtmp)
	echo "\setDate{`date +"%B, %Y"`}" >> $(vtmp)
	for i in $(targets); do \
	  (cat $(hdr) $(vtmp) $$i.latex2man $(ftr) > $(tmp) && \
	   ${LATEX2MAN} -t $(macros) $(tmp) $$i.1) && gzip -f $$i.1; \
	done
	rm $(vtmp) $(tmp)

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

clean:
	for i in $(targets); do \
	  rm -f $$i.tex $$i.html; \
	done

distclean: clean
	for i in $(targets); do \
	  rm -f $$i.1.gz; \
	done

.PHONY: default clean distclean
