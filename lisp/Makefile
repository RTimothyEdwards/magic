#
# rscid $Header:
#

MODULE    = lisp
MAGICDIR  = ..
SRCS      = lispMain.c lispEval.c lispPrint.c lispParse.c lispA-Z.c \
            lispGC.c lispFrame.c lispTrace.c lispMagic.c lispArith.c \
            lispString.c lispIO.c
SCMSRC    = default.scm box.scm label.scm draw.scm drc.scm \
            stack.scm layout.scm prs.scm help.scm gate.scm \
            sel.scm

all:

include ${MAGICDIR}/defs.mak

INST_SCMSRC = ${SCMSRC:%=$(DESTDIR)${INSTALL_SCMDIR}/%}

install: ${INST_SCMSRC}

$(DESTDIR)${SCMDIR}/%: scm/%
	${CP} scm/$* $(DESTDIR)$(INSTALL_SCMDIR)/$*

include ${MAGICDIR}/rules.mak
