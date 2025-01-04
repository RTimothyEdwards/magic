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
static const char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/lef/defRead.c,v 1.2 2008/06/01 18:37:43 tim Exp $";
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

/* C99 compat */
#include "textio/textio.h"
#include "commands/commands.h"

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
	DEF_SPECNET_WEIGHT, DEF_SPECNET_PROPERTY, DEF_SPECNET_ROUTED,
	DEF_SPECNET_FIXED, DEF_SPECNET_COVER, DEF_SPECNET_SHIELD };

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
DefAddRoutes(
    CellDef *rootDef,		/* Cell to paint */
    FILE *f,			/* Input file */
    float oscale,		/* Scale factor between LEF and magic units */
    bool special,		/* True if this section is SPECIALNETS */
    char *netname,		/* Name of the net, if net is to be labeled */
    LefRules *ruleset,		/* Non-default rule, or NULL */
    LefMapping *defLayerMap,	/* magic-to-lef layer mapping array */
    bool annotate)		/* If TRUE, do not generate any geometry */
{
    char *token;
    LinkedRect *routeList, *newRoute = NULL, *routeTop = NULL;
    Point refp;			/* reference point */
    bool valid = FALSE;		/* is there a valid reference point? */
    bool initial = TRUE;
    bool labeled = TRUE;
    bool iscontact = FALSE;
    Rect locarea, r;
    int extend, lextend, hextend;
    float x, y, z, w;
    int paintWidth, saveWidth, paintExtend;
    TileType routeLayer, paintLayer;
    HashEntry *he;
    lefLayer *lefl = NULL;
    lefRule *rule = NULL;
    int keyword;

    static const char * const specnet_keys[] = {
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
	"ROUTED",
	"FIXED",
	"COVER",
	"SHIELD",
	NULL
    };

    static const char * const specnet_shape_keys[] = {
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

	    /* assume this is not a via unless found otherwise */
	    iscontact = FALSE;

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
		paintExtend = 0;	/* SPECIALNETS always have 0 wire extension */
		saveWidth = paintWidth;
	    }
	    else
	    {
		if (ruleset)
		{
		    for (rule = ruleset->rule; rule; rule = rule->next)
			if (rule->lefInfo == lefl)
			    break;
		}
		else
		    rule = NULL;

		paintWidth = (rule) ? rule->width :
				(lefl) ? lefl->info.route.width :
				DEFAULT_WIDTH * DBLambda[1] / DBLambda[0];
		paintExtend = (rule) ? rule->width : paintWidth;
	    }
	}
	else if ((*token == '+') && (special == TRUE))
	{
	    int netstyle;

	    /* Check for SHAPE, STYLE, or USE keywords */
	    token = LefNextToken(f, TRUE);
	    keyword = LookupFull(token, specnet_keys);
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
		    keyword = LookupFull(token, specnet_shape_keys);
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

		case DEF_SPECNET_SHIELD:
		    /* Treat as the following case except to absorb the next token */
		    token = LefNextToken(f, TRUE);  /* Drop through */

		case DEF_SPECNET_ROUTED:
		case DEF_SPECNET_COVER:
		case DEF_SPECNET_FIXED:
		    initial = TRUE;
		    continue;
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
	    LefError(DEF_ERROR, "Route has %s entries, this is not handled!\n",
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
	else if (!strcmp(token, "TAPER"))
	{
	    /* Return to the default width for this layer */
	    paintWidth = (lefl) ? lefl->info.route.width :
				DEFAULT_WIDTH * DBLambda[1] / DBLambda[0];
	    paintExtend = (special) ? 0 : paintWidth;
	}
	else if (!strcmp(token, "TAPERRULE"))
	{
	    token = LefNextToken(f, TRUE);

	    he = HashLookOnly(&LefNonDefaultRules, token);
	    if (he != NULL)
	    {
	   	LefRules *tempruleset = (LefRules *)HashGetValue(he);
		for (rule = tempruleset->rule; rule; rule = rule->next)
		    if (rule->lefInfo == lefl)
			break;

		if (rule)
		{
		    paintWidth = rule->width;
		    paintExtend = rule->width;
		}
	    }
	    else if (!strcmp(token, "DEFAULT"))
	    {
	    	paintWidth = (lefl) ? lefl->info.route.width :
				DEFAULT_WIDTH * DBLambda[1] / DBLambda[0];
		paintExtend = (special) ? 0 : paintWidth;
	    }
	    else
	    	LefError(DEF_ERROR, "Unknown nondefault rule \"%s\"\n", token);
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

		    iscontact = TRUE;
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

		if (iscontact || (DBIsContact(paintLayer)))
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

				/* Get correct rule for nondefault rules */
				if (ruleset)
				{
				    for (rule = ruleset->rule; rule; rule = rule->next)
					if (rule->lefInfo == lefl)
					    break;
				}
				else
				    rule = NULL;

				if (special)
				    paintWidth = saveWidth;
				else
				    paintWidth = (rule) ? rule->width :
					(lefl) ? lefl->info.route.width :
					DEFAULT_WIDTH * DBLambda[1] / DBLambda[0];

				paintExtend = (special) ? 0 : paintWidth;
				break;
			    }
		    iscontact = FALSE;
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
		    if (newRoute)
		    {
			freeMagic(newRoute);
			newRoute = NULL;
		    }
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

	    extend = paintExtend;

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
	if (annotate == FALSE)
	{
	    DBPaint(rootDef, &routeTop->r_r, routeTop->r_type);

	    /* label */
	    if (labeled == FALSE)
	    {
		r.r_xbot = r.r_xtop = (routeTop->r_r.r_xbot + routeTop->r_r.r_xtop) / 2;
		r.r_ybot = r.r_ytop = (routeTop->r_r.r_ybot + routeTop->r_r.r_ytop) / 2;
		DBPutLabel(rootDef, &r, GEO_CENTER, netname, routeTop->r_type, 0, 0);
		labeled = TRUE;
	    }
	}
	else
	{
	    /* When annotating, make sure there is a valid layer under the	*/
	    /* label.  If not, then wait for the next bit of geometry.		*/

	    if (labeled == FALSE)
	    {
		Tile *tp;
		Plane *plane = rootDef->cd_planes[DBPlane(routeTop->r_type)];
		tp = plane->pl_hint;
		GOTOPOINT(tp, &routeTop->r_r.r_ll);
		if (TiGetType(tp) == routeTop->r_type)
		{
		    r.r_xbot = r.r_xtop =
				(routeTop->r_r.r_xbot + routeTop->r_r.r_xtop) / 2;
		    r.r_ybot = r.r_ytop =
				(routeTop->r_r.r_ybot + routeTop->r_r.r_ytop) / 2;
		    DBPutLabel(rootDef, &r, GEO_CENTER, netname, routeTop->r_type,
				0, 0);
		    labeled = TRUE;
		}
		if ((labeled == FALSE) && (routeTop->r_next == NULL))
		{
		    TxError("Warning:  Label \"%s\" did not land on any existing"
				" net geometry.\n", netname);
		    r.r_xbot = r.r_xtop =
				(routeTop->r_r.r_xbot + routeTop->r_r.r_xtop) / 2;
		    r.r_ybot = r.r_ytop =
				(routeTop->r_r.r_ybot + routeTop->r_r.r_ytop) / 2;
		    DBPutLabel(rootDef, &r, GEO_CENTER, netname, routeTop->r_type,
				0, 0);
		    labeled = TRUE;
		}
	    }
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
 * DefReadNonDefaultRules --
 *
 *	Read a NONDEFAULTRULES section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Adds information into the non-default rules hash table.
 *
 *------------------------------------------------------------
 */

enum def_nondef_keys {DEF_NONDEF_START = 0, DEF_NONDEF_END};
enum def_nondefprop_keys {
	DEF_NONDEFPROP_HARDSPACING = 0, DEF_NONDEFPROP_LAYER,
	DEF_NONDEFPROP_VIA, DEF_NONDEFPROP_VIARULE,
	DEF_NONDEFPROP_MINCUTS, DEF_NONDEFPROP_PROPERTY,
	DEF_NONDEFLAYER_WIDTH, DEF_NONDEFLAYER_DIAG,
	DEF_NONDEFLAYER_SPACE, DEF_NONDEFLAYER_EXT,
	DEF_NONDEFPROP_DONE};

void
DefReadNonDefaultRules(
    FILE *f,
    CellDef *rootDef,
    const char *sname,
    float oscale,
    int total)
{
    char *token;
    int keyword, subkey;
    int processed = 0;
    HashEntry *he;
    lefLayer *lefl;
    float fvalue;
    LefRules *ruleset = NULL;
    lefRule *rule = NULL;
    bool inlayer;

    static const char * const nondef_keys[] = {
	"-",
	"END",
	NULL
    };

    static const char * const nondef_property_keys[] = {
	"HARDSPACING",
	"LAYER",
	"VIA",
	"VIARULE",
	"MINCUTS",
	"PROPERTY",
	"WIDTH",
	"DIAGWIDTH",
	"SPACING",
	"WIREEXT",
	";",
	NULL
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = LookupFull(token, nondef_keys);
	if (keyword < 0)
	{
	    LefError(DEF_INFO, "Unknown keyword \"%s\" in NONDEFAULTRULES "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}

	switch (keyword)
	{
	    case DEF_NONDEF_START:

		/* Get non-default rule name */
		token = LefNextToken(f, TRUE);

		/* Create a hash entry for this nondefault rule */
		/* NOTE:  Needs to handle name collisions.	*/
		he = HashFind(&LefNonDefaultRules, token);
		ruleset = (LefRules *)mallocMagic(sizeof(LefRules));
		HashSetValue(he, ruleset);
		ruleset->name = StrDup((char **)NULL, token);
		ruleset->rule = NULL;
		processed++;

		/* Process all properties */
		while (token && (*token != ';'))
		{
		    if (*token != '+')
		    {
			token = LefNextToken(f, TRUE);
			if (!inlayer)
			    continue;
		    }
		    if (*token == '+')
		    {
			inlayer = FALSE;
			rule = NULL;
			token = LefNextToken(f, TRUE);
		    }

		    subkey = LookupFull(token, nondef_property_keys);
		    if (subkey < 0)
		    {
			LefError(DEF_INFO, "Unknown non-default rule property \"%s\" "
				"in NONDEFAULTRULE definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_NONDEFPROP_DONE:
			    break;
			case DEF_NONDEFPROP_HARDSPACING:
			    lefl = NULL;
			    /* Ignore this */
			    break;
			case DEF_NONDEFPROP_VIA:
			case DEF_NONDEFPROP_VIARULE:
			    lefl = NULL;
			    /* Fall through */
			case DEF_NONDEFPROP_LAYER:
			    token = LefNextToken(f, TRUE);
			    he = HashFind(&LefInfo, token);
			    lefl = (lefLayer *)HashGetValue(he);
			    if (ruleset)
			    {
				/* Chain new layer rule to linked list */
				rule = (lefRule *)mallocMagic(sizeof(lefRule));
			    	rule->lefInfo = lefl;
				rule->width = 0;
				rule->spacing = 0;
				rule->extend = 0;		/* unused */
				rule->next = ruleset->rule;
				ruleset->rule = rule;
			    }
			    else
				LefError(DEF_INFO, "No non-default rule name for \"%s\" "
					"in NONDEFAULTRULE definition!.\n", token);
			    if (subkey == DEF_NONDEFPROP_LAYER) inlayer = TRUE;
			    break;
			case DEF_NONDEFPROP_MINCUTS:
			case DEF_NONDEFPROP_PROPERTY:
			    lefl = NULL;
			    /* Ignore the next two tokens */
			    token = LefNextToken(f, TRUE);
			    token = LefNextToken(f, TRUE);
			    break;
			case DEF_NONDEFLAYER_WIDTH:
			    if (!inlayer)
				LefError(DEF_INFO, "WIDTH specified without layer.\n");
			    token = LefNextToken(f, TRUE);
			    sscanf(token, "%f", &fvalue);
			    if (rule == NULL)
				LefError(DEF_INFO, "No rule for non-default width.\n");
			    else if (lefl == NULL)
				LefError(DEF_INFO, "No layer for non-default width.\n");
			    else
			    	rule->width = (int)roundf(fvalue / oscale);
			    break;
			case DEF_NONDEFLAYER_SPACE:
			    if (!inlayer)
				LefError(DEF_INFO, "SPACING specified without layer.\n");
			    token = LefNextToken(f, TRUE);
			    sscanf(token, "%f", &fvalue);
			    if (rule == NULL)
				LefError(DEF_INFO, "No rule for non-default spacing.\n");
			    else if (lefl == NULL)
				LefError(DEF_INFO, "No layer for non-default spacing.\n");
			    else
			    	rule->spacing = (int)roundf(fvalue / oscale);
			    break;
			case DEF_NONDEFLAYER_EXT:
			    if (!inlayer)
				LefError(DEF_INFO, "WIREEXT specified without layer.\n");
			    token = LefNextToken(f, TRUE);
			    sscanf(token, "%f", &fvalue);
			    if (rule == NULL)
				LefError(DEF_INFO, "No rule for non-default extension.\n");
			    else if (lefl == NULL)
				LefError(DEF_INFO, "No layer for non-default extension.\n");
			    else
			    {
				LefError(DEF_WARNING, "Non-default extension at via not implemented.\n");
			    	rule->extend = (int)roundf((2 * fvalue) / oscale);
			    }
			    break;
			case DEF_NONDEFLAYER_DIAG:
			    if (!inlayer)
				LefError(DEF_INFO,
					"Layer value specified without layer.\n");
			    /* Absorb token and ignore */
			    token = LefNextToken(f, TRUE);
			    break;
		    }
		}
		inlayer = FALSE;
		break;

	    case DEF_NONDEF_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError(DEF_ERROR, "Non-default rule END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_NONDEF_END) break;
    }

    if (processed == total)
	TxPrintf("  Processed %d non-default rules total.\n", processed);
    else
	LefError(DEF_WARNING, "Number of non-default rules read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}

/*
 *------------------------------------------------------------
 *
 * defFoundOneFunc --
 *
 *	Simple callback function for DefReadNets() when using
 *	the "def read -annoatate" option.  Attempts to find
 *	paint in the top level in the area of a pin that is
 *	part of the net.
 *
 *	Note:  The routine does not search on connected tiles
 *	and so could miss the connecting material, although
 *	there are multiple fall-back chances to succeed.
 * 
 * Returns:
 *	1 to stop the search, as we just take the first tile
 *	found and run with it.
 *
 * Side effects:
 *	Copies a pointer to the tile found into the client
 *	data record.
 *
 *------------------------------------------------------------
 */

int
defFoundOneFunc(
    Tile *tile,
    Tile **tret)
{
    *tret = tile;
    return 1;
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
	DEF_NETPROP_USE = 0, DEF_NETPROP_ROUTED, DEF_NETPROP_NOSHIELD,
	DEF_NETPROP_FIXED, DEF_NETPROP_COVER, DEF_NETPROP_SOURCE,
	DEF_NETPROP_SHIELDNET, DEF_NETPROP_SUBNET, DEF_NETPROP_VPIN,
	DEF_NETPROP_XTALK, DEF_NETPROP_NONDEFRULE, DEF_NETPROP_FIXEDBUMP,
	DEF_NETPROP_FREQUENCY, DEF_NETPROP_ORIGINAL, DEF_NETPROP_PATTERN,
	DEF_NETPROP_ESTCAP, DEF_NETPROP_WEIGHT, DEF_NETPROP_PROPERTY
};

void
DefReadNets(
    FILE *f,
    CellDef *rootDef,
    const char *sname,
    float oscale,
    bool special,		/* True if this section is SPECIALNETS */
    bool dolabels,		/* If true, create a label for each net */
    bool annotate,		/* If true, create labels, not geometry */
    int total)
{
    char *token;
    char *netname = NULL, *prnet;
    int keyword, subkey;
    int processed = 0;
    LefMapping *defLayerMap;
    LefRules *ruleset = NULL;
    HashEntry *he;
    bool needanno;

    static const char * const net_keys[] = {
	"-",
	"END",
	NULL
    };

    static const char * const net_property_keys[] = {
	"USE",
	"ROUTED",
	"NOSHIELD",
	"FIXED",
	"COVER",
	"SOURCE",
	"SHIELDNET",
	"SUBNET",
	"VPIN",
	"XTALK",
	"NONDEFAULTRULE",
	"FIXEDBUMP",
	"FREQUENCY",
	"ORIGINAL",
	"PATTERN",
	"ESTCAP",
	"WEIGHT",
	"PROPERTY",
	NULL
    };

    defLayerMap = defMakeInverseLayerMap(LAYER_MAP_VIAS);

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = LookupFull(token, net_keys);
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
		needanno = annotate;

		/* Update the record of the number of nets processed	*/
		/* and spit out a message for every 5% finished.	*/

		LefEstimate(processed++, total,
			(special) ? "special nets" : "nets");

		/* Process all properties */
		while (token && (*token != ';'))
		{
		    if (needanno)
		    {
			char *compname, *termname;

			/* Annotation only---when back-annotating	*/
			/* labels into a layout, the safest place to	*/
			/* put labels is on a terminal position		*/

			if (*token == '(')
			{
			    token = LefNextToken(f, TRUE);
			    if (!strcmp(token, "PIN"))
				needanno = FALSE;
			    else
			    {
				Rect r;
				bool isvalid;
				TileType ttype;
				Tile *tp;

				compname = StrDup((char **)NULL, token);
			    	token = LefNextToken(f, TRUE);
				termname = (char *)mallocMagic(strlen(compname) +
					strlen(token) + 3);
				sprintf(termname, "%s/%s", compname, token);
				ttype = CmdFindNetProc(termname, EditCellUse, &r,
					FALSE, &isvalid);
				if (isvalid)
				{
				    /* The pin was found.  However, there may not be
				     * paint on the top level over the whole pin.
				     * Search the area (+1) for attached paint, then
				     * label inside that tile.
				     */
				    tp = NULL;
				    DBSrPaintArea((Tile *)NULL,
						rootDef->cd_planes[DBPlane(ttype)],
						&r, &DBConnectTbl[ttype],
						defFoundOneFunc, (ClientData)&tp);

				    if (tp != NULL)
				    {
					TiToRect(tp, &r);
					r.r_xbot = r.r_xtop = (r.r_xbot + r.r_xtop) / 2;
					r.r_ybot = r.r_ytop = (r.r_ybot + r.r_ytop) / 2;
					DBPutLabel(rootDef, &r, GEO_CENTER, netname,
						ttype, 0, 0);
					needanno = FALSE;
				    }
				}
				freeMagic(termname);
				freeMagic(compname);
			    }
			}
		    }

		    /* All connections are ignored, and we go		*/
		    /* go directly to the first property ("+") key	*/

		    if (*token != '+')
		    {
			token = LefNextToken(f, TRUE);
			continue;
		    }
		    else
			token = LefNextToken(f, TRUE);

		    subkey = LookupFull(token, net_property_keys);
		    if (subkey < 0)
		    {
			LefError(DEF_INFO, "Unknown net property \"%s\" in "
				"NET definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_NETPROP_ROUTED:
			case DEF_NETPROP_FIXED:
			case DEF_NETPROP_COVER:
			case DEF_NETPROP_NOSHIELD:
			    prnet = NULL;
			    if (dolabels && (needanno || (!annotate)))
				prnet = netname;
			    token = DefAddRoutes(rootDef, f, oscale, special,
					prnet, ruleset, defLayerMap, annotate);
			    ruleset = NULL;
			    break;

			case DEF_NETPROP_NONDEFRULE:
		    	    token = LefNextToken(f, TRUE);
			    /*
			     * Differs from "TAPERRULE" in that it specifies a non-default
			     * rule to use for the entire net.
			     */
			    he = HashLookOnly(&LefNonDefaultRules, token);
			    if (he != NULL)
			        ruleset = (LefRules *)HashGetValue(he);
			    else
			    	LefError(DEF_ERROR, "Unknown nondefault rule \"%s\"\n", token);
			    break;

			case DEF_NETPROP_PROPERTY:
			    /* Ignore except to absorb the next two tokens. */
			    token = LefNextToken(f, TRUE);  /* Drop through */

			case DEF_NETPROP_SOURCE:
			case DEF_NETPROP_USE:
			case DEF_NETPROP_SHIELDNET:
			case DEF_NETPROP_SUBNET:
			case DEF_NETPROP_XTALK:
			case DEF_NETPROP_FREQUENCY:
			case DEF_NETPROP_ORIGINAL:
			case DEF_NETPROP_PATTERN:
			case DEF_NETPROP_ESTCAP:
			case DEF_NETPROP_WEIGHT:
			    /* Ignore except to absorb the next token. */
			    token = LefNextToken(f, TRUE);  /* Drop through */

			case DEF_NETPROP_FIXEDBUMP:
			    /* Ignore this keyword */
			    break;

			case DEF_NETPROP_VPIN:
			    /* VPIN is an in-line pin not in the PINS section	*/
			    /* Need to handle this!				*/
			    token = LefNextToken(f, TRUE);
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
DefReadLocation(
    CellUse *use,
    FILE *f,
    float oscale,
    Transform *tptr,
    bool noplace)
{
    const Rect *r;
    Rect tr, rect;
    int keyword;
    char *token;
    float x, y;
    Transform t2;

    static const char * const orientations[] = {
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

        keyword = LookupFull(token, orientations);
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

	    propval = (char *)DBPropGet(use->cu_def, "FIXED_BBOX", &found);
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
DefReadPins(
    FILE *f,
    CellDef *rootDef,
    const char *sname,
    float oscale,
    int total,
    bool annotate)
{
    char *token;
    char pinname[LEF_LINE_MAX];
    int keyword, subkey, values, flags;
    int processed = 0;
    int pinDir = PORT_CLASS_DEFAULT;
    int pinUse = PORT_USE_DEFAULT;
    int pinNum = 0;
    int width, height, rot, size;
    TileType curlayer = -1;
    LinkedRect *rectList = NULL, *newRect;
    Rect *currect, topRect;
    Transform t;
    lefLayer *lefl;
    bool pending = FALSE;
    bool hasports = FALSE;

    static const char * const pin_keys[] = {
	"-",
	"END",
	NULL
    };

    static const char * const pin_property_keys[] = {
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

    static const char * const pin_classes[] = {
	"DEFAULT",
	"INPUT",
	"OUTPUT TRISTATE",
	"OUTPUT",
	"INOUT",
	"FEEDTHRU",
	NULL
    };

    static const char * const pin_uses[] = {
	"DEFAULT",
	"SIGNAL",
	"POWER",
	"GROUND",
	"CLOCK",
	"RESET",
	"ANALOG",
	"SCAN",
	"TIEOFF",
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

    static int lef_use_to_bitmask[] = {
	PORT_USE_DEFAULT,
	PORT_USE_SIGNAL,
	PORT_USE_POWER,
	PORT_USE_GROUND,
	PORT_USE_CLOCK,
	PORT_USE_RESET,
	PORT_USE_ANALOG,
	PORT_USE_SCAN,
	PORT_USE_TIEOFF
    };

    flags = 0;

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = LookupFull(token, pin_keys);

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
		    subkey = LookupFull(token, pin_property_keys);
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
			    token = LefNextToken(f, TRUE);
			    subkey = LookupFull(token, pin_uses);
			    if (subkey < 0)
				LefError(DEF_ERROR, "Unknown pin use \"%s\"\n", token);
			    else
				pinUse = lef_use_to_bitmask[subkey];
			    break;
			case DEF_PINS_PROP_NET:
			    /* Get the net name, but ignore it */
			    token = LefNextToken(f, TRUE);
			    break;
			case DEF_PINS_PROP_DIR:
			    token = LefNextToken(f, TRUE);
			    subkey = LookupFull(token, pin_classes);
			    if (subkey < 0)
				LefError(DEF_ERROR, "Unknown pin class \"%s\"\n", token);
			    else
				pinDir = lef_class_to_bitmask[subkey];
			    break;
			case DEF_PINS_PROP_LAYER:
			    curlayer = LefReadLayer(f, FALSE);
			    currect = LefReadRect(f, curlayer, oscale);

			    newRect = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
			    newRect->r_type = curlayer;
			    newRect->r_r = *currect;
			    newRect->r_next = rectList;
			    rectList = newRect;

			    if (pending)
			    {
				/* If layer was unknown, set to space and force	*/
				/* non-sticky.					*/
				flags = PORT_DIR_MASK;
				if (curlayer < 0)
				    curlayer = TT_SPACE;
				else
				    flags |= LABEL_STICKY;

				while (rectList != NULL)
				{
				    GeoTransRect(&t, &rectList->r_r, &topRect);
				    DBPaint(rootDef, &topRect, rectList->r_type);
				    // DBPutLabel(rootDef, &topRect, -1, pinname,
				    //		rectList->r_type,
				    //		pinDir | pinUse | flags, pinNum);
				    width = (topRect.r_xtop - topRect.r_xbot);
				    height = (topRect.r_ytop - topRect.r_ybot);
				    rot = 0;
				    if (height > (width * 2))
				    {
					int temp = height;
					height = width;
					width = temp;
					rot = 90;
				    }
				    size = DRCGetDefaultLayerWidth(rectList->r_type);
				    while ((size << 1) < height) size <<= 1;
				    size <<= 3;		/* Fonts are in 8x units */
				    DBPutFontLabel(rootDef, &topRect,
						0, size, rot, &GeoOrigin,
						GEO_CENTER, pinname,
				    		rectList->r_type,
				    		pinDir | pinUse | flags, pinNum);
				    freeMagic(rectList);
				    rectList = rectList->r_next;
				}
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

				while (rectList != NULL)
				{
				    GeoTransRect(&t, &rectList->r_r, &topRect);
				    DBPaint(rootDef, &topRect, rectList->r_type);
				    // DBPutLabel(rootDef, &topRect, -1, pinname,
				    //		rectList->r_type,
				    //		pinDir | pinUse | flags, pinNum);
				    width = (topRect.r_xtop - topRect.r_xbot);
				    height = (topRect.r_ytop - topRect.r_ybot);
				    rot = 0;
				    if (height > (width * 2))
				    {
					int temp = height;
					height = width;
					width = temp;
					rot = 90;
				    }
				    size = DRCGetDefaultLayerWidth(rectList->r_type);
				    while ((size << 1) < height) size <<= 1;
				    size <<= 3;		/* Fonts are in 8x units */

				    /* If DEF file is being imported to annotate a
				     * layout, then remove any existing label in
				     * the layout that matches the PIN record.
				     */
				    if (annotate)
					DBEraseLabelsByContent(rootDef, &topRect,
								-1, pinname);

				    DBPutFontLabel(rootDef, &topRect,
						0, size, rot, &GeoOrigin,
						GEO_CENTER, pinname,
				    		rectList->r_type,
				    		pinDir | pinUse | flags, pinNum);
				    freeMagic(rectList);
				    rectList = rectList->r_next;
				}
				pending = FALSE;
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
 * DefReadBlockages --
 *
 *	Read a BLOCKAGES section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Generates layout
 *
 *------------------------------------------------------------
 */

enum def_block_keys {DEF_BLOCK_START = 0, DEF_BLOCK_END};
enum def_block_prop_keys {
	DEF_BLOCK_PROP_RECT = 0, DEF_BLOCK_PROP_LAYER};

void
DefReadBlockages(
    FILE *f,
    CellDef *rootDef,
    const char *sname,
    float oscale,
    int total)
{
    char *token;
    int keyword, subkey, values;
    int processed = 0;
    TileType curlayer;
    Rect *currect;
    lefLayer *lefl;
    HashEntry *he;

    static const char * const block_keys[] = {
	"-",
	"END",
	NULL
    };

    static const char * const block_property_keys[] = {
	"RECT",
	"LAYER",
	NULL
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = LookupFull(token, block_keys);

	if (keyword < 0)
	{
	    LefError(DEF_INFO, "Unknown keyword \"%s\" in BLOCKAGES "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_BLOCK_START:		/* "-" keyword */

		/* Update the record of the number of blockages		*/
		/* processed and spit out a message for every 5% done.	*/

		LefEstimate(processed++, total, "blockages");

		while ((token = LefNextToken(f, TRUE)) != NULL)
		{
		    if (*token == ';')
			break;

		    subkey = LookupFull(token, block_property_keys);
		    if (subkey < 0)
		    {
			LefError(DEF_INFO, "Unknown blockage property \"%s\" in "
				"BLOCKAGES definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_BLOCK_PROP_LAYER:
			    curlayer = LefReadLayer(f, TRUE);
			    break;

			case DEF_BLOCK_PROP_RECT:
			    currect = LefReadRect(f, curlayer, oscale);
			    DBPaint(rootDef, currect, curlayer);
			    break;
		    }
		}
		break;

	    case DEF_BLOCK_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError(DEF_ERROR, "Blockage END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_BLOCK_END) break;
    }

    if (processed == total)
	TxPrintf("  Processed %d blockage%s.\n", processed,
		((processed > 1) ? "s" : ""));
    else
	LefError(DEF_WARNING, "Number of blockages read (%d) does not match "
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
DefReadVias(
    FILE *f,
    const char *sname,
    float oscale,
    int total)
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

    static const char * const via_keys[] = {
	"-",
	"END",
	NULL
    };

    static const char * const via_property_keys[] = {
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
	keyword = LookupFull(token, via_keys);

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
		    subkey = LookupFull(token, via_property_keys);
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
DefReadComponents(
    FILE *f,
    CellDef *rootDef,
    const char *sname,
    float oscale,
    int total)
{
    CellDef *defMacro;
    CellUse *defUse;
    Transform t;
    char *token, *dptr;
    char usename[512];
    int keyword, subkey, values;
    int processed = 0;

    static const char * const component_keys[] = {
	"-",
	"END",
	NULL
    };

    static const char * const property_keys[] = {
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
	keyword = LookupFull(token, component_keys);

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

		/* Magic prohibits slashes and commas in use names	*/
		/* when using the "identify" command.  Removing these	*/
		/* restrictions (at least the slash) is quite complex,	*/
		/* but really should be taken care of, since no other	*/
		/* tools consider this an illegal use, that I'm aware	*/
		/* of.							*/

		for (dptr = usename; *dptr; dptr++)
		    if ((*dptr == '/') || (*dptr == ','))
		    {
			LefError(DEF_WARNING, "Character in instance name "
				"converted to underscore.\n");
			*dptr = '_';
		    }

		token = LefNextToken(f, TRUE);

		/* Find the corresponding macro definition */
		defUse = NULL;
		defMacro = DBCellLookDef(token);

		if (defMacro == (CellDef *)NULL)
		{
		    /* Before giving up, assume that this cell has a	*/
		    /* magic .mag layout file.				*/
		    defMacro = DBCellNewDef(token);
		    defMacro->cd_flags &= ~CDNOTFOUND;

		    if (!DBCellRead(defMacro, TRUE, TRUE, NULL))
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
		    subkey = LookupFull(token, property_keys);
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
	DEF_CONSTRAINTS, DEF_GROUPS, DEF_EXTENSION, DEF_BLOCKAGES,
	DEF_NONDEFAULTRULES, DEF_END};

void
DefRead(
    const char *inName,
    bool dolabels,
    bool annotate,
    bool noblockage)
{
    CellDef *rootDef;
    FILE *f;
    char *filename;
    char *token;
    char *bboxstr;
    int keyword, dscale, total;
    float oscale;
    Rect *dierect;

    static const char * const sections[] = {
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
	"BLOCKAGES",
	"NONDEFAULTRULES",
	"END",
	NULL
    };

    /* "annotate" implies "dolabels" whether set or not */
    if (annotate) dolabels = TRUE;

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
	keyword = LookupFull(token, sections);
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
		dierect = LefReadRect(f, 0, oscale);
		bboxstr = mallocMagic(40);
		sprintf(bboxstr, "%d %d %d %d",
			dierect->r_xbot,
			dierect->r_ybot,
			dierect->r_xtop,
			dierect->r_ytop);
		DBPropPut(rootDef, "FIXED_BBOX", bboxstr);
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
		if (annotate)
		    LefSkipSection(f, sections[DEF_COMPONENTS]);
		else
		    DefReadComponents(f, rootDef, sections[DEF_COMPONENTS],
				oscale, total);
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
		DefReadPins(f, rootDef, sections[DEF_PINS], oscale, total, annotate);
		break;
	    case DEF_PINPROPERTIES:
		LefSkipSection(f, sections[DEF_PINPROPERTIES]);
		break;
	    case DEF_SPECIALNETS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadNets(f, rootDef, sections[DEF_SPECIALNETS], oscale, TRUE,
			dolabels, annotate, total);
		break;
	    case DEF_NETS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadNets(f, rootDef, sections[DEF_NETS], oscale, FALSE,
			dolabels, annotate, total);
		break;
	    case DEF_NONDEFAULTRULES:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadNonDefaultRules(f, rootDef, sections[DEF_NONDEFAULTRULES],
			oscale, total);
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
	    case DEF_BLOCKAGES:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		if (annotate || noblockage)
		    LefSkipSection(f, sections[DEF_BLOCKAGES]);
		else
		    DefReadBlockages(f, rootDef, sections[DEF_BLOCKAGES],
				oscale, total);
		break;
	    case DEF_END:
		if (!LefParseEndStatement(f, "DESIGN"))
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
