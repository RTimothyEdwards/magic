/*
 * ext2sim.c --
 *
 * Program to flatten hierarchical .ext files and produce
 * a .sim file, suitable for use as input to simulators
 * such as esim and crystal.
 *
 * Flattens the tree rooted at file.ext, reading in additional .ext
 * files as specified by "use" lines in file.ext.  The output is left
 * in file.sim, unless '-o esSimFile' is specified, in which case the
 * output is left in 'esSimFile'.
 *
 */

#ifndef lint
static const char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/ext2sim/ext2sim.c,v 1.2 2008/12/03 14:12:09 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>	/* for atof() */
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>	/* for sqrt() in bipolar L,W calculation */

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "windows/windows.h"
#include "textio/textio.h"
#include "dbwind/dbwind.h"	/* for DBWclientID */
#include "commands/commands.h"  /* for module auto-load */
#include "textio/txcommands.h"
#include "extflat/extflat.h"
#include "extflat/EFint.h"
#include "extract/extract.h"	/* for extDevTable */
#include "utils/runstats.h"
#include "utils/malloc.h"

/* C99 compat */
#include "extflat/extflat.h"

/* Forward declarations */
void CmdExtToSim();
bool simnAP();
bool simnAPHier();
int simParseArgs();
int simdevVisit(), simresistVisit(), simcapVisit(), simnodeVisit();
int simmergeVisit();

/* C99 compat */
int simdevOutNode();
int simdevSubstrate();

/* Options specific to ext2sim */
#ifdef EXT2SIM_AUTO
bool esDevNodesOnly = FALSE;
bool esNoAttrs = FALSE;
bool esHierAP = FALSE;
bool esMergeDevsA = FALSE;	/* merge devices of equal length */
bool esMergeDevsC = FALSE;	/* merge devices of equal length & width */

#else
extern bool esDevNodesOnly;
extern bool esNoAttrs;
extern bool esHierAP;
extern bool esMergeDevsA;
extern bool esMergeDevsC;
extern char esSpiceDefaultGnd[];
extern char *esSpiceCapNode;
#endif

bool esDoSimExtResis = FALSE;
bool esNoAlias = TRUE;
bool esNoLabel = TRUE;
char simesDefaultOut[FNSIZE];
char *simesOutName = simesDefaultOut;
char esDefaultAlias[FNSIZE], esDefaultLabel[FNSIZE];
char *esAliasName = esDefaultAlias;
char *esLabelName = esDefaultLabel;
char esCapFormat[FNSIZE];
FILE *esSimF = NULL;
FILE *esAliasF = NULL;
FILE *esLabF = NULL;


#define	MIT 0
#define	LBL 1
#define	SU  2

static unsigned short esFormat = MIT ;

struct {
   short resClassSource ;   /* The resistance class of the source of the dev */
   short resClassDrain ;    /* The resistance class of the drain of the dev */
   short resClassSub ;	    /* The resistance class of the substrate of the dev */
   TileType devType ;	    /* Magic tile type of the device */
   char  *defSubs ;	    /* The default substrate node */
} fetInfo[TT_MAXTYPES];

typedef struct {
	TileTypeBitMask visitMask;
} nodeClient;

typedef struct {
	HierName *lastPrefix;
	TileTypeBitMask visitMask;
} nodeClientHier;

#define NO_RESCLASS	-1

#define markVisited(client, rclass) \
  { TTMaskSetType(&((client)->visitMask), rclass); }

#define clearVisited(client) \
  { TTMaskZero(&((client)->visitMask)); }

#define beenVisited(client, rclass)  \
  ( TTMaskHasType(&((client)->visitMask), rclass) )

#define initNodeClient(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClient))); \
	TTMaskZero(&(( nodeClient *)(node)->efnode_client)->visitMask); \
}


#define initNodeClientHier(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClientHier))); \
	TTMaskZero(&(( nodeClientHier *)(node)->efnode_client)->visitMask); \
}


/* device merging */

#define devIsKilled(n) (esFMult[(n)] <= (float)0.0)
#define DEV_KILLED ((float) -1.0)
#define FMULT_SIZE (1<<10)
static float *esFMult = NULL;
static int    esFMIndex = 0;
static int    esFMSize = FMULT_SIZE;
int  esDevsMerged;

/* macro to add a dev's multiplier to the table and grow it if necessary */
#define addDevMult(f) \
{  \
        if ( esFMult == NULL ) { \
          esFMult = (float *) mallocMagic((unsigned) (esFMSize*sizeof(float)));  \
        } else if ( esFMIndex >= esFMSize ) {  \
          int i;  \
          float *op = esFMult ;  \
          esFMult = (float *) mallocMagic((unsigned) ((esFMSize = esFMSize*2)*sizeof(float))); \
          for ( i = 0 ; i < esFMSize/2 ; i++ ) esFMult[i] = op[i]; \
          if (op) freeMagic(op); \
        }  \
        esFMult[esFMIndex++] = (float)(f); \
}

#define setDevMult(i,f) { esFMult[(i)] = (float)(f); }

#define getCurDevMult() ((esFMult) ? esFMult[esFMIndex-1] : (float)1.0)

/* cache list used to find parallel devs */
typedef struct _devMerge {
        int     l, w;
        EFNode *g, *s, *d, *b;
        Dev * dev;
        int       esFMIndex;
        HierName *hierName;
        struct _devMerge *next;
} devMerge;

#ifdef EXT2SIM_AUTO
devMerge *devMergeList = NULL ;
#else
extern devMerge *devMergeList;
#endif

/* attributes controlling the Area/Perimeter extraction of fet terminals */
#define ATTR_FLATAP	"*[Ee][Xx][Tt]:[Aa][Pp][Ff]*"
#define ATTR_HIERAP	"*[Ee][Xx][Tt]:[Aa][Pp][Hh]*"
#define ATTR_SUBSAP	"*[Ee][Xx][Tt]:[Aa][Pp][Ss]*"

#define        atoCap(s)       ((EFCapValue)atof(s))

#ifdef MAGIC_WRAPPER

#ifdef EXT2SIM_AUTO
/*
 * ----------------------------------------------------------------------------
 *
 * Tcl package initialization function
 *
 * ----------------------------------------------------------------------------
 */

int
Exttosim_Init(
    Tcl_Interp *interp)
{
    /* Sanity checks! */
    if (interp == NULL) return TCL_ERROR;
    if (Tcl_PkgRequire(interp, "Tclmagic", MAGIC_VERSION, 0) == NULL)
	return TCL_ERROR;
    if (Tcl_InitStubs(interp, Tclmagic_InitStubsVersion, 0) == NULL) return TCL_ERROR;

    TxPrintf("Auto-loading EXTTOSIM module\n");
    TxFlushOut();

    /* Replace the auto-load function with the one defined in	*/
    /* this package in the command functions list.		*/

    if (WindReplaceCommand(DBWclientID, "exttosim", CmdExtToSim) < 0)
	return TCL_ERROR;

    /* ext2sim is an alias for exttosim */
    if (WindReplaceCommand(DBWclientID, "ext2sim", CmdExtToSim) < 0)
	return TCL_ERROR;

    Tcl_PkgProvide(interp, "Exttosim", MAGIC_VERSION);
    return TCL_OK;
}

#endif /* EXT2SIM_AUTO */
#endif /* MAGIC_WRAPPER */

/*
 * ----------------------------------------------------------------------------
 *
 * Main Tcl callback for command "magic::exttosim"
 *
 * ----------------------------------------------------------------------------
 */

#define EXTTOSIM_RUN		0
#define EXTTOSIM_ALIAS		1
#define EXTTOSIM_LABELS		2
#define EXTTOSIM_DEFAULT	3
#define EXTTOSIM_FORMAT		4
#define EXTTOSIM_RTHRESH	5
#define EXTTOSIM_CTHRESH	6
#define EXTTOSIM_MERGE		7
#define EXTTOSIM_EXTRESIST	8
#define EXTTOSIM_HELP		9

void
CmdExtToSim(
    MagWindow *w,
    TxCommand *cmd)
{
    int i,flatFlags;
    char *inName;
    FILE *f;

    int value;
    int option = EXTTOSIM_RUN;
    int argc = cmd->tx_argc;
    char **argv = cmd->tx_argv;
    const char * const *msg;
    bool err_result;

    short s_rclass, d_rclass, sub_rclass;
    char *devname;
    char *subname;
    TileType devtype;
    int idx;

    static EFCapValue LocCapThreshold = 2;
    static int LocResistThreshold = 10;

    static const char * const cmdExtToSimOption[] = {
	"[run] [options]	run exttosim on current cell\n"
	"			use \"run -help\" to get standard options",
	"alias on|off		enable/disable alias (.al) file",
	"labels on|off		enable/disable labels (.nodes) file",
	"default		reset to default values",
	"format MIT|SU|LBL	set output format",
	"rthresh [value]	set resistance threshold value",
	"cthresh [value]	set capacitance threshold value",
	"merge [option]		merge parallel transistors",
	"extresist on|off	incorporate extresist output",
        "help			print help information",
	NULL
    };

    static const char * const sim_formats[] = {
	"MIT",
	"LBL",
	"SU",
	NULL
    };

    static const char * const yesno[] = {
	"yes",
	"true",
	"on",
	"no",
	"false",
	"off",
	NULL
    };

    static const char * const cmdMergeTypes[] = {
	"none			don't merge parallel devices",
	"conservative		merge devices with same L, W",
	"aggressive		merge devices with same L"
    };

    if (cmd->tx_argc > 1)
    {
	option = Lookup(cmd->tx_argv[1], cmdExtToSimOption);
	if (option < 0) option = EXTTOSIM_RUN;
	else argv++;
    }

    switch (option)
    {
	case EXTTOSIM_EXTRESIST:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, (esDoSimExtResis) ? "on" : "off", NULL);
#else
		TxPrintf("Extresist: %s\n", (esDoSimExtResis) ? "on" : "off");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto usage;
	    idx = Lookup(cmd->tx_argv[2], yesno);
	    if (idx < 0) goto usage;
	    else if (idx < 3) esDoSimExtResis = TRUE;
	    else esDoSimExtResis = FALSE;
	    break;
	case EXTTOSIM_ALIAS:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, (esNoAlias) ? "off" : "on", NULL);
#else
		TxPrintf("Aliases: %s\n", (esNoAlias) ? "off" : "on");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto usage;
	    idx = Lookup(cmd->tx_argv[2], yesno);
	    if (idx < 0) goto usage;
	    else if (idx < 3) esNoAlias = FALSE;
	    else esNoAlias = TRUE;
	    break;
	case EXTTOSIM_LABELS:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, (esNoLabel) ? "off" : "on", NULL);
#else
		TxPrintf("Labels: %s\n", (esNoLabel) ? "off" : "on");
#endif
		return;
	    }
	    else if (cmd->tx_argc != 3)
		goto usage;
	    idx = Lookup(cmd->tx_argv[2], yesno);
	    if (idx < 0) goto usage;
	    else if (idx < 3) esNoLabel = FALSE;
	    else esNoLabel = TRUE;
	    break;
	case EXTTOSIM_CTHRESH:
	    if (cmd->tx_argc == 2)
	    {
		if (!IS_FINITE_F(LocCapThreshold))
#ifdef MAGIC_WRAPPER
		    Tcl_SetResult(magicinterp, "infinite", NULL);
#else
		    TxPrintf("Capacitance threshold: infinite\n");
#endif
		else
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp,
			Tcl_NewDoubleObj((double)LocCapThreshold));
#else
		    TxPrintf("Capacitance threshold: %lf\n", (double)LocCapThreshold);
#endif
		return;
	    }
	    else if (cmd->tx_argc < 3) goto usage;
	    if (!strncmp(cmd->tx_argv[2], "inf", 3))
		LocCapThreshold = INFINITE_THRESHOLD_F;
	    else if (StrIsNumeric(cmd->tx_argv[2]))
		LocCapThreshold = atoCap(cmd->tx_argv[2]);
	    else
		TxError("exttosim: numeric value or \"infinite\" expected.\n");
	    break;
	case EXTTOSIM_RTHRESH:
	    if (cmd->tx_argc == 2)
	    {
		if (LocResistThreshold == INFINITE_THRESHOLD)
#ifdef MAGIC_WRAPPER
		    Tcl_SetResult(magicinterp, "infinite", NULL);
#else
		    TxPrintf("Resistance threshold: infinite\n");
#endif
		else
#ifdef MAGIC_WRAPPER
		    Tcl_SetObjResult(magicinterp,
			Tcl_NewIntObj(LocResistThreshold));
#else
		    TxPrintf("Resistance threshold: %lf\n", (double)LocResistThreshold);
#endif
		return;
	    }
	    else if (cmd->tx_argc < 3) goto usage;
	    if (StrIsInt(cmd->tx_argv[2]))
		LocResistThreshold = atoi(cmd->tx_argv[2]);
	    else if (!strncmp(cmd->tx_argv[2], "inf", 3))
		LocResistThreshold = INFINITE_THRESHOLD;
	    else
		TxError("exttosim: integer value or \"infinite\" expected.\n");
	    break;
	case EXTTOSIM_FORMAT:
	    if (cmd->tx_argc == 2)
	    {
#ifdef MAGIC_WRAPPER
		Tcl_SetResult(magicinterp, (char*)sim_formats[esFormat], TCL_STATIC);
#else
		TxPrintf("Format: %s\n", sim_formats[esFormat]);
#endif
		return;
	    }
	    else if (cmd->tx_argc < 3) goto usage;
	    value = Lookup(cmd->tx_argv[2], sim_formats);
	    if (value < 0)
		TxError("exttosim: output formats are MIT, LBL, or SU\n");
	    else
		esFormat = value;
	    break;
	case EXTTOSIM_MERGE:
	    if (cmd->tx_argc == 2)
	    {
		if (esMergeDevsA)
#ifdef MAGIC_WRAPPER
		    Tcl_SetResult(magicinterp, "aggressive", NULL);
#else
		    TxPrintf("Merge: aggressive\n");
#endif
		else if (esMergeDevsC)
#ifdef MAGIC_WRAPPER
		    Tcl_SetResult(magicinterp, "conservative", NULL);
#else
		    TxPrintf("Merge: conservative\n");
#endif
		else
#ifdef MAGIC_WRAPPER
		    Tcl_SetResult(magicinterp, "none", NULL);
#else
		    TxPrintf("Merge: none\n");
#endif
		return;
	    }
	    else if (cmd->tx_argc < 3) goto usage;
	    value = Lookup(cmd->tx_argv[2], cmdMergeTypes);
	    if (value < 0)
	    {
		TxError("Merge types are:\n");
		for (msg = &(cmdMergeTypes[0]); *msg != NULL; msg++)
		    TxPrintf("    %s\n", *msg);
	    }
	    else switch (value) {
		case 0:
		    esMergeDevsA = FALSE;
		    esMergeDevsC = FALSE;
		    break;
		case 1:
		    esMergeDevsA = FALSE;
		    esMergeDevsC = TRUE;
		    break;
		case 2:
		    esMergeDevsA = TRUE;
		    esMergeDevsC = FALSE;
		    break;
	    }
	    break;

	case EXTTOSIM_DEFAULT:
	    LocCapThreshold = 2;
	    LocResistThreshold = 10;
	    EFOutputFlags = 0;
	    EFScale = 0.0;
	    if (EFArgTech)
	    {
		freeMagic(EFArgTech);
		EFArgTech = NULL;
	    }
	    if (EFSearchPath)
	    {
		freeMagic(EFSearchPath);
		EFSearchPath = NULL;
	    }
	    break;
	case EXTTOSIM_RUN:
	    goto runexttosim;
	    break;
	case EXTTOSIM_HELP:
usage:
	    for (msg = &(cmdExtToSimOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    %s\n", *msg);
	    }
	    break;
    }
    return;

runexttosim:

    esDevsMerged = 0;
    esFMIndex = 0;

    EFInit();

    /* Set local values for capacitor and resistor thresholds */
    EFCapThreshold = LocCapThreshold;
    EFResistThreshold = LocResistThreshold;

    /* Process command line arguments */
    inName = EFArgs(argc, argv, &err_result, simParseArgs, (ClientData) NULL);

    if (err_result == TRUE)
    {
	EFDone(NULL);
        return /* TCL_ERROR */;
    }

    if (inName == NULL)
    {
	/* Assume that we want to do exttospice on the currently loaded cell */

	if (w == (MagWindow *) NULL)
	    windCheckOnlyWindow(&w, DBWclientID);

	if (w == (MagWindow *) NULL)
	{
	    TxError("Point to a window or specify a cell name.\n");
	    EFDone(NULL);
	    return /* TCL_ERROR */;
	}
	inName = ((CellUse *) w->w_surfaceID)->cu_def->cd_name;
    }

    /*
     * Initializations specific to this program.
     * Make output, alias, and label names be of the form
     * inName.suffix if they weren't explicitly specified,
     * where suffix is .sim, .al, or .nodes.
     */

    /* Addendum:  Because extresis depends on ".sim" output, we	*/
    /* add ".res" in the output name (the same as used for the	*/
    /* ".res.ext" output) so that this output won't be grabbed	*/
    /* the next time "extresist" is run.			*/

    if (simesOutName == simesDefaultOut)
	(void) sprintf(simesDefaultOut, "%s%s.sim", inName,
		((esDoSimExtResis) ? ".ext" : ""));
    if (esAliasName == esDefaultAlias)
	(void) sprintf(esDefaultAlias, "%s%s.al", inName,
		((esDoSimExtResis) ? ".ext" : ""));
    if (esLabelName == esDefaultLabel)
	(void) sprintf(esDefaultLabel, "%s%s.nodes", inName,
		((esDoSimExtResis) ? ".ext" : ""));
    if ((esSimF = fopen(simesOutName, "w")) == NULL)
    {
#ifdef MAGIC_WRAPPER
	char *tclres = Tcl_Alloc(128);
	sprintf(tclres, "exttosim: Unable to open file %s for writing\n",
		simesOutName);
	Tcl_SetResult(magicinterp, tclres, TCL_DYNAMIC);
#else
	TxError("exttosim: Unable to open file %s for writing\n", simesOutName);
#endif
	EFDone(NULL);
	return /* TCL_ERROR */;
    }
    if (!esNoAlias && (esAliasF = fopen(esAliasName, "w")) == NULL)
    {
#ifdef MAGIC_WRAPPER
	char *tclres = Tcl_Alloc(128);
	sprintf(tclres, "exttosim: Unable to open file %s for writing\n",
		esAliasName);
	Tcl_SetResult(magicinterp, tclres, TCL_DYNAMIC);
#else
	TxError("exttosim: Unable to open file %s for writing\n", esAliasName);
#endif
	EFDone(NULL);
	return /* TCL_ERROR */;
    }
    if (!esNoLabel && (esLabF = fopen(esLabelName, "w")) == NULL)
    {
#ifdef MAGIC_WRAPPER
	char *tclres = Tcl_Alloc(128);
	sprintf(tclres, "exttosim: Unable to open file %s for writing\n",
		esLabelName);
	Tcl_SetResult(magicinterp, tclres, TCL_DYNAMIC);
#else
	TxError("exttosim: Unable to open file %s for writing\n", esLabelName);
#endif
	return /* TCL_ERROR */;
    }

    /* Read the hierarchical description of the input circuit */
    if (EFReadFile(inName, FALSE, esDoSimExtResis, FALSE, FALSE) == FALSE)
    {
	EFDone(NULL);
	return /* TCL_ERROR */;
    }

    if ((EFStyle == NULL) && (esFormat == SU))
    {
	TxError("Warning:  Current extraction style does not match .ext file!\n");
	TxError("Area/Perimeter values will be zero.\n");
    }

    /* create default fetinfo entries (MOSIS) which can be overriden by
       the command line arguments */

    for ( i = 0 ; i < TT_MAXTYPES ; i++ )
    {
	fetInfo[i].resClassSource = NO_RESCLASS;
	fetInfo[i].resClassDrain = NO_RESCLASS;
	fetInfo[i].resClassSub = NO_RESCLASS;
	fetInfo[i].defSubs = NULL;
	fetInfo[i].devType = TT_SPACE;
    }

    /* Get fetInfo information from the current extraction style 	 */
    /* (this works only for the Tcl version with the embedded exttospice */
    /* command)								 */

    idx = 0;
    while (ExtGetDevInfo(idx++, &devname, &devtype, &s_rclass, &d_rclass,
		&sub_rclass, &subname))
    {
	if (idx == TT_MAXTYPES)
	{
	    TxError("Error:  Ran out of space for device types!\n");
	    break;
	}
	i = efBuildAddStr(EFDevTypes, &EFDevNumTypes, TT_MAXTYPES, devname);

	if (EFStyle != NULL)
	{
	    fetInfo[i].resClassSource = s_rclass;
	    fetInfo[i].resClassDrain = d_rclass;
	    fetInfo[i].resClassSub = sub_rclass;
	    fetInfo[i].defSubs = subname;
	    fetInfo[i].devType = devtype;
	}
    }

    /* Write the output file */

    fprintf(esSimF, "| units: %g tech: %s format: %s\n", EFScale, EFTech,
		   (esFormat == MIT) ? "MIT" :
		      ( (esFormat == LBL) ? "LBL" : "SU" ) );

    /* Convert the hierarchical description to a flat one */
    flatFlags = EF_FLATNODES;
    if (IS_FINITE_F(LocCapThreshold)) flatFlags |= EF_FLATCAPS;
    if (LocResistThreshold != INFINITE_THRESHOLD) flatFlags |= EF_FLATRESISTS;
    EFFlatBuild(inName, flatFlags);

    if (esMergeDevsA || esMergeDevsC)
    {
	devMerge *p;

	EFVisitDevs(simmergeVisit, (ClientData) NULL);
	TxPrintf("Devices merged: %d\n", esDevsMerged);
	esFMIndex = 0;
	for (p = devMergeList; p != NULL; p = p->next)
	    freeMagic(p);
	devMergeList = NULL;
    }

    EFVisitDevs(simdevVisit, (ClientData)NULL);
    if (flatFlags & EF_FLATCAPS) {
	EFVisitCaps(simcapVisit, (ClientData) NULL);
    }
    EFVisitResists(simresistVisit, (ClientData) NULL);
    esSpiceCapNode = esSpiceDefaultGnd;
    EFVisitNodes(simnodeVisit, (ClientData) NULL);

    EFFlatDone(NULL);
    EFDone(NULL);

    if (esSimF) fclose(esSimF);
    if (esLabF) fclose(esLabF);
    if (esAliasF) fclose(esAliasF);

    TxPrintf("exttosim finished.\n");
}

#if 0	/* Independent program "ext2sim" deprecated */

/*
 * ----------------------------------------------------------------------------
 *
 * main --
 *
 * Top level of ext2sim (non-Tcl version only)
 *
 * ----------------------------------------------------------------------------
 */

int
main(
    int argc,
    char *argv[])
{

    int i,flatFlags;
    char *inName;
    FILE *f;

    esDevsMerged = 0;

    EFInit();
    /* create default fetinfo entries (MOSIS) which can be overriden by
       the command line arguments */
    for ( i = 0 ; i < TT_MAXTYPES ; i++ ) {
	fetInfo[i].resClassSource = NO_RESCLASS;
	fetInfo[i].resClassDrain = NO_RESCLASS;
	fetInfo[i].resClassSub = NO_RESCLASS;
	fetInfo[i].defSubs = NULL;
	fetInfo[i].devType = TT_SPACE;
    }
    i = efBuildAddStr(EFDevTypes, &EFDevNumTypes, TT_MAXTYPES, "nfet");
    fetInfo[i].resClassSource = fetInfo[i].resClassDrain = 0 ;
    fetInfo[i].resClassSub = NO_RESCLASS ;
    fetInfo[i].defSubs = "Gnd!";
    i = efBuildAddStr(EFDevTypes, &EFDevNumTypes, TT_MAXTYPES, "pfet");
    fetInfo[i].resClassSource = fetInfo[i].resClassDrain = 1 ;
    fetInfo[i].resClassSub = 6 ;
    fetInfo[i].defSubs = "Vdd!";
    i = efBuildAddStr(EFDevTypes, &EFDevNumTypes, TT_MAXTYPES, "nmos");
    fetInfo[i].resClassSource = fetInfo[i].resClassDrain = 0 ;
    fetInfo[i].resClassSub = NO_RESCLASS ;
    fetInfo[i].defSubs = "Gnd!";
    i = efBuildAddStr(EFDevTypes, &EFDevNumTypes, TT_MAXTYPES, "pmos");
    fetInfo[i].resClassSource = fetInfo[i].resClassDrain = 1 ;
    fetInfo[i].resClassSub = 6 ;
    fetInfo[i].defSubs = "Vdd!";
    /* Process command line arguments */
    inName = EFArgs(argc, argv, NULL, simParseArgs, (ClientData) NULL);
    if (inName == NULL)
	exit (1);

    /*
     * Initializations specific to this program.
     * Make output, alias, and label names be of the form
     * inName.suffix if they weren't explicitly specified,
     * where suffix is .sim, .al, or .nodes.
     */

    if (simesOutName == simesDefaultOut)
	(void) sprintf(simesDefaultOut, "%s.sim", inName);
    if (esAliasName == esDefaultAlias)
	(void) sprintf(esDefaultAlias, "%s.al", inName);
    if (esLabelName == esDefaultLabel)
	(void) sprintf(esDefaultLabel, "%s.nodes", inName);
    if ((esSimF = fopen(simesOutName, "w")) == NULL)
    {
	perror(simesOutName);
	exit (1);
    }
    if (!esNoAlias && (esAliasF = fopen(esAliasName, "w")) == NULL)
    {
	perror(esAliasName);
	exit (1);
    }
    if (!esNoLabel && (esLabF = fopen(esLabelName, "w")) == NULL)
    {
	perror(esLabelName);
	exit (1);
    }

    /* Read the hierarchical description of the input circuit */
    if (EFReadFile(inName, FALSE, esDoSimExtResis, FALSE, FALSE) == FALSE)
    {
	exit(1);
    }

    fprintf(esSimF, "| units: %g tech: %s format: %s\n", EFScale, EFTech,
		   (esFormat == MIT) ? "MIT" :
		      ( (esFormat == LBL) ? "LBL" : "SU" ) );

    /* Convert the hierarchical description to a flat one */
    flatFlags = EF_FLATNODES;
    if (IS_FINITE_F(EFCapThreshold)) flatFlags |= EF_FLATCAPS;
    if (EFResistThreshold != INFINITE_THRESHOLD) flatFlags |= EF_FLATRESISTS;
    EFFlatBuild(inName, flatFlags);

    if (esMergeDevsA || esMergeDevsC) {
	devMerge *p;

	EFVisitDevs(simmergeVisit, (ClientData) NULL);
	TxPrintf("Devices merged: %d\n", esDevsMerged);
	esFMIndex = 0;
	for (p = devMergeList; p != NULL; p = p->next) freeMagic(p);
    }

    EFVisitDevs(simdevVisit, (ClientData) NULL);
    if (flatFlags & EF_FLATCAPS)
	EFVisitCaps(simcapVisit, (ClientData) NULL);
    EFVisitResists(simresistVisit, (ClientData) NULL);
    esSpiceCapNode = esSpiceDefaultGnd;
    EFVisitNodes(simnodeVisit, (ClientData) NULL);

    EFFlatDone(NULL);
    EFDone(NULL);

    if (esSimF) fclose(esSimF);
    if (esLabF) fclose(esLabF);
    if (esAliasF) fclose(esAliasF);

    TxPrintf("Memory used: %s\n", RunStats(RS_MEM, NULL, NULL));
    exit(0);
}

#endif		/* Deprecated */

/*
 * ----------------------------------------------------------------------------
 *
 * simParseArgs --
 *
 * Process those arguments that are specific to ext2sim.
 * Assumes that *pargv[0][0] is '-', indicating a flag
 * argument.
 *
 * Results:
 *	None.  TCL version returns False if an error is encountered
 *	while parsing arguments, True otherwise.
 *
 * Side effects:
 *	After processing an argument, updates *pargc and *pargv
 *	to point to after the argument.
 *
 *	May initialize various global variables based on the
 *	arguments given to us.
 *
 *	Exits in the event of an improper argument.
 *
 * ----------------------------------------------------------------------------
 */

int
simParseArgs(
    int *pargc,
    char ***pargv)
{
    char **argv = *pargv, *cp;
    int argc = *pargc;

    switch (argv[0][1])
    {
	case 'A':
	    esNoAlias = TRUE;
	    break;
	case 'B':
	    esNoAttrs = TRUE;
	    break;
	case 'F':
	    esDevNodesOnly = TRUE;
	    break;
	case 'L':
	    esNoLabel = TRUE;
	    break;
	case 'M':
	    esMergeDevsA = TRUE;
	    break;
	case 'm':
	    esMergeDevsC = TRUE;
	    break;
	case 'a':
	    if ((esAliasName = ArgStr(&argc, &argv, "filename")) == NULL)
		goto usage;
	    break;
	case 'l':
	    if ((esLabelName = ArgStr(&argc, &argv, "filename")) == NULL)
		goto usage;
	    break;
	case 'o':
	    if ((simesOutName = ArgStr(&argc, &argv, "filename")) == NULL)
		goto usage;
	    break;
	case 'f': {
	     char *ftmp ;

	     if ((ftmp = ArgStr(&argc, &argv, "format")) == NULL)
		goto usage;
	     if ( strcasecmp(ftmp,"MIT") == 0 )
	        esFormat = MIT ;
	     else if ( strcasecmp(ftmp,"LBL") == 0 )
		esFormat = LBL ;
	     else if ( strcasecmp(ftmp,"SU") == 0 )
		esFormat = SU ;
	     else goto usage;
	     break;
	     }
	case 'y': {
	      char *t;

	      if (( t =  ArgStr(&argc, &argv, "cap-accuracy") ) == NULL)
		goto usage;
	      TxPrintf("Cap accuracy option -y is deprecated.\n");
	      break;
	      }
	case 'J':
	     {
	     char *ftmp ;

	     if ((ftmp = ArgStr(&argc, &argv, "hierAP_SD")) == NULL)
		goto usage;
	     if ( strcasecmp(ftmp, "HIER") == 0 )
		esHierAP = TRUE ;
	     else if ( strcasecmp(ftmp, "FLAT") == 0 )
		esHierAP = FALSE ;
	     else goto usage;

	     break;
	     }
	default:
	    TxError("Unrecognized flag: %s\n", argv[0]);
	    goto usage;
    }

    *pargv = argv;
    *pargc = argc;
    return 0;

usage:
    TxError("Usage: ext2sim [-a aliasfile] [-A] [-B] [-l labelfile] [-L]\n"
		"[-o simfile] [-J flat|hier] [-y cap_digits]\n"
		"[-f mit|lbl|su] "
		"[file]\n"
		);

    return 1;
}


/*
 * ----------------------------------------------------------------------------
 *
 * SimGetNode --
 *
 * function to find a node given its hierarchical prefix and suffix
 *
 * Results:
 *  a pointer to the node struct or NULL
 *
 * ----------------------------------------------------------------------------
 */
EFNode *
SimGetNode(
    HierName *prefix,
    HierName *suffix)
{
	HashEntry *he;

	he = EFHNConcatLook(prefix, suffix, "output");
	return(((EFNodeName *) HashGetValue(he))->efnn_node);
}


/*
 * ----------------------------------------------------------------------------
 *
 * simdevVisit --
 *
 * Procedure to output a single dev to the .sim file.
 * Called by EFVisitDevs().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the file esSimF.
 *
 * Format of a .sim dev line:
 *
 *	type gate source drain l w x y g= s= d=
 *
 * where
 *	type is a name identifying this type of transistor
 *	gate, source, and drain are the nodes to which these three
 *		terminals connect
 *	l, w are the length and width of the channel
 *	x, y are the x, y coordinates of a point within the channel.
 *	g=, s=, d= are the (optional) attributes; if present, each
 *		is followed by a comma-separated list of attributes.
 *
 * ----------------------------------------------------------------------------
 */

int
simdevVisit(
    Dev *dev,		/* Device being output */
    HierContext *hc,	/* Hierarchical context down to this device */
    float scale,	/* Scale transform for output */
    Transform *trans)	/* Coordinate transform */
{
    DevTerm *gate, *source, *drain, *term;
    EFNode  *subnode, *snode, *dnode;
    int l, w;
    Rect r;
    char name[12];
    bool is_subckt = FALSE;
    HierName *hierName = hc->hc_hierName;

    sprintf(name, "output");

    /* If no terminals, can't do much of anything */
    if (dev->dev_nterm < 1)
	return 0;

    /* If one terminal, can't do much of anything, either, 	*/
    /* except maybe with a subcircuit device.  That's not	*/
    /* supported by ext2sim, though. . .			*/

    if (dev->dev_nterm < 2)
	return 0;

    /* Merged devices */
    if ((esMergeDevsA || esMergeDevsC) && devIsKilled(esFMIndex++))
	return 0;

    /* Computation of length and width has been moved from efVisitDevs */
    EFGetLengthAndWidth(dev, &l, &w);
    if (esMergeDevsA || esMergeDevsC) w *= getCurDevMult();

    /* If only two terminals, connect the source to the drain */
    gate = &dev->dev_terms[0];
    if (dev->dev_nterm >= 2)
	source = drain = &dev->dev_terms[1];
    if (dev->dev_nterm >= 3)
	drain = &dev->dev_terms[2];
    subnode = dev->dev_subsnode;

    /* Kludge for .sim: transistor types can only be one character */
    switch(dev->dev_class)
    {
	/* "sim" types are fixed according to the device class */
	case DEV_BJT:
	    fprintf(esSimF, "b");	/* sim format extension */
	    break;
	case DEV_DIODE:
	case DEV_PDIODE:
	case DEV_NDIODE:
	    is_subckt = TRUE;
	    fprintf(esSimF, "x");	/* no class for diodes, use subckt */
	    break;
	case DEV_RES:
	    fprintf(esSimF, "r");
	    break;
	case DEV_CAP:
	case DEV_CAPREV:
	    fprintf(esSimF, "c");	/* sim format extension */
	    break;
	case DEV_FET:
	case DEV_MOSFET:
	case DEV_ASYMMETRIC:
	    /* The sim file format only understands "n" and "p" for FETs.   */
	    /* The extraction method says nothing about which is which.	    */
	    /* The EFDevTypes[] should ideally start with "n" or "p".  If   */
	    /* it doesn't, then dev->dev_type should.  If neither does,	    */
	    /* then use EFDevTypes[] but flag an error.			    */

	    if (EFDevTypes[dev->dev_type][0] == 'n' ||
		    EFDevTypes[dev->dev_type][0] == 'p')
	    {
		fprintf(esSimF, "%c", EFDevTypes[dev->dev_type][0]);
	    }
	    else
	    {
		TileType ttype = fetInfo[dev->dev_type].devType;

		if (DBTypeLongNameTbl[ttype][0] == 'n' ||
			    DBTypeLongNameTbl[ttype][0] == 'p')
		{
		    fprintf(esSimF, "%c", DBTypeLongNameTbl[ttype][0]);
		}
		else
		{
		    TxError("Error: MOSFET device type \"%s\" does not start with "
			    "\"n\" or \"p\" as required for the .sim format\n",
			    EFDevTypes[dev->dev_type]);

		    /* Default to "n" */
		    fprintf(esSimF, "n");
		}
	    }
	    break;
	case DEV_MSUBCKT:
	case DEV_CSUBCKT:
	case DEV_RSUBCKT:
	case DEV_SUBCKT:
	    /* Use the 'x' type in .sim format.  This is implemented in the */
	    /* IRSIM "user subcircuit" package, so it has a valid syntax.   */
	    /* It is used by the extresist code in magic as a way to work   */
	    /* around the lack of substrate and lack of device names in the */
	    /* .sim format.						    */
	    is_subckt = TRUE;
	    fprintf(esSimF, "x");
	    break;

	default:
	    fprintf(esSimF, "%c", EFDevTypes[dev->dev_type][0]);
	    break;
    }

    /* Output gate node name.  Resistor devices don't generate this node. */
    if (dev->dev_class != DEV_RES)
       simdevOutNode(hierName, gate->dterm_node->efnode_name->efnn_hier, name, esSimF);

    /* Output source and drain node names */
    if (dev->dev_nterm > 1)
	simdevOutNode(hierName, source->dterm_node->efnode_name->efnn_hier, name, esSimF);

    /* Hack for BiCMOS---see scmos.tech27 hack:  Tim, 7/16/96 */
    /* Second hack, using dev_class 2/20/03 */
    if (EFDevTypes[dev->dev_type][0] == 'b')
	dev->dev_class = DEV_BJT;

    if (dev->dev_class == DEV_BJT && subnode)
    {
        sprintf(name, "fet");
        simdevOutNode(hierName, subnode->efnode_name->efnn_hier, name, esSimF);
    }
    else if ((dev->dev_class == DEV_DIODE || dev->dev_class == DEV_PDIODE ||
		dev->dev_class == DEV_NDIODE) && dev->dev_nterm == 1 && subnode)
    {
        sprintf(name, "fet");
        simdevOutNode(hierName, subnode->efnode_name->efnn_hier, name, esSimF);
    }
    else if (dev->dev_nterm > 2)
        simdevOutNode(hierName, drain->dterm_node->efnode_name->efnn_hier, name, esSimF);

    if (dev->dev_nterm > 3)	/* For subcircuit support ('x' device) */
    {
	int i;

        sprintf(name, "subckt");
	for (i = 3; i < dev->dev_nterm; i++)
	{
	    term = &dev->dev_terms[i];
	    simdevOutNode(hierName, term->dterm_node->efnode_name->efnn_hier,
			name, esSimF);
	}
    }

    if (dev->dev_class != DEV_DIODE && dev->dev_class != DEV_NDIODE &&
		dev->dev_class != DEV_PDIODE)
    {
	if (is_subckt && subnode)
	{
	    /* As a general policy on subcircuits supporting extresist,	*/
	    /* output the subcircuit node as the last port of the	*/
	    /* subcircuit definition, *except* for the use of the 'x'	*/
	    /* device type for diodes.					*/

	    putc(' ', esSimF);
	    simdevSubstrate(hierName, subnode->efnode_name->efnn_hier,
	             dev->dev_type, 0.0, FALSE, esSimF);
	}

	/* Support gemini's substrate comparison */
	else if (esFormat == LBL && subnode)
	{
	    putc(' ', esSimF);
	    simdevSubstrate(hierName, subnode->efnode_name->efnn_hier,
	             dev->dev_type, 0.0, FALSE, esSimF);
	}
    }

    GeoTransRect(trans, &dev->dev_rect, &r);

    if (dev->dev_class == DEV_BJT || EFDevTypes[dev->dev_type][0] == 'b')
    {
       /* Bipolar sim format:  We don't have the length and width
	* of the collector well, but we can get it from the area
	* and perimeter measurements; hopefully any strict netlist
	* comparator will deal with any problem arising from
	* swapping L and W.
	*/

	int n;
	double cl, cw;
	double chp = 0.0;
	double ca = 0.0;

	for (n = 0; n < efNumResistClasses; n++) {
	    ca += (double)(subnode->efnode_pa[n].pa_area);
	    chp += 0.5 * (double)(subnode->efnode_pa[n].pa_perim);
	}

	cl = 0.5 * (chp + sqrt(chp * chp - 4 * ca));
	cw = ca / cl;

	fprintf(esSimF, " %d %d %g %g", (int)cl, (int)cw,
		r.r_xbot * scale, r.r_ybot * scale);
    }
    else if (dev->dev_class == DEV_RES) {	/* generate a resistor */
       fprintf(esSimF, " %f", (double)(dev->dev_res));
    }
    else if (dev->dev_class == DEV_CAP) {	/* generate a capacitor */
       fprintf(esSimF, " %f", (double)(dev->dev_cap));
    }
    else if (dev->dev_class == DEV_CAPREV) {	/* generate a capacitor */
       fprintf(esSimF, " %f", (double)(dev->dev_cap));
    }
    else if (is_subckt)
    {
	/* Output source and drain attributes */
	if (source->dterm_attrs)
	    fprintf(esSimF, " s=%s", source->dterm_attrs);
	if ((source != drain) && drain->dterm_attrs)
	       fprintf(esSimF, " d=%s", drain->dterm_attrs);

	/* Output length, width, and position as attributes */
        fprintf(esSimF, " l=%g w=%g x=%g y=%g",
		l * scale, w * scale, r.r_xbot * scale, r.r_ybot * scale);
    }
    else if ((dev->dev_class != DEV_DIODE) && (dev->dev_class != DEV_PDIODE)
		&& (dev->dev_class != DEV_NDIODE)) {

       /*
        * Scale L and W appropriately by the same amount as distance
        * values in the transform.  The transform will have a scale
        * different from 1 only in the case when the scale factors of
        * some of the .ext files differed, making it necessary to scale
        * all dimensions explicitly instead of having a single scale
        * factor at the beginning of the .sim file.
        */

       fprintf(esSimF, " %g %g %g %g",
		l * scale, w * scale, r.r_xbot * scale, r.r_ybot * scale);

       /* Attributes, if present */
       if (!esNoAttrs)
       {
	   bool subAP= FALSE, hierS = esHierAP, hierD = esHierAP;

	   if (gate->dterm_attrs)
	       fprintf(esSimF, " g=%s", gate->dterm_attrs);
	   if ( esFormat == SU ) {
	       if ( gate->dterm_attrs ) {
	         subAP = Match(ATTR_SUBSAP, gate->dterm_attrs ) ;
	         fprintf(esSimF, ",");
	       } else
	         fprintf(esSimF, " g=");
	       simdevSubstrate(hierName, subnode->efnode_name->efnn_hier,
		         dev->dev_type, scale, subAP, esSimF);
	   }
	   if (source->dterm_attrs) {
	       fprintf(esSimF, " s=%s", source->dterm_attrs);
	       if  ( Match(ATTR_HIERAP, source->dterm_attrs ) != FALSE )
		   hierS = TRUE ;
	       else if ( Match(ATTR_FLATAP, source->dterm_attrs ) != FALSE )
		   hierS = FALSE ;
	   }
	   if ( esFormat == SU ) {
	       fprintf(esSimF, "%s", (source->dterm_attrs) ? "," : " s=" );
	       if (hierS)
	         simnAPHier(source, hierName, fetInfo[dev->dev_type].resClassSource,
		      scale, esSimF);
	       else {
	         snode= SimGetNode(hierName,
			     source->dterm_node->efnode_name->efnn_hier);
	         simnAP(snode, fetInfo[dev->dev_type].resClassSource, scale, esSimF);
	       }
	   }
	   if (drain->dterm_attrs) {
	       fprintf(esSimF, " d=%s", drain->dterm_attrs);
	       if  ( Match(ATTR_HIERAP, drain->dterm_attrs ) != FALSE )
		   hierD = TRUE ;
	       else if ( Match(ATTR_FLATAP, drain->dterm_attrs ) != FALSE )
		   hierD = FALSE ;
	   }
	   if ( esFormat == SU ) {
	       fprintf(esSimF, "%s", (drain->dterm_attrs) ? "," : " d=" );
	       if (hierD)
	         simnAPHier(drain, hierName, fetInfo[dev->dev_type].resClassDrain,
		      scale, esSimF);
	       else {
	         dnode = SimGetNode(hierName,
			      drain->dterm_node->efnode_name->efnn_hier);
	         simnAP(dnode, fetInfo[dev->dev_type].resClassDrain,
		      scale, esSimF);
	       }
	   }
       }
    }

    if (is_subckt)
    {
	/* Last token on a subcircuit 'x' line is the subcircuit name */
	fprintf(esSimF, " %s", EFDevTypes[dev->dev_type]);
    }

    fprintf(esSimF, "\n");

    return 0;
}

int
simdevSubstrate(
    HierName *prefix,
    HierName *suffix,
    int type,
    float scale,
    bool doAP,
    FILE *outf)
{
    HashEntry *he;
    EFNodeName *nn;
    char *suf ;
    int  l ;
    EFNode *subnode;

    suf = EFHNToStr(suffix);
    if (fetInfo[type].defSubs && strcasecmp(suf,fetInfo[type].defSubs) == 0) {
    	l = strlen(suf) - 1;
	if (  (( EFOutputFlags & EF_TRIMGLOB ) && suf[l] =='!') ||
	      (( EFOutputFlags & EF_TRIMLOCAL ) && suf[l] == '#')  )
	      suf[l] = '\0' ;
	if ( esFormat == SU )
		fprintf(outf, "S_");
	fprintf(outf, "%s", suf);
    }
    else {
    	he = EFHNConcatLook(prefix, suffix, "substrate");
    	if (he == NULL)
    	{
		fprintf(outf, "errGnd!");
		return 0;
    	}
    	/* Canonical name */
    	nn = (EFNodeName *) HashGetValue(he);
	subnode = nn->efnn_node;
	if ( esFormat == SU ) {
	  if ( doAP ) {
	    if ( fetInfo[type].resClassSub < 0 ) {
	     TxError("Error: subap for devtype %d required but not "
			"specified on command line\n", type);
	     fprintf(outf,"A_0,P_0,");
	    }
	    else {
		simnAP(subnode, fetInfo[type].resClassSub, scale, outf);
		putc(',', outf);
	    }
	  }
	  fprintf(outf, "S_");
	}
    	EFHNOut(nn->efnn_node->efnode_name->efnn_hier, outf);
   }
   return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * simnAP, simnAPHier --
 *
 * Output the area perimeter of the node with type type if it has not
 * been visited.
 * The simnAPHier version outputs the area and perimeter only within the
 * local subcell with hierarchical name hierName.
 *
 * Side effects:
 *     Set the visited flags so that the node A/P will not be output multiple
 *     times
 *
 * ----------------------------------------------------------------------------
 */

bool
simnAP(
    EFNode *node,
    int resClass,
    float scale,
    FILE *outf)
{
	int a, p;

	if ( node->efnode_client == (ClientData) NULL )
		initNodeClient(node);
	if ( resClass == NO_RESCLASS ||
	     beenVisited((nodeClient *)node->efnode_client, resClass) ) {
		fprintf(outf,"A_0,P_0");
		return FALSE;
	}
	markVisited((nodeClient *)node->efnode_client, resClass);
	a = (int)(node->efnode_pa[resClass].pa_area*scale*scale);
	p = (int)(node->efnode_pa[resClass].pa_perim*scale);
	if ( a < 0 ) a = 0;
	if ( p < 0 ) p = 0;
	fprintf(outf,"A_%d,P_%d", a, p);
	return TRUE;
}

bool
simnAPHier(
    DevTerm *dterm,
    HierName *hierName,
    int resClass,
    float scale,
    FILE *outf)
{
	EFNode *node = dterm->dterm_node;
	nodeClientHier   *nc ;
	int a, p;

	if ( node->efnode_client == (ClientData) NULL )
		initNodeClientHier(node);
	nc = (nodeClientHier *)node->efnode_client;
	if ( nc->lastPrefix != hierName ) {
		TTMaskZero(&(nc->visitMask));
		nc->lastPrefix = hierName;
	}
	if ( resClass == NO_RESCLASS ||
	     beenVisited((nodeClientHier *)node->efnode_client, resClass) ) {
		fprintf(outf,"A_0,P_0");
		return FALSE;
	}
	markVisited((nodeClientHier *)node->efnode_client, resClass);
	a = (int)(node->efnode_pa[resClass].pa_area*scale*scale);
	p = (int)(node->efnode_pa[resClass].pa_perim*scale);
	if ( a < 0 ) a = 0;
	if ( p < 0 ) p = 0;
	fprintf(outf,"A_%d,P_%d", a, p);
	return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 *
 * simdevOutNode --
 *
 * Output the name of the node whose hierarchical prefix down to this
 * point is 'prefix' and whose name from the end of suffix down to the
 * leaves is 'suffix', just as in the arguments to EFHNConcat().
 *
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the file 'outf'.
 *	Sets the efnode_client field as described above.
 *
 * ----------------------------------------------------------------------------
 */

int
simdevOutNode(
    HierName *prefix,
    HierName *suffix,
    char *name,
    FILE *outf)
{
    HashEntry *he;
    EFNodeName *nn;

    he = EFHNConcatLook(prefix, suffix, name);
    if (he == NULL)
    {
	fprintf(outf, " GND");
	return 0;
    }

    /* Canonical name */
    nn = (EFNodeName *) HashGetValue(he);
    (void) putc(' ', outf);
    EFHNOut(nn->efnn_node->efnode_name->efnn_hier, outf);
    if ( nn->efnn_node->efnode_client == (ClientData) NULL )
	initNodeClient(nn->efnn_node);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * simcapVisit --
 *
 * Procedure to output a single capacitor to the .sim file.
 * Called by EFVisitCaps().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the file esSimF.
 *
 * Format of a .sim cap line:
 *
 *	C node1 node2 cap
 *
 * where
 *	node1, node2 are the terminals of the capacitor
 *	cap is the capacitance in femtofarads (NOT attofarads).
 *
 * ----------------------------------------------------------------------------
 */

int
simcapVisit(
    HierName *hierName1,
    HierName *hierName2,
    double cap)
{
    cap = cap / 1000;
    if (cap <= EFCapThreshold)
	return 0;

    fprintf(esSimF, "C ");
    EFHNOut(hierName1, esSimF);
    fprintf(esSimF, " ");
    EFHNOut(hierName2, esSimF);
    fprintf(esSimF, " %.1lf\n", cap);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * simresistVisit --
 *
 * Procedure to output a single resistor to the .sim file.
 * Called by EFVisitResists().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the file esSimF.
 *
 * Format of a .sim resistor line:
 *
 *	r node1 node2 res
 *
 * where
 *	node1, node2 are the terminals of the resistor
 *	res is the resistance in ohms (NOT milliohms)
 *
 *
 * ----------------------------------------------------------------------------
 */

int
simresistVisit(
    HierName *hierName1,
    HierName *hierName2,
    float res)
{
    fprintf(esSimF, "r ");
    EFHNOut(hierName1, esSimF);
    fprintf(esSimF, " ");
    EFHNOut(hierName2, esSimF);
    fprintf(esSimF, " %g\n", res / 1000.);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * simnodeVisit --
 *
 * Procedure to output a single node to the .sim file, along with
 * its aliases to the .al file and its location to the .nodes file.
 * Called by EFVisitNodes().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	Writes to the files esSimF, esAliasF, and esLabF.
 *
 * ----------------------------------------------------------------------------
 */

int
simnodeVisit(
    EFNode *node,
    int res,
    double cap)
{
    EFNodeName *nn;
    HierName *hierName;
    bool isGlob;
    const char *fmt;
    EFAttr *ap;

    if (esDevNodesOnly && node->efnode_client == (ClientData) NULL)
	return 0;

    hierName = (HierName *) node->efnode_name->efnn_hier;
    cap = cap  / 1000;
    res = (res + 500) / 1000;
    if (cap > EFCapThreshold)
    {
	fprintf(esSimF, "C ");
	EFHNOut(hierName, esSimF);
	fprintf(esSimF, "%s ", esSpiceCapNode);
	fprintf(esSimF, "%.1f\n", cap);
    }
    if (res > EFResistThreshold)
    {
	fprintf(esSimF, "R ");
	EFHNOut(hierName, esSimF);
	fprintf(esSimF, " %d\n", res);
    }
    if (node->efnode_attrs && !esNoAttrs)
    {
	fprintf(esSimF, "A ");
	EFHNOut(hierName, esSimF);
	for (fmt = " %s", ap = node->efnode_attrs; ap; ap = ap->efa_next)
	{
	    fprintf(esSimF, fmt, ap->efa_text);
	    fmt = ",%s";
	}
	putc('\n', esSimF);
    }

    /* Write aliases.  If the "ext2sim alias on" option was issued, then
     * write to the alias file only (<name>.al).  Otherwise write to the
     * <name>.sim file.
     */
    isGlob = EFHNIsGlob(hierName);
    for (nn = node->efnode_name->efnn_next; nn; nn = nn->efnn_next)
    {
	if (isGlob && EFHNIsGlob(nn->efnn_hier))
	    continue;

	if (esAliasF)
	{
	    fprintf(esAliasF, "= ");
	    EFHNOut(hierName, esAliasF);
	    fprintf(esAliasF, " ");
	    EFHNOut(nn->efnn_hier, esAliasF);
	    fprintf(esAliasF, "\n");
	}
	else
	{
	    fprintf(esSimF, "= ");
	    EFHNOut(hierName, esSimF);
	    fprintf(esSimF, " ");
	    EFHNOut(nn->efnn_hier, esSimF);
	    fprintf(esSimF, "\n");
	}
    }

    if (esLabF)
    {
	EFHNOut(hierName, esLabF);
	fprintf(esLabF, " %d %d %s\n",
		    node->efnode_loc.r_xbot, node->efnode_loc.r_ybot,
		    EFLayerNames[node->efnode_type]);
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * simmkDevMerge --
 * Create a new devMerge structure.
 *
 * Results:
 *    Obvious
 *
 * Side effects:
 *	Allocates memory and sets the fields of the structure.
 *
 * ----------------------------------------------------------------------------
 */
devMerge *
simmkDevMerge(
    int l,
    int w,
    EFNode *g,
    EFNode *s,
    EFNode *d,
    EFNode *b,
    HierName *hn,
    Dev *dev)
{
	devMerge *fp;

	fp = (devMerge *) mallocMagic((unsigned) (sizeof(devMerge)));
	fp->l = l; fp->w = w;
	fp->g = g; fp->s = s;
	fp->d = d; fp->b = b;
	fp->dev = dev;
	fp->esFMIndex = esFMIndex;
	fp->hierName = hn;
	fp->next = NULL;
	addDevMult(1.0);

	return fp;
}

/*
 * ----------------------------------------------------------------------------
 *
 * parallelDevs --
 * Macro to look if two devs are in parallel
 *
 * Results:
 *    NOT_PARALLEL  if not in parallel
 *    PARALLEL      if s==s d==d and g==g and bulk=bulk
 *    FLIP_PARALLEL if s==d d==s --->>----------------
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */
#define	NOT_PARALLEL	0
#define	PARALLEL	1
#define	PARALLEL_R	2

#define parallelDevs(f1, f2) \
( \
	( (f1)->g == (f2)->g && (f1)->b == (f2)->b && (f1)->l == (f2)->l && \
	  ( esMergeDevsA || (f1)->w == (f2)->w ) )  ? \
	   ( ((f1)->d == (f2)->d && (f1)->s == (f2)->s ) ? \
	       PARALLEL : \
	      (((f1)->s == (f2)->d && (f1)->d == (f2)->s ) ? PARALLEL_R : NOT_PARALLEL) )\
	  : NOT_PARALLEL  \
)

/*
 * ----------------------------------------------------------------------------
 *
 * simmergeVisit --
 * Visits each dev throu EFVisitDevs and finds if it is in parallel with
 * any previously visited dev.
 *
 * Results:
 *  0 always to keep the caller going.
 *
 * Side effects:
 *  Numerous.
 *
 * ----------------------------------------------------------------------------
 */
int
simmergeVisit(
    Dev *dev,		/* Dev to examine */
    HierContext *hc,	/* Hierarchical context down to this dev */
    float scale,	/* Scale transform */
    Transform *trans)	/* Coordinate transform (not used) */
{
	DevTerm *gate, *source, *drain;
	Dev     *cf;
	DevTerm *cg, *cs, *cd;
	EFNode	*subnode, *snode, *dnode, *gnode;
	int      pmode, l, w;
	float	 m;
	devMerge *fp, *cfp;
	HierName *hierName = hc->hc_hierName;

	if (dev->dev_nterm < 2) {
		TxError("outPremature\n");
		return 0;
	}

	gate = &dev->dev_terms[0];
	source = drain = &dev->dev_terms[1];
	if (dev->dev_nterm >= 3)
		drain = &dev->dev_terms[2];
	subnode = dev->dev_subsnode;

	gnode = SimGetNode (hierName, gate->dterm_node->efnode_name->efnn_hier);
	snode = SimGetNode (hierName, source->dterm_node->efnode_name->efnn_hier);
	dnode = SimGetNode (hierName, drain->dterm_node->efnode_name->efnn_hier);

	EFGetLengthAndWidth(dev, &l, &w);
	fp = simmkDevMerge((int)(l*scale), (int)(w*scale), gnode, snode,
			dnode, subnode, hierName, dev);

	/*
	 * run the list of devs. compare the current one with
	 * each one in the list. if they fullfill the matching requirements
	 * merge them.
	 */

	for ( cfp = devMergeList ; cfp != NULL ; cfp = cfp->next ) {
	  if ((pmode = parallelDevs(fp, cfp)) != NOT_PARALLEL) {

		cf = cfp->dev;
		cg = &cfp->dev->dev_terms[0];
		cs = cd = &cfp->dev->dev_terms[1];
		if (cfp->dev->dev_nterm >= 3) {
			if ( pmode == PARALLEL )
				cd = &cfp->dev->dev_terms[2];
			else if ( pmode == PARALLEL_R )
				cs = &cfp->dev->dev_terms[2];
		}

		m = esFMult[cfp->esFMIndex] + ((float)fp->w/(float)cfp->w);
		setDevMult(fp->esFMIndex, DEV_KILLED);
		setDevMult(cfp->esFMIndex, m);
		esDevsMerged++;
		freeMagic(fp);
		return 0;
	  }
	}
	/* No parallel devs to it yet */
	fp->next = devMergeList;
	devMergeList = fp;
	return 0;
}

