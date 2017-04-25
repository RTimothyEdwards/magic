#
# rcsid "$Header: /usr/cvsroot/magic-8.0/ext2spice/Makefile,v 1.2 2010/08/10 00:18:45 tim Exp $"
#

MODULE   = ext2spice
MAGICDIR = ..
SRCS     = ext2spice.c ext2hier.c

EXTRA_LIBS = ${MAGICDIR}/extflat/libextflat.o ${MAGICDIR}/utils/libutils.a

include ${MAGICDIR}/defs.mak

LIBS += -lm ${LD_EXTRA_LIBS} ${SUB_EXTRA_LIBS}
CLEANS += exttospice${SHDLIB_EXT} spicewrap.o spicehier.o

main: ext2spice

tcl-main: exttospice${SHDLIB_EXT}

spicewrap.o: ext2spice.c ext2spice.h
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ext2spice.c -c -o spicewrap.o

spicehier.o: ext2hier.c ext2spice.h
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ext2hier.c -c -o spicehier.o

exttospice${SHDLIB_EXT}: spicewrap.o spicehier.o ${MAGICDIR}/extflat/libextflat.o
	@echo --- making exttospice Tcl library \(exttospice${SHDLIB_EXT}\)
	${RM} exttospice${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} spicewrap.o spicehier.o \
		${MAGICDIR}/extflat/libextflat.o ${LD_SHARED} -lc ${LIBS}

install: $(DESTDIR)${BINDIR}/${MODULE}${EXEEXT} $(DESTDIR)${BINDIR}/spice2sim 

install-tcl: $(DESTDIR)${TCLDIR}/exttospice${SHDLIB_EXT} $(DESTDIR)${BINDIR}/spice2sim

$(DESTDIR)${TCLDIR}/exttospice${SHDLIB_EXT}: exttospice${SHDLIB_EXT}
	${RM} $(DESTDIR)${TCLDIR}/exttospice${SHDLIB_EXT}
	${CP} exttospice${SHDLIB_EXT} $(DESTDIR)${TCLDIR}/exttospice${SHDLIB_EXT}

$(DESTDIR)${BINDIR}/spice2sim: spice2sim
	${RM} $(DESTDIR)${BINDIR}/spice2sim
	${CP} spice2sim $(DESTDIR)${BINDIR}/spice2sim

include ${MAGICDIR}/rules.mak
