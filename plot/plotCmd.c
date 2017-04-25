/*
 * PlotCmd.c --
 *
 * Commands for the plot module only.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plot/plotCmd.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/utils.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "textio/txcommands.h"
#include "plow/plow.h"
#include "select/select.h"
#include "commands/commands.h"
#include "plot/plotInt.h"


/*
 * ----------------------------------------------------------------------------
 *
 * CmdPlot --
 *
 * Implement the "plot" command:  generate plot output for what's
 * underneath the box.
 *
 * Usage:
 *	plot type [options]
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Generates plot output on disk somewhere.
 *
 * ----------------------------------------------------------------------------
 */

/* These definitions must correspond to the ordering in cmdPlotOption[] below */

typedef enum {
	POSTSCRIPT=0,
	PLOTPNM,
#ifdef GREMLIN
	STYLE_GREMLIN,
#endif
#ifdef VERSATEC
	STYLE_VERSATEC,
#endif
#ifdef LLNL
	PIXELS,
#endif
	PARAMETERS,
	HELP } PlotOptions;

void
CmdPlot(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int option;
    char **msg;
    MagWindow *window;
    DBWclientRec *crec;
    TileTypeBitMask mask;
    CellDef *boxRootDef;
    SearchContext scx;
    float width;
    int iwidth, scale;

    static char *cmdPlotOption[] =
    {	
	"postscript file [layers]    generate PostScript file for what's\n\
                                     underneath the box",				
	"pnm file [width [layers]]   generate PNM file for what's\n\
		                     underneath the box",
#ifdef GREMLIN
	"gremlin file [layers]	     generate gremlin file for what's\n\
		                     underneath the box",
#endif
#ifdef VERSATEC
	"versatec [scale [layers]]    generate plot for selected printer;\n\
		                     scale is multiplying factor for parameter\n\
		                     lambda, for layers underneath the box",
#endif
#ifdef	LLNL
	"pixels [width [layers]]     generate plot in pix format, width pixels\n\
		                     wide, for layers underneath box",
#endif	/* LLNL */
	"parameters [name value]     set or print out plotting parameters",
	"help                        print this help information",
	NULL
    };

    if (cmd->tx_argc < 2)
    {
	option = HELP;
	cmd->tx_argc = 2;
    }
    else
    {
	option = Lookup(cmd->tx_argv[1], cmdPlotOption);
	if (option < 0)
	{
	    TxError("\"%s\" isn't a valid plot option.\n", cmd->tx_argv[1]);
	    option = HELP;
	    cmd->tx_argc = 2;
	}
    }

    if ((option == PLOTPNM) || (option == POSTSCRIPT)
#ifdef GREMLIN
		|| (option == STYLE_GREMLIN)
#endif
#ifdef VERSATEC
	        || (option == STYLE_VERSATEC)
#endif
#ifdef LLNL
		|| (option == PIXELS)
#endif
		 )
    {

	window = ToolGetPoint((Point *) NULL, (Rect *) NULL);
	if (window == NULL)
	{
	    windCheckOnlyWindow(&window, DBWclientID);
	    if ((window == (MagWindow *) NULL) || (window->w_client != DBWclientID))
	    {
		TxError("The cursor must be over a layout window to plot.\n");
		return; 
	    }
	}
	crec = (DBWclientRec *) window->w_clientData;
	scx.scx_use = (CellUse *) window->w_surfaceID;
	if ((!ToolGetBox(&boxRootDef, &scx.scx_area)) ||
		(scx.scx_use->cu_def != boxRootDef))
	{
	    TxError("The box and cursor must appear in the same window\n");
	    TxError("    for plotting.  The box indicates the area to\n");
	    TxError("    plot, and the cursor's window tells which\n");
	    TxError("    cells are expanded and unexpanded).\n");
	    return;
	}
	scx.scx_trans = GeoIdentityTransform;
	mask = crec->dbw_visibleLayers;
	if ((crec->dbw_flags & DBW_SEELABELS) && (crec->dbw_labelSize >= 0))
	    TTMaskSetType(&mask, L_LABEL);
	else TTMaskClearType(&mask, L_LABEL);
	TTMaskSetType(&mask, L_CELL);
    }

    switch (option)
    {
	case POSTSCRIPT:
	    if ((cmd->tx_argc != 3) && (cmd->tx_argc != 4))
	    {
		TxError("Wrong number of arguments:\n    plot %s\n",
			cmdPlotOption[POSTSCRIPT]);
		return;
	    }
	    if (cmd->tx_argc == 4)
	    {
		if (!CmdParseLayers(cmd->tx_argv[3], &mask))
			return;
	    }
	    PlotPS(cmd->tx_argv[2], &scx, &mask, crec->dbw_bitmask);
	    return;
	    
#ifdef GREMLIN
	case STYLE_GREMLIN:
	    if ((cmd->tx_argc != 3) && (cmd->tx_argc != 4))
	    {
		TxError("Wrong number of arguments:\n    plot %s\n",
			cmdPlotOption[STYLE_GREMLIN]);
		return;
	    }
	    if (cmd->tx_argc == 4)
	    {
		if (!CmdParseLayers(cmd->tx_argv[3], &mask))
			return;
	    }
	    PlotGremlin(cmd->tx_argv[2], &scx, &mask, crec->dbw_bitmask);
	    return;
#endif /* GREMLIN */
	    
	case HELP:
	    TxPrintf("The \"plot\" commands are:\n");
	    for (msg = &(cmdPlotOption[0]); *msg != NULL; msg++)
	    {
		TxPrintf("    plot %s\n", *msg);
	    }
	    return;
	
	case PARAMETERS:
	    if (cmd->tx_argc == 2)
		PlotPrintParams();
	    else if (cmd->tx_argc == 4)
		PlotSetParam(cmd->tx_argv[2], cmd->tx_argv[3]);
	    else
	    {
 		TxError("Wrong arguments:\n    plot %s\n",
			cmdPlotOption[PARAMETERS]);
	    }
	    return;
	
#ifdef VERSATEC
	case STYLE_VERSATEC:
	    if (cmd->tx_argc > 4)
	    {
		TxError("Too many arguments:\n    plot %s\n",
			cmdPlotOption[STYLE_VERSATEC]);
		return;
	    }
	    if (cmd->tx_argc >= 3)
		scale = atoi(cmd->tx_argv[2]);
	    else scale = 0.0;
	    if (cmd->tx_argc == 4)
	    {
		if (!CmdParseLayers(cmd->tx_argv[3], &mask))
			return;
	    }
	    PlotVersatec( &scx, &mask, crec->dbw_bitmask, scale);
	    return;
#endif

	case PLOTPNM:
	    if (cmd->tx_argc > 5)
	    {
		TxError("Too many arguments:\n    plot %s\n",
			cmdPlotOption[PLOTPNM]);
		return;
	    }
	    if (cmd->tx_argc < 3) {
		TxError("Too few arguments:\n    plot %s\n",
			cmdPlotOption[PLOTPNM]);
		return;
	    }
	    if (cmd->tx_argc >= 4)
	    {
#ifdef VERSATEC
		if (PlotPNMRTL && StrIsNumeric(cmd->tx_argv[3]))
		    iwidth = (int)(atof(cmd->tx_argv[3]) * (double)PlotVersDotsPerInch);
		else
#endif
	        if (!StrIsInt(cmd->tx_argv[3]))
		{
		    {
			TxError("Width should be an integer number of pixels\n");
			return;
		    }
		}
		else
		    iwidth = atoi(cmd->tx_argv[3]);
	    }
#ifdef VERSATEC
	    /* RTL mode allows no filename.  A temporary filename is created	*/
	    /* for the file being spooled to the printer.			*/

	    else if (PlotPNMRTL && StrIsNumeric(cmd->tx_argv[2]))
	    {
		iwidth = (int)(atof(cmd->tx_argv[2]) * (double)PlotVersDotsPerInch);
		PlotPNM(NULL, &scx, &mask, crec->dbw_bitmask, iwidth);
		return;
	    }
#endif
	    else 
		iwidth = 1000;		/* Default value */

	    if (cmd->tx_argc == 5)
	      if (!CmdParseLayers(cmd->tx_argv[4], &mask))
		return;

	    PlotPNM(cmd->tx_argv[2], &scx, &mask, crec->dbw_bitmask, iwidth);
	    return;
    
#ifdef	LLNL
	case PIXELS:
	    if (cmd->tx_argc > 4)
	    {
		TxError("Too many arguments:\n    plot %s\n",
			cmdPlotOption[PIXELS]);
		return;
	    }
	    if (cmd->tx_argc >=3)
	      iwidth = cmdParseCoord(w, cmd->tx_argv[2], TRUE, TRUE);
	    else
	      iwidth = 0; /* means get it from the plot parameters */
	    if (cmd->tx_argc == 4)
	    {
		if (!CmdParseLayers(cmd->tx_argv[3], &mask))
			return;
	    }
	    PlotPixels( &scx, &mask, crec->dbw_bitmask, iwidth);
	    return;
#endif	/* LLNL */
    }
}

