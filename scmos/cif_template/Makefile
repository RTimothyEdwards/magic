MAGICDIR = ../..

MKDIR	   = $(MAGICDIR)/scripts/mkdirs
OBJS_DIR   = objs
CIFIN	   = $(OBJS_DIR)/CIFin
CIFOUT     = $(OBJS_DIR)/CIFout
IBMCIFIN   = $(OBJS_DIR)/IBMCIFin
IBMCIFOUT  = $(OBJS_DIR)/IBMCIFout
TMCIFIN    = $(OBJS_DIR)/TMCIFin
TMCIFOUT   = $(OBJS_DIR)/TMCIFout
SUBCIFIN   = $(OBJS_DIR)/SUBCIFin
SUBCIFOUT  = $(OBJS_DIR)/SUBCIFout

SED_CMD    = sed -e "s/\\\\/\\\\\\\\/" -e "/^\#/D" -e "s/(gen )/(gen)/" -e "s/(nowell )/(nowell)/" -e "s/(nwell )/(nwell)/" -e "s/(pwell )/(pwell)/"

include ${MAGICDIR}/defs.mak

SC_CPP	   = ${CPP}

all: $(OBJS_DIR) $(CIFIN) $(CIFOUT) $(IBMCIFIN) $(IBMCIFOUT) $(TMCIFIN) \
	$(TMCIFOUT) $(SUBCIFIN) $(SUBCIFOUT)

clean:;
	rm -rf $(OBJS_DIR)

scg: cifout.c cifout-cmos26b.gen cifout.gen cifout.nw\
	   cifout.others cifout.pw cifout.scgnw cifout.scgpw
	rm -f $(CIFOUT)
	$(SC_CPP) scgcifout.c | ${SED_CMD} > $(CIFOUT)

$(OBJS_DIR):
	$(MKDIR) $(OBJS_DIR)

$(CIFIN): $(OBJS_DIR) cifin.c cifin-cmos26b.gen cifin.gen cifin.nw cifin.oldnw\
	  cifin.others cifin.pw cifin-ami16.gen
	rm -f $(CIFIN)
	$(SC_CPP) -DSTANDARD cifin.c | ${SED_CMD} > $(CIFIN)

$(CIFOUT): $(OBJS_DIR) cifout.c cifout-cmos26b.gen cifout.gen cifout.nw\
	   cifout.others cifout.pw cifout-ami16.gen
	rm -f $(CIFOUT)
	$(SC_CPP) -DSTANDARD cifout.c | ${SED_CMD} > $(CIFOUT)

$(IBMCIFIN): $(OBJS_DIR) cifin.c cifin-ibm.gen
	rm -f $(IBMCIFIN)
	$(SC_CPP) -DIBM cifin.c | ${SED_CMD} > $(IBMCIFIN)

$(IBMCIFOUT): $(OBJS_DIR) cifout.c cifout-ibm.gen
	rm -f $(IBMCIFOUT)
	$(SC_CPP) -DIBM cifout.c | ${SED_CMD} > $(IBMCIFOUT)

$(TMCIFIN): $(OBJS_DIR) cifin.c cifin-cmos26b.gen
	rm -f $(TMCIFIN)
	$(SC_CPP) -DTIGHTMETAL cifin.c | ${SED_CMD} > $(TMCIFIN)

$(TMCIFOUT): $(OBJS_DIR) cifout.c cifout-cmos26b.gen
	rm -f $(TMCIFOUT)
	$(SC_CPP) -DTIGHTMETAL cifout.c | ${SED_CMD} > $(TMCIFOUT)

$(SUBCIFIN): $(OBJS_DIR) cifin.c cifin-cmos26g.gen cifin-cmos14b.gen
	rm -f $(SUBCIFIN)
	$(SC_CPP) -DSUBMICRON cifin.c | ${SED_CMD} > $(SUBCIFIN)

$(SUBCIFOUT): $(OBJS_DIR) cifout.c cifout-cmos26g.gen cifout-cmos14b.gen
	rm -f $(SUBCIFOUT)
	$(SC_CPP) -DSUBMICRON cifout.c | ${SED_CMD} > $(SUBCIFOUT)
