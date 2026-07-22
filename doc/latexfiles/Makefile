MAGICDIR = ../..

include $(MAGICDIR)/defs.mak

# PS_SRCDIR: pre-built PostScript shipped in the source tree (read-only, used by
#            install when a doc has not been regenerated).
# PS_GENDIR: regenerated PostScript, written into the build tree (never source).
PS_SRCDIR=${MAGICSRC}/doc/psfiles
PS_GENDIR=psfiles
PS_INSTDIR=$(INSTALL_LIBDIR)/magic/doc

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

GEN_GENPS= $(GENPS:%=${PS_GENDIR}/%)
INST_GENPS= $(GENPS:%=$(DESTDIR)${PS_INSTDIR}/%)

# Regenerate all PostScript into the build tree (${PS_GENDIR}).
all: $(GEN_GENPS)
	@${RM} $(GENDVI) *.log *.aux

install: $(DESTDIR)${PS_INSTDIR} ${INST_GENPS}

$(DESTDIR)${PS_INSTDIR}: make-doc-dir

make-doc-dir:
	${SCRIPTS}/mkdirs $(DESTDIR)${PS_INSTDIR}

# Install each PostScript file from the build tree if it was regenerated there,
# otherwise from the pre-built copy shipped in the source tree.
$(DESTDIR)${PS_INSTDIR}/%: $(DESTDIR)${PS_INSTDIR}
	@f=${PS_GENDIR}/$*; test -f $$f || f=${PS_SRCDIR}/$*; \
	echo "${CP} $$f $(DESTDIR)${PS_INSTDIR}/$*"; \
	${CP} $$f $(DESTDIR)${PS_INSTDIR}/$*

# Regenerate a .ps (into the build tree) from its .tex (in the source tree).
${PS_GENDIR}/%.ps: %.tex
	@mkdir -p ${PS_GENDIR}
	@echo "Converting $*.tex -> $*.dvi"
	@TEXINPUTS=$(srcdir): latex $(srcdir)/$*.tex  < /dev/null > /dev/null
	@if grep 'LaTeX Warn' $*.log; \
	then \
		echo "Detected warnings, doing second pass for $*...";\
		TEXINPUTS=$(srcdir): latex $(srcdir)/$*.tex < /dev/null > /dev/null; \
		(grep 'LaTeX Warn' $*.log || (echo > /dev/null)); \
		echo; \
	fi
	@echo "Converting $*.dvi -> ${PS_GENDIR}/$*.ps"
	@dvips -t letter $*.dvi -o ${PS_GENDIR}/$*.ps > /dev/null

clean:
	${RM} $(GENDVI) $(GEN_GENPS) *.log *.aux
