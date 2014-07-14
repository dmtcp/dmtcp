hdr=header.tex
ftr=footer.tex
macros=macros.trans
tmp=tmp.tex
vtmp=vtmp.tex

targets = dmtcp_command dmtcp_coordinator dmtcp_launch dmtcp_restart

default:
	echo "\setVersion{`cat ../VERSION`}" > $(vtmp);
	for i in $(targets); do \
	  (cat $(hdr) $(vtmp) $$i.tex $(ftr) > $(tmp); \
	  latex2man -t $(macros) $(tmp) $$i.1); gzip -9 $$i.1; \
	done
	rm $(vtmp) $(tmp)

.PHONY: default 
