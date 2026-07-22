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

include ${MAGICDIR}/defs.mak

INST_SCMSRC = ${SCMSRC:%=$(DESTDIR)${INSTALL_SCMDIR}/%}

CLEANS = lisp${SHDLIB_EXT} liblisp.o liblisp.a ${SRCS:%.c=%.o}

# Empty default target
all:

lisp${SHDLIB_EXT}:
	@echo --- making lisp library \(lisp${SHDLIB_EXT}\)
	${RM} lisp${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDFLAGS} ${LDDL_FLAGS} ${SRCS} \
		${LD_SHARED} -lc ${LIBS}


install: ${INST_SCMSRC}

$(DESTDIR)${INSTALL_SCMDIR}/%: scm/%
	test -d "$(DESTDIR)$(INSTALL_SCMDIR)" || mkdir -p "$(DESTDIR)$(INSTALL_SCMDIR)"
	${CP} scm/$* $(DESTDIR)$(INSTALL_SCMDIR)/$*

include ${MAGICDIR}/rules.mak
