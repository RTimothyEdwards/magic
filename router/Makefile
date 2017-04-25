#
# rcsid="$Header: /usr/cvsroot/magic-8.0/router/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
#

MODULE    = router
MAGICDIR  = ..
SRCS      = rtrCmd.c rtrMain.c rtrDcmpose.c rtrFdback.c rtrHazards.c \
            rtrChannel.c rtrPaint.c rtrPin.c rtrSide.c rtrStem.c rtrTech.c \
            rtrVia.c rtrTravers.c 

include ${MAGICDIR}/defs.mak

LIBS += ${LD_EXTRA_LIBS} ${SUB_EXTRA_LIBS}
CLEANS += tclroute${SHDLIB_EXT} tclroute.o

EXTRA_LIBS = ${MAGICDIR}/garouter/libgarouter.o \
	${MAGICDIR}/mzrouter/libmzrouter.o ${MAGICDIR}/irouter/libirouter.o \
	${MAGICDIR}/grouter/libgrouter.o ${MAGICDIR}/gcr/libgcr.o

tcl-main: tclroute.o tclroute${SHDLIB_EXT}

tclroute.o: tclroute.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} tclroute.c -c -o tclroute.o

tclroute${SHDLIB_EXT}: tclroute.o ${OBJS}
	@echo --- making Tcl auto-load module \(tclroute${SHDLIB_EXT}\)
	${RM} tclroute${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} tclroute.o ${OBJS} \
		 ${EXTRA_LIBS} -lc ${LIBS}

install-tcl: $(DESTDIR)${TCLDIR}/tclroute${SHDLIB_EXT}

$(DESTDIR)${TCLDIR}/tclroute${SHDLIB_EXT}: tclroute${SHDLIB_EXT}
	${RM} $(DESTDIR)${TCLDIR}/tclroute${SHDLIB_EXT}
	${CP} tclroute${SHDLIB_EXT} $(DESTDIR)${TCLDIR}/tclroute${SHDLIB_EXT}

include ${MAGICDIR}/rules.mak
