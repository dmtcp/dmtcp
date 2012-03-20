# This Makefile was used to produce ../architecture-of-dmtcp.pdf

FIG=${wildcard *.fig}
EPS=${FIG:.fig=.eps}

architecture-of-dmtcp.pdf: architecture-of-dmtcp.ps
	ps2pdf $<
architecture-of-dmtcp.ps: architecture-of-dmtcp.dvi
	dvips $<
architecture-of-dmtcp.dvi: architecture-of-dmtcp.tex ${EPS}
	latex $<

%.eps: %.fig
	fig2dev -L eps $< > $@

clean:
	rm -rf *.eps *.dvi *.ps *.pdf *.aux *.log
