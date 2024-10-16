#
# rcsid $Header: /usr/cvsroot/magic-8.0/magic/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
#

MODULE   = magic
MAGICDIR = ..
SRCS     = magicTop.c

include ${MAGICDIR}/defs.mak

EXTRA_LIBS = ${MAGICDIR}/bplane/libbplane.o \
	    ${MAGICDIR}/cmwind/libcmwind.o \
	    ${MAGICDIR}/commands/libcommands.o \
	    ${MAGICDIR}/database/libdatabase.o \
	    ${MAGICDIR}/dbwind/libdbwind.o \
	    ${MAGICDIR}/drc/libdrc.o \
	    ${MAGICDIR}/debug/libdebug.o \
	    ${MAGICDIR}/extract/libextract.o \
	    ${MAGICDIR}/graphics/libgraphics.o \
	    ${MAGICDIR}/select/libselect.o \
	    ${MAGICDIR}/textio/libtextio.o \
	    ${MAGICDIR}/tiles/libtiles.o \
	    ${MAGICDIR}/windows/libwindows.o \
	    ${MAGICDIR}/wiring/libwiring.o \
	    ${MAGICDIR}/resis/libresis.o \
	    ${MAGICDIR}/sim/libsim.o \
	    ${MAGICDIR}/netmenu/libnetmenu.o \
	    ${MAGICDIR}/plow/libplow.o \
	    ${MAGICDIR}/utils/libutils.o \
	    ${MAIN_EXTRA_LIBS}

BITMAPS = up.gif down.gif left.gif right.gif zoom.gif lock.xbm
DEST_XBM = $(BITMAPS:%=$(DESTDIR)${INSTALL_TCLDIR}/bitmaps/%)

# DFLAGS += -DCAD_DIR="${LIBDIR}"
LIBS   += ${GR_LIBS} ${READLINE_LIBS} -lm ${LD_EXTRA_LIBS} \
		${OA_LIBS} ${ZLIB_FLAG} ${TOP_EXTRA_LIBS}
CLEANS += tclmagic${SHDLIB_EXT} libtclmagic${SHDLIB_EXT}.a proto.magicrc

main: magic proto.magicrc

tcl-main: tclmagic${SHDLIB_EXT} proto.magicrc

tclmagic${SHDLIB_EXT}: ${EXTRA_LIBS}
	@echo --- making magic Tcl library \(tclmagic${SHDLIB_EXT}\)
	${RM} tclmagic${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} ${LD_RUN_PATH} \
		${EXTRA_LIBS} ${LD_EXTRA_LIBS} \
		-lc ${LIBS} ${LIB_SPECS} ${LDFLAGS}

proto.magicrc: proto.magicrc.in
	${MCPP} ${GR_DFLAGS} ${DFLAGS} proto.magicrc.in > proto.magicrc

install: $(DESTDIR)${INSTALL_BINDIR}/${MODULE}${EXEEXT} $(DESTDIR)${INSTALL_SYSDIR}/.magicrc \
	$(DESTDIR)${INSTALL_SYSDIR}/magicps.pro

install-tcl: $(DESTDIR)${INSTALL_TCLDIR}/tclmagic${SHDLIB_EXT} $(DESTDIR)${INSTALL_SYSDIR}/.magicrc \
	$(DESTDIR)${INSTALL_SYSDIR}/.initrc $(DESTDIR)${INSTALL_SYSDIR}/magicps.pro ${DEST_XBM}

$(DESTDIR)${INSTALL_TCLDIR}/tclmagic${SHDLIB_EXT}: tclmagic${SHDLIB_EXT}
	${RM} $(DESTDIR)${INSTALL_TCLDIR}/tclmagic${SHDLIB_EXT}
	${CP} tclmagic${SHDLIB_EXT} $(DESTDIR)${INSTALL_TCLDIR}/tclmagic${SHDLIB_EXT}

$(DESTDIR)${INSTALL_TCLDIR}/bitmaps/%: bitmaps/%
	${RM} $(DESTDIR)${INSTALL_TCLDIR}/bitmaps/$*
	${CP} bitmaps/$* $(DESTDIR)${INSTALL_TCLDIR}/bitmaps/$*

$(DESTDIR)${INSTALL_SYSDIR}/.magicrc: proto.magicrc
	${RM} $(DESTDIR)${INSTALL_SYSDIR}/.magicrc
	${CP} proto.magicrc $(DESTDIR)${INSTALL_SYSDIR}/.magicrc

$(DESTDIR)${INSTALL_SYSDIR}/.initrc: proto.initrc
	${RM} $(DESTDIR)${INSTALL_SYSDIR}/.initrc
	${CP} proto.initrc $(DESTDIR)${INSTALL_SYSDIR}/.initrc

$(DESTDIR)${INSTALL_SYSDIR}/magicps.pro: magicps.pro
	${RM} $(DESTDIR)${INSTALL_SYSDIR}/magicps.pro
	${CP} magicps.pro $(DESTDIR)${INSTALL_SYSDIR}/magicps.pro

include ${MAGICDIR}/rules.mak
