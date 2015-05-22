# This Makefile was used to produce .pdf files from .tex files.

FIG=${wildcard *.fig}
EPS=${FIG:.fig=.eps}

plugin-tutorial.pdf: plugin-tutorial.tex
	pdflatex $<
	if grep -q Rerun `basename $< .tex`.log; then pdflatex $<; fi

architecture-of-dmtcp.pdf: architecture-of-dmtcp.ps
	ps2pdf $<
architecture-of-dmtcp.ps: architecture-of-dmtcp.dvi
	dvips $<
architecture-of-dmtcp.dvi: architecture-of-dmtcp.tex ${EPS}
	latex $<

%.eps: %.fig
	fig2dev -L eps $< > $@

clean:
	rm -rf *.eps *.dvi *.ps *.pdf *.aux *.log *.toc *.out
