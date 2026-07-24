# You shouldn't need to edit this file, see the defs.mak file

module: lib${MODULE}.o

# Dependencies are now generated automatically as a side effect of each compile
# (see the %.o rule below); "make depend" is a no-op kept only so scripts/habits
# that still invoke it do not error.
.PHONY: depend
depend:

# Compile a source into an object.  When the compiler supports it
# (AUTODEP_FLAGS = "-MMD -MP"), also write .deps/<stem>.d listing the non-system
# headers this object used -- generated as a side effect of the compile, so there
# is no separate serial "make depend" pass and it parallelises for free.  -MMD
# already drops system headers (the job the old sed did), and -MP adds a phony
# target per header so a later removed/renamed header does not break the build.
%.o: %.c
	@echo --- compiling ${MODULE}/$*.o
	@test -z "${AUTODEP_FLAGS}" || mkdir -p .deps
	${RM} $*.o
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ${AUTODEP_FLAGS} $(if ${AUTODEP_FLAGS},-MF .deps/$*.d) -c $< -o $@

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
	${RM} -r .deps

tags: ${SRCS} ${LIB_SRCS}
	ctags $(addprefix $(srcdir)/,${SRCS} ${LIB_SRCS})

# Per-file dependency fragments written by the %.o rule above.  Best effort: none
# exist on a first build (everything compiles anyway); on later builds they cause
# a recompile when a used header changes.  `-include` tolerates their absence and
# the `-MP` phony header targets tolerate a removed/renamed header.
-include $(wildcard .deps/*.d)
