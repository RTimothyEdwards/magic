#
# rcsid $Header$
#

MODULE    = windows
MAGICDIR  = ..
SRCS      = windClient.c windCmdAM.c windCmdNR.c windCmdSZ.c \
            windSend.c windDebug.c windDisp.c windMain.c windMove.c \
            windSearch.c windTrans.c windView.c 

GLYPHS    = windows7.glyphs windows11.glyphs windows14.glyphs windows22.glyphs 
FONTS	  = vfont.B.12 vfont.I.12 vfont.R.8

include ${MAGICDIR}/defs.mak
include ${MAGICDIR}/rules.mak

install-tcl:
	@echo --- installing glyphs to $(DESTDIR)${INSTALL_SYSDIR}
	for i in ${GLYPHS} ${FONTS}; do \
		(cd $(DESTDIR)${INSTALL_SYSDIR} && ${RM} $$i); \
		${CP} $$i $(DESTDIR)${INSTALL_SYSDIR}; done

install:
	@echo --- installing glyphs to $(DESTDIR)${INSTALL_SYSDIR}
	for i in ${GLYPHS} ${FONTS}; do \
		(cd $(DESTDIR)${INSTALL_SYSDIR} && ${RM} $$i); \
		${CP} $$i $(DESTDIR)${INSTALL_SYSDIR}; done
