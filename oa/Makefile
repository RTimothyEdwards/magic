#
# OpenAccess module Makefile
#

MODULE    = oa
MAGICDIR  = ..
SRCS      = oa.c
CXXSRCS	  = magicInit.cpp magicOA.cpp

include ${MAGICDIR}/defs.mak

#----------------------------------------

ifeq ($(OA),)
OA := fake_oa_stub
endif
# -I$(srcdir): this module's C++ sources/headers (source tree)
# -I${MAGICDIR}: build top, for the generated database/database.h
# -I${MAGICSRC}: source top, for "<module>/<header>.h" includes
CXX_INCLUDES = -I$(srcdir) -I${MAGICDIR} -I${MAGICSRC} -I${OA}/oa/include

DEPS_MAGIC := $(shell mkdir -p .deps > /dev/null 2>&1 || :)

DEP_FILES = $(addprefix .deps/,$(SRCS:.c=.P))\
	$(addprefix .deps/,$(CXXSRCS:.cpp=.P))

-include ${DEP_FILES}

%.o: %.cpp
	@echo '$(CXX) -c $<'; \
	${CXX} ${CXX_CFLAGS} ${OTHER_CFLAGS} ${CXX_INCLUDES} -Wp,-MD,.deps/$(*F).pp -c $<
	@-cp .deps/$(*F).pp .deps/$(*F).P; \
	tr ' ' '\012' < .deps/$(*F).pp \
	| ${SED} -e 's/^\\$$//' -e '/^$$/ d' -e '/:$$/ d' -e 's/$$/ :/' \
	>> .deps/$(*F).P; \
	${RM} .deps/$(*F).pp

oa-main: lib

module-clean:
	${RM} ${CLEANS}
	${RM} *.o
	${RM} *.a
	${RM} -r .deps

#------------------------------------------------------
include ${MAGICDIR}/rules.mak
