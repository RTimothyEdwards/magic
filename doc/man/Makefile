#
# makefile for Magic documentation
#

MAGICDIR = ../..

MANMACS= tmac.anc
TROFF=ditroff -h
TTROFF=ditroff -Ppsc -t
PSDIT=psdit
GRN=grn -Ppsc
TBL=tbl -Ppsc
EQN=eqn -Ppsc

include ${MAGICDIR}/defs.mak

MANFILES = \
    $(DESTDIR)${MANDIR}/man1/ext2spice.1 \
    $(DESTDIR)${MANDIR}/man1/extcheck.1 \
    $(DESTDIR)${MANDIR}/man1/ext2sim.1 \
    $(DESTDIR)${MANDIR}/man1/magic.1 \
    $(DESTDIR)${MANDIR}/man5/cmap.5 \
    $(DESTDIR)${MANDIR}/man5/displays.5 \
    $(DESTDIR)${MANDIR}/man5/dlys.5 \
    $(DESTDIR)${MANDIR}/man5/dstyle.5 \
    $(DESTDIR)${MANDIR}/man5/ext.5 \
    $(DESTDIR)${MANDIR}/man5/glyphs.5 \
    $(DESTDIR)${MANDIR}/man5/mag.5 \
    $(DESTDIR)${MANDIR}/man5/net.5 \
    $(DESTDIR)${MANDIR}/man5/sim.5

install: ${MANFILES}

$(DESTDIR)${MANDIR}:
	${SCRIPTS}/mkdirs $(DESTDIR)${MANDIR}

$(DESTDIR)${MANDIR}/man1: $(DESTDIR)${MANDIR}
	${SCRIPTS}/mkdirs $(DESTDIR)${MANDIR}/man1

$(DESTDIR)${MANDIR}/man1/%: % $(DESTDIR)${MANDIR}/man1
	${CP} $* $(DESTDIR)${MANDIR}/man1/$*

$(DESTDIR)${MANDIR}/man5: $(DESTDIR)${MANDIR}
	${SCRIPTS}/mkdirs $(DESTDIR)${MANDIR}/man5

$(DESTDIR)${MANDIR}/man5/%: % $(DESTDIR)${MANDIR}/man5
	${CP} $* $(DESTDIR)${MANDIR}/man5/$*

mans:
	${SCRIPTS}/printmans "${TROFF} ${MANMACS} -" *.1 *.5
