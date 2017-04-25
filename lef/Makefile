#
# LEF module Makefile
#

MODULE    = lef
MAGICDIR  = ..
SRCS      = lefCmd.c lefTech.c lefWrite.c defWrite.c lefRead.c defRead.c

include ${MAGICDIR}/defs.mak

LIBS += ${LD_EXTRA_LIBS} ${SUB_EXTRA_LIBS}
CLEANS += magiclef${SHDLIB_EXT} tcllef.o

tcl-main: tcllef.o magiclef${SHDLIB_EXT}

tcllef.o: tcllef.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} tcllef.c -c -o tcllef.o

magiclef${SHDLIB_EXT}: tcllef.o ${OBJS}
	@echo --- making Tcl auto-load module \(magiclef${SHDLIB_EXT}\)
	${RM} magiclef${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} tcllef.o ${OBJS} \
		 ${EXTRA_LIBS} -lc ${LIBS}

install-tcl: $(DESTDIR)${TCLDIR}/magiclef${SHDLIB_EXT}

$(DESTDIR)${TCLDIR}/magiclef${SHDLIB_EXT}: magiclef${SHDLIB_EXT}
	${RM} $(DESTDIR)${TCLDIR}/magiclef${SHDLIB_EXT}
	${CP} magiclef${SHDLIB_EXT} $(DESTDIR)${TCLDIR}/magiclef${SHDLIB_EXT}

include ${MAGICDIR}/rules.mak
