/*
 * EFantenna.c --
 *
 * Program to flatten hierarchical .ext files and then execute an
 * antenna violation check for every MOSFET device in the flattened
 * design.
 *
 * Flattens the tree rooted at file.ext, reading in additional .ext
 * files as specified by "use" lines in file.ext.
 * 
 */

#include <stdio.h>
#include <stdlib.h>	/* for atof() */
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#ifdef MAGIC_WRAPPER
#include "database/database.h"
#include "windows/windows.h"
#include "textio/textio.h"
#include "dbwind/dbwind.h"	/* for DBWclientID */
#include "textio/txcommands.h"
#endif
#include "extflat/extflat.h"
#include "extract/extract.h"	/* for extDevTable */
#include "utils/malloc.h"

/* Forward declarations */
int antennacheckArgs();
int antennacheckVisit();

typedef struct {
	long    visitMask:MAXDEVTYPES; 
} nodeClient;

typedef struct {
	HierName *lastPrefix;
	long    visitMask:MAXDEVTYPES; 
} nodeClientHier;

#define NO_RESCLASS	-1

#define markVisited(client, rclass) \
  { (client)->visitMask |= (1<<rclass); }

#define clearVisited(client) \
   { (client)->visitMask = (long)0; }

#define beenVisited(client, rclass)  \
   ( (client)->visitMask & (1<<rclass))

#define initNodeClient(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClient))); \
	(( nodeClient *)(node)->efnode_client)->visitMask = (long) 0; \
}


#define initNodeClientHier(node) \
{ \
	(node)->efnode_client = (ClientData) mallocMagic((unsigned) (sizeof(nodeClientHier))); \
	((nodeClientHier *) (node)->efnode_client)->visitMask = (long) 0; \
}


/*
 * ----------------------------------------------------------------------------
 *
 * Main Tcl callback for command "magic::antennacheck"
 *
 * ----------------------------------------------------------------------------
 */

#define ANTENNACHECK_RUN     0
#define ANTENNACHECK_HELP    1

void
CmdAntennaCheck(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int i,flatFlags;
    char *inName;
    FILE *f;

    int option = ANTENNACHECK_RUN;
    int value;
    int argc = cmd->tx_argc;
    char **argv = cmd->tx_argv; 
    char **msg;
    bool err_result;

    short sd_rclass;
    short sub_rclass;
    char *devname;
    char *subname;
    int idx;

    static char *cmdAntennaCheckOption[] = {
	"[run] [options]	run antennacheck on current cell\n"
	"			use \"run -help\" to get standard options",
        "help			print help information",
	NULL
    };

    if (cmd->tx_argc > 1)
    {
	option = Lookup(cmd->tx_argv[1], cmdAntennaCheckOption);
	if (option < 0) option = ANTENNACHECK_RUN;
	else argv++;
    }

    switch (option)
    {
	case ANTENNACHECK_RUN:
	    goto runantennacheck;
	    break;
	case ANTENNACHECK_HELP:
usage:
	    for (msg = &(cmdAntennaCheckOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    %s\n", *msg);
	    }
	    break;
    }
    return;

runantennacheck:

    EFInit();
    EFCapThreshold = INFINITY;
    EFResistThreshold = INFINITY;

    /* Process command line arguments */
    inName = EFArgs(argc, argv, &err_result, antennacheckArgs, (ClientData) NULL);

    if (err_result == TRUE)
    {
	EFDone();
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
	    EFDone();
	    return /* TCL_ERROR */;
	}
	inName = ((CellUse *) w->w_surfaceID)->cu_def->cd_name;
    }

    /*
     * Initializations specific to this program.
     */

    /* Read the hierarchical description of the input circuit */
    if (EFReadFile(inName, FALSE, FALSE, FALSE) == FALSE)
    {
	EFDone();
	return /* TCL_ERROR */;
    }

    /* Convert the hierarchical description to a flat one */
    flatFlags = EF_FLATNODES;
    EFFlatBuild(inName, flatFlags);

    EFVisitDevs(antennacheckVisit, (ClientData)NULL);
    EFFlatDone();
    EFDone();

    TxPrintf("antennacheck finished.\n");
}


/*
 * ----------------------------------------------------------------------------
 *
 * antennacheckArgs --
 *
 * Process those arguments that are specific to antennacheck.
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
antennacheckArgs(pargc, pargv)
    int *pargc;
    char ***pargv;
{
    char **argv = *pargv, *cp;
    int argc = *pargc;

    switch (argv[0][1])
    {
	default:
	    TxError("Unrecognized flag: %s\n", argv[0]);
	    goto usage;
    }

    *pargv = argv;
    *pargc = argc;
    return 0;

usage:
    TxError("Usage: antennacheck\n");
    return 1;
}



/*
 * ----------------------------------------------------------------------------
 *
 * antennacheckVisit --
 *
 * Procedure to check for antenna violations from a single device.
 * Called by EFVisitDevs().
 *
 * Results:
 *	Returns 0 always.
 *
 * Side effects:
 *	May tag other device records to avoid double-counting devices.
 *	Generates feedback entries if an antenna violation is found.
 *
 * ----------------------------------------------------------------------------
 */

int
antennacheckVisit(dev, hierName, scale, trans)
    Dev *dev;		/* Device being output */
    HierName *hierName;	/* Hierarchical path down to this device */
    float scale;	/* Scale transform for output */
    Transform *trans;	/* Coordinate transform */
{
    DevTerm *gate, *source, *drain;
    int l, w;
    Rect r;

    switch(dev->dev_class)
    {
	case DEV_FET:
	case DEV_MOSFET:
	    GeoTransRect(trans, &dev->dev_rect, &r);

	    /* Procedure:
	     *
	     * 1.  If device is marked visited, return.
	     * 2.  Mark device visited
	     * 3.  Mark all connected devices visited
	     * 4.  For each plane from metal1 up (determined by planeorder):
	     *   a.  Run SimTreeCopyConnect()
	     *   b.  Accumulate gate area of connected devices
	     *	 c.  Accumulate	metal area of connected devices
	     *	 d.  Check against antenna ratio
	     *	 e.  Generate feedback if in violation of antenna rule
	     *
	     * NOTE:  SimTreeCopyConnect() is used cumulatively, so that
	     * additional searching only needs to be done for the additional
	     * layer being searched.  This is the reason for using
	     * SimTreeCopyConnect() instead of DBTreeCopyConnect().
	     */

	    /* To be completed */
    }
    return 0;
}

