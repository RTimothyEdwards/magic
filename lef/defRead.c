/*
 * defRead.c --
 *
 * This module incorporates the LEF/DEF format for standard-cell place and
 * route.
 *
 * Version 0.1 (September 26, 2003):  DEF input of designs.
 *
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/lef/defRead.c,v 1.2 2008/06/01 18:37:43 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <math.h>		/* for roundf() function, if std=c99 */

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/undo.h"
#include "utils/utils.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/malloc.h"
#include "graphics/graphics.h"
#include "utils/main.h"
#include "cif/cif.h"
#include "lef/lefInt.h"

/*
 *------------------------------------------------------------
 *
 * DefAddRoutes --
 *
 *	Parse a network route statement from the DEF file,
 *	and add it to the linked list representing the route.
 *
 * Results:
 *	Returns the last token encountered.
 *
 * Side Effects:
 *	Reads from input stream;
 *	Adds information to the layout database.
 *
 *------------------------------------------------------------
 */

enum def_netspecial_keys {DEF_SPECNET_SHAPE = 0, DEF_SPECNET_STYLE,
	DEF_SPECNET_USE, DEF_SPECNET_VOLTAGE, DEF_SPECNET_FIXEDBUMP,
	DEF_SPECNET_ORIGINAL, DEF_SPECNET_PATTERN, DEF_SPECNET_ESTCAP,
	DEF_SPECNET_WEIGHT, DEF_SPECNET_PROPERTY};

enum def_netspecial_shape_keys {
	DEF_SPECNET_SHAPE_RING = 0,
	DEF_SPECNET_SHAPE_PADRING,
	DEF_SPECNET_SHAPE_BLOCKRING,
	DEF_SPECNET_SHAPE_STRIPE,
	DEF_SPECNET_SHAPE_FOLLOWPIN,
	DEF_SPECNET_SHAPE_IOWIRE,
	DEF_SPECNET_SHAPE_COREWIRE,
	DEF_SPECNET_SHAPE_BLOCKWIRE,
	DEF_SPECNET_SHAPE_BLOCKAGEWIRE,
	DEF_SPECNET_SHAPE_FILLWIRE,
	DEF_SPECNET_SHAPE_FILLWIREOPC,
	DEF_SPECNET_SHAPE_DRCFILL};

char *
DefAddRoutes(rootDef, f, oscale, special, netname, defLayerMap)
    CellDef *rootDef;		/* Cell to paint */
    FILE *f;			/* Input file */
    float oscale;		/* Scale factor between LEF and magic units */
    bool special;		/* True if this section is SPECIALNETS */
    char *netname;		/* Name of the net, if net is to be labeled */
    LefMapping *defLayerMap;	/* magic-to-lef layer mapping array */
{
    char *token;
    LinkedRect *routeList, *newRoute = NULL, *routeTop = NULL;
    Point refp;			/* reference point */
    bool valid = FALSE;		/* is there a valid reference point? */
    bool initial = TRUE;
    bool labeled = TRUE;
    Rect locarea;
    int extend, lextend, hextend;
    float x, y, z, w;
    int routeWidth, paintWidth, saveWidth;
    TileType routeLayer, paintLayer;
    HashEntry *he;
    lefLayer *lefl;
    int keyword;

    static char *specnet_keys[] = {
	"SHAPE",
	"STYLE",
	"USE",
	"VOLTAGE",
	"FIXEDBUMP",
	"ORIGINAL",
	"PATTERN",
	"ESTCAP",
	"WEIGHT",
	"PROPERTY",
	NULL
    };

    static char *specnet_shape_keys[] = {
	"RING",
	"PADRING",
	"BLOCKRING",
	"STRIPE",
	"FOLLOWPIN",
	"IOWIRE",
	"COREWIRE",
	"BLOCKWIRE",
	"BLOCKAGEWIRE",
	"FILLWIRE",
	"FILLWIREOPC",
	"DRCFILL",
	NULL
    };

    if (netname != NULL) labeled = FALSE;

    while (initial || (token = LefNextToken(f, TRUE)) != NULL)
    {
	/* Get next point, token "NEW", or via name */
	if (initial || !strcmp(token, "NEW") || !strcmp(token, "new"))
	{
	    /* initial pass is like a NEW record, but has no NEW keyword */
	    initial = FALSE;

	    /* invalidate reference point */
	    valid = FALSE;

	    token = LefNextToken(f, TRUE);

	    he = HashLookOnly(&LefInfo, token);
	    if (he != NULL)
	    {
		lefl = (lefLayer *)HashGetValue(he);
		if (lefl) routeLayer = lefl->type;
	    }
	    else
	    {
		/* The fallback position is to match the DEF name to the  */
		/* magic layer name.  This is ad-hoc, and the declaration */
		/* of type mappings in the lef section of the techfile is */
		/* preferred.						  */

		routeLayer = DBTechNameType(LefLower(token));
		lefl = NULL;
	    }

	    if (routeLayer < 0)
	    {
		LefError(DEF_ERROR, "Unknown layer type \"%s\" for NEW route\n", token);
		continue;
	    }
	    paintLayer = routeLayer;

	    if (special)
	    {
		/* SPECIALNETS has the additional width */
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%f", &w) != 1)
		{
		    LefError(DEF_ERROR, "Bad width in special net\n");
		    continue;
		}
		if (w != 0)
		    paintWidth = (int)roundf(w / oscale);
		else
		    paintWidth = (lefl) ? lefl->info.route.width :
				DEFAULT_WIDTH * DBLambda[1] / DBLambda[0];
		saveWidth = paintWidth;
	    }
	    else
		paintWidth = (lefl) ? lefl->info.route.width :
				DEFAULT_WIDTH * DBLambda[1] / DBLambda[0];
	}
	else if ((*token == '+') && (special == TRUE))
	{
	    int netstyle;

	    /* Check for SHAPE, STYLE, or USE keywords */
	    token = LefNextToken(f, TRUE);
	    keyword = Lookup(token, specnet_keys);
	    if (keyword < 0)
	    {
		LefError(DEF_INFO, "Unknown keyword \"%s\" in SPECIALNET "
			"definition; ignoring.\n", token);
		LefEndStatement(f);
		continue;
	    }
	    switch(keyword)
	    {
		case DEF_SPECNET_STYLE:
		    token = LefNextToken(f, TRUE);
		    if (sscanf(token, "%d", &netstyle) != 1)
		    {
			LefError(DEF_INFO, "Net style \"%s\" in SPECIALNET "
				    "definition is not a number; ignoring.\n", token);
			LefEndStatement(f);
			continue;
		    }
		    break;

		case DEF_SPECNET_SHAPE:
		    token = LefNextToken(f, TRUE);
		    keyword = Lookup(token, specnet_shape_keys);
		    if (keyword < 0)
		    {
			LefError(DEF_INFO, "Unknown SHAPE \"%s\" in SPECIALNET "
				"definition; ignoring.\n", token);
			LefEndStatement(f);
			continue;
		    }
		    break;

		case DEF_SPECNET_PROPERTY:
		    /* Ignore except to absorb the next two tokens. */
		    token = LefNextToken(f, TRUE);  /* Drop through */

		case DEF_SPECNET_USE:
		case DEF_SPECNET_VOLTAGE:
		case DEF_SPECNET_ORIGINAL:
		case DEF_SPECNET_PATTERN:
		case DEF_SPECNET_ESTCAP:
		case DEF_SPECNET_WEIGHT:
		    /* Ignore except to absorb the next token. */
		    token = LefNextToken(f, TRUE);  /* Drop through */

		case DEF_SPECNET_FIXEDBUMP:
		    /* Ignore this keyword */
		    break;


	    }
	}
	else if (!strcmp(token, "RECT"))
	{
	    /* NOTE:  Use of "RECT" in NETS is not in the LEF/DEF spec.	*/
	    /* However, its use has been seen.	So "special" is not	*/
	    /* checked here.						*/

	    /* The rectangle coordinates are relative to the current	*/
	    /* reference point, not absolute.				*/

	    newRoute = (LinkedRect *)mallocMagic(sizeof(LinkedRect));

	    /* Read an (llx lly urx ury) rectangle */
	    token = LefNextToken(f, TRUE);	/* read llx */
	    if (*token == '(') token = LefNextToken(f, TRUE);
	    if (sscanf(token, "%f", &x) == 1)
	    {
		locarea.r_xbot = (refp.p_x / 2) + (int)roundf(x / oscale);
	    }
	    else
	    {
		LefError(DEF_ERROR, "Cannot parse X coordinate in RECT.\n");
		goto endCoord;
	    }

	    token = LefNextToken(f, TRUE);	/* read lly */
	    if (sscanf(token, "%f", &y) == 1)
	    {
		locarea.r_ybot = (refp.p_y / 2) + (int)roundf(y / oscale);
	    }
	    else
	    {
		LefError(DEF_ERROR, "Cannot parse Y coordinate in RECT.\n");
		goto endCoord;
	    }

	    token = LefNextToken(f, TRUE);	/* read urx */
	    if (sscanf(token, "%f", &x) == 1)
	    {
		locarea.r_xtop = (refp.p_x / 2) + (int)roundf(x / oscale);
	    }
	    else
	    {
		LefError(DEF_ERROR, "Cannot parse X coordinate in RECT.\n");
		goto endCoord;
	    }
	    token = LefNextToken(f, TRUE);	/* read ury */
	    if (sscanf(token, "%f", &y) == 1)
	    {
		locarea.r_ytop = (refp.p_y / 2) + (int)roundf(y / oscale);
	    }
	    else
	    {
		LefError(DEF_ERROR, "Cannot parse Y coordinate in RECT.\n");
		goto endCoord;
	    }
	    token = LefNextToken(f, TRUE);	/* read closing parens */
	    if (*token != ')')
	    {
		LefError(DEF_ERROR, "Bad coordinates in RECT.\n");
		goto endCoord;
	    }
	    GeoCanonicalRect(&locarea, &newRoute->r_r);
	}
	else if (!strcmp(token, "POLYGON"))
	{
	    LefError(DEF_ERROR, "Route has POLYGON entries, this is not handled!\n",
			token);
	    token = LefNextToken(f, TRUE);	/* read opening parens */
	    goto endCoord;
	}
	else if (!strcmp(token, "VIRTUAL"))
	{
	    /* Is this a LEF 5.8 thing?  Not sure if it should be ignored!  */
	    /* Should the whole wire leg be ignored?			    */
	    continue;
	}
	else if (*token != '(')	/* via name */
	{
	    /* A '+' or ';' record ends the route */
	    if (*token == ';' || *token == '+')
		break;

	    else if (valid == FALSE)
	    {
		LefError(DEF_ERROR, "Route has via name \"%s\" but no points!\n", token);
		continue;
	    }
	    he = HashLookOnly(&LefInfo, token);
	    if (he != NULL)
	    {
		lefl = (lefLayer *)HashGetValue(he);
		newRoute = (LinkedRect *)mallocMagic(sizeof(LinkedRect));

		/* The area to paint is derived from the via definitions. */

		if (lefl != NULL)
		{
		    LinkedRect *viaRoute, *addRoute;

		    /* If there is a LinkedRect structure for the via,	*/
		    /* add those records to the route first.		*/

		    for (viaRoute = lefl->info.via.lr; viaRoute != NULL;
				viaRoute = viaRoute->r_next)
		    {
			addRoute = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
			addRoute->r_next = NULL;
			addRoute->r_type = viaRoute->r_type;
			addRoute->r_r = viaRoute->r_r;

			addRoute->r_r.r_xbot += refp.p_x;
			addRoute->r_r.r_ybot += refp.p_y;
			addRoute->r_r.r_xtop += refp.p_x;
			addRoute->r_r.r_ytop += refp.p_y;

			addRoute->r_r.r_xbot >>= 1;
			addRoute->r_r.r_ybot >>= 1;
			addRoute->r_r.r_xtop >>= 1;
			addRoute->r_r.r_ytop >>= 1;

			if (routeTop)
			    routeList->r_next = addRoute;
			else
			    routeTop = addRoute;

			routeList = addRoute;
		    }

		    paintLayer = lefl->type;

		    newRoute->r_r.r_xbot = refp.p_x + lefl->info.via.area.r_xbot;
		    newRoute->r_r.r_ybot = refp.p_y + lefl->info.via.area.r_ybot;
		    newRoute->r_r.r_xtop = refp.p_x + lefl->info.via.area.r_xtop;
		    newRoute->r_r.r_ytop = refp.p_y + lefl->info.via.area.r_ytop;

		    newRoute->r_r.r_xbot >>= 1;
		    newRoute->r_r.r_ybot >>= 1;
		    newRoute->r_r.r_xtop >>= 1;
		    newRoute->r_r.r_ytop >>= 1;

		}
		else if ((paintLayer = DBTechNameType(LefLower(token))) >= 0)
		{
		    LefError(DEF_ERROR, "Error: Via \"%s\" named but undefined.\n",
				token);
		    newRoute->r_r.r_xbot = refp.p_x - paintWidth;
		    newRoute->r_r.r_ybot = refp.p_y - paintWidth;
		    newRoute->r_r.r_xtop = refp.p_x + paintWidth;
		    newRoute->r_r.r_ytop = refp.p_y + paintWidth;

		    newRoute->r_r.r_xbot >>= 1;
		    newRoute->r_r.r_ybot >>= 1;
		    newRoute->r_r.r_xtop >>= 1;
		    newRoute->r_r.r_ytop >>= 1;
		}
		else
		   LefError(DEF_ERROR, "Via name \"%s\" unknown in route.\n", token);

		/* After the via, the new route layer becomes whatever	*/
		/* residue of the via was NOT the previous route layer.	*/
		/* This is absolutely impossible to make consistent	*/
		/* with the DEF	spec, but there you have it. . .	*/

		if (DBIsContact(paintLayer))
		{
		    TileTypeBitMask *rMask = DBResidueMask(paintLayer);
		    TileType stype;

		    for (stype = TT_TECHDEPBASE; stype < DBNumUserLayers; stype++)
			if (TTMaskHasType(rMask, stype))
			    if (stype != routeLayer)
			    {
				/* Diagnostic */
				/*
				TxPrintf("Contact %s: In=%s Out=%s\n",
					DBTypeLongNameTbl[paintLayer],
					DBTypeLongNameTbl[routeLayer],
					DBTypeLongNameTbl[stype]);
				*/
				routeLayer = stype;
				lefl = defLayerMap[routeLayer].lefInfo;
				if (special)
				    paintWidth = saveWidth;
				else
				    paintWidth = (lefl) ? lefl->info.route.width
					: DEFAULT_WIDTH * DBLambda[1] / DBLambda[0];
				break;
			    }
		}
	    }
	    else
		LefError(DEF_ERROR, "Via name \"%s\" unknown in route.\n", token);
	}
	else
	{
	    /* Revert to the routing layer type, in case we painted a via */
	    paintLayer = routeLayer;

	    /* Record current reference point */
	    locarea.r_xbot = refp.p_x;
	    locarea.r_ybot = refp.p_y;
	    lextend = extend;

	    /* Read an (X Y [extend]) point */
	    token = LefNextToken(f, TRUE);	/* read X */
	    if (*token == '*')
	    {
		if (valid == FALSE)
		{
		    LefError(DEF_ERROR, "No reference point for \"*\" wildcard\n");
		    goto endCoord;
		}
	    }
	    else if (sscanf(token, "%f", &x) == 1)
	    {
		refp.p_x = (int)roundf((2 * x) / oscale);
	    }
	    else
	    {
		LefError(DEF_ERROR, "Cannot parse X coordinate.\n");
		goto endCoord;
	    }
	    token = LefNextToken(f, TRUE);	/* read Y */
	    if (*token == '*')
	    {
		if (valid == FALSE)
		{
		    LefError(DEF_ERROR, "No reference point for \"*\" wildcard\n");
		    freeMagic(newRoute);
		    newRoute = NULL;
		    goto endCoord;
		}
	    }
	    else if (sscanf(token, "%f", &y) == 1)
	    {
		refp.p_y = (int)roundf((2 * y) / oscale);
	    }
	    else
	    {
		LefError(DEF_ERROR, "Cannot parse Y coordinate.\n");
		goto endCoord;
	    }

	    /* Extension is half-width for regular nets, 0 for special nets */
	    /* 0 for special nets is *not* how the 5.3 spec reads, but it   */
	    /* is apparently how everyone interprets it, and is true for    */
	    /* 5.6 spec.						    */

	    extend = (special) ? 0 : paintWidth;
	    token = LefNextToken(f, TRUE);
	    if (*token != ')')
	    {
		/* non-default route extension */
		if (sscanf(token, "%f", &z) != 1)
		    LefError(DEF_ERROR, "Can't parse route extension value.\n");

		/* all values will be divided by 2, so we need	*/
		/* to multiply up by 2 now.			*/

		else
		    extend = (int)roundf((2 * z) / oscale);
	    }

	    /* Indicate that we have a valid reference point */

	    if (valid == FALSE)
	    {
		valid = TRUE;
	    }
	    else if ((locarea.r_xbot != refp.p_x) && (locarea.r_ybot != refp.p_y))
	    {
		/* Skip over nonmanhattan segments, reset the reference	*/
		/* point, and output a warning.				*/

		LefError(DEF_ERROR, "Can't deal with nonmanhattan geometry in route.\n");
		locarea.r_xbot = refp.p_x;
		locarea.r_ybot = refp.p_y;
		lextend = extend;
	    }
	    else
	    {
		newRoute = (LinkedRect *)mallocMagic(sizeof(LinkedRect));

		/* Route coordinates become the centerline of the	*/
		/* segment.  "refp" is kept in 1/2 lambda units so	*/
		/* we should always end up with integer units.		*/

		locarea.r_xtop = refp.p_x;
		locarea.r_ytop = refp.p_y;

		/* Change route segment to a canonical rectangle.  If	*/
		/* the route is flipped relative to canonical coords,	*/
		/* then the wire extentions have to be swapped as well.	*/

		if ((locarea.r_xtop < locarea.r_xbot) ||
			(locarea.r_ytop < locarea.r_ybot))
		{
		    hextend = lextend;
		    lextend = extend;
		}
		else
		    hextend = extend;

		GeoCanonicalRect(&locarea, &newRoute->r_r);

		if (newRoute->r_r.r_xbot == newRoute->r_r.r_xtop)
		{
		    newRoute->r_r.r_xbot -= paintWidth;
		    newRoute->r_r.r_xtop += paintWidth;
		}
		else
		{
		    newRoute->r_r.r_xbot -= lextend;
		    newRoute->r_r.r_xtop += hextend;
		}

		if (newRoute->r_r.r_ybot == newRoute->r_r.r_ytop)
		{
		    newRoute->r_r.r_ybot -= paintWidth;
		    newRoute->r_r.r_ytop += paintWidth;
		}
		else
		{
		    newRoute->r_r.r_ybot -= lextend;
		    newRoute->r_r.r_ytop += hextend;
		}

		/* If we don't have integer units here, we should	*/
		/* rescale the magic internal grid.			*/

		newRoute->r_r.r_xbot >>= 1;
		newRoute->r_r.r_ybot >>= 1;
		newRoute->r_r.r_xtop >>= 1;
		newRoute->r_r.r_ytop >>= 1;
	    }

endCoord:
	    /* Find the closing parenthesis for the coordinate pair */
	    while (*token != ')')
		token = LefNextToken(f, TRUE);
	}

	/* Link in the new route segment */
	if (newRoute)
	{
	    newRoute->r_type = paintLayer;
	    newRoute->r_next = NULL;

	    if (routeTop)
		routeList->r_next = newRoute;
	    else
		routeTop = newRoute;

	    routeList = newRoute;
	    newRoute = NULL;
	}
    }

    /* Process each segment and paint into the layout */

    while (routeTop != NULL)
    {
	/* paint */
	DBPaint(rootDef, &routeTop->r_r, routeTop->r_type);

	/* label */
	if (labeled == FALSE)
	{
	    Rect r;
	    r.r_xbot = r.r_xtop = (routeTop->r_r.r_xbot + routeTop->r_r.r_xtop) / 2;
	    r.r_ybot = r.r_ytop = (routeTop->r_r.r_ybot + routeTop->r_r.r_ytop) / 2;
	    DBPutLabel(rootDef, &r, GEO_CENTER, netname, routeTop->r_type, 0);
	    labeled = TRUE;
	}

	/* advance to next point and free record (1-delayed) */
	freeMagic((char *)routeTop);
	routeTop = routeTop->r_next;
    }
    return token;	/* Pass back the last token found */
}

/*
 *------------------------------------------------------------
 *
 * DefReadNets --
 *
 *	Read a NETS or SPECIALNETS section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Many.  Networks are created, and geometry may be
 *	painted into the database top-level cell.
 *
 *------------------------------------------------------------
 */

enum def_net_keys {DEF_NET_START = 0, DEF_NET_END};
enum def_netprop_keys {
	DEF_NETPROP_USE = 0, DEF_NETPROP_ROUTED, DEF_NETPROP_FIXED,
	DEF_NETPROP_COVER, DEF_NETPROP_SOURCE, DEF_NETPROP_WEIGHT,
	DEF_NETPROP_PROPERTY};

void
DefReadNets(f, rootDef, sname, oscale, special, dolabels, total)
    FILE *f;
    CellDef *rootDef;
    char *sname;
    float oscale;
    bool special;		/* True if this section is SPECIALNETS */
    bool dolabels;		/* If true, create a label for each net */
    int total;
{
    char *token;
    char *netname = NULL;
    int keyword, subkey;
    int processed = 0;
    LefMapping *defLayerMap;

    static char *net_keys[] = {
	"-",
	"END",
	NULL
    };

    static char *net_property_keys[] = {
	"USE",
	"ROUTED",
	"FIXED",
	"COVER",
	"SOURCE",
	"WEIGHT",
	"PROPERTY",
	NULL
    };

    defLayerMap = defMakeInverseLayerMap(LAYER_MAP_VIAS);

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, net_keys);
	if (keyword < 0)
	{
	    LefError(DEF_INFO, "Unknown keyword \"%s\" in NET "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}

	switch (keyword)
	{
	    case DEF_NET_START:

		/* Get net name */
		token = LefNextToken(f, TRUE);
		if (dolabels) netname = StrDup((char **)NULL, token);

		/* Update the record of the number of nets processed	*/
		/* and spit out a message for every 5% finished.	*/

		LefEstimate(processed++, total,
			(special) ? "special nets" : "nets");

		/* Process all properties */
		while (token && (*token != ';'))
		{
		    /* All connections are ignored, and we go		*/
		    /* go directly to the first property ("+") key	*/

		    if (*token != '+')
		    {
			token = LefNextToken(f, TRUE);
			continue;
		    }
		    else
			token = LefNextToken(f, TRUE);

		    subkey = Lookup(token, net_property_keys);
		    if (subkey < 0)
		    {
			LefError(DEF_INFO, "Unknown net property \"%s\" in "
				"NET definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_NETPROP_USE:
			    /* Presently, we ignore this, except to	*/
			    /* absorb the following value.		*/
			    token = LefNextToken(f, TRUE);
			    break;
			case DEF_NETPROP_ROUTED:
			case DEF_NETPROP_FIXED:
			case DEF_NETPROP_COVER:
			    token = DefAddRoutes(rootDef, f, oscale, special,
					netname, defLayerMap);
			    break;
		    }
		}
		if (dolabels) freeMagic(netname);
		break;

	    case DEF_NET_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError(DEF_ERROR, "Net END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_NET_END) break;
    }

    if (processed == total)
	TxPrintf("  Processed %d%s nets total.\n", processed,
		(special) ? " special" : "");
    else
	LefError(DEF_WARNING, "Number of nets read (%d) does not match "
		"the number declared (%d).\n", processed, total);

    freeMagic((char *)defLayerMap);
}

/*
 *------------------------------------------------------------
 *
 * DefReadUseLocation --
 *
 *	Read location and orientation of a cell use
 *	Syntax: ( X Y ) O
 *
 * Results:
 *	0 on success, -1 on failure
 *
 * Side Effects:
 *	Transform is placed in the location pointed to by "tptr".
 *	This routine can be used for placement of geometry (as
 *	opposed to cell uses) by setting "use" to NULL, and using
 *	the resulting transform to modify the geometry.
 *------------------------------------------------------------
 */
enum def_orient {DEF_NORTH, DEF_SOUTH, DEF_EAST, DEF_WEST,
	DEF_FLIPPED_NORTH, DEF_FLIPPED_SOUTH, DEF_FLIPPED_EAST,
	DEF_FLIPPED_WEST};

int
DefReadLocation(use, f, oscale, tptr, noplace)
    CellUse *use;
    FILE *f;
    float oscale;
    Transform *tptr;
    bool noplace;
{
    Rect *r, tr, rect;
    int keyword;
    char *token;
    float x, y;
    Transform t2;

    static char *orientations[] = {
	"N", "S", "E", "W", "FN", "FS", "FE", "FW"
    };

    if (noplace)
    {
	LefError(DEF_WARNING, "Unplaced component \"%s\" will be put at origin.\n",
		    use->cu_id);
	x = 0;
	y = 0;
	keyword = DEF_NORTH;
    }
    else
    {
        token = LefNextToken(f, TRUE);
        if (*token != '(') goto parse_error;
        token = LefNextToken(f, TRUE);
        if (sscanf(token, "%f", &x) != 1) goto parse_error;
        token = LefNextToken(f, TRUE);
        if (sscanf(token, "%f", &y) != 1) goto parse_error;
        token = LefNextToken(f, TRUE);
        if (*token != ')') goto parse_error;
        token = LefNextToken(f, TRUE);

        keyword = Lookup(token, orientations);
        if (keyword < 0)
        {
	    LefError(DEF_ERROR, "Unknown macro orientation \"%s\".\n", token);
	    return -1;
	}
    }

    /* The standard transformations are all defined to rotate	*/
    /* or flip about the origin.  However, DEF defines them	*/
    /* around the center such that the lower left corner is	*/
    /* unchanged after the transformation.  Case conditions	*/
    /* restore the lower-left corner position.			*/

    if (use)
    {
	r = &use->cu_def->cd_bbox;

	/* Abstract views with fixed bounding boxes use the FIXED_BBOX property */

	if (use->cu_def->cd_flags & CDFIXEDBBOX)
	{
	    char *propval;
	    bool found;

	    propval = DBPropGet(use->cu_def, "FIXED_BBOX", &found);
	    if (found)
	    {
		if (sscanf(propval, "%d %d %d %d", &rect.r_xbot, &rect.r_ybot,
			    &rect.r_xtop, &rect.r_ytop) == 4)
		    r = &rect;
	    }
	}
    }
    else
	r = &GeoNullRect;

    switch (keyword)
    {
	case DEF_NORTH:
	    *tptr = GeoIdentityTransform;
	    break;
	case DEF_SOUTH:
	    *tptr = Geo180Transform;
	    break;
	case DEF_EAST:
	    *tptr = Geo90Transform;
	    break;
	case DEF_WEST:
	    *tptr = Geo270Transform;
	    break;
	case DEF_FLIPPED_NORTH:
	    *tptr = GeoSidewaysTransform;
	    break;
	case DEF_FLIPPED_SOUTH:
	    *tptr = GeoUpsideDownTransform;
	    break;
	case DEF_FLIPPED_EAST:
	    *tptr = GeoRef135Transform;
	    break;
	case DEF_FLIPPED_WEST:
	    *tptr = GeoRef45Transform;
	    break;
    }
    GeoTransRect(tptr, r, &tr);
    GeoTranslateTrans(tptr, -tr.r_xbot, -tr.r_ybot, &t2);
    GeoTranslateTrans(&t2, (int)roundf(x / oscale), (int)roundf(y / oscale), tptr);
    if (use)
	DBSetTrans(use, tptr);
    return 0;

parse_error:
    LefError(DEF_ERROR, "Cannot parse location: must be ( X Y ) orient\n");
    return -1;
}

/*
 *------------------------------------------------------------
 *
 * DefReadPins --
 *
 *	Read a PINS section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Generates paint and labels in the layout.
 *
 *------------------------------------------------------------
 */

enum def_pins_keys {DEF_PINS_START = 0, DEF_PINS_END};
enum def_pins_prop_keys {
	DEF_PINS_PROP_NET = 0, DEF_PINS_PROP_DIR,
	DEF_PINS_PROP_LAYER, DEF_PINS_PROP_USE,
	DEF_PINS_PROP_PLACED, DEF_PINS_PROP_FIXED,
	DEF_PINS_PROP_PORT, DEF_PINS_PROP_SPECIAL};

void
DefReadPins(f, rootDef, sname, oscale, total)
    FILE *f;
    CellDef *rootDef;
    char *sname;
    float oscale;
    int total;
{
    char *token;
    char pinname[LEF_LINE_MAX];
    int keyword, subkey, values, flags;
    int processed = 0;
    int pinDir = PORT_CLASS_DEFAULT;
    int pinNum = 0;
    TileType curlayer = -1;
    Rect *currect, topRect;
    Transform t;
    lefLayer *lefl;
    bool pending = FALSE;
    bool hasports = FALSE;

    static char *pin_keys[] = {
	"-",
	"END",
	NULL
    };

    static char *pin_property_keys[] = {
	"NET",
	"DIRECTION",
	"LAYER",
	"USE",
	"PLACED",
	"FIXED",
	"PORT",
	"SPECIAL",
	NULL
    };

    static char *pin_classes[] = {
	"DEFAULT",
	"INPUT",
	"OUTPUT TRISTATE",
	"OUTPUT",
	"INOUT",
	"FEEDTHRU",
	NULL
    };

    static int lef_class_to_bitmask[] = {
	PORT_CLASS_DEFAULT,
	PORT_CLASS_INPUT,
	PORT_CLASS_TRISTATE,
	PORT_CLASS_OUTPUT,
	PORT_CLASS_BIDIRECTIONAL,
	PORT_CLASS_FEEDTHROUGH
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, pin_keys);

	if (keyword < 0)
	{
	    LefError(DEF_INFO, "Unknown keyword \"%s\" in PINS "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_PINS_START:		/* "-" keyword */
		hasports = FALSE;

		// Flag an error if a pin was waiting on a layer
		// specification that was never given.

		if (pending)
		{
		    LefError(DEF_ERROR, "Pin specified without layer, was not placed.\n");
		}

		/* Update the record of the number of pins		*/
		/* processed and spit out a message for every 5% done.	*/

		LefEstimate(processed++, total, "pins");

		/* Get pin name */
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%2047s", pinname) != 1)
		{
		    LefError(DEF_ERROR, "Bad pin statement:  Need pin name\n");
		    LefEndStatement(f);
		    break;
		}
		pending = FALSE;
		curlayer = -1;

		/* Now do a search through the line for "+" entries	*/
		/* And process each.					*/

		while ((token = LefNextToken(f, TRUE)) != NULL)
		{
		    if (*token == ';') break;
		    if (*token != '+') continue;

		    token = LefNextToken(f, TRUE);
		    subkey = Lookup(token, pin_property_keys);
		    if (subkey < 0)
		    {
			LefError(DEF_INFO, "Unknown pin property \"%s\" in "
				"PINS definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_PINS_PROP_SPECIAL:
			    /* Ignore this */
			    break;
			case DEF_PINS_PROP_PORT:
			    /* Ignore this, except that each port adds to   */
			    /* the count of total pins processed.  Note	    */
			    /* that since "processed" is incremented before */
			    /* the first PORT is seen, then "processed"	    */
			    /* should not be incremented until the 2nd PORT */
			    if (hasports) processed++;
			    hasports = TRUE;
			    break;
			case DEF_PINS_PROP_USE:
			case DEF_PINS_PROP_NET:
			    /* Get the net name, but ignore it */
			    token = LefNextToken(f, TRUE);
			    break;
			case DEF_PINS_PROP_DIR:
			    token = LefNextToken(f, TRUE);
			    subkey = Lookup(token, pin_classes);
			    if (subkey < 0)
				LefError(DEF_ERROR, "Unknown pin class\n");
			    else
				pinDir = lef_class_to_bitmask[subkey];
			    break;
			case DEF_PINS_PROP_LAYER:
			    curlayer = LefReadLayer(f, FALSE);
			    currect = LefReadRect(f, curlayer, oscale);
			    if (pending)
			    {
				/* If layer was unknown, set to space and force	*/
				/* non-sticky.					*/
				flags = PORT_DIR_MASK;
				if (curlayer < 0)
				    curlayer = TT_SPACE;
				else
				    flags |= LABEL_STICKY;

				GeoTransRect(&t, currect, &topRect);
				DBPaint(rootDef, &topRect, curlayer);
				DBPutLabel(rootDef, &topRect, -1, pinname, curlayer,
					pinNum | pinDir | flags);
				pending = FALSE;
				pinNum++;
			    }
			    break;
			case DEF_PINS_PROP_FIXED:
			case DEF_PINS_PROP_PLACED:
			    DefReadLocation(NULL, f, oscale, &t, FALSE);
			    if (curlayer == -1)
				pending = TRUE;
			    else
			    {
				/* If layer was unknown, set to space and force	*/
				/* non-sticky.					*/
				flags = PORT_DIR_MASK;
				if (curlayer < 0)
				    curlayer = TT_SPACE;
				else
				    flags |= LABEL_STICKY;

				GeoTransRect(&t, currect, &topRect);
				DBPaint(rootDef, &topRect, curlayer);
				DBPutLabel(rootDef, &topRect, -1, pinname, curlayer,
					pinNum | pinDir | flags);
				pinNum++;
			    }
			    break;
		    }
		}

		break;

	    case DEF_PINS_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError(DEF_ERROR, "Pins END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_PINS_END) break;
    }

    if (processed == total)
	TxPrintf("  Processed %d pins total.\n", processed);
    else
	LefError(DEF_WARNING, "Number of pins read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}

/*
 *------------------------------------------------------------
 *
 * DefReadVias --
 *
 *	Read a VIAS section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Technically, this routine should be creating a cell for
 *	each defined via.  For now, it just computes the bounding
 *	rectangle and layer.
 *
 *------------------------------------------------------------
 */

enum def_vias_keys {DEF_VIAS_START = 0, DEF_VIAS_END};
enum def_vias_prop_keys {
	DEF_VIAS_PROP_RECT = 0, DEF_VIAS_PROP_VIARULE,
	DEF_VIAS_PROP_CUTSIZE, DEF_VIAS_PROP_LAYERS,
	DEF_VIAS_PROP_CUTSPACING, DEF_VIAS_PROP_ENCLOSURE,
	DEF_VIAS_PROP_ROWCOL};

void
DefReadVias(f, sname, oscale, total)
    FILE *f;
    char *sname;
    float oscale;
    int total;
{
    char *token;
    char vianame[LEF_LINE_MAX];
    int keyword, subkey, values;
    int processed = 0;
    TileType curlayer;
    Rect *currect;
    lefLayer *lefl;
    HashEntry *he;

    /* For generated vias */
    bool generated = FALSE;
    int sizex, sizey, spacex, spacey;
    int encbx, encby, enctx, encty;
    int rows = 1, cols = 1;
    TileType tlayer, clayer, blayer;

    static char *via_keys[] = {
	"-",
	"END",
	NULL
    };

    static char *via_property_keys[] = {
	"RECT",
	"VIARULE",
	"CUTSIZE",
	"LAYERS",
	"CUTSPACING",
	"ENCLOSURE",
	"ROWCOL",
	NULL
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, via_keys);

	if (keyword < 0)
	{
	    LefError(DEF_INFO, "Unknown keyword \"%s\" in VIAS "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_VIAS_START:		/* "-" keyword */

		/* Update the record of the number of vias		*/
		/* processed and spit out a message for every 5% done.	*/

		LefEstimate(processed++, total, "vias");

		/* If not otherwise specified, rows and columns default to 1 */
		rows = cols = 1;

		/* Get via name */
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%2047s", vianame) != 1)
		{
		    LefError(DEF_ERROR, "Bad via statement:  Need via name\n");
		    LefEndStatement(f);
		    break;
		}
		he = HashFind(&LefInfo, vianame);
		lefl = (lefLayer *)HashGetValue(he);
		if (lefl == NULL)
		{
		    lefl = (lefLayer *)mallocMagic(sizeof(lefLayer));
		    lefl->type = -1;
		    lefl->obsType = -1;
		    lefl->lefClass = CLASS_VIA;
		    lefl->info.via.area = GeoNullRect;
		    lefl->info.via.cell = (CellDef *)NULL;
		    lefl->info.via.lr = (LinkedRect *)NULL;
		    HashSetValue(he, lefl);
		    lefl->canonName = (char *)he->h_key.h_name;
		}
		else
		{
		    LefError(DEF_WARNING, "Composite via \"%s\" redefined.\n", vianame);
		    lefl = LefRedefined(lefl, vianame);
		}

		/* Now do a search through the line for "+" entries	*/
		/* And process each.					*/

		while ((token = LefNextToken(f, TRUE)) != NULL)
		{
		    if (*token == ';') {
			if (generated == TRUE) {
			    /* Complete the generated via */
			    LefGenViaGeometry(f, lefl,
				    sizex, sizey, spacex, spacey,
				    encbx, encby, enctx, encty,
				    rows, cols, tlayer, clayer, blayer,
				    oscale);
			}
			break;
		    }
		    if (*token != '+') continue;

		    token = LefNextToken(f, TRUE);
		    subkey = Lookup(token, via_property_keys);
		    if (subkey < 0)
		    {
			LefError(DEF_INFO, "Unknown via property \"%s\" in "
				"VIAS definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_VIAS_PROP_RECT:
			    curlayer = LefReadLayer(f, FALSE);
			    LefAddViaGeometry(f, lefl, curlayer, oscale);
			    break;

			case DEF_VIAS_PROP_VIARULE:
			    token = LefNextToken(f, TRUE);
			    /* Ignore this.  To do:  Parse VIARULE statements	*/
			    /* and use the rule to fill any missing values.	*/
			    break;
			case DEF_VIAS_PROP_CUTSIZE:
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &sizex) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for CUTSIZE.\n");
				/* To do:  Get cut size from DRC ruleset */
			    }
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &sizey) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for CUTSIZE.\n");
				/* To do:  Get cut size from DRC ruleset */
			    }
			    generated = TRUE;
			    break;
			case DEF_VIAS_PROP_LAYERS:
			    blayer = LefReadLayer(f, FALSE);
			    clayer = LefReadLayer(f, FALSE);
			    tlayer = LefReadLayer(f, FALSE);

			    /* Provisional behavior:  A known tool generating	*/
			    /* DEF uses the order (bottom, top, cut).  This may	*/
			    /* be a bug in the tool and an issue is being	*/
			    /* raised.  However, there is no harm in detecting	*/
			    /* which layer is the cut and swapping as needed.	*/

			    if (!DBIsContact(clayer))
			    {
				TileType swaplayer;
				LefError(DEF_WARNING, "Improper layer order for"
					" VIARULE.\n");
				if (DBIsContact(tlayer))
				{
				    swaplayer = clayer;
				    clayer = tlayer;
				    tlayer = swaplayer;
				}
				else if (DBIsContact(blayer))
				{
				    swaplayer = clayer;
				    clayer = blayer;
				    blayer = swaplayer;
				}
				else
				    LefError(DEF_ERROR, "No cut layer specified in"
					    " VIARULE.\n");
			    }

			    generated = TRUE;
			    break;
			case DEF_VIAS_PROP_CUTSPACING:
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &spacex) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for CUTSPACING.\n");
				/* To do:  Get cut spacing from DRC ruleset */
			    }
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &spacey) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for CUTSPACING.\n");
				/* To do:  Get cut spacing from DRC ruleset */
			    }
			    generated = TRUE;
			    break;
			case DEF_VIAS_PROP_ENCLOSURE:
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &encbx) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for ENCLOSURE.\n");
				/* To do:  Get cut enclosures from DRC ruleset */
			    }
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &encby) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for ENCLOSURE.\n");
				/* To do:  Get cut enclosures from DRC ruleset */
			    }
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &enctx) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for ENCLOSURE.\n");
				/* To do:  Get cut enclosures from DRC ruleset */
			    }
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &encty) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for ENCLOSURE.\n");
				/* To do:  Get cut enclosures from DRC ruleset */
			    }
			    generated = TRUE;
			    break;
			case DEF_VIAS_PROP_ROWCOL:
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &rows) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for ROWCOL.\n");
				rows = 1;
			    }
			    token = LefNextToken(f, TRUE);
			    if (sscanf(token, "%d", &cols) != 1)
			    {
				LefError(DEF_ERROR, "Invalid syntax for ROWCOL.\n");
				cols = 1;
			    }
			    generated = TRUE;
			    break;
		    }
		}
		break;

	    case DEF_VIAS_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError(DEF_ERROR, "Vias END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_VIAS_END) break;
    }

    if (processed == total)
	TxPrintf("  Processed %d vias total.\n", processed);
    else
	LefError(DEF_WARNING, "Number of vias read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}

/*
 *------------------------------------------------------------
 *
 * DefReadComponents --
 *
 *	Read a COMPONENTS section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Many.  Cell definitions and uses are created and added to
 *	the database.
 *
 *------------------------------------------------------------
 */

enum def_comp_keys {DEF_COMP_START = 0, DEF_COMP_END};
enum def_prop_keys {
	DEF_PROP_FIXED = 0, DEF_PROP_COVER,
	DEF_PROP_PLACED, DEF_PROP_UNPLACED,
	DEF_PROP_SOURCE, DEF_PROP_WEIGHT, DEF_PROP_FOREIGN,
	DEF_PROP_REGION, DEF_PROP_GENERATE, DEF_PROP_PROPERTY,
	DEF_PROP_EEQMASTER};

void
DefReadComponents(f, rootDef, sname, oscale, total)
    FILE *f;
    CellDef *rootDef;
    char *sname;
    float oscale;
    int total;
{
    CellDef *defMacro;
    CellUse *defUse;
    Transform t;
    char *token, *dptr;
    char usename[512];
    int keyword, subkey, values;
    int processed = 0;

    static char *component_keys[] = {
	"-",
	"END",
	NULL
    };

    static char *property_keys[] = {
	"FIXED",
	"COVER",
	"PLACED",
	"UNPLACED",
	"SOURCE",
	"WEIGHT",
	"FOREIGN",
	"REGION",
	"GENERATE",
	"PROPERTY",
	"EEQMASTER",
	NULL
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, component_keys);

	if (keyword < 0)
	{
	    LefError(DEF_INFO, "Unknown keyword \"%s\" in COMPONENT "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_COMP_START:		/* "-" keyword */

		/* Update the record of the number of components	*/
		/* processed and spit out a message for every 5% done.	*/

		LefEstimate(processed++, total, "subcell instances");

		/* Get use and macro names */
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%511s", usename) != 1)
		{
		    LefError(DEF_ERROR, "Bad component statement:  Need "
			    "use and macro names\n");
		    LefEndStatement(f);
		    break;
		}

		/* Does use name contain brackets?  If so, this can */
		/* interfere with magic's use of arrays.	    */
		/* NOTE:  This has been commented out.  I think	    */
		/* the only confusion is in ext2spice and can be    */
		/* avoided by allowing any bracket notation in an   */
		/* instance name other than that used by the .ext   */
		/* file for dealing with arrays, which uses the	    */
		/* specific syntax [xlo:xsep:xhi][ylo:ysep:yhi] and */
		/* is easy enough to distinguish.		    */

		/*
		    dptr = strchr(usename, '[');
		    if (dptr != NULL) {
			*dptr = '_';
			dptr = strchr(dptr + 1, ']');
			if (dptr != NULL) *dptr = '_';
		    }
		*/

		token = LefNextToken(f, TRUE);

		/* Find the corresponding macro definition */
		defUse = NULL;
		defMacro = DBCellLookDef(token);

		if (defMacro == (CellDef *)NULL)
		{
		    bool dereference;

		    /* Before giving up, assume that this cell has a	*/
		    /* magic .mag layout file.				*/
		    defMacro = DBCellNewDef(token);
		    defMacro->cd_flags &= ~CDNOTFOUND;
		    dereference = (defMacro->cd_flags & CDDEREFERENCE) ? TRUE : FALSE;
		    if (!DBCellRead(defMacro, (char *)NULL, TRUE, dereference, NULL))
		    {
		        LefError(DEF_ERROR, "Cell %s is not defined.  Maybe you "
				"have not read the corresponding LEF file?\n",
				token);
		        LefEndStatement(f);
			DBCellDeleteDef(defMacro);
			defMacro = NULL;
		    }
		    else
		       DBReComputeBbox(defMacro);
		}

		/* Create a use for this celldef in the edit cell */
		/* Don't process properties for cells we could not find */

		if ((defMacro == NULL) || ((defUse = DBCellNewUse(defMacro, usename))
			== NULL))
		{
		    if (defMacro != NULL) LefEndStatement(f);
		    break;
		}
		DBLinkCell(defUse, rootDef);

		/* Now do a search through the line for "+" entries	*/
		/* And process each.					*/

		while ((token = LefNextToken(f, TRUE)) != NULL)
		{
		    if (*token == ';') break;
		    if (*token != '+') continue;

		    token = LefNextToken(f, TRUE);
		    subkey = Lookup(token, property_keys);
		    if (subkey < 0)
		    {
			LefError(DEF_INFO, "Unknown component property \"%s\" in "
				"COMPONENT definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_PROP_PLACED:
			case DEF_PROP_FIXED:
			case DEF_PROP_COVER:
			    DefReadLocation(defUse, f, oscale, &t, FALSE);
			    break;
			case DEF_PROP_UNPLACED:
			    DefReadLocation(defUse, f, oscale, &t, TRUE);
			    break;
			case DEF_PROP_SOURCE:
			case DEF_PROP_WEIGHT:
			case DEF_PROP_FOREIGN:
			case DEF_PROP_REGION:
			case DEF_PROP_GENERATE:
			case DEF_PROP_PROPERTY:
			case DEF_PROP_EEQMASTER:
			    token = LefNextToken(f, TRUE);
			    break;
		    }
		}

		/* Place the cell */
		if (defUse != NULL)
		{
		    DBPlaceCell(defUse, rootDef);
		    defUse = NULL;
		}
		break;

	    case DEF_COMP_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError(DEF_ERROR, "Component END statement missing.\n");
		    keyword = -1;
		}

		/* Finish final call by placing the cell use */
		if ((total > 0) && (defUse != NULL))
		{
		    DBPlaceCell(defUse, rootDef);
		    defUse = NULL;
		}
		break;
	}
	if (keyword == DEF_COMP_END) break;
    }

    if (processed == total)
	TxPrintf("  Processed %d subcell instances total.\n", processed);
    else
	LefError(DEF_WARNING, "Number of subcells read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}

/*
 *------------------------------------------------------------
 *
 * DefRead --
 *
 *	Read a .def file into a magic layout.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Many.  Cell definitions and uses are created and added to
 *	the database.
 *
 *------------------------------------------------------------
 */

/* Enumeration of sections defined in DEF files */

enum def_sections {DEF_VERSION = 0, DEF_NAMESCASESENSITIVE,
	DEF_UNITS, DEF_DESIGN, DEF_REGIONS, DEF_ROW, DEF_TRACKS,
	DEF_GCELLGRID, DEF_DIVIDERCHAR, DEF_BUSBITCHARS,
	DEF_PROPERTYDEFINITIONS, DEF_DEFAULTCAP, DEF_TECHNOLOGY,
	DEF_HISTORY, DEF_DIEAREA, DEF_COMPONENTS, DEF_VIAS,
	DEF_PINS, DEF_PINPROPERTIES, DEF_SPECIALNETS,
	DEF_NETS, DEF_IOTIMINGS, DEF_SCANCHAINS,
	DEF_CONSTRAINTS, DEF_GROUPS, DEF_EXTENSION,
	DEF_END};

void
DefRead(inName, dolabels)
    char *inName;
    bool dolabels;
{
    CellDef *rootDef;
    FILE *f;
    char *filename;
    char *token;
    int keyword, dscale, total;
    float oscale;

    static char *sections[] = {
	"VERSION",
	"NAMESCASESENSITIVE",
	"UNITS",
	"DESIGN",
	"REGIONS",
	"ROW",
	"TRACKS",
	"GCELLGRID",
	"DIVIDERCHAR",
	"BUSBITCHARS",
	"PROPERTYDEFINITIONS",
	"DEFAULTCAP",
	"TECHNOLOGY",
	"HISTORY",
	"DIEAREA",
	"COMPONENTS",
	"VIAS",
	"PINS",
	"PINPROPERTIES",
	"SPECIALNETS",
	"NETS",
	"IOTIMINGS",
	"SCANCHAINS",
	"CONSTRAINTS",
	"GROUPS",
	"BEGINEXT",
	"END",
	NULL
    };

    /* Make sure we have a valid LefInfo hash table, even if it's empty */
    if (LefInfo.ht_table == (HashEntry **) NULL)
	LefTechInit();

    f = lefFileOpen(NULL, inName, ".def", "r", &filename);

    if (f == NULL)
    {
#ifdef MAGIC_WRAPPER
	TxError("Cannot open input file %s (%s).\n", filename,
		strerror(errno));
#else
	TxError("Cannot open input file: ");
	perror(filename);
#endif
	return;
    }

    /* Initialize */

    TxPrintf("Reading DEF data from file %s.\n", filename);
    TxPrintf("This action cannot be undone.\n");
    UndoDisable();

    /* This works for CIF reads;  maybe should only do this if the top	*/
    /* cell is (UNNAMED)?						*/

    rootDef = EditCellUse->cu_def;
    DBCellRenameDef(rootDef, inName);
    oscale = CIFGetOutputScale(1000);
    lefCurrentLine = 0;

    /* Read file contents */

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, sections);
	if (keyword < 0)
	{
	    LefError(DEF_INFO, "Unknown keyword \"%s\" in DEF file; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_VERSION:
		LefEndStatement(f);
		break;
	    case DEF_NAMESCASESENSITIVE:
		LefEndStatement(f);
		break;
	    case DEF_TECHNOLOGY:
		token = LefNextToken(f, TRUE);
		if (strcmp(token, DBTechName))
		{
		    LefError(DEF_WARNING, "DEF technology name \"%s\" does not"
			" match current magic technology name \"%s\"\n",
			token, DBTechName);
		}
		LefEndStatement(f);
	 	break;
	    case DEF_REGIONS:
		LefSkipSection(f, sections[DEF_REGIONS]);
		break;
	    case DEF_DESIGN:
		token = LefNextToken(f, TRUE);
		DBCellRenameDef(rootDef, token);
		LefEndStatement(f);
		break;
	    case DEF_UNITS:
		token = LefNextToken(f, TRUE);
		token = LefNextToken(f, TRUE);
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &dscale) != 1)
		{
		    LefError(DEF_ERROR, "Invalid syntax for UNITS statement.\n");
		    LefError(DEF_INFO, "Assuming default value of 100\n");
		    dscale = 100;
		}
		/* We don't care if the scale is 100, 200, 1000, or 2000. */
		/* Do we need to deal with numeric roundoff issues?	  */
		oscale *= (float)dscale;
		LefEndStatement(f);
		break;
	    case DEF_ROW:
		LefEndStatement(f);
		break;
	    case DEF_TRACKS:
		LefEndStatement(f);
		break;
	    case DEF_GCELLGRID:
		LefEndStatement(f);
		break;
	    case DEF_DIVIDERCHAR:
		LefEndStatement(f);
		break;
	    case DEF_BUSBITCHARS:
		LefEndStatement(f);
		break;
	    case DEF_HISTORY:
		LefEndStatement(f);
		break;
	    case DEF_DIEAREA:
		LefEndStatement(f);
		break;
	    case DEF_PROPERTYDEFINITIONS:
		LefSkipSection(f, sections[DEF_PROPERTYDEFINITIONS]);
		break;
	    case DEF_DEFAULTCAP:
		LefSkipSection(f, sections[DEF_DEFAULTCAP]);
		break;
	    case DEF_COMPONENTS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadComponents(f, rootDef, sections[DEF_COMPONENTS], oscale, total);
		break;
	    case DEF_VIAS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadVias(f, sections[DEF_VIAS], oscale, total);
		break;
	    case DEF_PINS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadPins(f, rootDef, sections[DEF_PINS], oscale, total);
		break;
	    case DEF_PINPROPERTIES:
		LefSkipSection(f, sections[DEF_PINPROPERTIES]);
		break;
	    case DEF_SPECIALNETS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadNets(f, rootDef, sections[DEF_SPECIALNETS], oscale, TRUE,
			dolabels, total);
		break;
	    case DEF_NETS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadNets(f, rootDef, sections[DEF_NETS], oscale, FALSE,
			dolabels, total);
		break;
	    case DEF_IOTIMINGS:
		LefSkipSection(f, sections[DEF_IOTIMINGS]);
		break;
	    case DEF_SCANCHAINS:
		LefSkipSection(f, sections[DEF_SCANCHAINS]);
		break;
	    case DEF_CONSTRAINTS:
		LefSkipSection(f, sections[DEF_CONSTRAINTS]);
		break;
	    case DEF_GROUPS:
		LefSkipSection(f, sections[DEF_GROUPS]);
		break;
	    case DEF_EXTENSION:
		LefSkipSection(f, sections[DEF_EXTENSION]);
		break;
	    case DEF_END:
		if (!LefParseEndStatement(token, "DESIGN"))
		{
		    LefError(DEF_ERROR, "END statement out of context.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_END) break;
    }
    TxPrintf("DEF read: Processed %d lines.\n", lefCurrentLine);
    LefError(DEF_SUMMARY, NULL);    /* print statement of errors, if any, and reset */

    /* Cleanup */

    DBAdjustLabels(rootDef, &TiPlaneRect);
    DBReComputeBbox(rootDef);
    DBWAreaChanged(rootDef, &rootDef->cd_bbox, DBW_ALLWINDOWS,
		&DBAllButSpaceBits);
    DBCellSetModified(rootDef, TRUE);

    if (f != NULL) fclose(f);
    UndoEnable();
}
