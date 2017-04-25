MAGICDIR = ../..

include $(MAGICDIR)/defs.mak

PS_SRCDIR=../psfiles
PS_INSTDIR=$(LIBDIR)/magic/doc

.SUFFIXES: .dvi .tex .ps

GENDVI=tut1.dvi tut2.dvi tut3.dvi tut4.dvi tut5.dvi tut6.dvi \
	tut7.dvi tut8.dvi tut9.dvi tut10.dvi tut11.dvi \
	maint1.dvi maint2.dvi maint3.dvi maint4.dvi \
	introduction.dvi copyright.dvi addendum6_5.dvi \
	tutscm1.dvi tutscm2.dvi tutscm3.dvi tutscm4.dvi \
	tuttcl1.dvi tuttcl2.dvi tuttcl3.dvi tuttcl4.dvi tuttcl5.dvi \
	tutwrl1.dvi

GENPS=tut1.ps tut2.ps tut3.ps tut4.ps tut5.ps tut6.ps \
	tut7.ps tut8.ps tut9.ps tut10.ps tut11.ps \
	maint1.ps maint2.ps maint3.ps maint4.ps \
	introduction.ps copyright.ps addendum6_5.ps \
	tutscm1.ps tutscm2.ps tutscm3.ps tutscm4.ps \
	tuttcl1.ps tuttcl2.ps tuttcl3.ps tuttcl4.ps tuttcl5.ps \
	tutwrl1.ps

SRC_GENPS= $(GENPS:%=${PS_SRCDIR}/%)
INST_GENPS= $(GENPS:%=$(DESTDIR)${PS_INSTDIR}/%)

all: $(SRC_GENPS)
	@${RM} $(GENDVI) *.log *.aux

install: $(DESTDIR)${PS_INSTDIR} ${INST_GENPS}

$(DESTDIR)${PS_INSTDIR}: make-doc-dir

make-doc-dir:
	${SCRIPTS}/mkdirs $(DESTDIR)${PS_INSTDIR}

$(DESTDIR)${PS_INSTDIR}/%: ${PS_SRCDIR}/% $(DESTDIR)${PS_INSTDIR}
	${CP} ${PS_SRCDIR}/$* $(DESTDIR)${PS_INSTDIR}/$*

.tex.ps:
	@echo "Converting $*.tex -> $*.dvi"
	@latex $*.tex  < /dev/null > /dev/null
	@if grep 'LaTeX Warn' $*.log; \
	then \
		echo "Detected warnings, doing second pass for $*...";\
		latex $*.tex < /dev/null > /dev/null; \
		(grep 'LaTeX Warn' $*.log || (echo > /dev/null)); \
		echo; \
	fi
	@echo "Converting $*.dvi -> $(PS_SRCDIR)/$*.ps"
	@dvips -t letter $*.dvi -o $(PS_SRCDIR)/$*.ps > /dev/null

clean:
	${RM} $(GENDVI) $(SRC_GENPS) *.log *.aux
