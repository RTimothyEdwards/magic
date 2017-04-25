#
# OpenAccess module Makefile
#

MODULE    = oa
MAGICDIR  = ..
SRCS      = oa.c
CXXSRCS	  = magicInit.cpp magicOA.cpp

include ${MAGICDIR}/defs.mak

#----------------------------------------

CXX_INCLUDES = -I. -I${OA}/oa/include

DEPS_MAGIC := $(shell mkdir .deps > /dev/null 2>&1 || :)

DEP_FILES = $(addprefix .deps/,$(SRCS:.c=.P))\
	$(addprefix .deps/,$(CXXSRCS:.cpp=.P))

-include ${DEP_FILES}

%.o: %.cpp
	@echo '$(COMPILE) -c $<'; \
	${CXX} ${CXX_CFLAGS} ${OTHER_CFLAGS} ${CXX_INCLUDES} -Wp,-MD,.deps/$(*F).pp -c $<
	@-cp .deps/$(*F).pp .deps/$(*F).P; \
	tr ' ' '\012' < .deps/$(*F).pp \
	| sed -e 's/^\\$$//' -e '/^$$/ d' -e '/:$$/ d' -e 's/$$/ :/' \
	>> .deps/$(*F).P; \
	rm .deps/$(*F).pp

clean:
	${RM} *.o
	${RM} -r .deps

#------------------------------------------------------
include ${MAGICDIR}/rules.mak
