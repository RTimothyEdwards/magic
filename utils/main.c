/*
 * main.c --
 *
 * The topmost module of the Magic VLSI tool.  This module
 * initializes the other modules and then calls the 'textio'
 * module to read and execute commands.
 *
 *     ********************************************************************* 
 *     * Copyright (C) 1985, 1990 Regents of the University of California. * 
 *     * Permission to use, copy, modify, and distribute this              * 
 *     * software and its documentation for any purpose and without        * 
 *     * fee is hereby granted, provided that the above copyright          * 
 *     * notice appear in all copies.  The University of California        * 
 *     * makes no representations about the suitability of this            * 
 *     * software for any purpose.  It is provided "as is" without         * 
 *     * express or implied warranty.  Export of this software outside     * 
 *     * of the United States of America may require an export license.    * 
 *     *********************************************************************
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/main.c,v 1.2 2008/02/07 17:33:19 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/times.h>
#include <sys/time.h>

#include "tcltk/tclmagic.h"
#include "utils/main.h"
#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/magsgtty.h"
#include "utils/hash.h"
#include "utils/macros.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "textio/txcommands.h"
#include "tiles/tile.h"
#include "utils/tech.h"
#include "database/database.h"
#include "drc/drc.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "commands/commands.h"
#include "utils/signals.h"
#include "utils/utils.h"
#include "utils/runstats.h"
#include "cif/cif.h"
#ifdef ROUTE_MODULE
#include "router/router.h"
#endif
#ifdef LEF_MODULE
#include "lef/lef.h"
#endif
#include "extract/extract.h"
#include "utils/undo.h"
#include "netmenu/netmenu.h"
#include "plow/plow.h"
#include "utils/paths.h"
#include "wiring/wiring.h"
#ifdef PLOT_MODULE
#include "plot/plot.h"
#endif
#include "sim/sim.h"
#include "utils/list.h"
#ifdef ROUTE_MODULE
#include "mzrouter/mzrouter.h"
#endif
#include "lisp/lisp.h"
#ifdef THREE_D
#include "graphics/wind3d.h"
#endif


/*
 * Global data structures
 *
 */

global char	*Path = NULL;		/* Search path */
global char	*CellLibPath = NULL;	/* Used to find cells. */
global char	*SysLibPath = NULL;	/* Used to find color maps, styles, */
					/* technologies, etc. */

/*
 * Flag that tells if various options have been set on the command line
 * (see utils.h for explanation of individual flags).
 */

global short RuntimeFlags = MAIN_MAKE_WINDOW;

/*
 * See the file main.h for a description of the information kept
 * pertaining to the edit cell.
 */

global CellUse	*EditCellUse = NULL;
global CellDef	*EditRootDef = NULL;
global Transform EditToRootTransform;
global Transform RootToEditTransform;


/*
 * data structures local to main.c
 *
 */

/* the filename specified on the command line */
static char *MainFileName = NULL;	

/* RC file specified on the command line */
static char *RCFileName = NULL;	

/* Definition of file types that magic can read */
#define FN_MAGIC_DB	0
#define FN_LEF_FILE	1
#define FN_DEF_FILE	2
#define FN_GDS_FILE	3
#define FN_CIF_FILE	4
#define FN_TCL_SCRIPT	5

/* List of filenames specified on the command line */
typedef struct filename {
    char *fn;
    unsigned char fn_type;
    struct filename *fn_prev;
} FileName;
FileName *CurrentName;

/* tech name specified on the command line */
static char *TechDefault = NULL;

/* the filename for the graphics and mouse ports */
global char *MainGraphicsFile = NULL;
global char *MainMouseFile = NULL;

/* information about the color display. */
global char *MainDisplayType = NULL;
global char *MainMonType = NULL;


/* Copyright notice for the binary file. */
global char *MainCopyright = "\n--- MAGIC: Copyright (C) 1985, 1990 "
		"Regents of the University of California.  ---\n";

/* Forward declarations */
char *mainArg();


/*
 * ----------------------------------------------------------------------------
 * MainExit:
 *
 *	Magic's own exit procedure
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	We exit.
 * ----------------------------------------------------------------------------
 */

void
MainExit(errNum)
    int errNum;
{
#ifdef	MOCHA
    MochaExit(errNum);
#endif
    if (GrClosePtr != NULL) /* We are not guarenteed that everthing will
			     * be initialized already!
			     */
	GrClose();

    DBRemoveBackup();

    TxFlush();
    TxResetTerminal();

#ifdef MAGIC_WRAPPER

    // NOTE:  This needs to be done in conjunction with the following
    // commands in the console:
    // (1) tkcon eval rename ::exit ::quit
    // (2) tkcon eval proc::exit args {slave eval quit}
    //
    // The lines above redirect tkcon's "exit" routine to be named
    // "quit" (in the console, not the slave!).  Because the tkcon
    // File->Exit callback is set to eval "exit", we then can create
    // a new proc called "exit" in the console that runs "quit" in
    // the slave (magic), and will therefore do the usual checks to
    // save work before exiting;  if all responses are to exit without
    // saving, then it finally gets to here, where it runs the (renamed)
    // "quit" command in tkcon.  That will ensure that tkcon runs
    // various cleanup activities such as saving the command-line
    // history file before the final (!) exit.

    if (TxTkConsole)
	Tcl_Eval(magicinterp, "catch {tkcon eval quit}\n");
#endif

    exit(errNum);
}

/*
 * ----------------------------------------------------------------------------
 *
 * mainDoArgs:
 *
 *	Process command line arguments
 *
 * Results:
 *	Return 0 on success, 1 on failure
 *
 * Side effects:
 *	Global variables are modified
 *
 * Notes:
 *	In order to work properly with the -F flag, we need to
 *	use StrDup() to make copies of any arguments we want
 *	to be visible when we restart a frozen file.
 *
 * ----------------------------------------------------------------------------
 */

int
mainDoArgs(argc, argv)
    int argc;
    char **argv;
{
    bool haveDashI = FALSE;
    char *cp;

    /* Startup filename (may be changed with the "-rcfile" option or	*/
    /* the "-norcfile" option).						*/

    RCFileName = StrDup((char **) NULL, ".magicrc");

    while (--argc > 0)
    {
	argv++;
	if (**argv == '-')
	{
	    switch (argv[0][1])
	    {
		case 'g':
		    if ((cp = mainArg(&argc, &argv, "tty name")) == NULL)
			return 1;
		    MainGraphicsFile = StrDup((char **) NULL, cp);
		    if (!haveDashI)
			MainMouseFile = MainGraphicsFile;
		    break;

		case 'i':
		    haveDashI = TRUE;
		    if ((cp = mainArg(&argc, &argv, "tty name")) == NULL)
			return 1;
		    MainMouseFile = StrDup((char **) NULL, cp);
		    break;

		case 'd':
		    if ((cp = mainArg(&argc, &argv, "display type")) ==NULL)
			return 1;
		    MainDisplayType = StrDup((char **) NULL, cp);
		    break;

		case 'm':
		    if ((cp = mainArg(&argc, &argv, "monitor type")) ==NULL)
			return 1;
		    MainMonType = StrDup((char **) NULL, cp);
		    break;

		/*
		 * Declare the technology.
		 */
		case 'T':
		    if ((cp = mainArg(&argc, &argv, "technology")) == NULL)
			return 1;
		    TechDefault = StrDup((char **) NULL, cp);
		    TechOverridesDefault = TRUE;
		    break;

		/*
		 * -r or -re or -recover: Recover a crash file.
		 * -rc or -rcfile: Declare a specific startup file to read.
		 */
		case 'r':
		    if ((strlen(argv[0]) <= 2) || argv[0][2] == 'e')
			RuntimeFlags |= MAIN_RECOVER;
		    else if ((argc > 1) && (argv[0][2] == 'c'))
		    {
			argv[0][2] = '\0';
		        if ((cp = mainArg(&argc, &argv, "startup file")) == NULL)
			    return 1;
			RCFileName = StrDup((char **) NULL, cp);
		    }
		    else
		    {
			TxError("Unknown option: '%s'\n", *argv);
			return 1;
		    }
		    break;

		/* 
		 * We are being debugged.
		 */
		case 'D':
		    RuntimeFlags |= MAIN_DEBUG;
		    break;

#ifdef MAGIC_WRAPPER
		/*
		 * "-w" for wrapper * implies -nowindow (no initial window)
		 */
		case 'w':
		    RuntimeFlags &= ~MAIN_MAKE_WINDOW;
		    break;
		/*
		 * No initial window / no console options / no startup file read.
		 */
		case 'n':
		    if (strlen(argv[0]) < 4)
		    {
			TxError("Ambiguous option %s:  use -nowindow, -noconsole, "
				"or -norcfile\n", argv[0]);
			return 1;
		    }
		    else if (argv[0][3] == 'c')
			RuntimeFlags &= ~MAIN_TK_CONSOLE;
		    else if (argv[0][3] == 'w')
			RuntimeFlags &= ~MAIN_MAKE_WINDOW;
		    else if (argv[0][3] == 'r')
		    {
			freeMagic(RCFileName);
			RCFileName = NULL;
		    }
		    else
		    {
			TxError("Unknown option: '%s'\n", *argv);
			return 1;
		    }
		    break;
#endif
		default:
		    TxError("Unknown option: '%s'\n", *argv);
		    TxError("Usage:  magic [-g gPort] [-d devType] [-m monType] "
				"[-i tabletPort] [-D] [-F objFile saveFile]\n"
				"[-T technology] [-rcfile startupFile | -norcfile]"
#ifdef MAGIC_WRAPPER
				"[-noconsole] [-nowindow] [-wrapper] "
#endif
				"[file]\n");
		    return 1;
	    }
	}
	else if (MakeMainWindow)
	{
	    if (MainFileName == NULL) {
		MainFileName = StrDup((char **) NULL, *argv);
		CurrentName = (FileName *) mallocMagic(sizeof(FileName));
		CurrentName->fn = MainFileName;
		CurrentName->fn_prev = (FileName *) NULL;
		CurrentName->fn_type = FN_MAGIC_DB;
	    }
	    else
	    {
		FileName *temporary;

		temporary = (FileName *) mallocMagic(sizeof(FileName));
		temporary->fn = StrDup((char **) NULL, *argv);
		temporary->fn_prev = CurrentName;
		temporary->fn_type = FN_MAGIC_DB;
		CurrentName = temporary;
	    }

	    /* Remove suffix if the file name already has it */
	    {
		char *c,*d;

		for(c = CurrentName->fn; (*c) != '\0'; c++);
		for(d = DBSuffix; (*d) != '\0'; d++);
		while( (*c) == (*d) ) {
		    if (c == MainFileName) break;
		    if (d == DBSuffix) {
			(*c) = '\0';
			break;
		    }
		    c--;
		    d--;
		}

		// Additional checks
		if ((c = strrchr(CurrentName->fn, '.')) != NULL)
		{
#ifdef LEF_MODULE
		    if (!strcasecmp(c, ".lef"))
			CurrentName->fn_type = FN_LEF_FILE;
		    else if (!strcasecmp(c, ".def"))
			CurrentName->fn_type = FN_DEF_FILE;
#endif
#ifdef CIF_MODULE
		    if (!strcasecmp(c, ".cif"))
			CurrentName->fn_type = FN_CIF_FILE;
		    else if (!strncasecmp(c, ".gds", 3))
			CurrentName->fn_type = FN_GDS_FILE;
#endif
#ifdef MAGIC_WRAPPER
		    if (!strcasecmp(c, ".tcl"))
			CurrentName->fn_type = FN_TCL_SCRIPT;
#endif
		}
	    }
	}
    }

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * mainArg --
 *
 * Pull off an argument from the (argc, argv) pair and also check
 * to make sure it's not another flag (i.e, it doesn't begin with
 * a '-').
 *
 * Results:
 *	Return pointer to the argument string.
 *
 * Side effects:
 *	See the comments in ArgStr() in the utils module -- they
 *	apply here.
 *
 * ----------------------------------------------------------------------------
 */

char *
mainArg(pargc, pargv, mesg)
    int *pargc;
    char ***pargv;
    char *mesg;
{
    char option, *cp;

    option = (*pargv)[0][1];
    cp = ArgStr(pargc, pargv, mesg);
    if (cp == NULL)
	return (char *) NULL;

    if (*cp == '-')
    {
	TxError("Bad name after '-%c' option: '%s'\n", option, cp);
	return (char *) NULL;
    }
    return cp;
}


/*
 * ----------------------------------------------------------------------------
 * mainInitBeforeArgs:
 *
 *	Initializes things before argument processing.
 *
 * Results:
 *	0 on success.  As written, there are no failure modes.
 *
 * Side effects:
 *	All sorts of initialization.  Most initialization, however, is done
 *	in 'mainInitAfterArgs'.
 * ----------------------------------------------------------------------------
 */

int
mainInitBeforeArgs(argc, argv)
    int argc;
    char *argv[];
{
    TechOverridesDefault = FALSE;
    if (Path == NULL)
	Path = StrDup((char **) NULL, ".");

    /* initialize text display */
    TxInit();
    TxSetTerminal();

#ifdef SCHEME_INTERPRETER
    /* Initialize Lisp module. (rajit@cs.caltch.edu) */
    LispInit();
#endif

    /*
     * Get preliminary info on the graphics display.
     * This may be overriden later.
     */
    GrGuessDisplayType(&MainGraphicsFile, &MainMouseFile, 
	&MainDisplayType, &MainMonType);
    FindDisplay((char *)NULL, "displays", CAD_LIB_PATH, &MainGraphicsFile,
	&MainMouseFile, &MainDisplayType, &MainMonType);

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 * mainInitAfterArgs:
 *
 *	Initializes things after argument processing.
 *
 * Results:
 *	Status: 0 = success, 1 = failure to set graphics display,
 *	2 = failure to load technology file.
 *
 * Side effects:
 *	All sorts of initialization.
 * ----------------------------------------------------------------------------
 */

int
mainInitAfterArgs()
{
    int (*nullProc)() = 0;
    SectionID sec_tech, sec_planes, sec_types, sec_aliases;
    SectionID sec_styles;
    SectionID sec_connect, sec_contact, sec_compose;
    SectionID sec_cifinput, sec_cifoutput;
    SectionID sec_drc, sec_extract, sec_wiring, sec_router;
    SectionID sec_plow, sec_plot, sec_mzrouter;

    DBTypeInit();
    MacroInit();

#ifdef LEF_MODULE
    /* Pre-techfile-loading intialization of the LEF module */
    LefInit();
#endif

#ifdef OPENACCESS
    OAInit();
#endif

    /*
     * Setup path names for system directory searches
     */

    StrDup(&SysLibPath, MAGIC_SYS_PATH);

    if (TechFileName != NULL)
    {
	CellLibPath = (char *)mallocMagic(strlen(MAGIC_LIB_PATH)
		+ strlen(TechFileName) - 1);
	sprintf(CellLibPath, MAGIC_LIB_PATH, TechFileName);
    }
    else
	CellLibPath = StrDup((char **)NULL, MAGIC_LIB_PATH);
    
    if (MainGraphicsFile == NULL) MainGraphicsFile = "/dev/null";
    if (MainMouseFile == NULL) MainMouseFile = MainGraphicsFile;

#ifdef MAGIC_WRAPPER
    /* Check for batch mode operation and disable interrupts in	*/
    /* batch mode by not calling SigInit().			*/
    if (Tcl_GetVar(magicinterp, "batch_mode", TCL_GLOBAL_ONLY) != NULL)
	SigInit(1);
    else
#endif

    /* catch signals, must come after mainDoArgs & before SigWatchFile */
    SigInit(0);

    /* set up graphics */
    if ( !GrSetDisplay(MainDisplayType, MainGraphicsFile, MainMouseFile) )
    {
	return 1;
    }

    /* initialize technology */
    TechInit();
    TechAddClient("tech", DBTechInit, DBTechSetTech, nullProc,
			(SectionID) 0, &sec_tech, FALSE);
    TechAddClient("version", DBTechInitVersion, DBTechSetVersion, nullProc,
			(SectionID) 0, (int *)0, TRUE);
    TechAddClient("planes",	DBTechInitPlane, DBTechAddPlane, nullProc,
			(SectionID) 0, &sec_planes, FALSE);
    TechAddClient("types", DBTechInitType, DBTechAddType, DBTechFinalType,
			sec_planes, &sec_types, FALSE);

    TechAddClient("styles", nullProc, DBWTechAddStyle, nullProc,
			sec_types, &sec_styles, FALSE);

    TechAddClient("contact", DBTechInitContact,
			DBTechAddContact, DBTechFinalContact,
			sec_types|sec_planes, &sec_contact, FALSE);

    TechAddAlias("contact", "images");
    TechAddClient("aliases", nullProc, DBTechAddAlias, nullProc,
			sec_planes|sec_types|sec_contact, &sec_aliases, TRUE);

    TechAddClient("compose", DBTechInitCompose,
			DBTechAddCompose, DBTechFinalCompose,
			sec_types|sec_planes|sec_contact, &sec_compose, FALSE);

    TechAddClient("connect", DBTechInitConnect,
			DBTechAddConnect, DBTechFinalConnect,
			sec_types|sec_planes|sec_contact, &sec_connect, FALSE);

#ifdef CIF_MODULE
    TechAddClient("cifoutput", CIFTechStyleInit, CIFTechLine, CIFTechFinal,
			(SectionID) 0, &sec_cifoutput, FALSE);
	
    TechAddClient("cifinput", CIFReadTechStyleInit, CIFReadTechLine,
		    CIFReadTechFinal, (SectionID) 0, &sec_cifinput, FALSE);
#else
    TechAddClient("cifoutput", nullProc,nullProc,nullProc,
			(SectionID) 0, &sec_cifoutput, FALSE);
	
    TechAddClient("cifinput", nullProc,nullProc,nullProc,
		     (SectionID) 0, &sec_cifinput, FALSE);
#endif
#ifdef ROUTE_MODULE
    TechAddClient("mzrouter", MZTechInit, MZTechLine, MZTechFinal,
			sec_types|sec_planes, &sec_mzrouter, TRUE);
#else
    TechAddClient("mzrouter", nullProc,nullProc,nullProc,
			sec_types|sec_planes, &sec_mzrouter, TRUE);
#endif
    TechAddClient("drc", DRCTechStyleInit, DRCTechLine, DRCTechFinal,
			sec_types|sec_planes, &sec_drc, FALSE);

#ifdef LEF_MODULE
    TechAddClient("lef", LefTechInit, LefTechLine, nullProc,
			sec_types|sec_planes, (SectionID *) 0, TRUE);
#endif

#ifdef NO_EXT
    TechAddClient("extract", nullProc, nullProc,nullProc,
			sec_types|sec_connect, &sec_extract, FALSE);
#else
    TechAddClient("extract", nullProc, ExtTechLine, ExtTechFinal,
			sec_types|sec_connect, &sec_extract, FALSE);
#endif
	
    TechAddClient("wiring", WireTechInit, WireTechLine, WireTechFinal,
			sec_types, &sec_wiring, TRUE);

#ifdef ROUTE_MODULE
    TechAddClient("router", RtrTechInit, RtrTechLine, RtrTechFinal,
			sec_types, &sec_router, TRUE);
#else
    TechAddClient("router", nullProc,nullProc,nullProc,
			sec_types, &sec_router, TRUE);
#endif
    TechAddClient("plowing", PlowTechInit, PlowTechLine, PlowTechFinal,
			sec_types|sec_connect|sec_contact, &sec_plow, TRUE);
#ifdef PLOT_MODULE
    TechAddClient("plot", PlotTechInit, PlotTechLine, PlotTechFinal,
			sec_types, &sec_plot, TRUE);
#else
    TechAddClient("plot", nullProc,nullProc,nullProc,
			sec_types, &sec_plot, TRUE);
#endif

    /* Load minimum technology file needed to keep things from	*/
    /* crashing during initialization.				*/

    if (!TechLoad("minimum", 0))
    {
	TxError("Cannot load technology \"minimum\" for initialization\n");
	return 2;
    }

    /* The minimum tech has been loaded only to keep the database from	*/
    /* becoming corrupted during initialization.  Free the tech file	*/
    /* name so that a "real" technology file can be forced to replace	*/
    /* it in mainInitFinal().						*/

    if (TechFileName != NULL)
    {
	freeMagic(TechFileName);
	TechFileName = NULL;
    }

    /* initialize the undo package */
    (void) UndoInit((char *) NULL, (char *) NULL);

    /* initialize windows */
    WindInit();

    /* initialize commands */
    CmdInit();

    /* Initialize the interface between windows and its clients */
    DBWinit();
#ifdef USE_READLINE
    TxInitReadline();
#endif
    CMWinit();
#ifdef THREE_D
    W3Dinit();
#endif

    /* Initialize the circuit extractor */
#ifndef NO_EXT
    ExtInit();
#endif

    /* Initialize plowing */
    PlowInit();

    /* Initialize selection */
    SelectInit();

    /* Initialize the wiring module */
    WireInit();

#ifdef ROUTE_MODULE
    /* Initialize the netlist menu */
    NMinit();
#endif

    /* Initialize the design-rule checker */
    DRCInit();

    /* Initialize the maze router */
#ifdef ROUTE_MODULE
    MZInit();

    /* Initialize the interactive router - 
     * NOTE the mzrouter must be initialized prior to the irouter
     * so that default parameters will be completely setup
     */
    IRDebugInit();
    IRAfterTech();
#endif

    PlowAfterTech();	/* Copies DRC rule information into plow database */

	/* Initialize the Sim Module (the part of it which involves (i)rsim) */
#if !defined(NO_SIM_MODULE) && defined(RSIM_MODULE)
    SimInit();
#endif

    TxSetPoint(GR_CURSOR_X, GR_CURSOR_Y, WIND_UNKNOWN_WINDOW);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * mainInitFinal:
 *
 *	Final initialization; reads startup files and any initial file
 *	specified.
 *
 * Results:
 *	Return 0 on success.  As written, there is no failure mode.
 *
 * Side effects:
 *	All sorts of initialization.
 * ----------------------------------------------------------------------------
 */

int
mainInitFinal()
{
    char *home, cwd[512];
    char startupFileName[256];
    FILE *f;
    char *rname;
    int result;

#ifdef MAGIC_WRAPPER

    /* Read in system pre-startup file, if it exists. */

    /* Use PaOpen first to perform variable substitutions, and	*/
    /* return the actual filename in rname.			*/

    f = PaOpen(MAGIC_PRE_DOT, "r", (char *) NULL, ".",
	    (char *) NULL, (char **) &rname);
    if (f != NULL)
    {
	fclose(f);
	result = Tcl_EvalFile(magicinterp, rname);
	if (result != TCL_OK)
	{
	    TxError("Error parsing pre-startup file \"%s\": %s\n", rname,
				Tcl_GetStringResult(magicinterp));
	    Tcl_ResetResult(magicinterp);
	}
    }
#endif	/* MAGIC_WRAPPER */

    // Make a first attempt to load the technology if specified on the
    // command line with the -T option.  This will preempt most other
    // ways that the technology file is determined.  If the technology
    // specified cannot be loaded, then the forced override is revoked.

    if ((TechFileName == NULL) && (TechDefault != NULL) && TechOverridesDefault)
    {
        if (!TechLoad(TechDefault, -2))
	{
            TxError("Failed to load technology \"%s\"\n", TechDefault);
	    TechOverridesDefault = FALSE;
	}
        else if (!TechLoad(TechDefault, 0))
	{
            TxError("Error loading technology \"%s\"\n", TechDefault);
	    TechOverridesDefault = FALSE;
	}
    }

#ifndef MAGIC_WRAPPER

    // Let the wrapper script be responsible for formatting and
    // printing the technology file informaiton.

    if (DBTechName != 0) {
	TxPrintf("Using technology \"%s\"", DBTechName);
	if (DBTechVersion != 0) TxPrintf(", version %s.", DBTechVersion);
	TxPrintf("\n");
    }
    if (DBTechDescription != 0) TxPrintf("%s\n", DBTechDescription);
#endif

#ifdef MAGIC_WRAPPER
    /* Read in system startup file, if it exists. */

    /* Use PaOpen first to perform variable substitutions, and	*/
    /* return the actual filename in rname.			*/

    f = PaOpen(MAGIC_SYS_DOT, "r", (char *) NULL, ".",
	    (char *) NULL, (char **) &rname);
    if (f != NULL)
    {
	fclose(f);
	result = Tcl_EvalFile(magicinterp, rname);
	if (result != TCL_OK)
	{
	    TxError("Error parsing system startup file \"%s\": %s\n", rname,
				Tcl_GetStringResult(magicinterp));
	    Tcl_ResetResult(magicinterp);
	}
    }

#else /* !MAGIC_WRAPPER */

    f = PaOpen(MAGIC_SYS_DOT, "r", (char *) NULL, ".",
	    (char *) NULL, (char **) NULL);
    if (f != NULL)
    { 
	TxDispatch(f); 
	(void) fclose(f);
    }

#endif  /* !MAGIC_WRAPPER */

    /*
     * Strive for a wee bit more parallelism; let the graphics
     * display run while we're reading in startup files & initial cell.
     */

    GrFlush();

    /* Ignore this whole section if we have received the -norc option */
    if (RCFileName != NULL)
    {

	/* Read in user's startup files, if there are any. */
	/* If the "-rcfile" option has been used, and it doesn't specify   */
	/* a full path, then look for this file in the home directory too. */

	home = getenv("HOME");

#ifdef MAGIC_WRAPPER

	if (home != NULL && (RCFileName[0] != '/'))
	{
	    Tcl_Channel fc;

	    (void) sprintf(startupFileName, "%s/%s", home, RCFileName);

	    fc = Tcl_OpenFileChannel(magicinterp, startupFileName, "r", 0);
	    if (fc != NULL)
	    {
		Tcl_Close(magicinterp, fc);
		result = Tcl_EvalFile(magicinterp, startupFileName);
		if (result != TCL_OK)
		{
		    TxError("Error parsing user \"%s\": %s\n", RCFileName,
				Tcl_GetStringResult(magicinterp));
		    Tcl_ResetResult(magicinterp);
		}
	    }
	    else
	    {
		/* Try the (deprecated) name ".magic" */
		(void) sprintf(startupFileName, "%s/.magic", home);
		fc = Tcl_OpenFileChannel(magicinterp, startupFileName, "r", 0);
		if (fc != NULL)
		{
		    TxPrintf("Note:  Use of the file name \"~/.magic\" is deprecated."
			"  Please change this to \"~/.magicrc\".\n");

		    Tcl_Close(magicinterp, fc);
		    result = Tcl_EvalFile(magicinterp, startupFileName);

		    if (result != TCL_OK)
		    {
			TxError("Error parsing user \".magic\": %s\n",
				Tcl_GetStringResult(magicinterp));
			Tcl_ResetResult(magicinterp);
		    }
		}
	    }
	}

        if (getcwd(cwd, 512) == NULL || strcmp(cwd, home))
	{
	    /* Read in the .magicrc file from the current directory, if	*/
	    /* different from HOME.					*/

	    Tcl_Channel fc;

	    fc = Tcl_OpenFileChannel(magicinterp, RCFileName, "r", 0);
	    if (fc != NULL)
	    {
		Tcl_Close(magicinterp, fc);
		result = Tcl_EvalFile(magicinterp, RCFileName);

		if (result != TCL_OK)
		{
		    // Print error message but continue anyway

		    TxError("Error parsing \"%s\": %s\n", RCFileName,
				Tcl_GetStringResult(magicinterp));
		    Tcl_ResetResult(magicinterp);
		    TxPrintf("Bad local startup file \"%s\", continuing without.\n",
					RCFileName);
		}
	    }
	    else
	    {
		/* Try the (deprecated) name ".magic" */

		Tcl_ResetResult(magicinterp);
		fc = Tcl_OpenFileChannel(magicinterp, ".magic", "r", 0);
		if (fc != NULL)
		{
		    Tcl_Close(magicinterp, fc);

		    TxPrintf("Note:  Use of the file name \".magic\" is deprecated."
				"  Please change this to \".magicrc\".\n");

		    result = Tcl_EvalFile(magicinterp, ".magic");

		    if (result != TCL_OK)
		    {
			// Print error message but continue anyway

			TxError("Error parsing local \".magic\": %s\n",
				Tcl_GetStringResult(magicinterp));
			Tcl_ResetResult(magicinterp);
			TxPrintf("Bad local startup file \".magic\","
					" continuing without.\n");
		    }
		}
		else
		{
		    /* Try the alternative name "magic_setup" */

		    Tcl_ResetResult(magicinterp);

		    fc = Tcl_OpenFileChannel(magicinterp, "magic_setup", "r", 0);
		    if (fc != NULL)
		    {
			Tcl_Close(magicinterp, fc);

			result = Tcl_EvalFile(magicinterp, "magic_setup");
			if (result != TCL_OK)
			{
			    TxError("Error parsing local \"magic_setup\": %s\n",
					Tcl_GetStringResult(magicinterp));
			    TxError("%s\n", Tcl_GetStringResult(magicinterp));
			    Tcl_ResetResult(magicinterp);	// Still not an error
			    TxPrintf("Bad local startup file \"magic_setup\","
					" continuing without.\n");
			}
		    }
		}
	    }
	}

#else /* !MAGIC_WRAPPER */

	if (home != NULL && (RCFileName[0] != '/'))
	{
	    (void) sprintf(startupFileName, "%s/%s", home, RCFileName);


	    f = PaOpen(startupFileName, "r", (char *) NULL, ".",
		(char *) NULL, (char **) NULL);

	    if ((f == NULL) && (!strcmp(RCFileName, ".magicrc")))
	    {
		/* Try the (deprecated) name ".magic" */
		(void) sprintf(startupFileName, "%s/.magic", home);
		f = PaOpen(startupFileName, "r", (char *) NULL, ".",
		    (char *) NULL, (char **) NULL);
		if (f != NULL)
		    TxPrintf("Note:  Use of the file name \"~/.magic\" is deprecated."
			"  Please change this to \"~/.magicrc\".\n");
	    }

	    if (f != NULL)
	    {
		TxDispatch(f); 
		(void) fclose(f);
	    }
	}

	/* Read in any startup file in the current directory, or one that was	*/
	/* specified on the commandline by the "-rcfile <name>" option.		*/

	f = PaOpen(RCFileName, "r", (char *) NULL, ".",
			(char *) NULL, (char **) NULL);

	/* Again, check for the deprecated name ".magic" */
	if (f == NULL)
	{
	    if (!strcmp(RCFileName, ".magicrc"))
	    {
		f = PaOpen(".magic", "r", (char *) NULL, ".",
			(char *) NULL, (char **) NULL);
		if (f != NULL)
		    TxPrintf("Note:  Use of the file name \"./.magic\" is deprecated."
				"  Please change this to \"./.magicrc\".\n");

		else
		    f = PaOpen("magic_setup", "r", (char *) NULL, ".",
				(char *) NULL, (char **) NULL);
	    }
	    else
		TxError("Startup file \"%s\" not found or unreadable!\n", RCFileName);
	}

	if (f != NULL)
	{
	    TxDispatch(f); 
	    fclose(f);
	}

#endif /* !MAGIC_WRAPPER */

    }

    /* We are done forcing the "tech load" command to be ignored */
    TechOverridesDefault = FALSE;

    /* If no technology has been specified yet, try to read one from
     * the initial cell, or else assign a default.
     */

    if ((TechFileName == NULL) && (TechDefault == NULL) && (MainFileName != NULL))
	StrDup(&TechDefault, DBGetTech(MainFileName));

    /* Load the technology file.  If any startup file loaded a		*/
    /* technology file, then "TechFileName" will be set, and we		*/
    /* should not override it.						*/

    if ((TechFileName == NULL) && (TechDefault != NULL))
    {
        if (!TechLoad(TechDefault, -2))
            TxError("Failed to load technology \"%s\"\n", TechDefault);
        else if (!TechLoad(TechDefault, 0))
            TxError("Error loading technology \"%s\"\n", TechDefault);
    }

    if (TechDefault != NULL)
    {
	freeMagic(TechDefault);
	TechDefault = NULL;
    }

    /* If that failed, then load the "minimum" technology again and	*/
    /* keep it.  It's not very useful, but it will keep everything	*/
    /* up and running.  In the worst case, if site.pre has removed the	*/
    /* standard locations from the system path, then magic will exit.	*/

    if (TechFileName == NULL)
	if (!TechLoad("minimum", 0))
	    return -1;

#ifdef SCHEME_INTERPRETER
    /* Pass technology name to Lisp interpreter (rajit@cs.caltech.edu) */
    LispSetTech (TechFileName);
#endif

    /*
     * Recover crash files from the temp directory if we have specified
     * the -r option on the command line (non-tcl version.  Tcl version
     * uses the command-line command crashrecover.
     */

    if (mainRecover && MakeMainWindow)
    {
	DBFileRecovery();
    }

    /*
     * Bring in a new cell or cells to start up if one was given
     * on the command line
     */

    else if (MainFileName && MakeMainWindow)
    {
	FileName *temporary;

	while(CurrentName != NULL)
	{
	    temporary = CurrentName;
	    CurrentName = temporary->fn_prev;
	    TxPrintf("Loading \"%s\" from command line.\n", temporary->fn);
	    switch (temporary->fn_type)
	    {
		case FN_MAGIC_DB:
		    DBWreload(temporary->fn);
		    break;
#ifdef LEF_MODULE
		case FN_LEF_FILE:
		    LefRead(temporary->fn, FALSE);
		    break;
		case FN_DEF_FILE:
		    DefRead(temporary->fn);
		    break;
#endif
#ifdef MAGIC_WRAPPER
		case FN_TCL_SCRIPT:
		    result = Tcl_EvalFile(magicinterp, temporary->fn);
		    if (result != TCL_OK)
		    {
			TxError("Error parsing \"%s\": %s\n",
				temporary->fn,
				Tcl_GetStringResult(magicinterp));
			Tcl_ResetResult(magicinterp);
		    }
		    break;
#endif
	    }
	    freeMagic(temporary);
	}
    }
    
    /* Create an initial box. */

    if (MakeMainWindow && EditCellUse)
	DBWSetBox(EditCellUse->cu_def, &EditCellUse->cu_def->cd_bbox);

    /* Set the initial fence for undo-ing:  don't want to be able to
     * undo past this point.
     */
    UndoFlush();
    TxClearPoint();

    return 0;
}


/*
 * ----------------------------------------------------------------------------
 * mainFinish:
 *
 *	Finish up things for Magic.  This routine is NOT called on an
 *	error exit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Various things, such as stopping measurement gathering.
 * ----------------------------------------------------------------------------
 */

void
mainFinished()
{
    /* Close up things */
    MainExit(0);
}

/*---------------------------------------------------------------------------
 * magicMain:
 *
 *	Top-level procedure of the Magic Layout System.  There is purposely
 *	not much in here so that we have more flexibility.  Also, it is
 *	not called 'main' so that other programs that use Magic may do
 *	something else.
 *
 * Results:	
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * Note:  Try not to add code to this procedure.  Add it instead to one of the
 *	procedures that it calls.
 *
 *----------------------------------------------------------------------------
 */

void
magicMain(argc, argv)
    int argc;
    char *argv[];
{
    int rstatus;

    if ((rstatus = mainInitBeforeArgs(argc, argv)) != 0) MainExit(rstatus);
    if ((rstatus = mainDoArgs(argc, argv)) != 0) MainExit(rstatus);
    if ((rstatus = mainInitAfterArgs()) != 0) MainExit(rstatus);
    if ((rstatus = mainInitFinal()) != 0) MainExit(rstatus);
    TxDispatch( (FILE *) NULL);
    mainFinished();
}



