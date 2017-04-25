#
# rscid $Header:
#

MODULE    = plot
MAGICDIR  = ..
SRCS      = plotCmd.c plotGremln.c plotHP.c plotPS.c plotMain.c plotRutils.c \
	    plotVers.c plotPixels.c plotPNM.c 

include ${MAGICDIR}/defs.mak

LIBS += ${LD_EXTRA_LIBS} ${SUB_EXTRA_LIBS}
CLEANS += tclplot${SHDLIB_EXT} tclplot.o

# Note:  Add -DGREMLIN and/or -DVERSATEC to DFLAGS if you want
# those output types to be added.  Without them, only a minimal
# amount of code is compiled to keep magic from complaining when
# parameters are found in the technology file.

tcl-main: tclplot.o tclplot${SHDLIB_EXT}

tclplot.o: tclplot.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} tclplot.c -c -o tclplot.o

tclplot${SHDLIB_EXT}: tclplot.o ${OBJS}
	@echo --- making Tcl auto-load module \(tclplot${SHDLIB_EXT}\)
	${RM} tclplot${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} tclplot.o ${OBJS} \
		 ${EXTRA_LIBS} -lc ${LIBS}

install-tcl: $(DESTDIR)${TCLDIR}/tclplot${SHDLIB_EXT}

$(DESTDIR)${TCLDIR}/tclplot${SHDLIB_EXT}: tclplot${SHDLIB_EXT}
	${RM} $(DESTDIR)${TCLDIR}/tclplot${SHDLIB_EXT}
	${CP} tclplot${SHDLIB_EXT} $(DESTDIR)${TCLDIR}/tclplot${SHDLIB_EXT}

include ${MAGICDIR}/rules.mak
