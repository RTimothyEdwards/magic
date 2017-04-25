/*
 * plotMain.c --
 *
 * This is the central file in the plot module.  It contains tables
 * that define the various styles of plotting that are available, and
 * also contains central technology-file reading routines.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/plot/plotMain.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "utils/malloc.h"
#include "plot/plotInt.h"
#include "textio/textio.h"
#include "utils/utils.h"

/* Magic can generate plots in several different ways, e.g. as a
 * Gremlin file or as a direct raster plot to a Versatec printer.
 * For each style of plot, there is a subsection of the plot section
 * of technology files.  The tables below define the names of those
 * subsections, and the procedures to call to handle lines within
 * those subsections.  To add a new style of plot, extend the tables
 * below and then modify the procedure CmdPlot to actually invoke
 * the top-level plotting routine.
 */

/* Note (10/8/04):  All of the plot styles except PostScript and PNM	*/
/* have been removed in the default compilation.  However, they should	*/
/* remain in the following lists so that magic doesn't complain when	*/
/* encountering these styles in the technology file.			*/

static char *plotStyles[] =		/* Names of tech subsections. */
{
    "postscript",
    "pnm",
    "gremlin",
    "versatec",
    "colorversatec",
    "pixels",
    NULL
};

/* These names need to match the plot types enumerated in plotInt.h */
static char *plotTypeNames[] =
{
    "versatec_color",
    "versatec_bw",
    "hprtl",
    "hpgl2",
    NULL
};


static void (*plotInitProcs[])() =	/* Initialization procedures for
					 * each style.
					 */
{
    PlotPSTechInit,
    PlotPNMTechInit,
    PlotGremlinTechInit,
    PlotVersTechInit,
    PlotColorVersTechInit,
    PlotPixTechInit,
    NULL
};

static bool (*plotLineProcs[])() =	/* Proc to call for each line in
					 * relevant subsection of tech file.
					 */
{
    PlotPSTechLine,
    PlotPNMTechLine,
    PlotGremlinTechLine,
    PlotVersTechLine,
    PlotColorVersTechLine,
    PlotPixTechLine,
    NULL
};

static void (*plotFinalProcs[])() =	/* Proc to call at end of reading
					 * tech files.
					 */
{
    NULL,
    PlotPNMTechFinal,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static int plotCurStyle = -1;		/* Current style being processed in
					 * technology file.  -1 means no
					 * "style" line seen yet.  -2 means
					 * skipping to next "style" line.
					 */

bool PlotShowCellNames = TRUE;		/* TRUE if cell names and use-ids
					 * should be printed inside cell
					 * bounding boxes; if this is FALSE,
					 * then only the bounding box is
					 * drawn.
					 */


/*
 * ----------------------------------------------------------------------------
 *	PlotTechInit --
 *
 * 	Called once at beginning of technology file read-in to initialize
 *	data structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls the initialization procedures (if any) for each of the
 *	various styles of plotting.
 * ----------------------------------------------------------------------------
 */

void
PlotTechInit()
{
    int i;

    PlotRastInit();

    plotCurStyle = -1;
    for (i = 0; plotStyles[i] != NULL; i++)
    {
	if (plotInitProcs[i] != NULL)
	    (*(plotInitProcs[i]))();
    }
}

/*
 * ----------------------------------------------------------------------------
 *	PlotTechLine --
 *
 * 	This procedure is invoked by the technology module once for
 *	each line in the "plot" section of the technology file.  It
 *	processes "style x" lines directly, to change the current style
 *	of plot information.  For other lines, it just passes the lines
 *	onto the procedure for the current style.
 *
 * Results:
 *	Returns whatever the handler for the current style returns when
 *	we call it.
 *
 * Side effects:
 *	Builds up plot technology information.
 * ----------------------------------------------------------------------------
 */

bool
PlotTechLine(sectionName, argc, argv)
    char *sectionName;		/* Name of this section. */
    int argc;			/* Number of arguments on line. */
    char *argv[];		/* Pointers to fields of line. */
{
    int i;

    if (strcmp(argv[0], "style") == 0)
    {
	if (argc != 2)
	{
	    TechError("\"style\" lines must have exactly two arguments\n");
	    return TRUE;
	}

	/* Change the style of plot for which information is being read. */

	plotCurStyle = -2;
	for (i = 0; plotStyles[i] != NULL; i++)
	{
	    if (strcmp(argv[1], plotStyles[i]) == 0)
	    {
		plotCurStyle = i;
		break;
	    }
	}

	if (plotCurStyle == -2)
	{
	    TechError("Plot style \"%s\" doesn't exist.  Ignoring.\n",
		    argv[1]);
	}
	return TRUE;
    }

    /* Not a new style.  Just farm out this line to the handler for the
     * current style.
     */
    
    if (plotCurStyle == -1)
    {
	TechError("Must declare a plot style before anything else.\n");
	plotCurStyle = -2;
	return TRUE;
    }
    else if (plotCurStyle == -2)
	return TRUE;
    
    if (plotLineProcs[plotCurStyle] == NULL)
	return TRUE;
    return (*(plotLineProcs[plotCurStyle]))(sectionName, argc, argv);
}

/*
 * ----------------------------------------------------------------------------
 *	PlotTechFinal --
 *
 * 	Called once at the end of technology file read-in.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls the finalization procedures (if any) for each of the
 *	various style of plotting.
 * ----------------------------------------------------------------------------
 */

void
PlotTechFinal()
{
    int i;

    plotCurStyle = -1;
    for (i = 0; plotStyles[i] != NULL; i++)
    {
	if (plotFinalProcs[i] != NULL)
	    (*(plotFinalProcs[i]))();
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotPrintParams --
 *
 * 	Print out a list of all the plotting parameters and their
 *	current values.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff gets printed.
 *
 * ----------------------------------------------------------------------------
 */

void
PlotPrintParams()
{
    TxPrintf("General plotting parameters are:\n");
    TxPrintf("    showCellNames: %s\n", PlotShowCellNames ? "true" : "false");
    TxPrintf("");
    TxPrintf("Postscript plotting parameters are:\n");
    TxPrintf("    PS_cellIdFont:  \"%s\"\n", PlotPSIdFont);
    TxPrintf("    PS_cellNameFont:\"%s\"\n", PlotPSNameFont);
    TxPrintf("    PS_labelFont:   \"%s\"\n", PlotPSLabelFont);
    TxPrintf("    PS_cellIdSize:  %d\n", PlotPSIdSize);
    TxPrintf("    PS_cellNameSize:%d\n", PlotPSNameSize);
    TxPrintf("    PS_labelSize:   %d\n", PlotPSLabelSize);
    TxPrintf("    PS_boundary:   %s\n",  PlotPSBoundary ? "true" : "false");
    TxPrintf("    PS_width:       %d (%.3f in)\n", PlotPSWidth,
		(float)PlotPSWidth / 72);
    TxPrintf("    PS_height:      %d (%.3f in)\n", PlotPSHeight,
		(float)PlotPSHeight / 72);
    TxPrintf("    PS_margin:      %d (%.3f in)\n", PlotPSMargin,
		(float)PlotPSMargin / 72);

    TxPrintf("");
    TxPrintf("PNM plotting parameters are:\n");
    TxPrintf("    pnmmaxmem: %d KB\n", PlotPNMmaxmem);
    TxPrintf("    pnmdownsample: %d\n", PlotPNMdownsample);
    TxPrintf("    pnmbackground: %d\n", PlotPNMBG);

#ifdef VERSATEC
    TxPrintf("    pnmplotRTL: %s\n", PlotPNMRTL ? "true" : "false");
    TxPrintf("");
    TxPrintf("HP/Versatec plotting parameters are:\n");
    TxPrintf("    cellIdFont:    \"%s\"\n", PlotVersIdFont);
    TxPrintf("    cellNameFont:  \"%s\"\n", PlotVersNameFont);
    TxPrintf("    directory:     \"%s\"\n", PlotTempDirectory);
    TxPrintf("    dotsPerInch:   %d\n", PlotVersDotsPerInch);
    TxPrintf("    labelFont:     \"%s\"\n", PlotVersLabelFont);
    TxPrintf("    printer:       \"%s\"\n", PlotVersPrinter);
    TxPrintf("    spoolCommand:  \"%s\"\n", PlotVersCommand);
    TxPrintf("    swathHeight:   %d\n", PlotVersSwathHeight);
    TxPrintf("    width:         %d\n", PlotVersWidth);
    TxPrintf("    plotType:      %s\n", plotTypeNames[PlotVersPlotType]);
#endif
#ifdef LLNL
    TxPrintf("");
    TxPrintf("Pixel plotting parameters are:\n");
    TxPrintf("    pixheight:   %d\n", PlotPixHeight);
    TxPrintf("    pixwidth:         %d\n", PlotPixWidth);
#endif /* LLNL */
}

/*
 * ----------------------------------------------------------------------------
 *
 * PlotSetParam --
 *
 * 	This procedure is called to change the value of one
 *	of the plotting parameters.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whichever parameter is named by "name" is set to "value".
 *	The interpretation of "value" depends on the parameter.
 *
 * ----------------------------------------------------------------------------
 */

typedef enum {
	SHOWCELLNAMES=0,
	PSCELLIDFONT,
	PSNAMEFONT,
	PSLABELFONT,
	PSIDSIZE,
	PSNAMESIZE,
	PSLABELSIZE,
	PSBOUNDARY,
	PSWIDTH,
	PSHEIGHT,
	PSMARGIN,
#ifdef VERSATEC
	CELLIDFONT,
	CELLNAMEFONT,
	LABELFONT,
	DIRECTORY,
	DOTSPERINCH,
	PRINTER,
	SPOOLCOMMAND,
	SWATHHEIGHT,
	WIDTH,
	PLOTTYPE,
	PNMRTL,
#endif
#ifdef LLNL
	PIXWIDTH,
	PIXHEIGHT,
#endif
	PNMMAXMEM,
	PNMDOWNSAMPLE,
	PNMBACKGROUND
} PlotParameterOptions;

void
PlotSetParam(name, value)
    char *name;			/* Name of a parameter. */
    char *value;		/* New value for the parameter. */
{
    int indx, i;
    static char *tfNames[] = { "false", "true", 0 };
    static char *paramNames[] =
    {
	"showcellnames",
	"ps_cellidfont",
	"ps_namefont",
	"ps_labelfont",
	"ps_cellidsize",
	"ps_namesize",
	"ps_labelsize",
	"ps_boundary",
	"ps_width",
	"ps_height",
	"ps_margin",
#ifdef VERSATEC
	"cellidfont",
	"cellnamefont",
	"labelfont",
	"directory",
	"dotsperinch",
	"printer",
	"spoolcommand",
	"swathheight",
	"width",
	"plottype",
	"pnmplotRTL",	  /* PNM output piped through an HP plotter */
#endif
#ifdef LLNL
	"pixwidth",
	"pixheight",
#endif
	"pnmmaxmem",
	"pnmdownsample",
	"pnmbackground",
	NULL
    };

    indx = Lookup(name, paramNames);
    if (indx < 0)
    {
	TxError("\"%s\" isn't a valid plot parameter.\n", name);
	PlotPrintParams();
	return;
    }

    i = atoi(value);
    switch (indx)
    {
	case SHOWCELLNAMES:
	    i = Lookup(value, tfNames);
	    if (i < 0)
	    {
		TxError("ShowCellNames can only be \"true\" or \"false\".\n");
		return;
	    }
	    PlotShowCellNames = i;
	    break;
	case PSCELLIDFONT:
	    StrDup(&PlotPSIdFont, value);
	    break;
	case PSNAMEFONT:
	    StrDup(&PlotPSNameFont, value);
	    break;
	case PSLABELFONT:
	    StrDup(&PlotPSLabelFont, value);
	    break;
	case PSIDSIZE:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("PS_cellIdSize must be an integer greater than zero.\n");
		return;
	    }
	    else PlotPSIdSize = i;
	    break;
	case PSNAMESIZE:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("PS_cellNameSize must be an integer greater than zero.\n");
		return;
	    }
	    else PlotPSNameSize = i;
	    break;
	case PSLABELSIZE:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("PS_labelSize must be an integer greater than zero.\n");
		return;
	    }
	    else PlotPSLabelSize = i;
	    break;
        case PSBOUNDARY:
	    i = Lookup(value, tfNames);
	    if (i < 0)
	    {
		TxError("PS_boundary can only be \"true\" or \"false\".\n");
		return;
	    }
	    PlotPSBoundary = i;
	    break;
	case PSWIDTH:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("PS_Width must be an integer greater than zero.\n");
		return;
	    }
	    else PlotPSWidth = i;
	    break;
	case PSHEIGHT:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("PS_Height must be an integer greater than zero.\n");
		return;
	    }
	    else PlotPSHeight = i;
	    break;
	case PSMARGIN:
	    if (!StrIsInt(value) || (i < 0))
	    {
		TxError("PS_Margin must be an integer greater than or equal to zero.\n");
		return;
	    }
	    else PlotPSMargin = i;
	    break;

#ifdef VERSATEC
	case CELLIDFONT:
	    StrDup(&PlotVersIdFont, value);
	    break;
	case CELLNAMEFONT:
	    StrDup(&PlotVersNameFont, value);
	    break;
	case LABELFONT:
	    StrDup(&PlotVersLabelFont, value);
	    break;
	case DIRECTORY:
	    StrDup(&PlotTempDirectory, value);
	    break;
	case DOTSPERINCH:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("DotsPerInch must be an integer greater than zero.\n");
		return;
	    }
	    else PlotVersDotsPerInch = i;
	    break;
	case PRINTER:
	    StrDup(&PlotVersPrinter, value);
	    break;
	case PLOTTYPE:
	    i = Lookup(value, plotTypeNames);
	    if (i < 0)
	    {
		int j;

		TxError("%s is not a supported plot type.  Plot types are:\n");
		for (j = 0 ; plotTypeNames[j] != NULL; j++)
		{
		    TxError("\t%s\n", plotTypeNames[j]);
		}
		return;
	    }
	    PlotVersPlotType = i;
	    switch(PlotVersPlotType)
	    {
		case VERSATEC_COLOR:
		    PlotVersDotsPerInch = 215;
		    PlotVersWidth = 7904;
		    break;
		case VERSATEC_BW:
		    PlotVersDotsPerInch = 215;
		    PlotVersWidth = 7904;
		    break;
		case HPRTL:
		    PlotVersDotsPerInch = 316;
		    PlotVersWidth = 2400;
		    break;
		case HPGL2:
		    PlotVersDotsPerInch = 300;
		    PlotVersWidth = 10650;
		    break;
	    }
	    break;
	case SPOOLCOMMAND:
	    StrDup(&PlotVersCommand, value);
	    break;
	case SWATHHEIGHT:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("SwathHeight must be an integer greater than zero.\n");
		return;
	    }
	    else PlotVersSwathHeight= i;
	    break;
	case WIDTH:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("Width must be an integer greater than zero.\n");
		return;
	    }
	    else PlotVersWidth = i;
	    break;

	case PNMRTL:
	    i = Lookup(value, tfNames);
	    if (i < 0)
	    {
		TxError("pnmplotRTL can only be \"true\" or \"false\".\n");
		return;
	    }
	    PlotPNMRTL = i;
	    break;

#endif /* VERSATEC */

#ifdef LLNL

	case PIXWIDTH:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("PixWidth must be an integer greater than zero.\n");
		return;
	    }
	    else PlotPixWidth = i;
	    break;
	case PIXHEIGHT:
	    if (!StrIsInt(value) || (i <= 0))
	    {
		TxError("PixHeight must be an integer greater than zero.\n");
		return;
	    }
	    else PlotPixHeight = i;
	    break;

#endif /* LLNL */

        case PNMMAXMEM:
  	    if (!StrIsInt(value) || (i <= 0))
	    {
	        TxError("pnmmaxmem must be an integer greater than zero.\n");
		return;
	    }
	    else PlotPNMmaxmem = i;
	    break;

        case PNMDOWNSAMPLE:
  	    if (!StrIsInt(value) || (i < 0))
	    {
	        TxError("pnmdownsample must be an integer zero or larger.\n");
		return;
	    }
	    else PlotPNMdownsample = i;
	    break;

        case PNMBACKGROUND:
  	    if (!StrIsInt(value) || (i < 0) || (i > 255))
	    {
	        TxError("pnmbackground must be an integer 0-255.\n");
		return;
	    }
	    else PlotPNMBG = (unsigned char)i;
	    break;
    }
}
