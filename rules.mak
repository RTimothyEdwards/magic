# You shouldn't need to edit this file, see the defs.mak file

module: lib${MODULE}.o

depend: ${DEPEND_FILE}

# New Depend file generating line (Tim Edwards, 1/25/06).  This gets around
# problems with gcc.  The purpose of "make depend" is to generate a list of
# all local dependencies, but gcc insists that anything that is in, for
# example, the /usr/X11R6 path should also be included.  The sed scripts
# (respectively) do:  1) remove comment lines generated by gcc, 2) remove
# any header (.h) files with an absolute path (beginning with "/"), and
# 3) remove isolated backslash-returns just to clean things up a bit.

${DEPEND_FILE}:
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ${DEPEND_FLAG} ${DEPSRCS} | \
	sed -e "/#/D" -e "/ \//s/ \/.*\.h//" -e "/  \\\/D" > ${DEPEND_FILE}

# Original Depend file generating line:
#	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ${DEPEND_FLAG} ${SRCS} > ${DEPEND_FILE}

${OBJS}: %.o: ${SRCS} ../database/database.h
	@echo --- compiling ${MODULE}/$*.o
	${RM} $*.o
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS}  -c $*.c

lib${MODULE}.o: ${OBJS}
	@echo --- linking lib${MODULE}.o
	${RM} lib${MODULE}.o
	${LINK} ${OBJS} -o lib${MODULE}.o ${EXTERN_LIBS}

lib: lib${MODULE}.a

lib${MODULE}.a: ${OBJS} ${LIB_OBJS}
	@echo --- archiving lib${MODULE}.a
	${RM} lib${MODULE}.a
	${AR} ${ARFLAGS} lib${MODULE}.a ${OBJS} ${LIB_OBJS}
	${RANLIB} lib${MODULE}.a

${MODULE}: lib${MODULE}.o ${EXTRA_LIBS}
	@echo --- building main ${MODULE}
	${RM} ${MODULE}
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} lib${MODULE}.o ${EXTRA_LIBS} -o ${MODULE} ${LIBS}

${DESTDIR}${BINDIR}/${MODULE}${EXEEXT}: ${MODULE}${EXEEXT}
	${RM} ${DESTDIR}${BINDIR}/${MODULE}${EXEEXT}
	${CP} ${MODULE}${EXEEXT} ${DESTDIR}${BINDIR}

.PHONY: clean
clean:
	${RM} ${CLEANS}

tags: ${SRCS} ${LIB_SRCS}
	ctags ${SRCS} ${LIB_SRCS}

# If "include" is unqualified, it will attempt to build all targets of "include"
# So, we gate the include, only running it if "clean" is not the target
ifneq ($(MAKECMDGOALS),clean)
include ${DEPEND_FILE}
endif
