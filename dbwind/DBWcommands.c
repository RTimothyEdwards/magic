/*
 * DBWCommands.c --
 *
 * 	This file contains the dispatch tables for layout commands.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/dbwind/DBWcommands.c,v 1.3 2010/06/24 12:37:16 tim Exp $";
#endif  /* not lint */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "textio/textio.h"
#include "textio/txcommands.h"

/*
 * Standard DBWind command set
 */

extern void CmdAddPath(), CmdArray();
extern void CmdBox(), CmdCellname(), CmdClockwise();
extern void CmdContact(), CmdCopy(), CmdCorner();
extern void CmdCrash(), CmdCrosshair();
extern void CmdDelete(), CmdDown(), CmdDrc(), CmdDump();
extern void CmdEdit(), CmdElement(), CmdErase(), CmdExpand(), CmdExtract();
extern void CmdFeedback(), CmdFill(), CmdFindBox(), CmdFindLabel(), CmdFlush();
extern void CmdGetcell(), CmdGrid(), CmdIdentify();
extern void CmdLabel(), CmdLoad();
extern void CmdMove(), CmdNetlist(), CmdPaint(), CmdPath(), CmdPlow();
extern void CmdPolygon(), CmdPort(), CmdProperty();
extern void CmdSave(), CmdScaleGrid(), CmdSee();
extern void CmdSelect(), CmdSetLabel(), CmdSideways();
extern void CmdShell(), CmdSnap();
extern void CmdStretch(), CmdStraighten();
extern void CmdTech(), CmdTool(), CmdUnexpand();
extern void CmdUpsidedown(), CmdWhat(), CmdWire(), CmdWriteall();
extern void CmdGoto(), CmdFlatten(), CmdXload(), CmdXor();

/*
 * Nonmanhattan geometry extensions
 */

extern void CmdSplit();
extern void CmdSplitErase();

/*
 * Wizard commands
 */

extern void CmdCoord();
extern void CmdExtractTest();
extern void CmdExtResis();
extern void CmdPsearch();
extern void CmdPlowTest();
extern void CmdShowtech();
extern void CmdTilestats();
extern void CmdTsearch();
extern void CmdWatch();

/*
 * CIF and GDS commands
 */

#ifdef CIF_MODULE
extern void CmdCif();
#endif
#ifdef CALMA_MODULE
extern void CmdCalma();
#endif

/*
 * Plot module commands
 */
#ifdef PLOT_MODULE
extern void CmdPlot();
#endif
#ifdef PLOT_AUTO
extern void CmdAutoPlot();
#endif

/*
 * SIM module commands
 */
#ifndef NO_SIM_MODULE
extern void CmdGetnode();
#ifdef RSIM_MODULE
extern void CmdRsim(), CmdSimCmd(), CmdStartRsim();
#endif
#endif

/*
 * Router module commands
 */

#ifdef ROUTE_MODULE
extern void CmdGARouterTest(), CmdGRouterTest(), CmdIRouterTest();
extern void CmdMZRouterTest(), CmdSeeFlags();
extern void CmdChannel(), CmdGaRoute(), CmdIRoute(), CmdRoute();
#endif
#ifdef ROUTE_AUTO
extern void CmdAutoRoute();
#endif

/*
 * LEF module commands
 */
#ifdef LEF_MODULE
extern void CmdLef();
#endif
#ifdef LEF_AUTO
extern void CmdAutoLef();
#endif

/*
 * Wrapper commands for ext2spice and ext2sim
 */ 

#ifdef MAGIC_WRAPPER
#ifdef EXT2SIM_AUTO
extern void CmdAutoExtToSim();
#else
extern void CmdExtToSim();
#endif
#ifdef EXT2SPICE_AUTO
extern void CmdAutoExtToSpice();
#else
extern void CmdExtToSpice();
#endif
#endif

/*
 * Readline extension history command
 */

#ifdef USE_READLINE
extern void CmdHistory();
#endif

/*
 * Commands only in the Lawrence Livermore Version 
 */

#ifdef	LLNL
extern void CmdMakeSW();
extern void CmdSgraph();
#endif	/* LLNL */

/*
 * --------------------------------------------------------------
 * DBWInitCommands --
 *
 *	Add commands to the dbw command-line interface.  As of
 *  Magic 7.2.41, this has been changed from a statically-pre-
 *  allocated array to a dynamically allocated array, allowing
 *  individual modules to add their commands outside of the
 *  scope of the "commands" subdirectory, and inside of their
 *  own initialization functions.  The bulk of the dbwind
 *  interface functions is still in this file, however, with
 *  only a few functions moved out to improve modularization
 *  of the source code.  It would be preferable to move every-
 *  thing out and remove the "commands" subdirectory altogether,
 *  but this is unlikely to happen.
 *
 * Results:
 *	None.
 *
 * SideEffects:
 *	DBWind commands are registered with the command interpreter.
 *
 * --------------------------------------------------------------
 */

void
DBWInitCommands()
{
    /* Add wizard commands */
    WindAddCommand(DBWclientID,
	"*coord			show coordinates of various things",
	CmdCoord, FALSE);
    WindAddCommand(DBWclientID,
	"*extract [args]	debug the circuit extractor",
	CmdExtractTest, FALSE);
    WindAddCommand(DBWclientID,
	"*plow cmd [args]	debug plowing",
	CmdPlowTest, FALSE);
    WindAddCommand(DBWclientID,
	"*psearch plane count	invoke point search over box area",
	CmdPsearch, FALSE);
    WindAddCommand(DBWclientID,
	"*showtech [file]	print internal technology tables",
	CmdShowtech, FALSE);
    WindAddCommand(DBWclientID,
	"*tilestats [file]	print statistics on tile utilization",
	CmdTilestats, FALSE);
    WindAddCommand(DBWclientID,
	"*tsearch plane count	invoke area search over box area",
	CmdTsearch, FALSE);
    WindAddCommand(DBWclientID,
	"*watch [plane]		enable verbose tile plane redisplay",
	CmdWatch, FALSE);
    WindAddCommand(DBWclientID,
	"addpath [path]		append to current search path",
	CmdAddPath, FALSE);
    WindAddCommand(DBWclientID,
	"array xsize ysize	OR\n"
	"array xlo xhi ylo yhi\n"
	"			array everything in selection",
	CmdArray, FALSE);
    WindAddCommand(DBWclientID,
	"box [dir [amount]]	move box dist units in direction or (with\n"
	"			no arguments) show box size",
	CmdBox, FALSE);
    WindAddCommand(DBWclientID,
	"cellname [list] children|parents|exists|self [name]	OR\n"
	"cellname [list] allcells|topcells\n"
	"			list cells by relationship to cell \"name\", or\n"
	"			the current selection if no name given.  \"list\"\n"
	"			returns result as a list. \"exists + name\" returns\n"
	"			true or false (also see command instance)",
	CmdCellname, FALSE);
    WindAddCommand(DBWclientID,
	"clockwise [deg]		rotate selection and box clockwise",
	CmdClockwise, FALSE);
    WindAddCommand(DBWclientID,
	"contact type		paint contact type on the intersection of its\n"
	"			residues",
	CmdContact, FALSE);
    WindAddCommand(DBWclientID,
	"copy [dir [amount]]	OR\n"
	"copy to x y		copy selection:  what used to be at box\n"
	"			lower-left will be copied at cursor location (or,\n"
	"			copy will appear amount units in dir from original);\n"
	"			second form will copy to location x y",
	CmdCopy, FALSE);
    WindAddCommand(DBWclientID,
	"corner d1 d2 [layers]	make L-shaped wires inside box, filling\n"
	"			first in direction d1, then in d2",
	CmdCorner, FALSE);
    WindAddCommand(DBWclientID,
	"crash save|recover [file]\n"
	"			recover the crash file \"file\", or the first\n"
	"			crash file discovered.",
	CmdCrash, FALSE);		
    WindAddCommand(DBWclientID,
	"crosshair x y | off	enable and move or disable the screen crosshair",
	CmdCrosshair, FALSE);		
    WindAddCommand(DBWclientID,
	"delete			delete everything in selection",
	CmdDelete, FALSE);
    WindAddCommand(DBWclientID,
	"down			load selected cell into a window",
	CmdDown, FALSE);
    WindAddCommand(DBWclientID,
	"drc option		design rule checker; type \"drc help\"\n"
	"			for information on options",
	CmdDrc, FALSE);
    WindAddCommand(DBWclientID,
	"dump cell [child refPointC] [parent refPointP]\n\
			copy contents of cell into edit cell, so that\n\
			refPointC (or lower left) of cell is at refPointP\n\
			(or box lower-left); refPoints are either labels\n\
			or a pair of coordinates (e.g, 100 200)",
	CmdDump, FALSE);
    WindAddCommand(DBWclientID,
	"edit		use selected cell as new edit cell",
	CmdEdit, FALSE);
    WindAddCommand(DBWclientID,
	"element option	add a generic drawing element to the layout",
	CmdElement, FALSE);
    WindAddCommand(DBWclientID,
	"erase [layers]|cursor		erase mask information",
	CmdErase, FALSE);
    WindAddCommand(DBWclientID,
	"expand [toggle]		expand everything under box, or toggle\n\
			expanded/unexpanded cells in selection",
	CmdExpand, FALSE);
    WindAddCommand(DBWclientID,
	"ext option		OR",
	CmdExtract, FALSE);		/* For "ext" abbreviation */
    WindAddCommand(DBWclientID,
	"extract option		circuit extractor; type \"extract help\"\n\
			for information on options",
	CmdExtract, FALSE);		/* For "extract" */
    WindAddCommand(DBWclientID,
	"extresist [args]	patch .ext file with resistance info",
	CmdExtResis, FALSE);
    WindAddCommand(DBWclientID,
	"feedback option		find out about problems; "
	"type \"feedback help\"\n\t\t\tfor information on options",
	CmdFeedback, FALSE);
    WindAddCommand(DBWclientID,
	"fill dir [layers]	fill layers from one side of box to other",
	CmdFill, FALSE);
    WindAddCommand(DBWclientID,
	"findbox [zoom]		center the window on the box and optionally zoom in",
	CmdFindBox, FALSE);
    WindAddCommand(DBWclientID,
	"findlabel lab		set the box to the location of this label",
	CmdFindLabel, FALSE);
    WindAddCommand(DBWclientID,
	"flatten destname	flatten edit cell into cell destname",
	CmdFlatten, FALSE);
    WindAddCommand(DBWclientID,
	"flush [cellname]	flush changes to cellname (edit cell default)",
	CmdFlush, FALSE);
    WindAddCommand(DBWclientID,
	"get	OR",
	CmdGetcell, FALSE);
    WindAddCommand(DBWclientID,
	"getcell cell [child refPointC] [parent refPointP]\n\
			get cell as a subcell of the edit cell, so that\n\
			refPointC (or lower left) of cell is at refPointP\n\
			(or box lower-left); refPoints are either labels\n\
			or a pair of coordinates (e.g, 100 200)",
	CmdGetcell, FALSE);
    WindAddCommand(DBWclientID,
	"goto nodename		goto the node named nodename",
	CmdGoto, FALSE);
    WindAddCommand(DBWclientID,
	"grid [xSpacing [ySpacing [xOrigin yOrigin]]]\n\
			toggle grid on/off (and set parameters)",
	CmdGrid, FALSE);
    WindAddCommand(DBWclientID,
	"id	OR",
	CmdIdentify, FALSE);
    WindAddCommand(DBWclientID,
	"identify use_id		set the use identifier of the selected cell",
	CmdIdentify, FALSE);
    WindAddCommand(DBWclientID,
	"instance [list] children|parents|exists|self [name]	OR\n"
	"instance [list] allcells|topcells\n\
			List cells by relationship to cell \"name\", or\n\
			the current selection if no name given.  \"list\"\n\
			returns result as a list. \"exists + name\" returns\n\
			true or false (also see command cellname)",
	CmdCellname, FALSE);
    WindAddCommand(DBWclientID,
	"label str [pos [layer]]\n\
			place a label",
	CmdLabel, FALSE);
    WindAddCommand(DBWclientID,
	"load [cellname]	load a cell into a window",
	CmdLoad, FALSE);
    WindAddCommand(DBWclientID,
	"move [dir [amount]]	OR\n"
	"move to x y		move box and selection, either by amount\n\
			in dir, or pick up by box lower-left and put\n\
			down at cursor position; second form will\n\
			put box at location x y",
	CmdMove, FALSE);
    /* Added by NP 10/28/2004 */
    WindAddCommand(DBWclientID,
	"netlist option		netlist operation; type \"netlist help\"\n\
			for information on options",
	CmdNetlist, FALSE);

    WindAddCommand(DBWclientID,
	"paint layers|cursor		paint mask information",
	CmdPaint, FALSE);
    WindAddCommand(DBWclientID,
	"path [search|cell|sys] [[+]path]\n\
			set or print search paths",
	CmdPath, FALSE);
    WindAddCommand(DBWclientID,
	"plow option [args]	stretching and compaction; type \"plow help\"\n\
			for information on options",
	CmdPlow, FALSE);
    WindAddCommand(DBWclientID,
	"port [index] [direction...]\n\
			declare a label to be a subcircuit port",
	CmdPort, FALSE);
    WindAddCommand(DBWclientID,
	"polygon type x1 y1 x2 y2 [x3 y3. . .] xn yn\n\
			draw a polygon from a list of point pairs",
	CmdPolygon, FALSE);
    WindAddCommand(DBWclientID,
	"property [name] [value]\n\
			add a property (string) to the edit cell",
	CmdProperty, FALSE);
    WindAddCommand(DBWclientID,
	"rotate [+/-][deg]	rotate selection and box (counter)clockwise",
	CmdClockwise, FALSE);		/* "rotate" is alias for "clockwise" */
    WindAddCommand(DBWclientID,
	"save [filename]		save edit cell on disk",
	CmdSave, FALSE);
    WindAddCommand(DBWclientID,
	"scalegrid a b		scale magic units vs. lambda by a / b",
	CmdScaleGrid, FALSE);
    WindAddCommand(DBWclientID,
	"see [no] layers|allSame	change what's displayed in the window",
	CmdSee, FALSE);
    WindAddCommand(DBWclientID,
	"select [option]		change selection; type \"select help\"\n\
			for information on options",
	CmdSelect, FALSE);
    WindAddCommand(DBWclientID,
	"setlabel [option [value]]	place a label",
	CmdSetLabel, FALSE);
    WindAddCommand(DBWclientID,
	"shell [command]		execute a command in a subshell",
	CmdShell, FALSE);
    WindAddCommand(DBWclientID,
	"sideways		flip selection and box sideways",
	CmdSideways, FALSE);
    WindAddCommand(DBWclientID,
	"snap [internal|lambda|user]\n\
			cause box to snap to the selected grid when moved\n\
			by the cursor",
	CmdSnap, FALSE);
    WindAddCommand(DBWclientID,
	"straighten direction	straighten jogs by pulling in direction",
	CmdStraighten, FALSE);
    WindAddCommand(DBWclientID,
	"stretch [dir [amount]]	stretch box and selection",
	CmdStretch, FALSE);
    WindAddCommand(DBWclientID,
	"tech option	technology handling; type \"techinfo help\"\n\
			for information on options",
	CmdTech, FALSE);
#ifndef MAGIC_WRAPPER
    WindAddCommand(DBWclientID,
	"tool [name|info]	change layout tool or print info about what\n\
			buttons mean for current tool",
	CmdTool, FALSE);
#endif
    WindAddCommand(DBWclientID,
	"unexpand		unexpand subcells under box",
	CmdUnexpand, FALSE);
    WindAddCommand(DBWclientID,
	"upsidedown		flip selection and box upside down",
	CmdUpsidedown, FALSE);
    WindAddCommand(DBWclientID,
	"what			print out information about what's selected",
	CmdWhat, FALSE);
    WindAddCommand(DBWclientID,
	"wire option [args]	wiring-style user interface; type\n\
			\"wire help\" for information on options",
	CmdWire, FALSE);
    WindAddCommand(DBWclientID,
	"writeall [force]	write out all modified cells to disk",
	CmdWriteall, FALSE);
    WindAddCommand(DBWclientID,
	"xload [cellname]	load a cell into a window unexpanded",
	CmdXload, FALSE);
    WindAddCommand(DBWclientID,
	"xor destname		flatten current top level cell into destname\n\
			and xor with existing contents",
	CmdXor, FALSE);

#ifdef CIF_MODULE
    /* Add the CIF extension commands */
    WindAddCommand(DBWclientID,
	"cif option		CIF processor; type \"cif help\"\n\
			for information on options",
	CmdCif, FALSE);
#endif

#ifdef CALMA_MODULE
    /* Add the GDS extension commands */
    WindAddCommand(DBWclientID,
	"calma option		Calma GDS-II stream file processor; type\n\
			\"calma help\" for information on options",
	CmdCalma, FALSE);		/* "gds" is an alias for "calma" */
    WindAddCommand(DBWclientID,
	"gds option		alias for the \"calma\" command",
	CmdCalma, FALSE);
#endif


    /* Add the NonManhattan Geometry extension commands */
    WindAddCommand(DBWclientID,
	"splitpaint dir layer [layer2]\n\
			split box diagonally with layer in corner dir and\n\
			layer2 in the opposite corner (default layer2 is space)",
	CmdSplit, FALSE);
    WindAddCommand(DBWclientID,
	"spliterase dir [layer]	erase layers from diagonal corner dir of the\n\
			edit box",
	CmdSplitErase, FALSE);   

#ifdef MAGIC_WRAPPER
    /* Add the Tcl commands for exttospice, exttosim, and aliases ext2spice, ext2sim */
#ifdef EXT2SIM_AUTO
    WindAddCommand(DBWclientID,
	"exttosim [args]	convert extracted file(s) to a sim format file;"
	" type\n\t\t\t\"exttosim help\" for information on options",
	CmdAutoExtToSim, FALSE);
    WindAddCommand(DBWclientID,
	"ext2sim [args]	convert extracted file(s) to a sim format file;"
	" type\n\t\t\t\"ext2sim help\" for information on options",
	CmdAutoExtToSim, FALSE);
#else
    WindAddCommand(DBWclientID,
	"exttosim [args]	convert extracted file(s) to a sim format file;"
	" type\n\t\t\t\"exttosim help\" for information on options",
	CmdExtToSim, FALSE);
    WindAddCommand(DBWclientID,
	"ext2sim [args]	convert extracted file(s) to a sim format file;"
	" type\n\t\t\t\"ext2sim help\" for information on options",
	CmdExtToSim, FALSE);
#endif /* EXT2SIM_AUTO */
#ifdef EXT2SPICE_AUTO
    WindAddCommand(DBWclientID,
	"exttospice [args]	convert extracted file(s) to a SPICE format file;"
	" type\n\t\t\t\"exttospice help\" for information on options",
	CmdAutoExtToSpice, FALSE);
    WindAddCommand(DBWclientID,
	"ext2spice [args]	convert extracted file(s) to a SPICE format file;"
	" type\n\t\t\t\"ext2spice help\" for information on options",
	CmdAutoExtToSpice, FALSE);
#else
    WindAddCommand(DBWclientID,
	"exttospice [args]	convert extracted file(s) to a SPICE format file;"
	" type\n\t\t\t\"exttospice help\" for information on options",
	CmdExtToSpice, FALSE);
    WindAddCommand(DBWclientID,
	"ext2spice [args]	convert extracted file(s) to a SPICE format file;"
	" type\n\t\t\t\"ext2spice help\" for information on options",
	CmdExtToSpice, FALSE);
#endif	/* EXT2SPICE_AUTO */
#endif  /* MAGIC_WRAPPER */


#ifdef USE_READLINE
    /* Add the Readline extension history command */
    WindAddCommand(DBWclientID,
	"history print out the command history list",
	CmdHistory, FALSE);
#endif

#ifdef	LLNL
    /* Add the Lawrence Livermore extensions */
    WindAddCommand(DBWclientID,
	"makesw options		generate scan window for LP apparatus",
	CmdMakeSW, FALSE);
    WindAddCommand(DBWclientID,
	"sgraph [options]	manipulate a cell's stretch graphs",
	CmdSgraph, FALSE);
#endif

#ifndef NO_SIM_MODULE
    /* Add the IRSIM commands */
    WindAddCommand(DBWclientID,
	"getnode option		get node names of all selected paint",
	CmdGetnode, FALSE);

#ifdef RSIM_MODULE
    WindAddCommand(DBWclientID,
	"rsim [options] filename		run Rsim under Magic",
	CmdRsim, FALSE);
    WindAddCommand(DBWclientID,
	"simcmd cmd	        send a command to Rsim, applying it to selected paint",
	CmdSimCmd, FALSE);
    WindAddCommand(DBWclientID,
	"startrsim [options] file    start Rsim and return to Magic",
	CmdStartRsim, FALSE);
#endif
#endif

#ifdef PLOT_MODULE
    /* Add the plot extensions */
    WindAddCommand(DBWclientID,
	"plot type [args]	hardcopy plotting; type \"plot help\"\n\
			for information on types and args",
	CmdPlot, FALSE);  

#endif
#ifdef PLOT_AUTO
    /* Placeholder for plot extensions */
    WindAddCommand(DBWclientID,
	"plot type [args]	hardcopy plotting; type \"plot help\"\n\
			for information on types and args",
	CmdAutoPlot, FALSE);
#endif

#ifdef LEF_MODULE
    /* Add the LEF/DEF extensions */
    WindAddCommand(DBWclientID,
	"lef [options]	LEF-format input/output; type \"lef help\"\n\
			for information on options",
	CmdLef, FALSE);
    WindAddCommand(DBWclientID,
	"def [options]	DEF-format input; type \"def help\"\n\
			for information on options",
	CmdLef, FALSE);
#endif
#ifdef LEF_AUTO
    /* Placeholder for LEF/DEF extensions */
    WindAddCommand(DBWclientID,
	"lef [options]	LEF-format input/output; type \"lef help\"\n\
			for information on options",
	CmdAutoLef, FALSE);
    WindAddCommand(DBWclientID,
	"def [options]	DEF-format input; type \"def help\"\n\
			for information on options",
	CmdAutoLef, FALSE);
#endif

#ifdef ROUTE_MODULE
    /* Add the router extensions */
    WindAddCommand(DBWclientID,
	"*garoute [cmd [args]]	debug the gate-array router",
	CmdGARouterTest, FALSE);
    WindAddCommand(DBWclientID,
	"*groute [cmd [args]]	debug the global router",
	CmdGRouterTest, FALSE);
    WindAddCommand(DBWclientID,
	"*iroute [cmd [args]]	debug the interactive router",
	CmdIRouterTest, FALSE);
    WindAddCommand(DBWclientID,
	"*mzroute [cmd [args]]	debug the maze router",
	CmdMZRouterTest, FALSE);
    WindAddCommand(DBWclientID,
	"*seeflags [flag]	display channel flags over channel",
	CmdSeeFlags, FALSE);
    WindAddCommand(DBWclientID,
	"channels		see channels (feedback) without doing routing",
	CmdChannel, FALSE);
    WindAddCommand(DBWclientID,
	"garoute [cmd [args]]	gate-array router",
	CmdGaRoute, FALSE);
    WindAddCommand(DBWclientID,
	"iroute [cmd [args]]	do interactive point to point route",
	CmdIRoute, FALSE);
    WindAddCommand(DBWclientID,
	"route			route the current cell",
	CmdRoute, FALSE);
#endif
#ifdef ROUTE_AUTO
    /* Placeholder for router extensions */
    WindAddCommand(DBWclientID,
	"*garoute [cmd [args]]	debug the gate-array router",
	CmdAutoRoute, FALSE);
    WindAddCommand(DBWclientID,
	"*groute [cmd [args]]	debug the global router",
	CmdAutoRoute, FALSE);
    WindAddCommand(DBWclientID,
	"*iroute [cmd [args]]	debug the interactive router",
	CmdAutoRoute, FALSE);
    WindAddCommand(DBWclientID,
	"*mzroute [cmd [args]]	debug the maze router",
	CmdAutoRoute, FALSE);
    WindAddCommand(DBWclientID,
	"*seeflags [flag]	display channel flags over channel",
	CmdAutoRoute, FALSE);
    WindAddCommand(DBWclientID,
	"channels		see channels (feedback) without doing routing",
	CmdAutoRoute, FALSE);
    WindAddCommand(DBWclientID,
	"garoute [cmd [args]]	gate-array router",
	CmdAutoRoute, FALSE);
    WindAddCommand(DBWclientID,
	"iroute [cmd [args]]	do interactive point to point route",
	CmdAutoRoute, FALSE);
    WindAddCommand(DBWclientID,
	"route			route the current cell",
	CmdAutoRoute, FALSE);
#endif
}
