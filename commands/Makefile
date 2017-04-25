#
# rcsid $Header: /usr/cvsroot/magic-8.0/commands/Makefile,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
#

MODULE    = commands
MAGICDIR  = ..
SRCS      = CmdSubrs.c CmdAB.c CmdCD.c CmdE.c CmdFI.c \
            CmdLQ.c CmdRS.c CmdTZ.c CmdWizard.c CmdAuto.c

# Force the module to regenerate the symbolic link in the readline
# directory, as it may be needed by CmdFI.c (in the non-Tcl compile)

module: ${MAGICDIR}/readline/readline lib${MODULE}.o

${MAGICDIR}/readline/readline:
	@if ( ! test -f ${MAGICDIR}/readline/readline ) ; then \
	(cd ${MAGICDIR}/readline; ln -s `ls | grep readline` readline) ; \
	fi

include ${MAGICDIR}/defs.mak
include ${MAGICDIR}/rules.mak
