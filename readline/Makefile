MAGICDIR = ..

include ${MAGICDIR}/defs.mak

module: readline readline/libreadline.a readline/libhistory.a

depend:

# NOTE:  Previously, source code was in a directory readline-4.3 and
# recipe "readline" created a symbolic link to it.  This symbolic
# link has been removed, and code is now just in real directory
# "readline".

readline:

readline/libhistory.a: readline/libreadline.a

readline/libreadline.a:
	@echo --- building GNU readline
	(cd readline; CC="${CC}" CFLAGS="${CFLAGS}" LOCAL_DEFS="${READLINE_DEFS}" ./configure --disable-shared ; ${MAKE})

clean:
	@if ( test -f readline/Makefile ) ; then \
	(cd readline && ${MAKE} distclean) ; \
	fi
	${RM} readline

