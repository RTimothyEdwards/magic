#
# makefile for Magic documentation
#

MAGICDIR = ..
PSDIR = psfiles

include ${MAGICDIR}/defs.mak

PRINTER=verlag
TROFF=ditroff -P${PRINTER}
TTROFF=ditroff -Ppsc -t
PSDIT=psdit
GRN=grn -Ppsc
TBL=tbl -Ppsc
EQN=eqn -Ppsc

install-tcl:
	cd latexfiles && ${MAKE} install
	cd man && ${MAKE} install
	cd tutcells && ${MAKE} install
	cd html && ${MAKE} install

install:
	cd latexfiles && ${MAKE} install
	cd man && ${MAKE} install
	cd tutcells && ${MAKE} install
	cd html && ${MAKE} install

print_only:  force
	lpr -h ${PSDIR}/copyright.ps
	lpr -h ${PSDIR}/tofc.ps
	lpr -h ${PSDIR}/introduction.ps
	lpr -h ${PSDIR}/tut1.ps
	lpr -h ${PSDIR}/tut2.ps
	lpr -h ${PSDIR}/tut3.ps
	lpr -h ${PSDIR}/tut4.ps
	lpr -h ${PSDIR}/tut5.ps
	lpr -h ${PSDIR}/tut6.ps
	lpr -h ${PSDIR}/tut7.ps
	lpr -h ${PSDIR}/tut8.ps
	lpr -h ${PSDIR}/tut9.ps
	lpr -h ${PSDIR}/tut10.ps
	lpr -h ${PSDIR}/tut11.ps
	lpr -h ${PSDIR}/maint1.ps
	lpr -h ${PSDIR}/maint2.ps
	lpr -h ${PSDIR}/maint3.ps
	lpr -h ${PSDIR}/maint4.ps
	lpr -h ${PSDIR}/otherreports.ps
	lpr -h ${PSDIR}/Addendum6.5.ps

print:  postscript print_only

postscript: copyright.ps introduction.ps tofc.ps tut1.ps tut2.ps tut3.ps \
	tut4.ps tut5.ps tut6.ps tut7.ps tut8.ps tut9.ps tut10.ps tut11.ps \
	maint1.ps maint2.ps maint3.ps maint4.ps Addendum6.5.ps
	cd nmos; make postscript PRINTER=psc "PSDIT=${PSDIT}"
	cd scmos; make postscript PRINTER=psc "PSDIT=${PSDIT}"
