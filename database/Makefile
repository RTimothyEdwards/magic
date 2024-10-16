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
# database.h is managed by the toplevel Makefile, because it has a build dependency
# order affecting multiple modules that need it.   Both the creation time and the
# removal time (during 'clean') are managed by toplevel Makefile.
# if it was additionally removed by this clause here, it only causes bogus errors
# to be seen during some make operations due to race conditions caused by the
# unexpected removal by this clause in parallel MAKE execution of other modules
# that thought the file existed.  FWIW database.h should be created first (near the
# start of top level build) and removed last (near the end of a top level clean).
#CLEANS += database.h

# This is delegated back to the top level Makefile
database.h: ${MAGICDIR}/database/database.h.in
	${MAKE} -C ${MAGICDIR} database/database.h

include ${MAGICDIR}/rules.mak
