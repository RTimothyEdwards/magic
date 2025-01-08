#
# rcsid $Header: /usr/cvsroot/magic-8.0/Makefile,v 1.1.1.1 2008/02/03 20:43:49 tim Exp $
#

MAGICDIR   = .
PROGRAMS   = magic
TECHS      = scmos
LIBRARIES  = database utils extflat
MODULES    = bplane cmwind commands database dbwind debug drc extflat \
	     extract graphics netmenu plow resis select sim textio tiles \
	     utils windows wiring

# This was `cat VERSION`
VERSION    := $(shell cat ${MAGICDIR}/VERSION)

MAKEFLAGS  =
INSTALL_CAD_DIRS = windows doc ${TECHS}

-include defs.mak

all:	$(ALL_TARGET) techs

standard:
	@echo --- errors and warnings logged in file make.log
	@${MAKE} mains

tcl:
	@echo --- errors and warnings logged in file make.log
	@${MAKE} tcllibrary

force: clean all

defs.mak:
	@echo No \"defs.mak\" file found.  Run "configure" to make one.
	@exit 1

config:
	${MAGICDIR}/configure

tcllibrary: database/database.h modules
	@echo --- making Tcl shared libraries
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} tcl-main) || exit 1; done

mains: database/database.h modules libs
	@echo --- making main programs
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} main) || exit 1; done

database/database.h: ${MAGICDIR}/database/database.h.in
	@echo --- making header file database/database.h
	${SCRIPTS}/makedbh ${MAGICDIR}/database/database.h.in database/database.h

modules: database/database.h depend
	@echo --- making modules
	for dir in ${MODULES} ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} module) || exit 1; done

libs: database/database.h depend
	@echo --- making libraries
	for dir in ${LIBRARIES}; do \
		(cd $$dir && ${MAKE} lib) || exit 1; done

#
# extcheck - utility tool
# net2ir   - utility tool
# oa       - disabled (needs 'clean' target renaming)
SUBDIRS = bplane cmwind commands database dbwind debug drc extflat extract graphics \
	  magic netmenu plow resis select sim textio tiles utils windows wiring

BUNDLED_MODULES = readline lisp

# Unique list of all subdir that might have Depend file, we have to deduplicate otherwise
# MAKE will warning loudly.  This list is somewhat empty when defs.mak does not exist
SUBDIRS_FILTERED := $(shell echo ${MODULES} ${PROGRAMS} ${SUBDIRS} | tr ' ' '\n' | sort | uniq)

$(addsuffix /Depend, ${SUBDIRS_FILTERED}): database/database.h
	@echo --- making dependencies
	${MAKE} -C $(dir $@) depend

.PHONY: depend
depend: defs.mak $(addsuffix /Depend, ${SUBDIRS_FILTERED})

.PHONY: techs
techs: depend
	@echo --- making techs
	for dir in ${TECHS}; do \
		(cd $$dir && ${MAKE} all) || exit 1; done

install: $(INSTALL_TARGET)

install-magic:
	@echo --- installing executable to $(DESTDIR)${INSTALL_BINDIR}
	@echo --- installing runtime files to $(DESTDIR)${INSTALL_LIBDIR}
	@${MAKE} install-real

install-real: install-dirs
	for dir in ${INSTALL_CAD_DIRS}; do \
		(cd $$dir && ${MAKE} install); done
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} install); done

install-tcl-dirs:
	${MAGICDIR}/scripts/mkdirs $(DESTDIR)${INSTALL_BINDIR} \
	$(DESTDIR)${INSTALL_MANDIR} $(DESTDIR)${INSTALL_SYSDIR} \
	$(DESTDIR)${INSTALL_TCLDIR} $(DESTDIR)${INSTALL_TCLDIR}/bitmaps

install-dirs:
	${MAGICDIR}/scripts/mkdirs $(DESTDIR)${INSTALL_BINDIR} \
	$(DESTDIR)${INSTALL_MANDIR} $(DESTDIR)${INSTALL_SYSDIR} \
	$(DESTDIR)${INSTALL_SCMDIR}

install-tcl:
	@echo --- installing executable to $(DESTDIR)${INSTALL_BINDIR}
	@echo --- installing runtime files to $(DESTDIR)${INSTALL_LIBDIR}
	@${MAKE} install-tcl-real

install-tcl-real: install-tcl-dirs
	for dir in ${INSTALL_CAD_DIRS} ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} install-tcl); done

clean:
	for dir in ${SUBDIRS_FILTERED} ${TECHS} ${BUNDLED_MODULES}; do \
		(cd $$dir && ${MAKE} clean); done
	${RM} *.tmp */*.tmp *.sav */*.sav *.log TAGS tags

distclean:
	touch defs.mak
	@${MAKE} clean
	${RM} defs.mak old.defs.mak ${MAGICDIR}/scripts/defs.mak
	${RM} ${MAGICDIR}/scripts/default.conf
	${RM} ${MAGICDIR}/scripts/config.log ${MAGICDIR}/scripts/config.status
	${RM} database/database.h
	${RM} scripts/magic.spec magic-${VERSION} magic-${VERSION}.tgz
	${RM} *.log

dist:
	${RM} scripts/magic.spec magic-${VERSION} magic-${VERSION}.tgz
	sed -e /@VERSION@/s%@VERSION@%${VERSION}% \
	    scripts/magic.spec.in > scripts/magic.spec
	${LN} -nsf . magic-${VERSION}
	tar zchvf magic-${VERSION}.tgz --exclude CVS \
	    --exclude magic-${VERSION}/magic-${VERSION} \
	    --exclude magic-${VERSION}/magic-${VERSION}.tgz \
	    magic-${VERSION}

clean-mains:
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${RM} $$dir); done

tags:
	${RM} tags
	find . ${MODULES} ${PROGRAMS} -name "*.[ch]" -maxdepth 1 | xargs ctags -o tags

TAGS:
	${RM} TAGS
	find . ${MODULES} ${PROGRAMS} -name "*.[ch]" -maxdepth 1 | xargs etags -o TAGS

setup-git:
	git config --local include.path ../.gitconfig
	git stash save
	${RM} .git/index
	git checkout HEAD -- "$$(git rev-parse --show-toplevel)"
	git stash pop
