#
# rcsid "$Header: /usr/cvsroot/magic-8.0/ext2sim/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
#

MODULE   = ext2sim
MAGICDIR = ..
SRCS     = ext2sim.c

EXTRA_LIBS  = ${MAGICDIR}/extflat/libextflat.o ${MAGICDIR}/utils/libutils.a

include ${MAGICDIR}/defs.mak

LIBS += -lm ${LD_EXTRA_LIBS} ${SUB_EXTRA_LIBS}
CLEANS += exttosim${SHDLIB_EXT} simwrap.o finds.o ext2sim_main.o ext2sim_main finds sim2simp

main: finds sim2simp # ext2sim_main # deprecated

tcl-main: exttosim${SHDLIB_EXT}

# Not linkable due to libutils.a needing more hierarchical separation
ext2sim_main: ext2sim_main.o ${EXTRA_LIBS} ${MAGICDIR}/tiles/libtiles.o
	${CC} ${LDFLAGS} ${CFLAGS} -o $@ $^ ${LIBS} -lX11 -lz

ext2sim_main.o: ext2sim.c
	${CC} ${CFLAGS} -I${MAGICDIR} -DEXT2SIM_MAIN -c -o $@ $^

finds: finds.o ${MAGICDIR}/utils/libutils.a
	${CC} ${LDFLAGS} ${CFLAGS} -o $@ $^

simwrap.o: ext2sim.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ext2sim.c -c -o simwrap.o

exttosim${SHDLIB_EXT}: simwrap.o ${MAGICDIR}/extflat/libextflat.o
	@echo --- making exttosim Tcl library \(exttosim${SHDLIB_EXT}\)
	${RM} exttosim${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} simwrap.o \
		${MAGICDIR}/extflat/libextflat.o ${LD_SHARED} -lc ${LIBS}

install: $(DESTDIR)${INSTALL_BINDIR}/${MODULE}${EXEEXT}

install-tcl: $(DESTDIR)${INSTALL_TCLDIR}/exttosim${SHDLIB_EXT}

$(DESTDIR)${INSTALL_TCLDIR}/exttosim${SHDLIB_EXT}: exttosim${SHDLIB_EXT}
	${RM} $(DESTDIR)${INSTALL_TCLDIR}/exttosim${SHDLIB_EXT}
	${CP} exttosim${SHDLIB_EXT} $(DESTDIR)${INSTALL_TCLDIR}/exttosim${SHDLIB_EXT}

include ${MAGICDIR}/rules.mak
