hdr=header.tex
ftr=footer.tex
macros=macros.trans
tmp=tmp.tex

targets = dmtcp_command dmtcp_coordinator dmtcp_launch dmtcp_restart

default:
	for i in $(targets); do \
	  (cat $(hdr) $$i.tex $(ftr) > $(tmp); \
	  latex2man -t $(macros) $(tmp) $$i.1.gz); \
	done
	rm $(tmp)

.PHONY: default 
