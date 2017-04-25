#
# rcsid $Header: /usr/cvsroot/magic-8.0/utils/Makefile,v 1.2 2008/12/04 16:21:44 tim Exp $
#

MODULE    = utils
MAGICDIR  = ..
LIB_SRCS  = LIBdbio.c LIBmain.c LIBtextio.c
SRCS      = args.c child.c dqueue.c finddisp.c flock.c flsbuf.c fraction.c \
            geometry.c getrect.c hash.c heap.c list.c lookup.c lookupany.c \
            lookupfull.c macros.c main.c malloc.c match.c maxrect.c netlist.c \
	    niceabort.c parser.c path.c pathvisit.c port.c printstuff.c \
	    signals.c stack.c strdup.c runstats.c set.c show.c tech.c \
	    touchtypes.c undo.c

include ${MAGICDIR}/defs.mak

# The following required to pass XLIB to the macros.c routines.
DFLAGS += ${GR_DFLAGS}
CFLAGS += ${GR_CFLAGS}

include ${MAGICDIR}/rules.mak
