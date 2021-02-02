#
# rcsid $Header: /usr/cvsroot/magic-8.0/tcltk/Makefile,v 1.6 2010/06/24 12:37:56 tim Exp $
#

MODULE   = tcltk
MAGICDIR = ..
SRCS = tclmagic.c
OBJS = $(subst .c,.o,${SRCS})

include ${MAGICDIR}/defs.mak

DFLAGS += -DMAGIC_DATE="\"`date`\""
CLEANS += magic.sh magic.tcl magicexec magicdnull ext2spice.sh ext2sim.sh

TCL_FILES = \
	tkcon.tcl \
	tkshell.tcl \
	wrapper.tcl \
	console.tcl \
	techbuilder.tcl \
	cellmgr.tcl \
	drcmgr.tcl \
	libmgr.tcl \
	texthelper.tcl \
	tools.tcl \
	mazeroute.tcl \
	strip_reflibs.tcl \
	toolkit.tcl \
	toolkit_rev0.tcl \
	bsitools.tcl \
	socketcmd.tcl \
	readspice.tcl \
	magic.tcl

BIN_FILES = \
	$(DESTDIR)${INSTALL_BINDIR}/magic.sh \
	$(DESTDIR)${INSTALL_BINDIR}/ext2spice.sh \
	$(DESTDIR)${INSTALL_BINDIR}/ext2sim.sh

tcl-main: magicexec magicdnull magic.tcl magic.sh ext2spice.sh ext2sim.sh

install-tcl: magicexec magicdnull ${BIN_FILES} ${TCL_FILES}
	${RM} $(DESTDIR)${INSTALL_TCLDIR}/magicexec
	${CP} magicexec $(DESTDIR)${INSTALL_TCLDIR}/magicexec
	${RM} $(DESTDIR)${INSTALL_TCLDIR}/magicdnull
	${CP} magicdnull $(DESTDIR)${INSTALL_TCLDIR}/magicdnull
	(cd $(DESTDIR)${INSTALL_TCLDIR}; ${RM} ${TCL_FILES})
	for i in ${TCL_FILES}; do \
		${CP} $$i $(DESTDIR)${INSTALL_TCLDIR}; done
	(cd $(DESTDIR)${INSTALL_TCLDIR}; chmod 0755 tkcon.tcl tkshell.tcl)

magicexec: magicexec.c ${MAGICDIR}/defs.mak
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS_NOSTUB} ${LDFLAGS} magicexec.c \
		-o magicexec ${LD_RUN_PATH} ${LIB_SPECS_NOSTUB} ${LIBS} \
		${GR_LIBS}

magicdnull: magicdnull.c ${MAGICDIR}/defs.mak
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS_NOSTUB} ${LDFLAGS} magicdnull.c \
		-o magicdnull ${LD_RUN_PATH} ${LIB_SPECS_NOSTUB} ${LIBS} \
		${GR_LIBS}

magic.tcl: magic.tcl.in ${MAGICDIR}/defs.mak ${MAGICDIR}/VERSION
	sed -e /MAGIC_VERSION/s%MAGIC_VERSION%${MAGIC_VERSION}%g \
	    -e /MAGIC_REVISION/s%MAGIC_REVISION%${MAGIC_REVISION}%g \
	    magic.tcl.in > magic.tcl

magic.sh: magic.sh.in ${MAGICDIR}/defs.mak
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g \
	    -e /TCLLIB_DIR/s%TCLLIB_DIR%${TCL_LIB_DIR}%g \
	    -e /WISH_EXE/s%WISH_EXE%${WISH_EXE}%g magic.sh.in > magic.sh

ext2spice.sh: ext2spice.sh.in ${MAGICDIR}/defs.mak
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g \
	    ext2spice.sh.in > ext2spice.sh

ext2sim.sh: ext2sim.sh.in ${MAGICDIR}/defs.mak
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g \
	    ext2sim.sh.in > ext2sim.sh

$(DESTDIR)${INSTALL_TCLDIR}/%: %
	${RM} $(DESTDIR)${INSTALL_TCLDIR}/$*
	${CP} $* $(DESTDIR)${INSTALL_TCLDIR}/$*

$(DESTDIR)${INSTALL_BINDIR}/magic.sh: magic.sh
	${RM} $(DESTDIR)${INSTALL_BINDIR}/magic.sh $(DESTDIR)${INSTALL_BINDIR}/magic
	${CP} magic.sh $(DESTDIR)${INSTALL_BINDIR}/magic
	(cd $(DESTDIR)${INSTALL_BINDIR}; chmod 0755 magic)

$(DESTDIR)${INSTALL_BINDIR}/ext2spice.sh: ext2spice.sh
	${RM} $(DESTDIR)${INSTALL_BINDIR}/ext2spice
	${CP} ext2spice.sh $(DESTDIR)${INSTALL_BINDIR}/ext2spice
	(cd $(DESTDIR)${INSTALL_BINDIR}; chmod 0755 ext2spice)

$(DESTDIR)${INSTALL_BINDIR}/ext2sim.sh: ext2sim.sh
	${RM} $(DESTDIR)${INSTALL_BINDIR}/ext2sim
	${CP} ext2sim.sh $(DESTDIR)${INSTALL_BINDIR}/ext2sim
	(cd $(DESTDIR)${INSTALL_BINDIR}; chmod 0755 ext2sim)

# An additional dependency on the VERSION file
${OBJS}: ${MAGICDIR}/VERSION

include ${MAGICDIR}/rules.mak
