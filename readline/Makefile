MAGICDIR = ..

include ${MAGICDIR}/defs.mak

module: readline-create-symlinks libreadline.a libhistory.a

depend:

# NOTE:  Previously, source code was in a directory readline-4.3 and
# recipe "readline" created a symbolic link to it.  This symbolic
# link has been removed, and code is now just in real directory
# "readline".

#
# The symlink resolves where the source for readline is coming from,
# so it is a well known path ${top_builddir}/readline/readline
#
# For the purpose of #include <readline/readline/readline.h> to work
#
# Then we do an out of tree build in build-readline subdir in $builddir
#
# Then we symlink the output archives inside build-readline to
#  ${top_builddir}/readline/lib*.a so they are on a well known path
# for the project to find and link against.
#

.PHONY: readline-create-symlinks
readline-create-symlinks:
	@if ! test -e readline ; then \
	readline_target_dir=$$(ls | grep readline | grep -v "^readline$$" | tail -n1); \
	if test -d "$${readline_target_dir}" ; then \
	$(LN) -nsf "$${readline_target_dir}" ./readline ; \
	fi; \
	fi

libhistory.a: build-readline/libhistory.a
	@if ! test -e libhistory.a ; then \
	    $(LN) -nsf build-readline/libreadline.a libhistory.a ; \
	fi

libreadline.a: build-readline/libreadline.a
	@if ! test -e libreadline.a ; then \
	    $(LN) -nsf build-readline/libreadline.a libreadline.a ; \
	fi

build-readline/libreadline.a build-readline/libhistory.a: readline-create-symlinks build-readline

.PHONY: build-readline
build-readline:
	@if ! test -e build-readline/Makefile ; then \
	echo --- building GNU readline ; \
	$(MKDIR) -p build-readline ; \
	(cd build-readline && CC="${CC}" CFLAGS="${CFLAGS}" LOCAL_DEFS="${READLINE_DEFS}" \
	    "../readline/configure" --disable-shared) || exit 1 ; \
	fi
	@(cd build-readline && ${MAKE}) || exit 1

clean:
	@if test -f build-readline/Makefile ; then \
	    (cd build-readline && ${MAKE} distclean) ; \
	fi
	@for d in build-readline/doc build-readline/examples build-readline/shlib build-readline ; do \
	    test -d "$$d" && $(RMDIR) "$$d" 2>/dev/null ; \
	done ; true
	$(RM) libhistory.a
	$(RM) libreadline.a
	$(RM) readline

