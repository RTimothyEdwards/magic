#
# rcsid $Header: /usr/cvsroot/magic-8.0/graphics/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
#

MODULE    = graphics
MAGICDIR  = ..
BASE_SRCS = grMain.c grLock.c grDStyle.c grText.c grCMap.c grClip.c \
            grGlyphs.c grNull.c W3Dmain.c
X11_SRCS  = grX11su1.c grX11su2.c grX11su3.c grX11su4.c grX11su5.c
TK_SRCS   = grTk1.c grTk2.c grTk3.c grTk4.c grTk5.c
TOGL_SRCS = grTOGL1.c grTOGL2.c grTOGL3.c grTOGL4.c grTOGL5.c
OGL_SRCS  = grOGL1.c grOGL2.c grOGL3.c grOGL4.c grOGL5.c
TCAIRO_SRCS = grTCairo1.c grTCairo2.c grTCairo3.c grTCairo4.c grTCairo5.c

GLYPHS = bw.glyphs color.glyphs
OUTLINE_FONTS = FreeSerif.pt3 FreeSans.pt3 FreeMono.pt3

TKCOMMON_SRCS  = grTkCommon.c
X11THREAD_SRCS = grX11thread.c
X11HELPER_SRCS = X11Helper.c
X11HELP_PROG   = XHelper7

# Sets ${GR_SRCS} to one or more of the above
include ${MAGICDIR}/defs.mak

SRCS    = ${BASE_SRCS} ${GR_SRCS}
DEPSRCS = ${BASE_SRCS}
ifeq (${MAKE_X11},1)
DEPSRCS += ${X11_SRCS} ${TK_SRCS}
endif
ifeq (${MAKE_GLU},1)
DEPSRCS += ${TOGL_SRCS}
endif
ifeq (${MAKE_X11},1)
DEPSRCS += ${TKCOMMON_SRCS}
endif
ifeq (${MAKE_GL},1)
DEPSRCS += ${OGL_SRCS}
endif
DEPSRCS += ${X11HELPER_SRCS} ${X11THREAD_SRCS}
ifeq (${MAKE_CAIRO},1)
DEPSRCS += ${TCAIRO_SRCS}
endif
OBJS    = ${BASE_SRCS:.c=.o} ${GR_SRCS:.c=.o}
CFLAGS += ${GR_CFLAGS}
DFLAGS += ${GR_DFLAGS}
CLEANS += ${GR_HELPER_PROG} *.o
LIBS   += ${GR_LIBS}

main: ${GR_HELPER_PROG}

${GR_HELPER_PROG}: ${GR_HELPER_SRCS}
	@echo --- building main ${GR_HELPER_PROG}
	${RM} ${GR_HELPER_PROG}
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ${GR_HELPER_SRCS} \
		-o ${GR_HELPER_PROG} ${LIBS}

install: $(DESTDIR)${INSTALL_BINDIR}/${GR_HELPER_PROG} glyphs fonts

install-tcl: glyphs fonts

$(DESTDIR)${INSTALL_BINDIR}/${GR_HELPER_PROG}: ${GR_HELPER_PROG}
	${RM} $(DESTDIR)${INSTALL_BINDIR}/${GR_HELPER_PROG}
	${CP} ${GR_HELPER_PROG} $(DESTDIR)${INSTALL_BINDIR}

glyphs: ${GLYPHS}
	for i in ${GLYPHS}; do \
	${RM} $(DESTDIR)${INSTALL_SYSDIR}/$$i; \
	${CP} $$i $(DESTDIR)${INSTALL_SYSDIR}; done

fonts: ${OUTLINE_FONTS}
	for i in ${OUTLINE_FONTS}; do \
	${RM} $(DESTDIR)${INSTALL_SYSDIR}/$$i; \
	${CP} $$i $(DESTDIR)${INSTALL_SYSDIR}; done

include ${MAGICDIR}/rules.mak
