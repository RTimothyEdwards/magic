#
# rcsid $Header: /usr/cvsroot/magic-8.0/tcltk/Makefile,v 1.6 2010/06/24 12:37:56 tim Exp $
#

MODULE   = tcltk
MAGICDIR = ..
SRCS = tclmagic.c

include ${MAGICDIR}/defs.mak

DFLAGS += -DMAGIC_DATE="\"`date`\""
CLEANS += magic.sh magic.tcl magicexec magicdnull

TCL_FILES = \
	tkcon.tcl \
	tkshell.tcl \
	wrapper.tcl \
	console.tcl \
	techbuilder.tcl \
	cellmgr.tcl \
	libmgr.tcl \
	texthelper.tcl \
	tools.tcl \
	mazeroute.tcl \
	strip_reflibs.tcl \
	drc.tcl \
	toolkit.tcl \
	toolkit_rev0.tcl \
	bsitools.tcl \
	socketcmd.tcl \
	magic.tcl

BIN_FILES = \
	$(DESTDIR)${BINDIR}/magic.sh \
	$(DESTDIR)${BINDIR}/ext2spice.sh \
	$(DESTDIR)${BINDIR}/ext2sim.sh

tcl-main: magicexec magicdnull magic.tcl magic.sh ext2spice.sh ext2sim.sh

install-tcl: magicexec magicdnull ${BIN_FILES} ${TCL_FILES}
	${RM} $(DESTDIR)${TCLDIR}/magicexec
	${CP} magicexec $(DESTDIR)${TCLDIR}/magicexec
	${RM} $(DESTDIR)${TCLDIR}/magicdnull
	${CP} magicdnull $(DESTDIR)${TCLDIR}/magicdnull
	(cd $(DESTDIR)${TCLDIR}; ${RM} ${TCL_FILES})
	for i in ${TCL_FILES}; do \
		${CP} $$i $(DESTDIR)${TCLDIR}; done
	(cd $(DESTDIR)${TCLDIR}; chmod 0755 tkcon.tcl tkshell.tcl)

magicexec: magicexec.c ${MAGICDIR}/defs.mak
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ${LDFLAGS} magicexec.c \
		-o magicexec ${LD_RUN_PATH} ${LIBS} ${LIB_SPECS}

magicdnull: magicdnull.c ${MAGICDIR}/defs.mak
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ${LDFLAGS} magicdnull.c \
		-o magicdnull ${LD_RUN_PATH} ${LIBS} ${LIB_SPECS}

magic.tcl: magic.tcl.in ${MAGICDIR}/defs.mak
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g  \
	    -e /MAGIC_VERSION/s%MAGIC_VERSION%${MAGIC_VERSION}%g \
	    -e /MAGIC_REVISION/s%MAGIC_REVISION%${MAGIC_REVISION}%g \
            -e /SHDLIB_EXT/s%SHDLIB_EXT%${SHDLIB_EXT}%g magic.tcl.in > magic.tcl

magic.sh: magic.sh.in ${MAGICDIR}/defs.mak
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g \
	    -e /TCLLIB_DIR/s%TCLLIB_DIR%${TCL_LIB_DIR}%g \
	    -e /WISH_EXE/s%WISH_EXE%${WISH_EXE}%g magic.sh.in > magic.sh

ext2spice.sh: ext2spice.sh.in ${MAGICDIR}/defs.mak
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g ext2spice.sh.in > ext2spice.sh

ext2sim.sh: ext2sim.sh.in ${MAGICDIR}/defs.mak
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g ext2sim.sh.in > ext2sim.sh

$(DESTDIR)${TCLDIR}/%: %
	${RM} $(DESTDIR)${TCLDIR}/$*
	${CP} $* $(DESTDIR)${TCLDIR}/$*

$(DESTDIR)${BINDIR}/magic.sh: magic.sh
	${RM} $(DESTDIR)${BINDIR}/magic.sh $(DESTDIR)${BINDIR}/magic
	${CP} magic.sh $(DESTDIR)${BINDIR}/magic
	(cd $(DESTDIR)${BINDIR}; chmod 0755 magic)

$(DESTDIR)${BINDIR}/ext2spice.sh: ext2spice.sh
	${RM} $(DESTDIR)${BINDIR}/ext2spice
	${CP} ext2spice.sh $(DESTDIR)${BINDIR}/ext2spice
	(cd $(DESTDIR)${BINDIR}; chmod 0755 ext2spice)

$(DESTDIR)${BINDIR}/ext2sim.sh: ext2sim.sh
	${RM} $(DESTDIR)${BINDIR}/ext2sim
	${CP} ext2sim.sh $(DESTDIR)${BINDIR}/ext2sim
	(cd $(DESTDIR)${BINDIR}; chmod 0755 ext2sim)

include ${MAGICDIR}/rules.mak
