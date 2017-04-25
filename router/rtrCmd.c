/*
 * RouteCmd.c --
 *
 * Commands for the route modules only.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrCmd.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/styles.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "textio/txcommands.h"
#include "select/select.h"
#include "commands/commands.h"
#include "utils/heap.h"
#include "router/router.h"
#include "utils/netlist.h"
#include "netmenu/netmenu.h"
#include "gcr/gcr.h"
#include "grouter/grouter.h"

/* Global variable initialization */

bool RtrMazeStems = FALSE;	/* Set by default to original behavior */

/*
 * ----------------------------------------------------------------------------
 *
 * CmdGARouterTest --
 *
 * Debugging of gate-array router.
 *
 * Usage:
 *	*garoute cmd [args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See comments in GATest() in garouter/gaTest.c for details.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdGARouterTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    GATest(w, cmd);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdGRouterTest --
 *
 * Debugging of global router.
 *
 * Usage:
 *	*groute cmd [args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See comments in GlTest() in grouter/glTest.c for details.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdGRouterTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    GlTest(w, cmd);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdIRouterTest --
 *
 * Debugging of interactive router.
 *
 * Usage:
 *	*iroute [subcmd] [args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See comments in IRTest() in irouter/irTestCmd.c for details.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdIRouterTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    IRTest(w, cmd);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdMZRouterTest --
 *
 * Debugging of maze router.
 *
 * Usage:
 *	*mzroute [subcmd] [args]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See comments in MZTest() in mzrouter/mzTestCmd.c for details.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdMZRouterTest(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    MZTest(w, cmd);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdChannel --
 *
 * Implement the "channel" command.  Generate a cell __CHANNEL__ showing
 * the channel structure for the edit cell, within the area of the box.
 *
 * Useage:
 *      :channel [netlist]
 *      :channel [-]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a __CHANNEL__ def if it does not already exist.  Paints
 *	on the error layer of that def.  Uses the feedback layer to display
 *	the channels that result.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdChannel(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    Rect newBox;
    CellDef *def, *RtrDecomposeName();
    char *name;
    extern int cmdChannelFunc();		/* Forward declaration. */

    if (cmd->tx_argc > 3)
    {
	TxError("Usage: %s [netlist | -]\n", cmd->tx_argv[0]);
	return;
    }

    if (!ToolGetEditBox(&newBox))
	return;

    name = (char *) NULL;
    if (cmd->tx_argc == 2)
	name = cmd->tx_argv[1];

    if (RtrDecomposeName(EditCellUse, &newBox, name) == (CellDef *) NULL)
    {
	TxError("\nRouting area (box) is too small to hold useful channels.\n");
	return;
    }
    TxPrintf("\n");

    /* Display the channels using feedback. */

    def = DBCellLookDef("__CHANNEL__");
    if (def == NULL) return;
    (void) DBSrPaintArea((Tile *) NULL, def->cd_planes[PL_DRC_ERROR],
	&newBox, &DBSpaceBits, cmdChannelFunc,
	(ClientData) NULL);
}

int
cmdChannelFunc(tile)
    Tile *tile;
{
    Rect area, rootArea;

    TiToRect(tile, &area);
    GeoTransRect(&EditToRootTransform, &area, &rootArea);
    DBWFeedbackAdd(&area, "Channel area", EditRootDef, 1,
	STYLE_OUTLINEHIGHLIGHTS);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdGaRoute --
 *
 * Command interface for gate-array routing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on the command; see below.
 *
 * ----------------------------------------------------------------------------
 */

void
CmdGaRoute(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    typedef enum { CHANNEL, GEN, HELP, NOWARN, RESET, ROUTE, WARN } cmdType;
    static char *chanTypeName[] = { "NORMAL", "HRIVER", "VRIVER" };
    extern bool GAStemWarn;
    char *name, *channame;
    int n, chanType;
    Rect editArea;
    FILE *f;
    static struct
    {
	char	*cmd_name;
	cmdType	 cmd_val;
    } cmds[] = {
	"channel xl yl xh yh [type]\n\
channel	[type]		define a channel",			CHANNEL,
	"generate type [file]	generate channel definition commands",
								GEN,
	"help			print this message",		HELP,
	"nowarn			only warn if all locations of a terminal\n\
			are unreachable",			NOWARN,
	"route [netlist]		route the current cell",ROUTE,
	"reset			clear all channel definitions",	RESET,
	"warn			leave feedback for each location of a\n\
			terminal that is unreachable",		WARN,
	0
    };

    GAInit();
    if (cmd->tx_argc == 1)
	goto doRoute;

    n = LookupStruct(cmd->tx_argv[1], (LookupTable *) cmds, sizeof cmds[0]);
    if (n < 0)
    {
	if (n == -1)
	    TxError("Ambiguous option: \"%s\"\n", cmd->tx_argv[1]);
	else
	    TxError("Unrecognized routing command: %s\n", cmd->tx_argv[1]);
	TxError("    Type \"garoute help\" for help.\n");
	return;
    }

    switch (cmds[n].cmd_val)
    {
	case HELP:
	    TxPrintf("Gate-array router commands have the form:\n");
	    TxPrintf("\"garoute option\", where option is one of:\n\n");
	    for (n = 0; cmds[n].cmd_name; n++)
		TxPrintf("%s\n", cmds[n].cmd_name);
	    TxPrintf("\n");
	    break;
	case WARN:
	    if (cmd->tx_argc != 2) goto badWarn;
	    GAStemWarn = TRUE;
	    TxPrintf(
    "Will leave feedback for each unusable terminal loc.\n");
	    break;
	case NOWARN:
	    if (cmd->tx_argc != 2)
	    {
badWarn:
		TxError("Usage: \"garoute warn\" or \"garoute nowarn\"\n");
		return;
	    }
	    GAStemWarn = FALSE;
	    TxPrintf(
    "Will only leave feedback if all locs for a terminal are unusable.\n");
	    break;
	case RESET:
	    TxPrintf("Clearing all channel information.\n");
	    GAClearChannels();
	    break;
	case GEN:
	    if (cmd->tx_argc < 3 || cmd->tx_argc > 4)
	    {
		TxError("Usage: garoute generate type [file]\n");
		return;
	    }
	    if (!ToolGetEditBox(&editArea))
		return;
	    channame = cmd->tx_argv[2];
	    f = stdout;
	    if (cmd->tx_argc == 4)
	    {
		f = fopen(cmd->tx_argv[3], "w");
		if (f == NULL)
		{
		    perror(cmd->tx_argv[3]);
		    return;
		}
	    }
	    if (channame[0] == 'h') GAGenChans(CHAN_HRIVER, &editArea, f);
	    else if (channame[0] == 'v') GAGenChans(CHAN_VRIVER, &editArea, f);
	    else
	    {
		TxError("Unrecognized channel type: %s\n", channame);
		TxError("Legal types are \"h\" or \"v\"\n");
	    }
	    if (f != stdout)
		(void) fclose(f);
	    break;
	case CHANNEL:
	    channame = (char *) NULL;
	    if (cmd->tx_argc == 2 || cmd->tx_argc == 3)
	    {
		if (!ToolGetEditBox(&editArea))
		    return;
		if (cmd->tx_argc == 3)
		    channame = cmd->tx_argv[2];
		else
		    chanType = CHAN_NORMAL;	/* default */
	    }
	    else if (cmd->tx_argc == 6 || cmd->tx_argc == 7)
	    {
		editArea.r_xbot = cmdParseCoord(w, cmd->tx_argv[2], FALSE, TRUE);
		editArea.r_ybot = cmdParseCoord(w, cmd->tx_argv[3], FALSE, FALSE);
		editArea.r_xtop = cmdParseCoord(w, cmd->tx_argv[4], FALSE, TRUE);
		editArea.r_ytop = cmdParseCoord(w, cmd->tx_argv[5], FALSE, FALSE);
		chanType = CHAN_NORMAL;
		if (cmd->tx_argc == 7)
		    channame = cmd->tx_argv[6];
	    }
	    else goto badChanCmd;
	    if (channame)
	    {
		if (channame[0] == 'h') chanType = CHAN_HRIVER;
		else if (channame[0] == 'v') chanType = CHAN_VRIVER;
		else
		{
		    TxError("Unrecognized channel type: %s\n", channame);
		    goto badChanCmd;
		}
	    }
	    TxPrintf("Channel [%s] %d %d %d %d\n", chanTypeName[chanType],
		    editArea.r_xbot, editArea.r_ybot,
		    editArea.r_xtop, editArea.r_ytop);
	    if (!GADefineChannel(chanType, &editArea))
	    {
		TxError("Channel definition failed.\n");
		break;
	    }
	    break;
	case ROUTE:
	doRoute:
	    if (cmd->tx_argc > 3)
	    {
		TxError("Usage: garoute route [netlist]\n");
		break;
	    }
	    name = (char *) NULL;
	    if (cmd->tx_argc == 3)
		name = cmd->tx_argv[2];
	    n = GARouteCmd(EditCellUse, name);
	    if (n < 0)
		TxError("Couldn't route at all.\n");
	    else if (n > 0)
		TxPrintf("%d routing error%s.\n", n, n == 1 ? "" : "s");
	    else
		TxPrintf("No routing errors.\n");
	    break;
    }
    return;

badChanCmd:
    TxError("Usage: garoute channel xlo ylo xhi yhi [type]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdIRoute --
 *
 * Interactive route command.
 * Calls command entry point in irouter/irCommand.c
 *
 * Usage:
 *	iroute [subcmd [args]]
 *	(See irCommand.c for details.)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on subcommand, see irCommand.c
 *
 * ----------------------------------------------------------------------------
 */

void
CmdIRoute(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    IRCommand(w,cmd);

    return;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CmdRoute --
 *
 * 	Route the nets in the current netlist.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

#define ROUTERCHECKS	0
#define ROUTERDEBUG	1
#define ROUTERFILE	2
#define ROUTERSHOWMAP	3
#define ROUTERSHOWRES	4
#define ROUTERSHOWEND	5
#define ROUTEREND	6
#define ROUTERHELP	7
#define ROUTERJOG	8
#define ROUTERMMAX	9
#define ROUTERNETLIST	10
#define ROUTEROBST	11
#define	ROUTERORIGIN	12
#define ROUTERSTATS     13
#define ROUTERSETTINGS	14
#define ROUTERSTEADY	15
#define ROUTERTECHINFO	16
#define ROUTERVIAS	17
#define	ROUTERVIAMIN	18
#define	ROUTERMAZESTEMS	19

void
CmdRoute(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int option;
    GCRChannel *ch;
    Rect area;
    NLNetList netList;
    char *netListName;

    static char *cmdRouteOption[] =
    {	
	"*checks		toggle column checking",
	"*debug		toggle router trace messages",
	"*file <file>		route from a channel data file",
	"*map			toggle channel obstacle map display",
	"*showcolumns		toggle channel column text display",
	"*showfinal		toggle text display of final channels",

	"end <real>		set channel router end constant",
	"help			print this help information",
	"jog int		set minimum jog length (grid units)",
	"metal			toggle metal maximization",
	"netlist <file>		set current netlist",
	"obstacle <real>	set obstacle constant",
	"origin [x y]		print or set routing grid origin",
	"stats			print and clear previous route statistics",
	"settings		show router parameters", 
	"steady <int>		set steady net constant",
	"tech			show router technology information",
	"vias <int>		set metal maximization via limit (grid units)",
	"viamin			via minimization",
	"mazestems		use maze router to route channel stems to pins",
	NULL
    };

    if (cmd->tx_argc == 1)
    {
	if (!ToolGetEditBox(&area))
	    return;
	Route(EditCellUse, &area);
	return;
    }

    option = Lookup(cmd->tx_argv[1], cmdRouteOption);
    if (option == -1)
    {
	TxError("Ambiguous routing option: \"%s\"\n", cmd->tx_argv[1]);
	goto usage2;
    }
    if (option < 0)
	goto usage;

    switch (option)
    {
	/* These first few options are for wizards only.
	 */
	case ROUTERDEBUG:
	    GcrDebug= !GcrDebug;
	    TxPrintf("Router debug tracing: %s\n", GcrDebug ? "on" : "off");
	    break;
	case ROUTERCHECKS:
	    GcrNoCheck= !GcrNoCheck;
	    TxPrintf("Router column checking: %s\n", GcrNoCheck ? "on" : "off");
	    break;
	case ROUTERFILE:
	    /*  Display routing problems from file directly on the screen.
	     */
	    if(cmd->tx_argc!=3)
		goto wrongNumArgs;
	    if((ch = GCRRouteFromFile(cmd->tx_argv[2])) == (GCRChannel *) NULL)
		 TxError("Bad channel from file %s\n", cmd->tx_argv[2]);
	    else RtrPaintBack(ch, EditCellUse->cu_def);
	    break;
	case ROUTERSHOWRES:
	    GcrShowResult= !GcrShowResult;
	    TxPrintf("Show channel columns: %s\n", GcrShowResult ? "on":"off");
	    break;
	case ROUTERSHOWEND:
	    GcrShowEnd= !GcrShowEnd;
	    TxPrintf("Show finished channels: %s\n", GcrShowEnd ? "on" : "off");
	    break;
	case ROUTERSHOWMAP:
	    GcrShowMap= !GcrShowMap;
	    TxPrintf("Show channel maps: %s\n", GcrShowMap ? "on" : "off");
	    break;

	case ROUTERHELP:
	    TxPrintf("Router commands have the form \"route option\",\n");
	    TxPrintf("where option is one of:\n\n");
	    if(cmd->tx_argc==2)
		for (option=ROUTEREND; option<= ROUTERMAZESTEMS; option++)
		    TxPrintf("  %s\n", cmdRouteOption[option]);
	    else
	    if((cmd->tx_argc==3) && !strcmp(cmd->tx_argv[2], "*wizard"))
		for (option=0; option !=ROUTEREND; option++)
			TxPrintf("  %s\n", cmdRouteOption[option]);
	    TxPrintf("\n");
	    break;
	case ROUTERSTATS:
	    RtrPaintStats(TT_SPACE, 0);
	    break;
	case ROUTERSETTINGS:
	    TxPrintf("Router parameter settings:\n");
	    TxPrintf("   Channel end constant... %f\n", RtrEndConst);
	    TxPrintf("   Metal maximization..... %s\n", RtrDoMMax ? "on":"off");
	    TxPrintf("   Minimum jog length..... %d\n", GCRMinJog);
	    TxPrintf("   Net list............... \"%s\"\n", NMNetlistName());
	    TxPrintf("   Obstacle constant...... %f\n", GCRObstDist);
	    TxPrintf("   Steady net constant.... %d\n", GCRSteadyNet);
	    TxPrintf("   Via limit.............. %d\n", RtrViaLimit);
	    TxPrintf("   Maze route stems....... %s\n", RtrMazeStems ? "yes" : "no");
	    if((cmd->tx_argc!=3) || strcmp(cmd->tx_argv[2], "*wizard"))
		break;
	    TxPrintf("\n");
	    TxPrintf("   Debug tracing.......... %s\n", GcrDebug ? "on":"off");
	    TxPrintf("   Column checking........ %s\n", GcrNoCheck?"on":"off");
	    TxPrintf("   Show channel columns... %s\n",
		    GcrShowResult ? "on" : "off");
	    TxPrintf("   Show finished channels  %s\n", GcrShowEnd?"on":"off");
	    TxPrintf("   Show channel maps...... %s\n", GcrShowMap?"on":"off");
	    break;
	case ROUTERTECHINFO:
	    TxPrintf("Router Technology Information:\n");
	    TxPrintf("   Preferred layer..... %s; width %d\n",
		    DBTypeLongName(RtrMetalType), RtrMetalWidth);
	    TxPrintf("   Alternate layer..... %s; width %d\n",
		    DBTypeLongName(RtrPolyType), RtrPolyWidth);
	    TxPrintf("   Contacts............ %s; ",
		    DBTypeLongName(RtrContactType));
	    TxPrintf("width %d; offset %d; surrounds %d, %d\n", RtrContactWidth,
		    RtrContactOffset, RtrMetalSurround, RtrPolySurround);
	    TxPrintf("   Subcell separations: %d up; %d down\n",
		    RtrSubcellSepUp, RtrSubcellSepDown);
	    TxPrintf("   Router grid spacing: %d\n", RtrGridSpacing);
	    break;
	case ROUTERMMAX:
	    RtrDoMMax= !RtrDoMMax;
	    TxPrintf("Metal maximization: %s\n", RtrDoMMax ? "on" : "off");
	    break;
	case ROUTERVIAS:
	    if(cmd->tx_argc!=2)
	    {
		if(cmd->tx_argc!=3)
		    goto wrongNumArgs;
		if(!sscanf(cmd->tx_argv[2], "%d", &RtrViaLimit))
		    TxError("Bad value for via limit\n");
	    }
	    TxPrintf("Via limit is %d\n", RtrViaLimit);
	    break;
	case ROUTEREND:
	    if(cmd->tx_argc!=2)
	    {
		if(cmd->tx_argc!=3)
		    goto wrongNumArgs;
		if(!sscanf(cmd->tx_argv[2], "%f", &RtrEndConst))
		    TxError("Bad value for channel end distance\n");
	    }
	    TxPrintf("Channel end constant is %f\n", RtrEndConst);
	    break;
	case ROUTERJOG:
	    if(cmd->tx_argc!=2)
	    {
		if(cmd->tx_argc!=3)
		    goto wrongNumArgs;
		if(!sscanf(cmd->tx_argv[2], "%d", &GCRMinJog))
		    TxError("Bad value for minimum jog length\n");
	    }
	    TxPrintf("Minimum jog length is %d\n", GCRMinJog);
	    break;
	case ROUTEROBST:
	    if(cmd->tx_argc!=2)
	    {
		if(cmd->tx_argc!=3)
		    goto wrongNumArgs;
		if(!sscanf(cmd->tx_argv[2], "%f", &GCRObstDist))
		    TxError("Bad value for obstacle constant\n");
	    }
	    TxPrintf("Obstacle constant is %f\n", GCRObstDist);
	    break;
	case ROUTERSTEADY:
	    if(cmd->tx_argc!=2)
	    {
		if(cmd->tx_argc!=3)
		    goto wrongNumArgs;
		if(!sscanf(cmd->tx_argv[2], "%d", &GCRSteadyNet))
		    TxError("Bad value for steady net constant\n");
	    }
	    TxPrintf("Steady net constant is %d\n", GCRSteadyNet);
	    break;
	case ROUTERNETLIST:
	    if(cmd->tx_argc!=2)
	    {
		if(cmd->tx_argc!=3)
		    goto wrongNumArgs;
		NMNewNetlist(cmd->tx_argv[2]);
	    }
	    TxPrintf("Current list is \"%s\"\n", NMNetlistName());
	    break;
	case ROUTERORIGIN:
	    if (cmd->tx_argc != 2)
	    {
		if (cmd->tx_argc != 4)
		    goto wrongNumArgs;
		RtrOrigin.p_x = cmdParseCoord(w, cmd->tx_argv[2], FALSE, TRUE);
		RtrOrigin.p_y = cmdParseCoord(w, cmd->tx_argv[3], FALSE, FALSE);
		break;
	    }
	    TxPrintf("Routing grid origin = (%d,%d)\n",
			RtrOrigin.p_x, RtrOrigin.p_y);
	    break;
	case ROUTERVIAMIN:
	    if (!ToolGetEditBox(&area))
		return;
	    if (!NMHasList())
	    {
		netListName = EditCellUse->cu_def->cd_name;
		TxPrintf("No netlist selected yet;  using \"%s\".\n", netListName);
		NMNewNetlist(netListName);
	    }
	    else
		netListName = NMNetlistName();

	    if ( NLBuild(EditCellUse, &netList))
	    {
		int nvia;

		nvia = RtrViaMinimize(EditCellUse->cu_def);
		DBWAreaChanged(EditCellUse->cu_def, &area,
		    DBW_ALLWINDOWS, &DBAllButSpaceBits);
		WindUpdate();
		TxPrintf("\n%d vias removed\n",nvia);
		NLFree(&netList);
	    }
	    break;
	case ROUTERMAZESTEMS:
	    RtrMazeStems = !RtrMazeStems;
	    TxPrintf("Maze route channel stems: %s\n", RtrMazeStems ? "yes" : "no");
	    break;
    } /* switch*/
    return;

wrongNumArgs:
    TxError("Wrong number of arguments to %s option.\n", cmd->tx_argv[1]);
    TxError("Type \":route help\" for help.\n");
	    return;
usage:
    TxError("\"%s\" isn't a valid router option.", cmd->tx_argv[1]);

usage2:
    TxError("  Type \"route help\" for help.\n");
    return;
}

/*
 * ----------------------------------------------------------------------------
 * CmdSeeFlags --
 *
 * 	Display router-generated flags on the highlight layer.  User points
 *	to a channel and invokes the command with an argument naming the
 *	flag to be displayed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paints highlights on the screen.  Allocates a special cell def and
 *	initializes a hash table glTileToChannel the first time it is called.
 * ----------------------------------------------------------------------------
 */

void
CmdSeeFlags(w, cmd)
    MagWindow * w;
    TxCommand *cmd;
{
    Rect      rootRect;
    Point     point;
    MagWindow * window;

    window = CmdGetRootPoint(&point, &rootRect);
    if (window == (MagWindow *) NULL)
	return;

    if(cmd->tx_argc > 2)
    {
	TxError("Useage:  %s [flag name]\n", cmd->tx_argv[0]);
	return;
    }
    if(cmd->tx_argc == 2)
    {
	GCRShow(&point, cmd->tx_argv[1]);
	TxError("%s:  flag highlights turned on.\n", cmd->tx_argv[0]);
    }
    else
    {
	NMUnsetCell();
	TxError("%s:  flag highlights turned off.\n", cmd->tx_argv[0]);
    }
}

