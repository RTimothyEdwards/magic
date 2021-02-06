#######################################################################
#
#                     MOSIS distribution Version 8.2
#
#######################################################################
#
# The intent in magic version 7.2 was to convert all the "c" style
# files to m4.  However, given the number of files, and the fact that
# they are out of date and should all be replaced anyway, this doesn't
# seem to be worth the effort.  Therefore, some trivial things (like
# minimum.tech) have been converted to m4 and the rest have been left
# alone.
#
#######################################################################

MAGICDIR = ..

include ${MAGICDIR}/defs.mak

SC_PP = ${MCPP} -I./extract_template

FILES = mos.7bit.dstyle mos.7bit.std.cmap \
	mos.24bit.dstyle mos.24bit.std.cmap \
	mos.7bit.mraster_dstyle mos.7bit.mraster.cmap \
	mos.OpenGL.dstyle mos.OpenGL.std.cmap
TECHFILES = minimum.tech gdsquery.tech scmos.tech scmos-tm.tech \
	scmos-sub.tech scmosWR.tech nmos.tech

CIFin   = cif_template/objs/CIFin
CIFout  = cif_template/objs/CIFout
ICIFin  = cif_template/objs/IBMCIFin
ICIFout = cif_template/objs/IBMCIFout
HCIFin  = cif_template/objs/TMCIFin
HCIFout = cif_template/objs/TMCIFout
SCIFin  = cif_template/objs/SUBCIFin
SCIFout = cif_template/objs/SUBCIFout

OBJS =$(CIFin) $(CIFout) $(HCIFin) $(HCIFout) $(SCIFin) $(SCIFout)

DEPEND = scmos.tech.in \
		extract_template/scmosExt.tech.in \
		extract_template/scmosExt26b-sub.tech.in \
		extract_template/scmosExt060_orb.tech.in \
		extract_template/scmosExt080.tech.in \
		extract_template/scmosExt100.tech.in \
		extract_template/scmosExt34.tech.in \
		extract_template/scmosExt14b-sub.tech.in \
		extract_template/scmosExtDiag.tech.in \
		extract_template/scmosExt14b-tm.tech.in 

all: $(OBJS) $(DEPEND) ${TECHFILES}

install-tcl: all
	for i in ${FILES} ${TECHFILES}; do \
		${CP} $$i $(DESTDIR)${INSTALL_SYSDIR}; done

install: all
	for i in ${FILES} ${TECHFILES}; do \
		${CP} $$i $(DESTDIR)${INSTALL_SYSDIR}; done

scmos.tech: $(OBJS)
	$(MSED) scmos.tech.in > scmos.tech.out
	$(SC_PP) -DV5 -DSTANDARD scmos.tech.out > scmos.tech
	$(RM) scmos.tech.out

scmos-tm.tech: $(OBJS)
	$(MSED) scmos.tech.in > scmos.tech.out
	cat scmos.tech.in > scmos.tech.out
	$(SC_PP) -DV5 -DHPTECH -DTIGHTMETAL scmos.tech.out > scmos-tm.tech
	$(RM) scmos.tech.out

scmos-sub.tech: $(OBJS)
	$(MSED) scmos.tech.in > scmos.tech.out
	cat scmos.tech.in > scmos.tech.out
	$(SC_PP) -DV5 -DSUBMICRON scmos.tech.out > scmos-sub.tech
	$(RM) scmos.tech.out

scmosWR.tech: $(OBJS)
	$(MSED) scmos.tech.in > scmos.tech.out
	cat scmos.tech.in > scmos.tech.out
	$(SC_PP) -DV5 -DSTANDARD -DWELL_ROUTE_CHECK scmos.tech.out > scmosWR.tech
	$(RM) scmos.tech.out

minimum.tech: minimum.tech.in
	$(SC_PP) minimum.tech.in > minimum.tech

gdsquery.tech: gdsquery.tech.in
	$(SC_PP) gdsquery.tech.in > gdsquery.tech

nmos.tech: nmos.tech.in
	$(CP) nmos.tech.in nmos.tech

$(CIFin):
$(CIFout):
$(ICIFin):
$(ICIFout):
$(HCIFin):
$(HCIFout):
$(SCIFin):
$(SCIFout):
	cd cif_template; ${MAKE} clean; ${MAKE};

$(DESTDIR)${INSTALL_SYSDIR}/%: %
	cp $* $(DESTDIR)${INSTALL_SYSDIR}/$*

clean:;
	-rm -f *.tech *.tech27
	cd cif_template && ${MAKE} clean;
