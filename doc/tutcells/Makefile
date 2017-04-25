#
# makefile for Magic documentation
#

MAGICDIR = ../..

include ${MAGICDIR}/defs.mak

TUTDIR = ${LIBDIR}/magic/tutorial

TUTFILES= m3a.mag maint2a.mag tut1.mag \
    tut2a.mag tut2b.mag tut2c.mag tut2d.mag \
    tut3a.mag tut3b.mag tut3c.mag tut3d.mag tut3e.mag \
    tut3f.mag tut3g.mag tut3h.mag \
    tut4a.mag tut4x.mag tut4y.mag tut4z.mag \
    tut5a.mag tut5b.mag \
    tut6a.mag tut6b.mag tut6c.mag tut6x.mag tut6y.mag \
    tut7a.mag tut7b.mag tut7b.net tut7c.mag tut7d.mag tut7d.net \
    tut8a.mag tut8b.mag tut8c.mag tut8d.mag tut8e.mag tut8f.mag \
    tut8g.mag tut8h.mag tut8i.mag tut8j.mag tut8k.mag tut8l.mag \
    tut8m.mag tut8n.mag tut8r.mag \
    tut9a.mag tut9b.mag tut9x.mag tut9y.mag \
    tut11a.mag tut11b.mag tut11c.mag tut11d.mag \
    tut11a.al tut11a.cmd tut11a.ext tut11a.nodes tut11a.sim \
    tut11b.ext tut11c.ext tut11d.ext 

INST_TUTFILES= $(TUTFILES:%=$(DESTDIR)${TUTDIR}/%)

install: $(DESTDIR)${TUTDIR} ${INST_TUTFILES}

$(DESTDIR)${TUTDIR}: make-tut-dirs

make-tut-dirs:
	${SCRIPTS}/mkdirs $(DESTDIR)${TUTDIR}

$(DESTDIR)${TUTDIR}/%: % $(DESTDIR)${TUTDIR}
	${CP} $* $(DESTDIR)${TUTDIR}/$*
