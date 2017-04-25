MAGICDIR = ..

include ${MAGICDIR}/defs.mak

module: readline readline/libreadline.a readline/libhistory.a

depend:

readline:
	@if ( ! test -f readline/readline ) ; then \
	ln -s `ls | grep readline` readline ; \
	fi

readline/libhistory.a: readline/libreadline.a

readline/libreadline.a:
	@echo --- building GNU readline
	(cd readline; CC="${CC}" CFLAGS="${CFLAGS}" LOCAL_DEFS="${READLINE_DEFS}" ./configure --disable-shared ; ${MAKE})

clean:
	@if ( test -f readline/Makefile ) ; then \
	(cd readline && ${MAKE} distclean) ; \
	fi
	${RM} readline

