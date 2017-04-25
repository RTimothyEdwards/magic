#
# rcsid "$Header: /usr/cvsroot/magic-8.0/ext2sim/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
#

MODULE   = ext2sim
MAGICDIR = ..
SRCS     = ext2sim.c

EXTRA_LIBS  = ${MAGICDIR}/extflat/libextflat.o ${MAGICDIR}/utils/libutils.a

include ${MAGICDIR}/defs.mak

LIBS += -lm ${LD_EXTRA_LIBS} ${SUB_EXTRA_LIBS}
CLEANS += exttosim${SHDLIB_EXT} simwrap.o

main: ext2sim

tcl-main: exttosim${SHDLIB_EXT}

simwrap.o: ext2sim.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ext2sim.c -c -o simwrap.o

exttosim${SHDLIB_EXT}: simwrap.o ${MAGICDIR}/extflat/libextflat.o
	@echo --- making exttosim Tcl library \(exttosim${SHDLIB_EXT}\)
	${RM} exttosim${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} simwrap.o \
		${MAGICDIR}/extflat/libextflat.o ${LD_SHARED} -lc ${LIBS}

install: $(DESTDIR)${BINDIR}/${MODULE}${EXEEXT}

install-tcl: $(DESTDIR)${TCLDIR}/exttosim${SHDLIB_EXT}

$(DESTDIR)${TCLDIR}/exttosim${SHDLIB_EXT}: exttosim${SHDLIB_EXT}
	${RM} $(DESTDIR)${TCLDIR}/exttosim${SHDLIB_EXT}
	${CP} exttosim${SHDLIB_EXT} $(DESTDIR)${TCLDIR}/exttosim${SHDLIB_EXT}

include ${MAGICDIR}/rules.mak
