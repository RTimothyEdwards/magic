#
# Makefile      rcsid $Header: /usr/cvsroot/magic-8.0/database/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
#
 
MODULE   =  database
MAGICDIR =  ..
LIB_SRCS =
SRCS     =  DBbound.c DBcell.c DBcellbox.c DBcellcopy.c \
            DBcellname.c DBcellsrch.c DBcellsel.c DBcellsubr.c \
            DBconnect.c DBcount.c DBexpand.c DBio.c DBlabel.c DBlabel2.c \
            DBpaint2.c DBpaint.c DBprop.c DBtech.c DBtcontact.c \
	    DBtechname.c DBtpaint.c DBtpaint2.c DBtechtype.c \
            DBtiles.c DBtimestmp.c DBundo.c

include ${MAGICDIR}/defs.mak

LIB_OBJS += ${MAGICDIR}/tiles/libtiles.o ${MAGICDIR}/utils/libutils.o
CLEANS += database.h

include ${MAGICDIR}/rules.mak
