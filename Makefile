#
# rcsid $Header: /usr/cvsroot/magic-8.0/Makefile,v 1.1.1.1 2008/02/03 20:43:49 tim Exp $
#

MAGICDIR   = .
PROGRAMS   = magic
TECH       = scmos
LIBRARIES  = database utils extflat
MODULES    = cmwind commands database dbwind debug drc extflat extract \
	     graphics netmenu plow resis select sim textio tiles utils \
	     windows wiring

MAKEFLAGS  =
INSTALL_CAD_DIRS = windows doc ${TECH}

include defs.mak

all:	$(ALL_TARGET)

standard:
	@echo --- errors and warnings logged in file make.log
	@${MAKE} mains 2>&1 | tee -a make.log | egrep -i "(.c:|Stop.|---)"

tcl:
	@echo --- errors and warnings logged in file make.log
	@${MAKE} tcllibrary 2>&1 | tee -a make.log | egrep -i "(.c:|Stop.|---)"

force: clean all

defs.mak:
	@echo No \"defs.mak\" file found.  Run "configure" to make one.

config:
	${MAGICDIR}/configure

tcllibrary: database/database.h modules
	@echo --- making Tcl shared libraries
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} tcl-main); done

mains: database/database.h modules libs
	@echo --- making main programs
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} main); done

database/database.h: database/database.h.in
	@echo --- making header file database/database.h
	${SCRIPTS}/makedbh database/database.h.in database/database.h

modules:
	@echo --- making modules
	for dir in ${MODULES} ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} module); done

libs:
	@echo --- making libraries
	for dir in ${LIBRARIES}; do \
		(cd $$dir && ${MAKE} lib); done

depend:	database/database.h
	${RM} */Depend
	for dir in ${MODULES} ${UNUSED_MODULES} ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} depend); done

install: $(INSTALL_TARGET)

install-magic:
	@echo --- installing executable to $(DESTDIR)${BINDIR}
	@echo --- installing runtime files to $(DESTDIR)${LIBDIR}
	@${MAKE} install-real 2>&1 >> install.log

install-real: install-dirs
	for dir in ${INSTALL_CAD_DIRS}; do \
		(cd $$dir && ${MAKE} install); done
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} install); done

install-tcl-dirs:
	${MAGICDIR}/scripts/mkdirs $(DESTDIR)${BINDIR} $(DESTDIR)${MANDIR} \
		$(DESTDIR)${SYSDIR} $(DESTDIR)${TCLDIR} $(DESTDIR)${TCLDIR}/bitmaps

install-dirs:
	${MAGICDIR}/scripts/mkdirs $(DESTDIR)${BINDIR} $(DESTDIR)${MANDIR} \
		$(DESTDIR)${SYSDIR} $(DESTDIR)${SCMDIR}

install-tcl:
	@echo --- installing executable to $(DESTDIR)${BINDIR}
	@echo --- installing runtime files to $(DESTDIR)${LIBDIR}
	@${MAKE} install-tcl-real 2>&1 >> install.log

install-tcl-real: install-tcl-dirs
	for dir in ${INSTALL_CAD_DIRS} ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} install-tcl); done

clean:
	for dir in ${MODULES} ${PROGRAMS} ${TECH} ${UNUSED_MODULES}; do \
		(cd $$dir && ${MAKE} clean); done
	${RM} *.tmp */*.tmp *.sav */*.sav *.log TAGS tags

distclean:
	touch defs.mak
	@${MAKE} clean
	${RM} defs.mak old.defs.mak ${MAGICDIR}/scripts/defs.mak
	${RM} ${MAGICDIR}/scripts/default.conf
	${RM} ${MAGICDIR}/scripts/config.log ${MAGICDIR}/scripts/config.status
	${RM} scripts/magic.spec magic-`cat VERSION` magic-`cat VERSION`.tgz
	${RM} *.log

dist:
	${RM} scripts/magic.spec magic-`cat VERSION` magic-`cat VERSION`.tgz
	sed -e /@VERSION@/s%@VERSION@%`cat VERSION`% \
	    scripts/magic.spec.in > scripts/magic.spec
	ln -nsf . magic-`cat VERSION`
	tar zchvf magic-`cat VERSION`.tgz --exclude CVS \
	    --exclude magic-`cat VERSION`/magic-`cat VERSION` \
	    --exclude magic-`cat VERSION`/magic-`cat VERSION`.tgz \
	    magic-`cat VERSION`

clean-mains:
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${RM} $$dir); done

tags:
	${RM} tags
	find . ${MODULES} ${PROGRAMS} -name "*.[ch]" -maxdepth 1 | xargs ctags -o tags

TAGS: 
	${RM} TAGS
	find . ${MODULES} ${PROGRAMS} -name "*.[ch]" -maxdepth 1 | xargs etags -o TAGS
