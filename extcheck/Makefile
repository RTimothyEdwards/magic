#
# rcsid "$Header: /usr/cvsroot/magic-8.0/extcheck/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
#
 
MODULE   = extcheck
MAGICDIR = ..
SRCS     = extcheck.c

EXTRA_LIBS = ${MAGICDIR}/extflat/libextflat.o ${MAGICDIR}/utils/libutils.a

include ${MAGICDIR}/defs.mak

LIBS += -lm

main: extcheck

install: $(DESTDIR)${BINDIR}/${MODULE}${EXEEXT}

install-tcl:
	echo "Nothing to do here"

include ${MAGICDIR}/rules.mak
