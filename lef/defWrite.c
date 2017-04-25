/*
 * defWrite.c --      
 *
 * This module incorporates the LEF/DEF format for standard-cell place and
 * route.
 *
 *
 * Version 0.1 (June 9, 2004):  DEF output for layouts, to include netlist
 * from the extracted layout.
 *
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/lef/defWrite.c,v 1.2 2008/02/10 19:30:21 tim Exp $";            
#endif  /* not lint */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/tech.h"
#include "utils/malloc.h"
#include "utils/undo.h"
#include "cif/cif.h"
#include "extflat/extflat.h"
#include "lef/lefInt.h"
#include "drc/drc.h"		/* for querying width,spacing rules */

/*----------------------------------------------------------------------*/
/* Structures used by various routines					*/
/*----------------------------------------------------------------------*/

typedef struct {
   float	scale;
   FILE		*f;
   CellDef	*def;

   Tile		*tile;		/* Values of the last calculated route */
   TileType	type;
   float	x, y, extlen;
   unsigned char orient;

   LefMapping	*MagicToLefTbl;
   int		outcolumn;	/* Current column of output in file */
   unsigned char specialmode;	/* What nets to write as SPECIALNETS */
} DefData;

typedef struct {
   float scale;
   int total;
   int plane;
   TileTypeBitMask *mask;
   LefMapping *MagicToLefTbl;
} CViaData;

/*----------------------------------------------------------------------*/

char *defGetType();		/* Forward declaration */

/*----------------------------------------------------------------------*/


/*
 * ----------------------------------------------------------------------------
 *
 * defWriteHeader --
 *
 * This routine generates DEF header output for a cell or cell hierarchy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes output to the open file "f".
 *
 * ----------------------------------------------------------------------------
 */

void
defWriteHeader(def, f, oscale)
    CellDef *def;	/* Def for which to generate DEF output */
    FILE *f;		/* Output to this file */
    float oscale;
{
    TileType type;

    TxPrintf("Diagnostic:  Write DEF header for cell %s\n", def->cd_name);

    /* NOTE:  This routine corresponds to Envisia LEF/DEF Language	*/
    /* Reference version 5.3 (May 31, 2000)				*/

    fprintf(f, "VERSION 5.3 ;\n");
    fprintf(f, "   NAMESCASESENSITIVE ON ;\n");
    fprintf(f, "   DIVIDERCHAR \"/\" ;\n");

    /* Declare that buses are denoted with parentheses, since magic	*/
    /* uses brackets for arrays and instances.				*/
    fprintf(f, "   BUSBITCHARS \"()\" ;\n");

    /* Design name, taken from the cell def name */
    fprintf(f, "   DESIGN %s ;\n", def->cd_name);

    /* Technology name, taken from the magic tech file.			*/
    /* (which may not be a good idea. . .  may need a tech definition	*/
    /* in the tech file "lef" section to specifically name the LEF/DEF	*/
    /* technology).							*/
    fprintf(f, "   TECHNOLOGY %s ;\n", DBTechName);

    /* As I understand it, this refers to the scalefactor of the GDS	*/
    /* file output.  Magic does all GDS in nanometers, so the LEF	*/
    /* scalefactor (conversion to microns) is always 1000.		*/
    fprintf(f, "   UNITS DISTANCE MICRONS 1000 ;\n");

    /* Die area, taken from the cell def bounding box.			*/
    fprintf(f, "   DIEAREA ( %.10g %.10g ) ( %.10g %.10g ) ;\n", 
	(float)def->cd_bbox.r_xbot * oscale,
	(float)def->cd_bbox.r_ybot * oscale,
	(float)def->cd_bbox.r_xtop * oscale,
	(float)def->cd_bbox.r_ytop * oscale);

    fprintf(f, "\n");
}

/*
 *------------------------------------------------------------
 *
 * defTransPos --
 *
 *	Determine the DEF orientation of a specific magic
 *	transformation matrix.
 *
 * Results:
 *	The position, in DEF string format ("N" for north, etc.)
 *	This is a static string
 *
 * Side Effects:
 *	None.
 *
 *------------------------------------------------------------
 */

char *
defTransPos(Transform *t)
{
    static char *def_orient[] = {
	"N", "S", "E", "W", "FN", "FS", "FE", "FW"
    }; 

    bool ew;  /* east-or-west identifier */
    bool sw;  /* south-or-west identifier */
    bool flip;
    int pos = 0;

    ew = ((t->t_a == 0) && (t->t_e == 0)) ? TRUE : FALSE;
    if (ew)
    {
       flip = ((t->t_b * t->t_d) > 0) ? TRUE : FALSE;
       sw = (t->t_d > 0) ? TRUE : FALSE;
    }
    else
    {
       flip = ((t->t_a * t->t_e) < 0) ? TRUE : FALSE;
       sw = (t->t_e > 0) ? FALSE : TRUE;
    }

    if (flip) pos += 4;
    if (ew) pos += 2;
    if (sw) pos += 1;
    
    return def_orient[pos]; 
}

/*
 *------------------------------------------------------------
 *
 * defCountNets --
 *
 *	First-pass function to count the number of different
 *	nets used.  If "allSpecial" is TRUE, consider all
 *	geometry to be SPECIALNETS.
 *
 * Results:
 *	A NetCount structure holding the regular and special
 *	net totals upon completion.
 *
 * Side Effects:
 *	None.
 *
 *------------------------------------------------------------
 */

NetCount
defCountNets(rootDef, allSpecial)
    CellDef *rootDef;
    bool allSpecial;
{
    NetCount total;
    int defnodeCount();

    total.regular = (allSpecial) ? -1 : 0;
    total.special = 0;
    total.has_nets = TRUE;

    TxPrintf("Diagnostic:  Finding all nets in cell %s\n", rootDef->cd_name);
    TxPrintf("(This can take a while!)\n");

    /* Read in the extracted file */
    EFInit();

    /* There are no arguments for extflat, but we need to call the	*/
    /* routine to initialize a few things such as the search path.	*/
    EFArgs(0, NULL, NULL, NULL, NULL);

    EFScale = 0.0;	/* Allows EFScale to be set to the scale value	*/

    if (EFReadFile(rootDef->cd_name, TRUE, FALSE, TRUE))
    {
	EFFlatBuild(rootDef->cd_name, EF_FLATNODES | EF_NOFLATSUBCKT);
        EFVisitNodes(defnodeCount, (ClientData)&total);
    }
    else
    {
	TxError("Warning:  Circuit has no .ext file;  no nets written.\n");
	TxError("Run extract on this circuit if you want nets in the output.\n");
	EFDone();
	total.has_nets = FALSE;
    }
    
    if (allSpecial) total.regular = 0;
    return total;
}

/* Callback function used by defCountNets */

int
defnodeCount(node, res, cap, total)
    EFNode *node;
    int res;			/* not used */
    EFCapValue cap;		/* not used */
    NetCount *total;
{
    HierName *hierName;
    char ndn[256];
    char *cp, clast;

    /* Ignore power and ground lines, which we will treat	*/
    /* as SPECIALNETS types.					*/

    hierName = (HierName *) node->efnode_name->efnn_hier;

    if (!(hierName->hn_parent))  /* Extra processing of top-level nodes */
    {
	char *pwr;
	cp = hierName->hn_name;
	clast = *(cp + strlen(cp) - 1);
	
	/* Global nodes are marked as "special nets" */
	if (clast == '!')
	    node->efnode_flags |= EF_SPECIAL;

#ifdef MAGIC_WRAPPER
	/* Check if name is defined in array "globals" */
	pwr = (char *)Tcl_GetVar2(magicinterp, "globals", cp, TCL_GLOBAL_ONLY);
	if (pwr)
	{
	    /* Diagnostic */
	    TxPrintf("Node %s is defined in the \"globals\" array\n");
	    node->efnode_flags |= EF_SPECIAL;
	}

	/* Check against Tcl variables $VDD and $GND */
	pwr = (char *)Tcl_GetVar(magicinterp, "VDD", TCL_GLOBAL_ONLY);
	if (pwr && (!strcmp(cp, pwr)))
	{
	    /* Diagnostic */
	    TxPrintf("Node %s matches VDD variable definition!\n");
	    node->efnode_flags |= EF_SPECIAL;
	}

	pwr = (char *)Tcl_GetVar(magicinterp, "GND", TCL_GLOBAL_ONLY);
	if (pwr && (!strcmp(cp, pwr)))
	{
	    /* Diagnostic */
	    TxPrintf("Node %s matches GND variable definition!\n");
	    node->efnode_flags |= EF_SPECIAL;
	}

	/* If a node has not been marked as SPECIAL, does not connect	*/
	/* to a port, and does not have an internally-generated	name,	*/
	/* then mark it as "special".					*/
	if (!(node->efnode_flags & (EF_SPECIAL | EF_PORT)) &&
		(clast != '#'))
	    node->efnode_flags |= EF_SPECIAL;
#endif
    }

    if (total->regular < 0)
    {
	/* "allspecial" options:  all nets written as SPECIALNETS */

	if ((node->efnode_flags & EF_SPECIAL) || (node->efnode_flags & EF_PORT))
	    total->special++;
    }
    else
    {
	/* We only count nodes having a port connection as "regular" nets */

	if (node->efnode_flags & EF_SPECIAL)
	    total->special++;
	else if (node->efnode_flags & EF_PORT)
	    total->regular++;
    }
    
    return 0;	/* Keep going. . . */
}

/*
 * ----------------------------------------------------------------------------
 *
 * defHNsprintf --
 *
 * Create a hierarchical node name for the DEF output file..
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the area pointed to by str
 *
 * ----------------------------------------------------------------------------
 */

void 
defHNsprintf(str, hierName, divchar)
    char *str;
    HierName *hierName;
    char divchar;
{
    bool trimGlob, trimLocal;
    char *s, *cp, c;
    char *defHNsprintfPrefix();

    s = str;
    if (hierName->hn_parent) str = defHNsprintfPrefix(hierName->hn_parent, str,
		divchar);

    /* Make the name conform to valid LEF/DEF syntax.  This means	*/
    /* no pound signs or semicolons (which are illegal characters,	*/
    /* along with space and newline which won't be found in the 	*/
    /* magic name anyway), or dashes, asterisks, or percent signs	*/
    /* (which are interpreted as wildcard characters by LEF/DEF).	*/

    cp = hierName->hn_name; 
    while (c = *cp++)
    {
	switch (c)
	{
	    case '#':		/* Ignore---this is the final character */
				/* in internally-generated node names.	*/
		break;
	    case ';':
	    case '-':
	    case '*':
	    case '%':
		*str++ = '_';
		break;
	    default:
		*str++ = c;
		break;
	}
    }
    *str++ = '\0';
}

char *defHNsprintfPrefix(hierName, str, divchar)
    HierName *hierName;
    char *str;
    char divchar;
{
    char *cp, c;

    if (hierName->hn_parent)
	str = defHNsprintfPrefix(hierName->hn_parent, str);

    cp = hierName->hn_name;
    while (*str++ = *cp++) ;
    *(--str) = divchar;
    return ++str;
}


/*
 *------------------------------------------------------------
 *
 * nodeDefName ---
 *
 *	Determine the node name to write to the DEF file
 *	for the given hierachical name structure from
 *	extflat.
 *
 *------------------------------------------------------------
 */

char *
nodeDefName(hname)
    HierName *hname;
{
    EFNodeName *nn;
    HashEntry *he;
    EFNode *node;
    static char nodeName[256]; 

    he = EFHNLook(hname, (char *) NULL, "nodeName");
    if (he == NULL)
        return "errorNode";
    nn = (EFNodeName *) HashGetValue(he);
    node = nn->efnn_node;

    defHNsprintf(nodeName, node->efnode_name->efnn_hier, '/');
    return nodeName;
}

/*
 *------------------------------------------------------------
 *
 * defCheckForBreak --
 *
 *	Add the number "addlen" to the column value of
 *	the output.  If the DEF file output has reached or
 *	exceeds this value, write a newline character to
 *	the output and reset the column count.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output to DEF file; resets defdata->outcolumn
 *
 *------------------------------------------------------------
 */ 

#define MAX_DEF_COLUMNS 70

void
defCheckForBreak(addlen, defdata)
    int addlen;
    DefData *defdata;
{
    defdata->outcolumn += addlen;
    if (defdata->outcolumn > MAX_DEF_COLUMNS)
    {
	fprintf(defdata->f, "\n      ");
	defdata->outcolumn = 6 + addlen;
    }
}

/*
 *------------------------------------------------------------
 *
 * defWriteRouteWidth ---
 *
 *	Write the width of a SPECIALNET route to the output.
 *
 *------------------------------------------------------------
 */

void
defWriteRouteWidth(defdata, width)
    DefData *defdata;
    int width;
{
    float oscale = defdata->scale;
    char numstr[12];
    sprintf(numstr, "%.10g", ((float)width * defdata->scale));
    defCheckForBreak(strlen(numstr) + 1, defdata);
    fprintf(defdata->f, "%s ", numstr);
}

/*
 *------------------------------------------------------------
 *
 * defWriteCoord --
 *
 *	Output a coordinate pair in DEF syntax.  We supply the
 *	point to be written AND the previously written point
 *	so we can make use of the "*" notation in the DEF point
 *	format.  If the point to be written is not an extension
 *	of the previous point, "prevpt" should be NULL.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Output written to the DEF file.
 *
 *------------------------------------------------------------
 */

void
defWriteCoord(defdata, x, y, orient)
    DefData *defdata;
    float x, y;
    unsigned char orient;
{
    FILE *f = defdata->f;
    char numstr[12];
    int ctot = 4;

    /* The "12" here is just a fudge factor;  it is not crucial */
    /* to limit the output to exactly MAX_DEF_COLUMNS, and it 	*/
    /* is easier to assume that the output of a coordinate	*/
    /* pair is about 12 characters average rather than try to	*/
    /* predetermine what the actual output length will be.	*/

    if ((defdata->outcolumn + 12) > MAX_DEF_COLUMNS)
    {
	fprintf(f, "\n      ");
	defdata->outcolumn = 6;
    }

    fprintf(f, " ( ");
    if ((orient == GEO_NORTH) || (orient == GEO_SOUTH))
    {
	fprintf(f, "* ");
	ctot += 2;
    }
    else
    {
	sprintf(numstr, "%.10g", x);
	fprintf(f, "%s ", numstr);
	ctot += strlen(numstr) + 1;
    }

    if ((orient == GEO_EAST) || (orient == GEO_WEST))
    {
	fprintf(f, "* ");
	ctot += 2;
    }
    else
    {
	sprintf(numstr, "%.10g", y);
	fprintf(f, "%s ", numstr);
	ctot += strlen(numstr) + 1;
    }

    fprintf(f, ")");
    defdata->outcolumn += ctot;
}

/*
 *------------------------------------------------------------
 *
 * defWriteNets --
 *
 *	Output the NETS section of a DEF file.  We make use of
 *	the connectivity search routines used by "getnode" to
 *	determine unique notes and assign a net name to each.
 *	Then, we generate the geometry output for each NET
 *	entry.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Output written to the DEF output file.
 *
 *------------------------------------------------------------
 */

void
defWriteNets(f, rootDef, oscale, MagicToLefTable, specialmode)
    FILE *f;				/* File to write to */
    CellDef *rootDef;			/* Cell definition to use */
    float oscale;			/* Output scale factor */
    LefMapping *MagicToLefTable;	/* Magic to LEF layer mapping */
    unsigned char specialmode;		/* What to write as a SPECIALNET */
{
    DefData defdata;
    int defnodeVisit();

    defdata.f = f;
    defdata.scale = oscale;
    defdata.def = rootDef;
    defdata.MagicToLefTbl = MagicToLefTable;
    defdata.outcolumn = 0;
    defdata.specialmode = specialmode;

    EFVisitNodes(defnodeVisit, (ClientData)&defdata);
}

int
defnodeVisit(node, res, cap, defdata)
    EFNode *node;
    int res;
    EFCapValue cap;
    DefData *defdata;
{
    HierName *hierName;
    char *ndn;
    char ndn2[256];
    FILE *f = defdata->f;
    CellDef *def = defdata->def;
    float oscale = defdata->scale;
    TileTypeBitMask tmask, *rmask;
    TileType magictype;
    EFNodeName *thisnn;
    int defNetGeometryFunc();		/* Forward declaration */

    /* For regular nets, only count those nodes having port	*/
    /* connections.  For special nets, only count those nodes	*/
    /* that were marked with the EF_SPECIAL flag while counting	*/
    /* nets.							*/

    if (defdata->specialmode == DO_REGULAR)
    {
	if (!(node->efnode_flags & EF_PORT))
	    return 0;
    }
    else if (defdata->specialmode == DO_SPECIAL)
    {
	if (!(node->efnode_flags & EF_SPECIAL))
	    return 0;
    }
    else /* ALL_SPECIAL */
    {
	if (!(node->efnode_flags & EF_PORT) &&
		!(node->efnode_flags & EF_SPECIAL))
	    return 0;
    }

    hierName = (HierName *) node->efnode_name->efnn_hier;
    ndn = nodeDefName(hierName);
    defHNsprintf(ndn2, node->efnode_name->efnn_hier, '/');
    if (strcmp(ndn, ndn2))
    {
	TxError("Node mismatch: %s vs. %s\n", ndn, ndn2);
    }

    fprintf(f, "   - %s", ndn);
    defdata->outcolumn = 5 + strlen(ndn);

    /* Find all the node names that are port connections. 	*/
    /* For now, we will just use anything connecting one level	*/
    /* down in the hierarchy.  This is not definitive, however,	*/
    /* and we should confirm that the connection is an actual	*/
    /* port.							*/

    for (thisnn = node->efnode_name; thisnn != NULL; thisnn = thisnn->efnn_next)
    {
	char locndn[256];
	hierName = thisnn->efnn_hier;
	if (hierName->hn_parent && !hierName->hn_parent->hn_parent)
	{
	    /* This is just another kludgy check for a non-port and	*/
	    /* will eventually be removed.				*/
	    char endc = *(hierName->hn_name + strlen(hierName->hn_name) - 1);
	    if (endc != '#')
	    {
		defHNsprintf(locndn, thisnn->efnn_hier, ' ');
		defCheckForBreak(5 + strlen(locndn), defdata);
		fprintf(f, " ( %s )", locndn);
	    }
	}
    }

    /* TT_SPACE indicates that a layer name must be the next	*/
    /* thing to be written to the DEF file.			*/
    defdata->type = TT_SPACE;
    defdata->tile = (Tile *)NULL;

    /* Net geometry (this should be an option!)---	*/
    /* Use the DBconnect routines to find all geometry	*/
    /* connected to a specific node.  This is a		*/
    /* redundant search---we've already done this once	*/
    /* when extracting the circuit.  But, because the	*/
    /* DEF file requires a count of nodes up front, we	*/
    /* would have to do it twice anyway.  In this case,	*/
    /* we only do it once here, since the results of	*/
    /* the first pass are picked up from the .ext file.	*/

    magictype = DBTechNameType(EFLayerNames[node->efnode_type]);

    /* Note that the type of the node might be defined by the type */
    /* in the subcircuit itself, so we need to search for any type */
    /* that might validly connect to it, not just the type itself. */
    /* TTMaskSetOnlyType(&tmask, magictype); */
    TTMaskZero(&tmask);
    TTMaskSetMask(&tmask, &DBConnectTbl[magictype]);

    DBSrConnect(def, &node->efnode_loc, &tmask, DBConnectTbl,
		&TiPlaneRect, defNetGeometryFunc,
		(ClientData)defdata);

    if (defdata->tile == (Tile *)NULL)
    {
	/* No route layer?  It's possible that something connects to	*/
	/* the port location but doesn't overlap.  Try painting the	*/
	/* node type in def and trying again.				*/

	Rect rport;
	SearchContext scx;
	int defPortTileFunc();	/* Fwd declaration */

	scx.scx_area = node->efnode_loc;
	scx.scx_use = def->cd_parents;
	scx.scx_trans = GeoIdentityTransform;
	DBTreeSrUniqueTiles(&scx, &tmask, 0, defPortTileFunc, (ClientData)&rport);

	/* Add the residue types to any contact type */
	if (DBIsContact(magictype))
	{
	    rmask = DBResidueMask(magictype);
	    TTMaskSetMask(&tmask, rmask);
	    TTMaskSetType(&tmask, magictype);
	}
	/* Expand the rectangle around the port to overlap any	*/
	/* connecting material.					*/
	rport.r_xbot--;
	rport.r_ybot--;
	rport.r_xtop++;
	rport.r_ytop++;
  	DBSrConnect(def, &rport, &tmask, DBConnectTbl, &TiPlaneRect,
			defNetGeometryFunc, (ClientData)defdata);
    }

    /* Was there a last record pending?  If so, write it. */
    if (defdata->tile != (Tile *)NULL)
    {
	if (defdata->orient != GEO_CENTER)
	    defWriteCoord(defdata, defdata->x, defdata->y, defdata->orient);

	defdata->outcolumn = 0;
    }
    fprintf(f, " ;\n");

    return 0; 	/* Keep going */
}

/* Callback function for DBTreeSrUniqueTiles.  When no routed areas	*/
/* were found, we assume that there was no routing material overlapping	*/
/* the port.  So, we need to find the area of a tile defining the port	*/
/* so we can look for attaching material.				*/

int
defPortTileFunc(tile, cx)
    Tile *tile;
    TreeContext *cx;
{
    SearchContext *scx = cx->tc_scx;
    Rect *rport = (Rect *)cx->tc_filter->tf_arg;
    Rect r;

    TiToRect(tile, &r);
    GeoTransRect(&scx->scx_trans, &r, rport);

    /* Diagnostic */
    /*
    TxPrintf("Port tile at (%d %d) to (%d %d)\n",
		rport->r_xbot, rport->r_ybot,
		rport->r_xtop, rport->r_ytop);
    */
    
    return 1;	/* No need to check further */
}

/* Callback function for writing geometry of the network to	*/
/* the DEF file.						*/

int
defNetGeometryFunc(tile, plane, defdata)
    Tile *tile;			/* Tile being visited */
    int plane;			/* Plane of the tile being visited */
    DefData *defdata;		/* Data passed to this function */
{
    FILE *f = defdata->f;
    CellDef *def = defdata->def;
    float oscale = defdata->scale;
    TileTypeBitMask *rMask, *r2Mask;
    TileType rtype, r2type, ttype = TiGetType(tile);
    Rect r;
    unsigned char orient;
    bool sameroute = FALSE;
    int routeWidth, w, h, midlinex2;
    float x1, y1, x2, y2, extlen;
    lefLayer *lefType;
    char *lefName, viaName[24];
    LefMapping *MagicToLefTable = defdata->MagicToLefTbl;

    TiToRect(tile, &r);

    /* Treat contacts here exactly the same way as defCountVias	*/

    if (DBIsContact(ttype))
    {
	Rect r2;
	Tile *tp;

	rMask = NULL;
	if (ttype >= DBNumUserLayers)
	{
	    /* Stacked contact types need to be broken into their	*/
	    /* constituent types.  Process only if we are on the home	*/
	    /* plane of one of the constituent types.			*/

	    rMask = DBResidueMask(ttype);
	    for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
		if (TTMaskHasType(rMask, rtype))
		    if (DBPlane(rtype) == plane)
		    {
			ttype = rtype;
			break;
		    }
	    if (rtype == DBNumUserLayers)
		return 0;

	}
	else
	    if (DBPlane(ttype) != plane)
		return 0;

	/* Boundary search on stacked contact types to include any	*/
	/* tile areas belonging to ttype.				*/

	for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp)) /* Top */
	{
	    r2type = TiGetBottomType(tp);
	    if (r2type == ttype)
	    {
		if (!rMask) return 0;
		TiToRect(tp, &r2);
		GeoInclude(&r2, &r);
	    }
	    else if (r2type >= DBNumUserLayers)
	    {
		r2Mask = DBResidueMask(r2type);
		if (TTMaskHasType(r2Mask, ttype))
		    return 0;
	    }
	}

	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp)) /* Left */
	{
	    r2type = TiGetRightType(tp);
	    if (r2type == ttype)
	    {
		if (!rMask) return 0;
		TiToRect(tp, &r2);
		GeoInclude(&r2, &r);
	    }
	    else if (r2type >= DBNumUserLayers)
	    {
		r2Mask = DBResidueMask(r2type);
		if (TTMaskHasType(r2Mask, ttype))
		    return 0;
	    }
	}

	for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp)) /* Bottom */
	{
	    r2type = TiGetTopType(tp);
	    if (r2type == ttype)
	    {
		if (!rMask) return 0;
		TiToRect(tp, &r2);
		GeoInclude(&r2, &r);
	    }
	    else if (r2type >= DBNumUserLayers)
	    {
		r2Mask = DBResidueMask(r2type);
		if (TTMaskHasType(r2Mask, ttype))
		    return 0;
	    }
	}

	for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp)) /* Right */
	{
	    r2type = TiGetLeftType(tp);
	    if (r2type == ttype)
	    {
		if (!rMask) return 0;
		TiToRect(tp, &r2);
		GeoInclude(&r2, &r);
	    }
	    else if (r2type >= DBNumUserLayers)
	    {
		r2Mask = DBResidueMask(r2type);
		if (TTMaskHasType(r2Mask, ttype))
		    return 0;
	    }
	}
    }

    /* Layer names are taken from the LEF database. */

    lefName = MagicToLefTable[ttype].lefName;
    lefType = MagicToLefTable[ttype].lefInfo;

    orient = GEO_EAST;
    w = r.r_xtop - r.r_xbot;
    h = r.r_ytop - r.r_ybot;
    midlinex2 = (r.r_ytop + r.r_ybot);

    if (defdata->specialmode != DO_REGULAR)
    {
	routeWidth = (h > w) ? w : h;
	if ((lefType && lefType->lefClass == CLASS_VIA)
			|| (!lefType && DBIsContact(ttype)))
	    orient = GEO_CENTER;
    }
    else
    {
	routeWidth = 0;
	if ((lefType && (lefType->lefClass == CLASS_VIA))
			|| (!lefType && DBIsContact(ttype)))
	    orient = GEO_CENTER;
	else if (lefType)
	    routeWidth = lefType->info.route.width;

	if (routeWidth == 0)
	    routeWidth = DRCGetDefaultLayerWidth(ttype);
    }

    if (orient != GEO_CENTER)	/* not a via type */
    {
	if (h != routeWidth)
	{
	    if ((w == routeWidth) || ((routeWidth == 0) && (h > w)))
	    {
		orient = GEO_NORTH;
		midlinex2 = (r.r_xtop + r.r_xbot);
	    }
	}

	/* Warn if the route is not equal to the default route width--- */
	/* This means a regular net should have been a special net.	*/
	if ((h != routeWidth) && (w != routeWidth))
	{
	    /* Diagnostic */
	    TxPrintf("Net has width %d, default width is %d\n",
			(h > w) ? h : w, routeWidth);
	}

	/* Find the route orientation and centerline endpoint coordinates */

	if (orient == GEO_EAST)
	{
	    y1 = (midlinex2 * oscale) / 2;
	    y2 = y1;

	    x1 = r.r_xbot * oscale;
	    x2 = r.r_xtop * oscale;
	    if (routeWidth == 0) routeWidth = h;
	    extlen = (defdata->specialmode != DO_REGULAR) ?
			0 : (routeWidth * oscale) / 2;
	}
	else	/* vertical orientation */
	{
	    x1 = (midlinex2 * oscale) / 2;
	    x2 = x1;

	    y1 = r.r_ybot * oscale;
	    y2 = r.r_ytop * oscale;
	    if (routeWidth == 0) routeWidth = w;
	    extlen = (defdata->specialmode != DO_REGULAR) ?
			0 : (routeWidth * oscale) / 2;
	}
    }
    else	/* Type is a via */
    {
	y1 = y2 = (midlinex2 * oscale) / 2;
	x1 = x2 = ((r.r_xtop + r.r_xbot) * oscale) / 2;
	extlen = 0;
    }

    /* For contact types, find the residues of the contact	*/

    if (orient == GEO_CENTER)
    {
	TileType stype;

	rtype = r2type = TT_SPACE;
	rMask = DBResidueMask(ttype);
	for (stype = TT_TECHDEPBASE; stype < DBNumUserLayers; stype++)
	{
	    if (TTMaskHasType(rMask, stype))
	    {
		if ((stype == defdata->type) || (defdata->tile == (Tile *)NULL))
		   rtype = stype;
		else
		   r2type = stype;
	    }
	}
    }

    /* If we previously visited a tile, write out its second	*/
    /* coordinate pair, adjusting the position if necessary to	*/
    /* make the wire extensions line up correctly.  If they	*/
    /* don't line up, we assume a dogleg route and add		*/
    /* coordinate pairs as necessary to generate the correct	*/
    /* geometry in the DEF output.				*/

    if (defdata->tile)
    {
	Rect r2;
	TiToRect(defdata->tile, &r2);
	/* Only consider the endpoint of the previous tile at X2,Y2 */
	/* And the endpoint of the current tile at X1,Y1 */
	if (defdata->orient == GEO_EAST)
	    r2.r_xbot = r2.r_xtop - 1;
	else if (defdata->orient == GEO_NORTH)
	    r2.r_ybot = r2.r_ytop - 1;

	if (orient == GEO_EAST)
	    r.r_xtop = r.r_xbot + 1;
	else if (orient == GEO_NORTH)
	    r.r_ytop = r.r_ybot + 1;

	/* "sameroute" is true only if rectangles touch in the		*/
	/* direction of the route.					*/ 
	/* NOTE:  We should compute this FIRST and use it to determine	*/
	/* the current route direction!					*/
	/* Another hack---for special nets, don't continue routes that	*/
	/* have different widths, even if they're connected in the	*/
	/* direction of travel.  A separate record will be written for	*/
	/* the segment of different width.				*/

	if (GEO_TOUCH(&r, &r2))
	{
	    if (defdata->orient == GEO_EAST)
	    {
		if ((r.r_xbot == r2.r_xtop) || (r.r_xtop == r2.r_xbot))
		{
		    sameroute = TRUE;
		    if ((defdata->specialmode != DO_REGULAR) &&
				(r.r_ytop != r2.r_ytop || r.r_ybot != r2.r_ybot))
			sameroute = FALSE;
		}
	    }
	    else if (defdata->orient == GEO_NORTH)
	    {
		if ((r.r_ybot == r2.r_ytop) || (r.r_ytop == r2.r_ybot))
		{
		    sameroute = TRUE;
		    if ((defdata->specialmode != DO_REGULAR) &&
				(r.r_xtop != r2.r_xtop || r.r_xbot != r2.r_xbot))
			sameroute = FALSE;
		}
	    }
	    else
		sameroute = TRUE;
	}

	/* We should NOT continue a route from a via for a special net,	*/
	/* because the spec for this situation is too vaguely defined.	*/

	if (sameroute && (defdata->specialmode != DO_REGULAR) &&
		defdata->orient == GEO_CENTER)
	    sameroute = FALSE;
    }
		

    /* Determine if we need to write a NEW (type) record.  We do this	*/
    /* if 1) this is the first tile visited (except that we don't	*/
    /* write "NEW"), 2) the current tile doesn't touch the last tile	*/
    /* visited, or 3) the current type is not equal to the last type.	*/

    if ((!sameroute) || (ttype != defdata->type))
    {
	/* This is not a continuation of the last route.  Output	*/
	/* the last route position, and start a NEW record.		*/

	if ((sameroute) && (ttype != defdata->type) &&
			(orient == GEO_CENTER) &&
			(rtype == defdata->type))
	{
	    /* Adjust previous route to centerpoint of the via.	 If the	*/
	    /* via is not centered on the route, add segments to create	*/
	    /* the proper alignment.					*/
	    if ((defdata->orient == GEO_NORTH) && (x1 == defdata->x))
		defWriteCoord(defdata, defdata->x, y1, defdata->orient);
	    else if ((defdata->orient == GEO_EAST) && (y1 == defdata->y))
		defWriteCoord(defdata, x1, defdata->y, defdata->orient);
	    else if (defdata->orient == GEO_EAST)
	    {
		defWriteCoord(defdata, x1, defdata->y, defdata->orient);
		defWriteCoord(defdata, x1, y1, GEO_NORTH);
	    }
	    else if (defdata->orient == GEO_NORTH)
	    {
		defWriteCoord(defdata, defdata->x, y1, defdata->orient);
		defWriteCoord(defdata, x1, y1, GEO_EAST);
	    }

	    /* Via type continues route */
	    snprintf(viaName, (size_t)24, "_%.10g_%.10g",
			((float)w * oscale), ((float)h * oscale));
	    defCheckForBreak(strlen(lefName) + strlen(viaName) + 2, defdata);
	    fprintf(f, " %s%s ", lefName, viaName);
	}
	else
	{
	    /* New route segment.  Complete the last route segment. */
	    if (defdata->tile)
	    {
		/* Don't write out a segment for a via */
		if (defdata->orient != GEO_CENTER)
		    defWriteCoord(defdata,
				defdata->x - ((defdata->orient == GEO_EAST) ?
				defdata->extlen : 0),
				defdata->y - ((defdata->orient == GEO_NORTH) ?
				defdata->extlen : 0), defdata->orient);

		fprintf(f, "\n      NEW ");
		defdata->outcolumn = 10;
	    }
	    else
	    {
		/* First record printed for this node */
		fprintf(f, "\n      + ROUTED ");
		defdata->outcolumn = 15;
	    }

	    /* This is the first tile segment visited in the	*/
	    /* current type---use GEO_CENTER so that no		*/
	    /* coordinate wildcards ("*") get written.		*/

	    if (orient == GEO_CENTER)
	    {
		char *rName;

		/* Type can be zero (space) if the first tile	*/
		/* encountered is a via.  If so, use the 1st	*/
		/* residue of the contact as the route layer	*/
		/* type.					*/

		rName = defGetType((rtype == TT_SPACE) ? r2type : rtype, NULL);

		/* The first layer in a record may not be a via name */

	        defCheckForBreak(strlen(rName) + 1, defdata);
		fprintf(f, "%s ", rName);
		if (defdata->specialmode != DO_REGULAR)
		    defWriteRouteWidth(defdata, routeWidth);
		defWriteCoord(defdata, x1, y1, GEO_CENTER);
		snprintf(viaName, (size_t)24, "_%.10g_%.10g",
			((float)w * oscale), ((float)h * oscale));
	        defCheckForBreak(strlen(lefName) + strlen(viaName) + 2, defdata);
		fprintf(f, " %s%s ", lefName, viaName);
	    }
	    else
	    {
	        defCheckForBreak(strlen(lefName) + 1, defdata);
		fprintf(f, "%s ", lefName);
		if (defdata->specialmode != DO_REGULAR)
		    defWriteRouteWidth(defdata, routeWidth);

		/* defWriteCoord(defdata, x1, y1, GEO_CENTER); */
		defWriteCoord(defdata,
			x1 + ((orient == GEO_EAST) ?  extlen : 0),
			y1 + ((orient == GEO_NORTH) ?  extlen : 0),
			GEO_CENTER);
	    }
	}
    }
    else if (sameroute)
    {
	/* Adjust the previous route segment to match the new segment,	*/
	/* and write out the previous route segment record.		*/
	if ((orient == defdata->orient) && (defdata->x != x1) && (defdata->y != x2))
	{
	    /* Dogleg---insert extra segment */
	    defWriteCoord(defdata,
			defdata->x - ((defdata->orient == GEO_EAST) ?
			defdata->extlen : 0),
			defdata->y - ((defdata->orient == GEO_NORTH) ?
			defdata->extlen : 0), defdata->orient);
	    defWriteCoord(defdata, x1 + ((orient == GEO_EAST) ?  extlen : 0),
			y1 + ((orient == GEO_NORTH) ?  extlen : 0), orient);
	}
	else
	{
	    if (defdata->orient == GEO_EAST)
	    {
		if (((defdata->x + defdata->extlen) == x1) ||
			((defdata->x - defdata->extlen) == x1))
		    defWriteCoord(defdata, x1, defdata->y, defdata->orient);
		else
		{
		    /* Don't know how to connect the route segments. */
		    /* End the original route and start a new one. */
		    defWriteCoord(defdata,
				defdata->x - ((defdata->orient == GEO_EAST) ?
				defdata->extlen : 0),
				defdata->y - ((defdata->orient == GEO_NORTH) ?
				defdata->extlen : 0), defdata->orient);
		    fprintf(f, "\n      NEW %s", lefName);
		    defdata->outcolumn = 10 + strlen(lefName);
		    if (defdata->specialmode != DO_REGULAR)
		    {
			fprintf(f, " ");
			defdata->outcolumn++;
			defWriteRouteWidth(defdata, routeWidth);
		    }
		    defWriteCoord(defdata,
				x1 + ((orient == GEO_EAST) ?  extlen : 0),
				y1 + ((orient == GEO_NORTH) ?  extlen : 0),
				GEO_CENTER);
		}
	    }
	    else if (defdata->orient == GEO_NORTH)
	    {
		if (((defdata->y + defdata->extlen) == y1) ||
			((defdata->y - defdata->extlen) == y1))
		    defWriteCoord(defdata, defdata->x, y1, defdata->orient);
		else
		{
		    /* Don't know how to connect the route segments. */
		    /* End the original route and start a new one. */
		    defWriteCoord(defdata,
				defdata->x - ((defdata->orient == GEO_EAST) ?
				defdata->extlen : 0),
				defdata->y - ((defdata->orient == GEO_NORTH) ?
				defdata->extlen : 0), defdata->orient);
		    fprintf(f, "\n      NEW %s", lefName);
		    if (defdata->specialmode != DO_REGULAR) {
			fprintf(f, " ");
			defdata->outcolumn++;
			defWriteRouteWidth(defdata, routeWidth);
		    }
		    defdata->outcolumn = 10 + strlen(lefName);
		    defWriteCoord(defdata,
				x1 + ((orient == GEO_EAST) ?  extlen : 0),
				y1 + ((orient == GEO_NORTH) ?  extlen : 0),
				GEO_CENTER);
		}
	    }
	    else	/* last record was a via */
	    {
		/* Continuing route from via to other connecting layer type */
		/* Bend to meet via center---insert extra segment */

		if ((orient == GEO_NORTH) && (x1 != defdata->x))
		    defWriteCoord(defdata, x1, defdata->y, GEO_EAST);

		else if ((orient == GEO_EAST) && (y1 != defdata->y))
		    defWriteCoord(defdata, defdata->x, y1, GEO_NORTH);
	    }
	}
    }

    /* After a contact type, the route coordinates may continue in the	*/
    /* routing type connected by the contact to the type that was	*/
    /* previously seen connected to the contact.			*/

    /* NOTE!  The above comment matches the example on page 203 of the	*/
    /* LEF/DEF reference manual.  However, it is obvious that it is	*/
    /* logically fallacious.  A via can be declared to be multiple	*/
    /* types, and there is no way to know which type continues the	*/
    /* route without it being explicitly stated.  Nevertheless, that	*/
    /* is the way it's implemented. . .					*/

    if ((orient == GEO_CENTER) && (rtype != TT_SPACE) && (r2type != TT_SPACE))
	defdata->type = r2type;
    else if (orient == GEO_CENTER)
	defdata->type = TT_SPACE;
    else
	defdata->type = ttype;

    defdata->x = x2;
    defdata->y = y2;
    defdata->extlen = extlen;
    defdata->tile = tile;
    defdata->orient = orient;
    return 0;	/* Keep going */
}

/*
 *------------------------------------------------------------
 *
 * defCountVias --
 *
 *	First-pass function to count the number of different
 *	vias used, and retain this information for the netlist
 *	output.
 *
 * Results:
 *	The total number of via definitions to be written.
 *
 * Side Effects:
 *	None.
 *
 *------------------------------------------------------------
 */

int
defCountVias(rootDef, MagicToLefTable, oscale)
    CellDef *rootDef;
    LefMapping *MagicToLefTable;
    float oscale;
{
    TileTypeBitMask contactMask, *rmask;
    TileType ttype, stype;
    int pNum;
    CViaData cviadata;
    int defCountViaFunc();

    cviadata.scale = oscale;
    cviadata.total = 0;
    cviadata.MagicToLefTbl = MagicToLefTable;

    for (pNum = PL_SELECTBASE; pNum < DBNumPlanes; pNum++)
    {
	cviadata.plane = pNum;

        /* Only search for contacts that are on their *home* plane */

	TTMaskZero(&contactMask);
	for (ttype = TT_TECHDEPBASE; ttype < DBNumUserLayers; ttype++)
	    if (DBIsContact(ttype) && TTMaskHasType(&DBPlaneTypes[pNum], ttype))
		TTMaskSetType(&contactMask, ttype);

	/* Also search all stacked types whose residue contact types	*/
	/* are in the mask just generated.				*/

	for (ttype = DBNumUserLayers; ttype < DBNumTypes; ttype++)
	{
	    if (!DBIsContact(ttype)) continue;
	    rmask = DBResidueMask(ttype);
	    for (stype = TT_TECHDEPBASE; stype < DBNumUserLayers; stype++)
		if (TTMaskHasType(rmask, stype))
		{
		    TTMaskSetType(&contactMask, ttype);
		    break;
		}
	}
	cviadata.mask = &contactMask;

	DBSrPaintArea((Tile *)NULL, rootDef->cd_planes[pNum],
			&TiPlaneRect, &contactMask,
			defCountViaFunc, (ClientData)&cviadata);
    }
    return cviadata.total;
}

/* Callback function used by defCountVias */

int
defCountViaFunc(tile, cviadata)
    Tile *tile;
    CViaData *cviadata;
{
    TileType ttype = TiGetType(tile), ctype, rtype;
    TileTypeBitMask *rmask, *rmask2;
    Tile *tp;
    char *lname, vname[100], *vp;
    Rect r, r2;
    int w, h, offx, offy;
    float oscale = cviadata->scale;
    lefLayer *lefl;
    HashEntry *he;
    LefMapping *MagicToLefTable = cviadata->MagicToLefTbl;

    /* Techfiles are allowed not to declare a LEF entry, in which */
    /* case we would need to initialize the hash table.		  */
    if (LefInfo.ht_table == (HashEntry **) NULL) LefTechInit();

    /* Find the canonical type */
    if (ttype >= DBNumUserLayers)
    {
	rmask = DBResidueMask(ttype);
	for (ctype = TT_TECHDEPBASE; ctype < DBNumUserLayers; ctype++)
	    if (TTMaskHasType(rmask, ctype))
		break;
	if (ctype == DBNumUserLayers)
	    return 1;			/* Error condition */
    }
    else
    {
	rmask = NULL;
	ctype = ttype;
    }

    /* Generate a via name from the layer name and tile size */

    lname = MagicToLefTable[ctype].lefName;
    TiToRect(tile, &r);

    /* Boundary search.  WARNING:  This code is quite naive.  The	*/
    /* assumption is that all contacts are rectangular, and therefore	*/
    /* any contact area consisting of multiple tiles must be an amalgam	*/
    /* of regular and/or stacked types.  This whole thing should be	*/
    /* replaced by calls to generate layers via the CIF/Calma code.	*/

    /* Top */
    for (tp = RT(tile); RIGHT(tp) > LEFT(tile); tp = BL(tp))
    {
	rtype = TiGetBottomType(tp);
	if (rtype == ctype)
	{
	    if (!rmask) return 0; /* ignore tile but continue search */
	    TiToRect(tp, &r2);
	    GeoInclude(&r2, &r);
	}
	else if (rtype >= DBNumUserLayers)
	{
	    rmask2 = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask2, ctype))
		return 0;
	}
    }

    /* Left */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
    {
	rtype = TiGetRightType(tp);
	if (rtype == ctype)
	{
	    if (!rmask) return 0; /* ignore tile but continue search */
	    TiToRect(tp, &r2);
	    GeoInclude(&r2, &r);
	}
	else if (rtype >= DBNumUserLayers)
	{
	    rmask2 = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask2, ctype))
		return 0;
	}
    }

    /* Bottom */
    for (tp = LB(tile); LEFT(tp) < RIGHT(tile); tp = TR(tp))
    {
	rtype = TiGetTopType(tp);
	if (rtype == ctype)
	{
	    if (!rmask) return 0; /* ignore tile but continue search */
	    TiToRect(tp, &r2);
	    GeoInclude(&r2, &r);
	}
	else if (rtype >= DBNumUserLayers)
	{
	    rmask2 = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask2, ctype))
		return 0;
	}
    }

    /* Right */
    for (tp = TR(tile); TOP(tp) > BOTTOM(tile); tp = LB(tp))
    {
	rtype = TiGetLeftType(tp);
	if (rtype == ctype)
	{
	    if (!rmask) return 0; /* ignore tile but continue search */
	    TiToRect(tp, &r2);
	    GeoInclude(&r2, &r);
	}
	else if (rtype >= DBNumUserLayers)
	{
	    rmask2 = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask2, ctype))
		return 0;
	}
    }

    /* All values for the via rect are in 1/2 lambda to account */
    /* for a centerpoint not on the internal grid.		*/

    r.r_xbot <<= 1;
    r.r_xtop <<= 1;
    r.r_ybot <<= 1;
    r.r_ytop <<= 1;

    w = r.r_xtop - r.r_xbot;
    h = r.r_ytop - r.r_ybot;
    offx = (w >> 1);
    offy = (h >> 1);

    /* Center the via area on the origin */
    r.r_xbot = -offx;
    r.r_ybot = -offy;
    r.r_xtop = -offx + w;
    r.r_ytop = -offy + h;

    sprintf(vname, "%s_%.10g_%.10g", lname,
			((float)offx * oscale), ((float)offy * oscale));

    he = HashFind(&LefInfo, vname);
    lefl = (lefLayer *)HashGetValue(he);
    if (lefl == NULL)
    {
	cviadata->total++;	/* Increment the count of uses */
	lefl = (lefLayer *)mallocMagic(sizeof(lefLayer));
	lefl->type = ttype;
	lefl->obsType = -1; 
	lefl->lefClass = CLASS_VIA;
	lefl->info.via.area = r;
	lefl->info.via.cell = (CellDef *)NULL;
	lefl->info.via.lr = (LinkedRect *)NULL;
	lefl->refCnt = 0;	/* These entries will be removed after writing */
	HashSetValue(he, lefl);
	lefl->canonName = (char *)he->h_key.h_name;
    }
    return 0;	/* Keep the search going */
}

/*
 *------------------------------------------------------------
 *
 * defGetType --
 *
 *	Retrieve the LEF/DEF name of a magic layer from the
 *	LefInfo hash table.
 *
 * Results:
 *	The "official" LEF/DEF layer name of the magic type.
 *
 * Side Effects:
 *	If "lefptr" is non-NULL, it is filled with a pointer
 *	to the appropriate lefLayer entry, or NULL if there
 *	is no corresponding entry.
 *------------------------------------------------------------
 */

char *
defGetType(ttype, lefptr)
    TileType ttype;
    lefLayer **lefptr;
{
    HashSearch hs;
    HashEntry *he;
    lefLayer *lefl;
    int contact = DBIsContact(ttype) ? CLASS_VIA : CLASS_ROUTE;

    /* Pick up information from the original LefInfo hash table	*/
    /* entries created during read-in of the tech file.		*/

    if (LefInfo.ht_table != (HashEntry **) NULL)
    {
	HashStartSearch(&hs);
	while (he = HashNext(&LefInfo, &hs))
	{
	    lefl = (lefLayer *)HashGetValue(he);
	    if (lefl && (lefl->type == ttype) && (contact == lefl->lefClass))
	    {
		if (lefptr) *lefptr = lefl;
		return lefl->canonName;
	    }
	}
    }

    /* If we got here, there is no entry;  use the database name */
    if (lefptr) *lefptr = (lefLayer *)NULL;
    return DBTypeLongNameTbl[ttype];
}

/*
 *------------------------------------------------------------
 *
 * defWriteVias --
 *
 *	Output the VIAS section of a DEF file.  We equate magic
 *	contact areas with DEF "VIAS".  A separate via entry is
 *	generated for each unique geometry.  The exact output
 *	is determined from the CIF output rules.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Output written to the DEF output file.
 *
 *------------------------------------------------------------
 */

void
defWriteVias(f, rootDef, oscale, lefMagicToLefLayer)
    FILE *f;				/* File to write to */
    CellDef *rootDef;			/* Cell definition to use */
    float oscale;			/* Output scale factor */
    LefMapping *lefMagicToLefLayer;
{
    HashSearch hs;
    HashEntry *he;
    lefLayer *lefl;
    TileTypeBitMask *rMask;
    TileType ttype;

    /* Pick up information from the LefInfo hash table	*/
    /* created by fucntion defCountVias()		*/

    if (LefInfo.ht_table != (HashEntry **) NULL)
    {
	HashStartSearch(&hs);
	while (he = HashNext(&LefInfo, &hs))
	{
	    int size, sep, border;
	    char *us1, *us2;
	    lefl = (lefLayer *)HashGetValue(he);
	    if (!lefl) continue;

	    /* Only count the generated vias of the type name_sizex_sizey */

	    if ((us1 = strchr(lefl->canonName, '_')) == NULL ||
	    		(us2 = strrchr(lefl->canonName, '_')) == us1)
		continue;

	    if (lefl->lefClass == CLASS_VIA)
	    { 
		fprintf(f, "   - %s", (char *)lefl->canonName);

		/* Generate squares for the area as determined	*/
		/* by the cifoutput section of the tech file	*/
		
		rMask = DBResidueMask(lefl->type);
		for (ttype = TT_TECHDEPBASE; ttype < DBNumUserLayers; ttype++)
		    if (TTMaskHasType(rMask, ttype))
			fprintf(f, "\n      + RECT %s ( %.10g %.10g ) ( %.10g %.10g )",
				lefMagicToLefLayer[ttype].lefName,
				(float)(lefl->info.via.area.r_xbot) * oscale / 2,
				(float)(lefl->info.via.area.r_ybot) * oscale / 2,
				(float)(lefl->info.via.area.r_xtop) * oscale / 2,
				(float)(lefl->info.via.area.r_ytop) * oscale / 2);

		/* Handle the contact cuts.	*/

		if (CIFGetContactSize(lefl->type, &size, &sep, &border))
		{
		    int i, j, nAc, nUp, pitch, left;
		    Rect square, *r = &lefl->info.via.area;

		    pitch = size + sep;
		    nAc = (r->r_xtop - r->r_xbot + sep - (2 * border)) / pitch;
		    if (nAc == 0)
		    {
			left = (r->r_xbot + r->r_xtop - size) / 2;
			if (left >= r->r_xbot) nAc = 1;
		    }
		    else
			left = (r->r_xbot + r->r_xtop + sep - (nAc * pitch)) / 2;

		    nUp = (r->r_ytop - r->r_ybot + sep - (2 * border)) / pitch;
		    if (nUp == 0)
		    {
			square.r_ybot = (r->r_ybot + r->r_ytop - size) / 2;
			if (square.r_ybot >= r->r_ybot) nUp = 1;
		    }
		    else
			square.r_ybot = (r->r_ybot + r->r_ytop + sep - (nUp * pitch)) / 2;

		    for (i = 0; i < nUp; i++)
		    {
			square.r_ytop = square.r_ybot + size;
			square.r_xbot = left;
			for (j = 0; j < nAc; j++)
			{
			     square.r_xtop = square.r_xbot + size;
	
			     fprintf(f, "\n      + RECT %s ( %.10g %.10g )"
					" ( %.10g %.10g )",
					lefMagicToLefLayer[lefl->type].lefName,
					(float)(square.r_xbot) * oscale / 2,
					(float)(square.r_ybot) * oscale / 2,
					(float)(square.r_xtop) * oscale / 2,
					(float)(square.r_ytop) * oscale / 2);
			     square.r_xbot += pitch;
			}
			square.r_ybot += pitch;
		    }
		}
		else
		    /* If we can't find the CIF/GDS parameters for cut	*/
		    /* generation, then output a single rectangle the	*/
		    /* size of the contact tile.			*/
		{
		    fprintf(f, "\n      + RECT %s ( %.10g %.10g ) ( %.10g %.10g )",
			lefMagicToLefLayer[lefl->type].lefName,
			(float)(lefl->info.via.area.r_xbot) * oscale / 2,
			(float)(lefl->info.via.area.r_ybot) * oscale / 2,
			(float)(lefl->info.via.area.r_xtop) * oscale / 2,
			(float)(lefl->info.via.area.r_ytop) * oscale / 2);
	 	}	
		fprintf(f, " ;\n");
	    }
	}
    }
}

/*
 *------------------------------------------------------------
 *
 * defCountComponents --
 *
 *	First-pass function to count the number of cell
 *	uses (components) to be written to the DEF output
 *	file.
 *
 * Results:
 *	The total number of uses to be written.
 *
 * Side Effects:
 *	None.
 *
 *------------------------------------------------------------
 */
 
int
defCountComponents(rootDef)
    CellDef *rootDef;
{
    pointertype total;
    int defCountCompFunc();

    TxPrintf("Diagnostic:  Finding all components of cell %s\n", rootDef->cd_name);

    total = 0;
    DBCellEnum(rootDef, defCountCompFunc, (ClientData)&total);
    return (int)total;
}

/* Callback function used by defCountComponents */

int
defCountCompFunc(cellUse, total)
    CellUse *cellUse;
    pointertype *total;
{
    /* Ignore any cellUse that does not have an identifier string. */
    if (cellUse->cu_id == NULL) return 0;

    (*total)++;	/* Increment the count of uses */

    return 0;	/* Keep the search going */
}

/*
 *------------------------------------------------------------
 *
 * defWriteComponents --
 *
 *	Output the COMPONENTS section of the DEF file.  This
 *	is a listing of all cell uses, their placement, and
 *	orientation.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Output to the DEF file.
 *
 *------------------------------------------------------------
 */

void
defWriteComponents(f, rootDef, oscale)
    FILE *f;				/* File to write to */
    CellDef *rootDef;			/* Cell definition to use */
    float oscale;			/* Output scale factor */
{
    DefData defdata;
    int defComponentFunc();		/* Forward declaration */

    defdata.f = f;
    defdata.scale = oscale;

    DBCellEnum(rootDef, defComponentFunc, (ClientData)&defdata);
}

/* Callback function used by defWriteComponents */

int
defComponentFunc(cellUse, defdata)
    CellUse *cellUse;
    DefData *defdata;
{
    FILE *f = defdata->f;
    float oscale = defdata->scale;

    /* Ignore any cellUse that does not have an identifier string. */
    if (cellUse->cu_id == NULL) return 0;

    fprintf(f, "   - %s %s\n      + PLACED ( %.10g %.10g ) %s ;\n",
	cellUse->cu_id, cellUse->cu_def->cd_name,
	(float)cellUse->cu_bbox.r_xbot * oscale,
	(float)cellUse->cu_bbox.r_ybot * oscale,
	defTransPos(&cellUse->cu_transform));

    return 0;	/* Keep the search going */
}

/*
 *------------------------------------------------------------
 *
 * defMakeInverseLayerMap ---
 *
 *	Generate an array of pointers to lefLayer structures for each
 *	magic type so we can do a quick lookup when searching over tiles.
 *
 * Results:
 *	Pointer to the inverse layer map.
 *
 * Side effects:
 *	Memory is allocated for the map structure array.
 *
 *------------------------------------------------------------
 */

LefMapping *
defMakeInverseLayerMap()
{
    LefMapping *lefMagicToLefLayer;
    lefLayer *lefl;
    TileType i;
    char *lefname;

    lefMagicToLefLayer = (LefMapping *)mallocMagic(DBNumUserLayers
		* sizeof(LefMapping));
    for (i = TT_TECHDEPBASE; i < DBNumUserLayers; i++)
    {
	lefname = defGetType(i, &lefl);
	lefMagicToLefLayer[i].lefName = lefname;
	lefMagicToLefLayer[i].lefInfo = lefl;
    }
    return lefMagicToLefLayer;
}

/*
 *------------------------------------------------------------
 *
 * DefWriteAll --
 *
 * Results:
 *
 * Side Effects:
 *
 *------------------------------------------------------------
 */

/* To do:  routine DefWriteAll().					*/

/* DEF does not handle hierarchy.  However, we should assume that we	*/
/* want to write out a DEF file for each cell in the hierarchy.  But,	*/
/* we should stop at any cells defining ports, assuming that they are	*/
/* standard cells and not part of the routing.				*/

/* Maybe there should be a method for specifying that any hierarchy	*/
/* should be flattened when writing to the DEF file output.		*/


/*
 *------------------------------------------------------------
 *
 * DefWriteCell --
 *
 *	Write DEF-format output for the indicated cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes a single .def file to disk.
 *
 *------------------------------------------------------------
 */

void
DefWriteCell(def, outName, allSpecial)
    CellDef *def;		/* Cell being written */
    char *outName;		/* Name of output file, or NULL. */
    bool allSpecial;		/* Treat all nets as SPECIALNETS? */
{
    char *filename;
    FILE *f;
    NetCount nets;
    int total;
    float scale = CIFGetOutputScale(1);	/* Note that "1" here corresponds
					 * to "1000" in the header UNITS line
					 */
    LefMapping *lefMagicToLefLayer;
    int i;
    lefLayer *lefl;
    HashEntry *he;

    f = lefFileOpen(def, outName, ".def", "w", &filename);

    TxPrintf("Generating DEF output %s for cell %s:\n", filename, def->cd_name);

    if (f == NULL)
    {
#ifdef MAGIC_WRAPPER
	TxError("Cannot open output file %s (%s).\n", filename,
		strerror(errno));
#else
	TxError("Cannot open output file: ");
	perror(filename);
#endif
	return;
    }

    defWriteHeader(def, f, scale);

    lefMagicToLefLayer = defMakeInverseLayerMap();

    /* Vias---magic contact areas are reported as vias. */
    total = defCountVias(def, lefMagicToLefLayer, scale);
    fprintf(f, "VIAS %d ;\n", total); 
    if (total > 0)
	defWriteVias(f, def, scale, lefMagicToLefLayer);
    fprintf(f, "END VIAS\n\n");

    /* Components (i.e., cell uses) */
    total = defCountComponents(def);
    fprintf(f, "COMPONENTS %d ;\n", total); 
    if (total > 0)
	defWriteComponents(f, def, scale);
    fprintf(f, "END COMPONENTS\n\n");

    /* Pins---assume no pins (for now) */
    fprintf(f, "PINS 0 ;\nEND PINS\n\n");

    /* Count the number of nets and "special" nets */
    nets = defCountNets(def, allSpecial);

    /* "Special" nets---nets matching $GND, $VDD, or $globals(*) 	*/

    fprintf(f, "SPECIALNETS %d ;\n", nets.special); 
    if (nets.special > 0)
	defWriteNets(f, def, scale, lefMagicToLefLayer, (allSpecial) ? 
		ALL_SPECIAL : DO_SPECIAL);
    fprintf(f, "END SPECIALNETS\n\n");

    /* "Regular" nets */
    fprintf(f, "NETS %d ;\n", nets.regular); 
    if (nets.regular > 0)
	defWriteNets(f, def, scale, lefMagicToLefLayer, DO_REGULAR);
    fprintf(f, "END NETS\n\n");

    if (nets.has_nets) {
	EFFlatDone();
	EFDone();
    }

    fprintf(f, "END DESIGN\n\n");
    fclose(f);

    freeMagic((char *)lefMagicToLefLayer);
    lefRemoveGeneratedVias();
}

