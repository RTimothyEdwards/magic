#
# rcsid $Header: /usr/cvsroot/magic-8.0/magic/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
#

MODULE   = magic
MAGICDIR = ..
SRCS     = magicTop.c

include ${MAGICDIR}/defs.mak

EXTRA_LIBS = ${MAGICDIR}/cmwind/libcmwind.o ${MAGICDIR}/commands/libcommands.o \
	    ${MAGICDIR}/database/libdatabase.o ${MAGICDIR}/dbwind/libdbwind.o \
	    ${MAGICDIR}/drc/libdrc.o ${MAGICDIR}/debug/libdebug.o \
	    ${MAGICDIR}/extract/libextract.o ${MAGICDIR}/graphics/libgraphics.o \
	    ${MAGICDIR}/select/libselect.o ${MAGICDIR}/textio/libtextio.o \
	    ${MAGICDIR}/tiles/libtiles.o ${MAGICDIR}/windows/libwindows.o \
	    ${MAGICDIR}/wiring/libwiring.o ${MAGICDIR}/resis/libresis.o \
	    ${MAGICDIR}/sim/libsim.o ${MAGICDIR}/netmenu/libnetmenu.o \
	    ${MAGICDIR}/plow/libplow.o ${MAGICDIR}/utils/libutils.o \
	    ${MAIN_EXTRA_LIBS}

BITMAPS = up.xbm down.xbm left.xbm right.xbm zoom.xbm lock.xbm
DEST_XBM = $(BITMAPS:%=$(DESTDIR)${TCLDIR}/bitmaps/%)

DFLAGS += -DMAGIC_DATE="\"`date`\""
LIBS   += ${GR_LIBS} ${READLINE_LIBS} -lm ${LD_EXTRA_LIBS} \
		${OA_LIBS} ${TOP_EXTRA_LIBS}
CLEANS += tclmagic${SHDLIB_EXT} libtclmagic${SHDLIB_EXT}.a proto.magicrc

main: magic proto.magicrc

tcl-main: tclmagic${SHDLIB_EXT} proto.magicrc

tclmagic${SHDLIB_EXT}: ${EXTRA_LIBS}
	@echo --- making magic Tcl library \(tclmagic${SHDLIB_EXT}\)
	${RM} tclmagic${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} ${LD_RUN_PATH} \
		${EXTRA_LIBS} -lc ${LIBS}

proto.magicrc: proto.magicrc.m4
	sed -e /CAD_DIR/s%CAD_DIR%${LIBDIR}%g proto.magicrc.m4 | \
		${M4} ${GR_DFLAGS} ${DFLAGS} > proto.magicrc

install: $(DESTDIR)${BINDIR}/${MODULE}${EXEEXT} $(DESTDIR)${SYSDIR}/.magicrc \
	$(DESTDIR)${SYSDIR}/magicps.pro

install-tcl: $(DESTDIR)${TCLDIR}/tclmagic${SHDLIB_EXT} $(DESTDIR)${SYSDIR}/.magicrc \
	$(DESTDIR)${SYSDIR}/.initrc $(DESTDIR)${SYSDIR}/magicps.pro ${DEST_XBM}

$(DESTDIR)${TCLDIR}/tclmagic${SHDLIB_EXT}: tclmagic${SHDLIB_EXT}
	${RM} $(DESTDIR)${TCLDIR}/tclmagic${SHDLIB_EXT}
	${CP} tclmagic${SHDLIB_EXT} $(DESTDIR)${TCLDIR}/tclmagic${SHDLIB_EXT}

$(DESTDIR)${TCLDIR}/bitmaps/%: bitmaps/%
	${RM} $(DESTDIR)${TCLDIR}/bitmaps/$*
	${CP} bitmaps/$* $(DESTDIR)${TCLDIR}/bitmaps/$*

$(DESTDIR)${SYSDIR}/.magicrc: proto.magicrc
	${RM} $(DESTDIR)${SYSDIR}/.magicrc
	${CP} proto.magicrc $(DESTDIR)${SYSDIR}/.magicrc

$(DESTDIR)${SYSDIR}/.initrc: proto.initrc
	${RM} $(DESTDIR)${SYSDIR}/.initrc
	${CP} proto.initrc $(DESTDIR)${SYSDIR}/.initrc

$(DESTDIR)${SYSDIR}/magicps.pro: magicps.pro
	${RM} $(DESTDIR)${SYSDIR}/magicps.pro
	${CP} magicps.pro $(DESTDIR)${SYSDIR}/magicps.pro

include ${MAGICDIR}/rules.mak
