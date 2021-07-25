#
# Makefile for acmart package
#
# This file is in public domain
#
# $Id: Makefile,v 1.10 2016/04/14 21:55:57 boris Exp $
#

PAPER=design

PDF = $(PAPER).pdf

all:  ${PDF}

$(PAPER).pdf:  $(PAPER).tex
	bibtex $(PAPER); \
	latexmk -pdf -pdflatex="pdflatex -interaction=nonstopmode" -use-make $(PAPER).tex ;\
	latexmk -pdf -pdflatex="pdflatex -interaction=nonstopmode" -use-make $(PAPER).tex

open: $(PAPER).pdf # Open on Mac
	open $<

vi: $(PAPER).tex
	vim $<

gvi: $(PAPER).tex
	gvim $<

mvi: $(PAPER).tex
	mvim $<

pdf: $(PAPER).pdf

renoir: $(PAPER).pdf # For printing to renoir
	a2ps -Prenoir -1 -s2 $<

touch: $(PAPER).tex
	touch $<

evince: $(PAPER).pdf
	evince $<

okular: $(PAPER).pdf
	okular $<

spell: $(PAPER).tex
	sed -f detex.sed $(PAPER).tex | spell | sort | uniq | less

%.cls:   %.ins %.dtx
	pdflatex $<

deploy: $(PAPER).pdf $(PAPER).html
	scp $^ login:~/.www/thesis-proposal/

clean:
	$(RM)  *.log $(PAPER).pdf \
	*.cfg *.glo *.idx *.toc \
	*.ilg *.ind *.out *.lof \
	*.lot *.bbl *.blg *.gls *.cut *.hd \
	*.dvi *.ps *.thm *.tgz *.zip *.rpi

distclean: clean
	$(RM) $(PDF) *-converted-to.pdf

#
# Archive for the distribution. Includes typeset documentation
#
archive:  all clean
	tar -C .. -czvf ../$(PACKAGE).tgz --exclude '*~' --exclude '*.tgz' --exclude '*.zip'  --exclude CVS --exclude '.git*' $(PACKAGE); mv ../$(PACKAGE).tgz .

zip:  all clean
	zip -r  $(PACKAGE).zip * -x '*~' -x '*.tgz' -x '*.zip' -x CVS -x 'CVS/*'

.PHONY: pdf renoir okular evince open vi gvi mvi all clean spell archive zip distclean touch deploy
