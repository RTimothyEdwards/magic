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
#include "extract/extractInt.h"	/* for ExtCurStyle definition */
#include "extflat/extflat.h"
#include "lef/lefInt.h"
#include "drc/drc.h"		/* for querying width, spacing rules */

/* C99 compat */
#include "utils/signals.h"
#include "textio/textio.h"

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
   LefRules 	*ruleset;	/* Non-default ruleset or NULL */

   LefMapping	*MagicToLefTbl;
   HashTable	*defViaTable;
   int		outcolumn;	/* Current column of output in file */
   unsigned char specialmode;	/* What nets to write as SPECIALNETS */
} DefData;

typedef struct {
   CellDef	*def;
   int		nlayers;
   char		**baseNames;
   TileTypeBitMask *blockMasks;
   LinkedRect **blockData;
} DefObsData;

typedef struct {
   CellDef *def;
   float scale;
   int total;
   int plane;
   TileTypeBitMask *mask;
   LefMapping *MagicToLefTbl;
   HashTable *defViaTable;
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
defWriteHeader(def, f, oscale, units)
    CellDef *def;	/* Def for which to generate DEF output */
    FILE *f;		/* Output to this file */
    float oscale;
    int units;		/* Units for UNITS; could be derived from oscale */
{
    TileType type;
    char *propvalue;
    bool propfound;

    TxPrintf("Diagnostic:  Write DEF header for cell %s\n", def->cd_name);

    /* NOTE:  This routine corresponds to Envisia LEF/DEF Language	*/
    /* Reference version 5.7 (November, 2009)				*/

    fprintf(f, "VERSION 5.7 ;\n");
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

    /* The DEF scalefactor (conversion to microns) is always 1000	*/
    /* (nanometers) unless overridden on the command line.		*/

    fprintf(f, "   UNITS DISTANCE MICRONS %d ;\n", units);

    /* For DIEAREA, use the FIXED_BBOX property if present.  Otherwise,	*/
    /* use the extents of geometry (CellDef bounding box)		*/

    propvalue = (char *)DBPropGet(def, "FIXED_BBOX", &propfound);
    if (propfound)
    {
	Rect bbox;

	/* Die area, taken from the declared FIXED_BBOX.		*/
	if (sscanf(propvalue, "%d %d %d %d", &bbox.r_xbot, &bbox.r_ybot,
		&bbox.r_xtop, &bbox.r_ytop) == 4)
        {
	     fprintf(f, "   DIEAREA ( %.10g %.10g ) ( %.10g %.10g ) ;\n",
			(float)bbox.r_xbot * oscale,
			(float)bbox.r_ybot * oscale,
			(float)bbox.r_xtop * oscale,
			(float)bbox.r_ytop * oscale);
	}
	else
	    propfound = FALSE;
    }

    if (!propfound)
    {
	/* Die area, taken from the cell def bounding box.		*/
	fprintf(f, "   DIEAREA ( %.10g %.10g ) ( %.10g %.10g ) ;\n",
		(float)def->cd_bbox.r_xbot * oscale,
		(float)def->cd_bbox.r_ybot * oscale,
		(float)def->cd_bbox.r_xtop * oscale,
		(float)def->cd_bbox.r_ytop * oscale);
    }

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
    total.blockages = 0;
    total.has_nets = TRUE;

    TxPrintf("Diagnostic:  Finding all nets in cell %s\n", rootDef->cd_name);
    TxPrintf("(This can take a while!)\n");

    /* Read in the extracted file */
    EFInit();

    /* There are no arguments for extflat, but we need to call the	*/
    /* routine to initialize a few things such as the search path.	*/
    EFArgs(0, NULL, NULL, NULL, NULL);

    EFScale = 0.0;	/* Allows EFScale to be set to the scale value	*/

    if (EFReadFile(rootDef->cd_name, TRUE, FALSE, TRUE, FALSE))
    {
	EFFlatBuild(rootDef->cd_name, EF_FLATNODES | EF_NOFLATSUBCKT);
        EFVisitNodes(defnodeCount, (ClientData)&total);
    }
    else
    {
	TxError("Warning:  Circuit has no .ext file;  no nets written.\n");
	TxError("Run extract on this circuit if you want nets in the output.\n");
	EFDone(NULL);
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

    /* Ignore the substrate node if it is not connected to any routing */
    if (node->efnode_type == TT_SPACE)
	return 0;

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
	    TxPrintf("Node %s is defined in the \"globals\" array\n", cp);
	    node->efnode_flags |= EF_SPECIAL;
	}

	/* Check against Tcl variables $VDD and $GND */
	pwr = (char *)Tcl_GetVar(magicinterp, "VDD", TCL_GLOBAL_ONLY);
	if (pwr && (!strcmp(cp, pwr)))
	{
	    /* Diagnostic */
	    TxPrintf("Node %s matches VDD variable definition!\n", cp);
	    node->efnode_flags |= EF_SPECIAL;
	}

	pwr = (char *)Tcl_GetVar(magicinterp, "GND", TCL_GLOBAL_ONLY);
	if (pwr && (!strcmp(cp, pwr)))
	{
	    /* Diagnostic */
	    TxPrintf("Node %s matches GND variable definition!\n", cp);
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
	else
	    total->blockages++;
    }
    else
    {
	/* We only count nodes having a port connection as "regular" nets */

	if (node->efnode_flags & EF_SPECIAL)
	    total->special++;
	else if (node->efnode_flags & EF_PORT)
	    total->regular++;
	else
	    total->blockages++;
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
defWriteNets(f, rootDef, oscale, MagicToLefTable, defViaTable, specialmode)
    FILE *f;				/* File to write to */
    CellDef *rootDef;			/* Cell definition to use */
    float oscale;			/* Output scale factor */
    LefMapping *MagicToLefTable;	/* Magic to LEF layer mapping */
    HashTable *defViaTable;		/* Hash table of contact positions */
    unsigned char specialmode;		/* What to write as a SPECIALNET */
{
    DefData defdata;
    int defnodeVisit();

    defdata.f = f;
    defdata.scale = oscale;
    defdata.def = rootDef;
    defdata.MagicToLefTbl = MagicToLefTable;
    defdata.outcolumn = 0;
    defdata.ruleset = NULL;
    defdata.specialmode = specialmode;
    defdata.defViaTable = defViaTable;

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
    char locndn[256];
    FILE *f = defdata->f;
    CellDef *def = defdata->def;
    float oscale = defdata->scale;
    TileTypeBitMask tmask, *rmask;
    TileType nodetype, magictype;
    Rect *nodeloc;
    LinkedRect *lr;
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
	else if (node->efnode_flags & EF_SPECIAL)
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

    /* Avoid attempting to extract an implicit substrate into DEF */
    if (node->efnode_type == TT_SPACE)
	return 0;

    fprintf(f, "   - %s", ndn);
    defdata->outcolumn = 5 + strlen(ndn);

    /* If this net is marked as a PORT, then connect to the associated PIN */
    if (node->efnode_flags & EF_TOP_PORT)
	fprintf(f, " ( PIN %s )", ndn);

    /* Find all the node names that are port connections. 	*/
    /* For now, we will just use anything connecting one level	*/
    /* down in the hierarchy.  This is not definitive, however,	*/
    /* and we should confirm that the connection is an actual	*/
    /* port.							*/

    for (thisnn = node->efnode_name; thisnn != NULL; thisnn = thisnn->efnn_next)
    {
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

    /* TT_SPACE indicates that a layer name must be the	*/
    /* next thing to be written to the DEF file.	*/
    defdata->type = TT_SPACE;
    defdata->tile = (Tile *)NULL;

    /* Process all disjoint segments of the node */

    for (lr = node->efnode_disjoint; lr; lr = lr->r_next)
    {
	/* Watch for entries created from the substrate node */
	if ((lr->r_r.r_ll.p_x <= (MINFINITY + 2)) ||
			(lr->r_r.r_ll.p_y <= (MINFINITY + 2)))
	    continue;

	/* Net geometry (this should be an option!)---		*/
	/* Use the DBconnect routines to find all geometry	*/
	/* connected to a specific node.  This is a		*/
	/* redundant search---we've already done this once	*/
	/* when extracting the circuit.  But, because the	*/
	/* DEF file requires a count of nodes up front, we	*/
	/* would have to do it twice anyway.  In this case,	*/
	/* we only do it once here, since the results of	*/
	/* the first pass are picked up from the .ext file.	*/

	magictype = DBTechNameType(EFLayerNames[lr->r_type]);

	/* Note that the type of the node might be defined by the type */
	/* in the subcircuit itself, so we need to search for any type */
	/* that might validly connect to it, not just the type itself. */
	/* TTMaskSetOnlyType(&tmask, magictype); */
	TTMaskZero(&tmask);
	TTMaskSetMask(&tmask, &DBConnectTbl[magictype]);

	DBSrConnect(def, &lr->r_r, &tmask, DBConnectTbl, &TiPlaneRect,
			defNetGeometryFunc, (ClientData)defdata);

	if (defdata->tile == (Tile *)NULL)
	{
	    /* No route layer?  It's possible that something connects 	*/
	    /* to the port location but doesn't overlap.  Try painting	*/
	    /* the node type in def and trying again.			*/

	    Rect rport;
	    SearchContext scx;
	    int defPortTileFunc();	/* Fwd declaration */

	    scx.scx_area = node->efnode_loc;
	    scx.scx_use = def->cd_parents;
	    scx.scx_trans = GeoIdentityTransform;

	    rport = GeoNullRect;
	    DBTreeSrUniqueTiles(&scx, &tmask, 0, defPortTileFunc, (ClientData)&rport);

	    /* Add the residue types to any contact type */
	    if (DBIsContact(magictype))
	    {
		rmask = DBResidueMask(magictype);
		TTMaskSetMask(&tmask, rmask);
		TTMaskSetType(&tmask, magictype);
	    }

	    /* Check if anything was found in the location (e.g., .ext file
	     * could be invalid or outdated).  However, if magictype is not
	     * an electrically active type, then don't issue any warning.
	     */
	    if (GEO_RECTNULL(&rport))
	    {
		if (TTMaskHasType(&ExtCurStyle->exts_activeTypes, magictype))
		    TxError("Nothing of type %s found at node %s location"
				" (floating label?)!\n",
				DBTypeLongNameTbl[magictype], ndn);
	    }
	    else
	    {
		/* Expand the rectangle around the port to overlap any	*/
		/* connecting material.					*/
		rport.r_xbot--;
		rport.r_ybot--;
		rport.r_xtop++;
		rport.r_ytop++;
  		DBSrConnect(def, &rport, &tmask, DBConnectTbl, &TiPlaneRect,
				defNetGeometryFunc, (ClientData)defdata);
	    }
        }
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

/* Callback function for defNetGeometryFunc().  Determines if any tile	*/
/* sets the lower bound of the clip line for extending a wire upward	*/

int
defMaxWireFunc(tile, yclip)
    Tile *tile;
    int  *yclip;
{
    if (BOTTOM(tile) < (*yclip)) *yclip = BOTTOM(tile);
    return 0;
}

/* Callback function for defNetGeometryFunc().  Determines if any tile	*/
/* sets the upper bound of the clip line for extending a wire downward	*/

int
defMinWireFunc(tile, yclip)
    Tile *tile;
    int  *yclip;
{
    if (TOP(tile) > (*yclip)) *yclip = TOP(tile);
    return 0;
}

/* Callback function for defNetGeometryFunc().  Determines if any tile	*/
/* is enclosed by rect, and if so, marks the tile client so that	*/
/* DBSrConnectFunc() will not apply the client function to that tile.	*/
/* clientdata value (ClientData)1 means the tile has already been	*/
/* processed;  (ClientData)CLIENTDEFAULT means that it has not been	*/
/* processed.  Any other value will cause it to skip the client		*/
/* function when it is processed.					*/

int
defExemptWireFunc(tile, rect)
    Tile *tile;
    Rect *rect;
{
    Rect r;

    /* Do not change the client data of tiles that have been processed! */
    if (tile->ti_client != (ClientData) 1)
    {
	/* Ignore contacts, which need additional processing */
	if (DBIsContact(TiGetType(tile))) return 0;

	TiToRect(tile, &r);
	if (GEO_SURROUND(rect, &r))
	    tile->ti_client = (ClientData) 2;
    }
    return 0;
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
    Rect r, rorig;
    unsigned char orient;
    bool sameroute = FALSE;
    int routeWidth, w, h, midlinex2, topClip, botClip;
    float x1, y1, x2, y2, extlen;
    lefLayer *lefType, *lefl;
    char *lefName, viaName[128], posstr[24];
    LefRules *lastruleset = defdata->ruleset;
    HashEntry *he;
    HashTable *defViaTable = defdata->defViaTable;
    LefMapping *MagicToLefTable = defdata->MagicToLefTbl;

    if (ttype == TT_SPACE) return 0;
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
	/* tile areas belonging to ttype.  ONLY check top and right.	*/

	for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp)) /* Left */
	{
	    r2type = TiGetRightType(tp);
	    if (r2type == ttype)
		return 0;
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
		return 0;
	    else if (r2type >= DBNumUserLayers)
	    {
		r2Mask = DBResidueMask(r2type);
		if (TTMaskHasType(r2Mask, ttype))
		    return 0;
	    }
	}

	/* Extend boundary to top and right */

	for (tp = RT(tile); ; tp = RT(tp)) /* Top */
	{
	    r2type = TiGetBottomType(tp);
	    if (r2type == ttype)
	    {
		TiToRect(tp, &r2);
		GeoInclude(&r2, &r);
	    }
	    else if (r2type >= DBNumUserLayers)
	    {
		r2Mask = DBResidueMask(r2type);
		if (TTMaskHasType(r2Mask, ttype))
		{
		    TiToRect(tp, &r2);
		    GeoInclude(&r2, &r);
		}
		else break;
	    }
	    else break;
	}

	for (tp = TR(tile); ; tp = TR(tp)) /* Right */
	{
	    r2type = TiGetLeftType(tp);
	    if (r2type == ttype)
	    {
		TiToRect(tp, &r2);
		GeoInclude(&r2, &r);
	    }
	    else if (r2type >= DBNumUserLayers)
	    {
		r2Mask = DBResidueMask(r2type);
		if (TTMaskHasType(r2Mask, ttype))
		{
		    TiToRect(tp, &r2);
		    GeoInclude(&r2, &r);
		}
		else break;
	    }
	    else break;
	}
    }
    rorig = r;

    /* Layer names are taken from the LEF database. */

    lefName = MagicToLefTable[ttype].lefName;
    if (lefName == NULL) return 0;	/* Do not write types not in LEF definition */
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

	/* Check for non-default widths and slivers */

	defdata->ruleset = NULL;
	if (((h != routeWidth) && (w != routeWidth)) || 
		((h == routeWidth) && (w < routeWidth)) ||
		((w == routeWidth) && (h < routeWidth)))
	{
	    /* Handle slivers.  There are two main cases:
	     * (1) Sliver is part of a via extension, so it can be ignored.
	     * (2) Sliver is a route split into several tiles due to geometry
	     *     to the left.  Expand up and down to include all tiles.
	     *
	     * Slivers that violate design rule widths are assumed to get
	     * merged with another tile to make a full shape.  Slivers that
	     * continue to violate minimum width will be ignored.
	     */

	    if (w < routeWidth) return 0;

	    if (h < routeWidth)
	    {
		/* Check upward. */
		r = rorig;
		r.r_ytop = r.r_ybot + routeWidth;
		r.r_ybot = rorig.r_ytop;
		do
		{
		    r.r_ytop += routeWidth;
		    topClip = r.r_ytop;
		    DBSrPaintArea(tile, def->cd_planes[plane],
				&r, &DBNotConnectTbl[ttype],
				defMaxWireFunc, (ClientData)&topClip);
		}
		while (topClip == r.r_ytop);

		/* Check downward */
		r = rorig;
		r.r_ybot = r.r_ytop - routeWidth;
		r.r_ytop = rorig.r_ybot;
		do
		{
		    r.r_ybot -= routeWidth;
		    botClip = r.r_ybot;
		    DBSrPaintArea(tile, def->cd_planes[plane],
				&r, &DBNotConnectTbl[ttype],
				defMinWireFunc, (ClientData)&botClip);
		}
		while (botClip == r.r_ybot);

		r = rorig;
		if (topClip > r.r_ytop) r.r_ytop = topClip;
		if (botClip < r.r_ybot) r.r_ybot = botClip;

		/* If height is still less that a route width, bail */
		h = r.r_ytop - r.r_ybot;
		if (h < routeWidth) return 0;

		/* If r is larger than rorig, then exempt all unprocessed	*/
		/* tiles contained in rorig from being checked again.		*/

		if (!GEO_SAMERECT(r, rorig))
		    DBSrPaintArea(tile, def->cd_planes[plane],
				&r, &DBConnectTbl[ttype],
				defExemptWireFunc, (ClientData)&r);
	    }

	    /* Handle non-default width regular nets (use TAPERRULE) */
	    if ((h != routeWidth) && (w != routeWidth) &&
			(defdata->specialmode == DO_REGULAR))
	    {
		int ndv = (h > w) ? w : h;
		char ndname[100];
		LefRules *ruleset;
		lefRule *rule;

		/*
	    	TxPrintf("Net at (%d, %d) has width %d, default width is %d\n",
			r.r_xbot, r.r_ybot,
			(h < w) ? h : w, routeWidth);
		 */

		/* Create a nondefault rule.  Use one rule per layer and width	*/
		/* for now (to do:  keep the same ruleset when possible)	*/

		sprintf(ndname, "%s_width_%d", lefName, (int)((float)ndv * oscale));
		he = HashFind(&LefNonDefaultRules, ndname);
		ruleset = (LefRules *)HashGetValue(he);
		if (ruleset == NULL)
		{
		    ruleset = (LefRules *)mallocMagic(sizeof(LefRules));
		    HashSetValue(he, ruleset);
		    ruleset->name = StrDup((char **)NULL, ndname);
		    ruleset->rule = (lefRule *)mallocMagic(sizeof(lefRule));
		    ruleset->rule->lefInfo = lefType;
		    ruleset->rule->width = ndv;
		    /* Non-default rules wire extension-at-via is not used. */
		    ruleset->rule->extend = 0;
		    /* Spacing is not needed, but set it to the layer default */
		    ruleset->rule->spacing = DRCGetDefaultLayerSpacing(ttype, ttype);
		    /* There will only be one rule in this ruleset */
		    ruleset->rule->next = NULL;
		}
		defdata->ruleset = ruleset;
	    }

	    /* Set orientation based on longest side */
	    if (h > w)
	    {
		orient = GEO_NORTH;
		midlinex2 = (r.r_xtop + r.r_xbot);
	    }
	    else
	    {
		orient = GEO_EAST;
		midlinex2 = (r.r_ytop + r.r_ybot);
	    }
	}
	else
	    /* Non-default width has ended and the route returns to default rules */
	    defdata->ruleset = NULL;

	/* Find the route orientation and centerline endpoint coordinates */

	if (orient == GEO_EAST)
	{
	    y1 = (midlinex2 * oscale) / 2;
	    y2 = y1;

	    x1 = r.r_xbot * oscale;
	    x2 = r.r_xtop * oscale;
	    if (routeWidth == 0) routeWidth = h;
	    
	    extlen = 0;
	    /* NOTE: non-default tapers are not using wire */
	    /* extension-at-via (need to implement)	   */

	    if (defdata->specialmode == DO_REGULAR)
	    {
		if (defdata->ruleset != NULL)
		{
		    x1 = x1 + (defdata->ruleset->rule->width / 2 * oscale);
		    x2 = x2 - (defdata->ruleset->rule->width / 2 * oscale);
		}
		else
		{
		    x1 = x1 + (routeWidth / 2 * oscale);
		    x2 = x2 - (routeWidth / 2 * oscale);
		}
	    }
	}
	else	/* vertical orientation */
	{
	    x1 = (midlinex2 * oscale) / 2;
	    x2 = x1;

	    y1 = r.r_ybot * oscale;
	    y2 = r.r_ytop * oscale;
	    if (routeWidth == 0) routeWidth = w;
		
	    extlen = 0;
	    /* NOTE: non-default tapers are not using wire */
	    /* extension-at-via (need to implement)	   */
	    if (defdata->specialmode == DO_REGULAR)
	    {
		if (defdata->ruleset != NULL)
		{
		    y1 = y1 + (defdata->ruleset->rule->width / 2 * oscale);
		    y2 = y2 - (defdata->ruleset->rule->width / 2 * oscale);
		}
		else
		{
		    y1 = y1 + (routeWidth / 2 * oscale);
		    y2 = y2 - (routeWidth / 2 * oscale);
		}
	    }
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
		if ((rtype == TT_SPACE) &&
			((stype == defdata->type) || (defdata->tile == (Tile *)NULL)))
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

	/* For now, placement of a via after a taper rule cancels the	*/
	/* taper rule (see above note about combining layer rules).	*/
	if (sameroute && (defdata->ruleset != NULL))
	{
	    lastruleset = NULL;
	    sameroute = FALSE;
	}
    }

    /* If a non-default rule has changed, then we start a new route */
    if (lastruleset != defdata->ruleset)
    {
	sameroute = FALSE;
	lastruleset = defdata->ruleset;
    }

    /* Determine if we need to write a NEW (type) record.  We do this	*/
    /* if 1) this is the first tile visited (except that we don't	*/
    /* write "NEW"), 2) the current tile doesn't touch the last tile	*/
    /* visited, 3) the current type is not equal to the last type, or	*/
    /* 4) a new non-default rule is needed.				*/

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
    	    sprintf(posstr, "%s_%d_%d", DBPlaneShortName(DBPlane(ttype)),
			rorig.r_xbot, rorig.r_ybot);
    	    he = HashLookOnly(defViaTable, posstr);
	    if (he != NULL)
	    {
	    	lefl = (lefLayer *)HashGetValue(he);
	 	defCheckForBreak(strlen(lefl->canonName) + 2, defdata);
	    	fprintf(f, " %s ", lefl->canonName);
	    }
	    else
	    {
		TxError("Cannot find via name %s in table!\n", posstr);
	    	snprintf(viaName, (size_t)24, "_%.10g_%.10g",
			((float)w * oscale), ((float)h * oscale));
	    	defCheckForBreak(strlen(lefName) + strlen(viaName) + 2, defdata);
	    	fprintf(f, " %s%s ", lefName, viaName);
	    }
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

		rName = defGetType((rtype == TT_SPACE) ? r2type : rtype, NULL,
				LAYER_MAP_VIAS);

		/* The first layer in a record may not be a via name */

	        defCheckForBreak(strlen(rName) + 1, defdata);
		fprintf(f, "%s ", rName);
		if (defdata->ruleset != NULL)
		    fprintf(f, "TAPERRULE %s ", defdata->ruleset->name);
		if (defdata->specialmode != DO_REGULAR)
		    defWriteRouteWidth(defdata, routeWidth);
		defWriteCoord(defdata, x1, y1, GEO_CENTER);

    		sprintf(posstr, "%s_%d_%d", DBPlaneShortName(DBPlane(ttype)),
			rorig.r_xbot, rorig.r_ybot);
    		he = HashLookOnly(defViaTable, posstr);
		if (he != NULL)
		{
		    lefl = (lefLayer *)HashGetValue(he);
	 	    defCheckForBreak(strlen(lefl->canonName) + 2, defdata);
	    	    fprintf(f, " %s ", lefl->canonName);
	        }
		else
		{
		    TxError("Cannot find via name %s in table!\n", posstr);
		    snprintf(viaName, (size_t)24, "_%.10g_%.10g",
				((float)w * oscale), ((float)h * oscale));
	 	    defCheckForBreak(strlen(lefName) + strlen(viaName) + 2, defdata);
		    fprintf(f, " %s%s ", lefName, viaName);
		}
	    }
	    else
	    {
	        defCheckForBreak(strlen(lefName) + 1, defdata);
		fprintf(f, "%s ", lefName);
		if (defdata->ruleset != NULL)
		    fprintf(f, "TAPERRULE %s ", defdata->ruleset->name);
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
defCountVias(rootDef, MagicToLefTable, defViaTable, oscale)
    CellDef *rootDef;
    LefMapping *MagicToLefTable;
    HashTable *defViaTable;
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
    cviadata.defViaTable = defViaTable;
    cviadata.def = rootDef;

    for (pNum = PL_SELECTBASE; pNum < DBNumPlanes; pNum++)
    {
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
	cviadata.plane = pNum;

	DBSrPaintArea((Tile *)NULL, rootDef->cd_planes[pNum],
			&TiPlaneRect, &contactMask,
			defCountViaFunc, (ClientData)&cviadata);
    }
    return cviadata.total;
}

/* Simple callback function used by defCountViaFunc */

int
defCheckFunc(tile)
    Tile *tile;
{
    return 1;
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
    char *lname, vname[100], *vp, posstr[24];
    Rect r, r2, rorig;
    int w, h, offx, offy, sdist, lorient, horient;
    int ldist, hdist, sldist, shdist, pNum;
    TileType ltype, htype;
    float oscale = cviadata->scale;
    lefLayer *lefl;
    LinkedRect *newlr;
    CellDef *def = cviadata->def;
    HashEntry *he;
    HashTable *defViaTable = cviadata->defViaTable;
    LefMapping *MagicToLefTable = cviadata->MagicToLefTbl;

    /* Techfiles are allowed not to declare a LEF entry, in which */
    /* case we would need to initialize the hash table.		  */
    if (LefInfo.ht_table == (HashEntry **) NULL) LefTechInit();

    /* If type is a stacked contact, find the residue on the search plane */
    if (ttype >= DBNumUserLayers)
    {
	rmask = DBResidueMask(ttype);
	for (ctype = TT_TECHDEPBASE; ctype < DBNumUserLayers; ctype++)
	    if (TTMaskHasType(rmask, ctype))
		if (DBPlane(ctype) == cviadata->plane)
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
    if (lname == NULL) return 0;    /* Do not output undefined LEF layers */
    TiToRect(tile, &r);

    /* Boundary search.  WARNING:  This code is quite naive.  The	*/
    /* assumption is that all contacts are rectangular, and therefore	*/
    /* any contact area consisting of multiple tiles must be an amalgam	*/
    /* of regular and/or stacked types.  This whole thing should be	*/
    /* replaced by calls to generate layers via the CIF/Calma code.	*/

    /* If any matching tile exists to left or bottom, then return	*/
    /* immediately.  Only expand areas for which this is the bottom and	*/
    /* left-most contact tile.						*/

    /* Left */
    for (tp = BL(tile); BOTTOM(tp) < TOP(tile); tp = RT(tp))
    {
	rtype = TiGetRightType(tp);
	if (rtype == ctype)
	    return 0;
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
	    return 0;
	else if (rtype >= DBNumUserLayers)
	{
	    rmask2 = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask2, ctype))
		return 0;
	}
    }

    /* Expand to top and right until the whole contact area has been found */

    /* Top */
    for (tp = RT(tile); ; tp = RT(tp))
    {
	rtype = TiGetBottomType(tp);
	if (rtype == ctype)
	{
	    TiToRect(tp, &r2);
	    GeoInclude(&r2, &r);
	}
	else if (rtype >= DBNumUserLayers)
	{
	    rmask2 = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask2, ctype))
	    {
	    	TiToRect(tp, &r2);
	    	GeoInclude(&r2, &r);
	    }
	    else break;
	}
	else break;
    }

    /* Right */
    for (tp = TR(tile); ; tp = TR(tp))
    {
	rtype = TiGetLeftType(tp);
	if (rtype == ctype)
	{
	    TiToRect(tp, &r2);
	    GeoInclude(&r2, &r);
	}
	else if (rtype >= DBNumUserLayers)
	{
	    rmask2 = DBResidueMask(rtype);
	    if (TTMaskHasType(rmask2, ctype))
	    {
		TiToRect(tp, &r2);
		GeoInclude(&r2, &r);
	    }
	    else break;
	}
	else break;
    }

    /* All values for the via rect are in 1/2 lambda to account */
    /* for a centerpoint not on the internal grid.		*/

    rorig = r;
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

    /* If the via type has directional surround rules, then determine	*/
    /* the orientation of the lower and upper metal layers and add a	*/
    /* suffix to the via name.						*/

    rmask = DBResidueMask(ctype);
    ldist = hdist = 0;
    ltype = htype = TT_SPACE;
    for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
	if (TTMaskHasType(rmask, rtype))
	{
	    sdist = DRCGetDefaultLayerSurround(ctype, rtype);
	    if (ltype == TT_SPACE)
		sldist = sdist;
	    else
		shdist = sdist;

	    sdist = DRCGetDirectionalLayerSurround(ctype, rtype);
	    if (ltype == TT_SPACE)
	    {
		ldist = sdist;
		ltype = rtype;
	    }
	    else
	    {
		hdist = sdist;
		htype = rtype;
		break;
	    }
	}
    lorient = horient = 0;
    if (ldist > 0)
    {
	r2.r_ybot = rorig.r_ybot - sldist;
	r2.r_ytop = rorig.r_ytop + sldist;
	r2.r_xbot = rorig.r_xbot - ldist + sldist;
	r2.r_xtop = rorig.r_xtop + ldist + sldist;
	pNum = DBPlane(ltype);
	lorient = DBSrPaintArea((Tile *)NULL, def->cd_planes[pNum], &r2,
			&DBNotConnectTbl[ltype], defCheckFunc,
			(ClientData)NULL);
    }
    if (hdist > 0)
    {
	r2.r_ybot = rorig.r_ybot - shdist;
	r2.r_ytop = rorig.r_ytop + shdist;
	r2.r_xbot = rorig.r_xbot - hdist + shdist;
	r2.r_xtop = rorig.r_xtop + hdist + shdist;
	pNum = DBPlane(htype);
	horient = DBSrPaintArea((Tile *)NULL, def->cd_planes[pNum], &r2,
			&DBNotConnectTbl[htype], defCheckFunc,
			(ClientData)NULL);
    }
    if ((ldist > 0) || (hdist > 0))
    {
	sprintf(vname, "%s_%.10g_%.10g_%c%c", lname,
			((float)offx * oscale), ((float)offy * oscale),
			(ldist == 0) ? 'x' : (lorient == 0) ? 'h' : 'v',
			(hdist == 0) ? 'x' : (horient == 0) ? 'h' : 'v');
    }
    else
	sprintf(vname, "%s_%.10g_%.10g", lname,
			((float)offx * oscale), ((float)offy * oscale));

    he = HashFind(&LefInfo, vname);
    lefl = (lefLayer *)HashGetValue(he);
    if (lefl == NULL)
    {
	cviadata->total++;	/* Increment the count of uses */
	lefl = (lefLayer *)mallocMagic(sizeof(lefLayer));
	lefl->type = ctype;
	lefl->obsType = -1;
	lefl->lefClass = CLASS_VIA;
	lefl->info.via.area = r;
	lefl->info.via.cell = (CellDef *)NULL;
	lefl->info.via.lr = (LinkedRect *)NULL;
	lefl->refCnt = 0;	/* These entries will be removed after writing */
	HashSetValue(he, lefl);
	lefl->canonName = (char *)he->h_key.h_name;

	if ((sldist > 0) || (ldist > 0))
	{
	    newlr = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
	    newlr->r_next = lefl->info.via.lr;
	    lefl->info.via.lr = newlr;
	    newlr->r_type = ltype;
	    r2.r_xbot = r.r_xbot - 2 * sldist;
	    r2.r_xtop = r.r_xtop + 2 * sldist;
	    r2.r_ybot = r.r_ybot - 2 * sldist;
	    r2.r_ytop = r.r_ytop + 2 * sldist;
	    if (ldist > 0)
	    {
		if (lorient == 0)
		{
		    r2.r_xbot -= 2 * ldist;
		    r2.r_xtop += 2 * ldist;
		}
		else
		{
		    r2.r_ybot -= 2 * ldist;
		    r2.r_ytop += 2 * ldist;
		}
	    }
	    newlr->r_r = r2;
	}
	if ((shdist > 0) || (hdist > 0))
	{
	    newlr = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
	    newlr->r_next = lefl->info.via.lr;
	    lefl->info.via.lr = newlr;
	    newlr->r_type = htype;
	    r2.r_xbot = r.r_xbot - 2 * shdist;
	    r2.r_xtop = r.r_xtop + 2 * shdist;
	    r2.r_ybot = r.r_ybot - 2 * shdist;
	    r2.r_ytop = r.r_ytop + 2 * shdist;
	    if (hdist > 0)
	    {
		if (horient == 0)
		{
		    r2.r_xbot -= 2 * hdist;
		    r2.r_xtop += 2 * hdist;
		}
		else
		{
		    r2.r_ybot -= 2 * hdist;
		    r2.r_ytop += 2 * hdist;
		}
	    }
	    newlr->r_r = r2;
	}
    }
    /* Record this tile position in the contact hash table */
    sprintf(posstr, "%s_%d_%d", DBPlaneShortName(DBPlane(ctype)),
		rorig.r_xbot, rorig.r_ybot);
    he = HashFind(defViaTable, posstr);
    HashSetValue(he, lefl);

    /* TxPrintf("Via name \"%s\" hashed as \"%s\"\n", lefl->canonName, posstr); */

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
defGetType(ttype, lefptr, do_vias)
    TileType ttype;
    lefLayer **lefptr;
    bool do_vias;
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
	    if (lefl && (do_vias == FALSE) && (contact == CLASS_VIA) &&
			(lefl->lefClass == CLASS_VIA))
		continue;	/* Skip VIA definitions if do_vias is FALSE */
	    
	    if (lefl && ((contact == lefl->lefClass) ||
			((contact == CLASS_ROUTE) && (lefl->lefClass == CLASS_MASTER))))
		if ((lefl->type == ttype) || (lefl->obsType == ttype))
		{
		    if (lefptr) *lefptr = lefl;
		    return lefl->canonName;
		}
	}
    }

    /* If we got here, there is no entry;  return NULL. */
    if (lefptr) *lefptr = (lefLayer *)NULL;
    return NULL;
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
    Rect *r;
    LinkedRect *lr;
    float cscale;

    /* Pick up information from the LefInfo hash table	*/
    /* created by fucntion defCountVias()		*/

    if (LefInfo.ht_table != (HashEntry **) NULL)
    {
	cscale = CIFGetOutputScale(1);

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
		    {
			r = &lefl->info.via.area;

			/* If an lr entry was made, then it includes	*/
			/* any required surround distance, so use that	*/
			/* rectangle instead of the via area.		*/

			for (lr =lefl->info.via.lr; lr; lr = lr->r_next)
			    if (lr->r_type == ttype)
				r = &lr->r_r;

			fprintf(f, "\n      + RECT %s ( %.10g %.10g ) ( %.10g %.10g )",
				lefMagicToLefLayer[ttype].lefName,
				(float)(r->r_xbot) * oscale / 2,
				(float)(r->r_ybot) * oscale / 2,
				(float)(r->r_xtop) * oscale / 2,
				(float)(r->r_ytop) * oscale / 2);
		    }

		/* Handle the contact cuts.	*/

		if (CIFGetContactSize(lefl->type, &size, &sep, &border))
		{
		    int i, j, nAc, nUp, pitch, left;
		    Rect square, rect = lefl->info.via.area, *r;
		    r = &rect;

		    /* Scale contact dimensions to the output units */
		    size *= oscale;
		    sep *= oscale;
		    border *= oscale;

		    size /= cscale;
		    sep /= cscale;
		    border /= cscale;

		    /* Scale the area to output units */
		    r->r_xbot *= oscale;
		    r->r_ybot *= oscale;
		    r->r_xtop *= oscale;
		    r->r_ytop *= oscale;

		    r->r_xbot /= 2;
		    r->r_ybot /= 2;
		    r->r_xtop /= 2;
		    r->r_ytop /= 2;

		    pitch = size + sep;
		    nAc = (r->r_xtop - r->r_xbot + sep - (2 * border)) / pitch;
		    if (nAc == 0)
		    {
			left = (r->r_xbot + r->r_xtop - size) / 2;
			nAc = 1;
			if (left < r->r_xbot)
			{
			    TxError("Warning: via size is %d but area width is %d!\n",
					size, (r->r_xtop - r->r_xbot));
			}
		    }
		    else
			left = (r->r_xbot + r->r_xtop + sep - (nAc * pitch)) / 2;

		    nUp = (r->r_ytop - r->r_ybot + sep - (2 * border)) / pitch;
		    if (nUp == 0)
		    {
			square.r_ybot = (r->r_ybot + r->r_ytop - size) / 2;
			nUp = 1;
			if (square.r_ybot >= r->r_ybot)
			{
			    TxError("Warning: via size is %d but area height is %d!\n",
					size, (r->r_ytop - r->r_ybot));
			}
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

			     fprintf(f, "\n      + RECT %s ( %.10g %.10g ) "
					"( %.10g %.10g )",
					lefMagicToLefLayer[lefl->type].lefName,
					(float)(square.r_xbot),
					(float)(square.r_ybot),
					(float)(square.r_xtop),
					(float)(square.r_ytop));
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

    /* Make sure that arrays are counted correctly */
    int sx = cellUse->cu_xhi - cellUse->cu_xlo + 1;
    int sy = cellUse->cu_yhi - cellUse->cu_ylo + 1;
    // TxPrintf("Diagnostic: cell %s %d %d\n", cellUse->cu_id, sx, sy);
    ASSERT(sx >= 0 && sy >= 0, "Valid array");

    (*total) += sx * sy;	/* Increment the count of uses */

    return 0;	/* Keep the search going */
}

/*
 *------------------------------------------------------------
 *
 * defCountPins --
 *
 *	First-pass function to count the number of pins
 *	to be written to the DEF output file.
 *
 * Results:
 *	The total number of pins to be written.
 *
 * Side Effects:
 *	None.
 *
 *------------------------------------------------------------
 */

int
defCountPins(rootDef)
    CellDef *rootDef;
{
    int total;
    Label *lab;

    TxPrintf("Diagnostic:  Finding all pins of cell %s\n", rootDef->cd_name);

    total = 0;
    for (lab = rootDef->cd_labels; lab; lab = lab->lab_next)
	if (lab->lab_flags & PORT_DIR_MASK)
	    total++;

    return total;
}

/*
 *------------------------------------------------------------
 *
 * defWritePins --
 *
 *	Output the PINS section of the DEF file.  This
 *	is a listing of all ports, their placement, and
 *	name.
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
defWritePins(f, rootDef, lefMagicToLefLayer, oscale)
    FILE *f;				/* File to write to */
    CellDef *rootDef;			/* Cell definition to use */
    LefMapping *lefMagicToLefLayer;	/* Magic to LEF layer name mapping */
    float oscale;			/* Output scale factor */
{
    Label *lab;
    int lwidth, lheight;
    int dcenterx, dcentery;

    for (lab = rootDef->cd_labels; lab; lab = lab->lab_next)
    {
	if (lab->lab_flags & PORT_DIR_MASK)
	{
	    fprintf(f, "   - %s + NET %s\n", lab->lab_text, lab->lab_text);
	    if (lab->lab_flags & PORT_CLASS_MASK)
	    {
		fprintf(f, "     + DIRECTION ");
		switch (lab->lab_flags & PORT_CLASS_MASK)
		{
		    case PORT_CLASS_INPUT:
			fprintf(f, "INPUT");
			break;
		    case PORT_CLASS_OUTPUT:
			fprintf(f, "OUTPUT");
			break;
		    case PORT_CLASS_TRISTATE:
		    case PORT_CLASS_BIDIRECTIONAL:
			fprintf(f, "INOUT");
			break;
		    case PORT_CLASS_FEEDTHROUGH:
			fprintf(f, "FEEDTHRU");
			break;
		}
		fprintf(f, "\n");
	    }
	    if (lab->lab_flags & PORT_USE_MASK)
	    {
		fprintf(f, "     + USE ");
		switch (lab->lab_flags & PORT_USE_MASK)
		{
		    case PORT_USE_SIGNAL:
			fprintf(f, "SIGNAL");
			break;
		    case PORT_USE_ANALOG:
			fprintf(f, "ANALOG");
			break;
		    case PORT_USE_POWER:
			fprintf(f, "POWER");
			break;
		    case PORT_USE_GROUND:
			fprintf(f, "GROUND");
			break;
		    case PORT_USE_CLOCK:
			fprintf(f, "CLOCK");
			break;
		}
		fprintf(f, "\n");
	    }

	    lwidth = lab->lab_rect.r_xtop - lab->lab_rect.r_xbot;
	    lheight = lab->lab_rect.r_ytop - lab->lab_rect.r_ybot;

	    dcenterx = lab->lab_rect.r_xtop + lab->lab_rect.r_xbot;
	    dcentery = lab->lab_rect.r_ytop + lab->lab_rect.r_ybot;

	    fprintf(f, "     + PORT\n");
    	    if (lefMagicToLefLayer[lab->lab_type].lefName == NULL)
		TxError("No LEF layer corresponding to layer %s of pin \"%s\".\n",
			lab->lab_text,
			DBTypeLongNameTbl[lab->lab_type]);
	    else
		fprintf(f, "        + LAYER %s ( %.10g %.10g ) ( %.10g %.10g )",
    	    	    	lefMagicToLefLayer[lab->lab_type].lefName,
		    	oscale * (float)(-lwidth) / 2.0, oscale * (float)(-lheight) / 2.0,
		    	oscale * (float)lwidth / 2.0, oscale * (float)lheight / 2.0);

	    fprintf(f, "        + PLACED ( %.10g %.10g ) N ;\n",
			oscale * (float)dcenterx / 2.0, oscale * (float)dcentery / 2.0);
	}
    }
}

/*
 *------------------------------------------------------------
 *
 * defWriteBlockages --
 *
 *	Output the BLOCKAGES section of a DEF file.  Write
 *	geometry for any layer that is defined as an
 *	obstruction layer in the technology file "lef"
 *	section.
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
defWriteBlockages(f, rootDef, oscale, MagicToLefTable)
    FILE *f;				/* File to write to */
    CellDef *rootDef;			/* Cell definition to use */
    float oscale;			/* Output scale factor */
    LefMapping *MagicToLefTable;	/* Magic to LEF layer mapping */
{
    DefObsData defobsdata;
    lefLayer *lefl;
    int i, numblocks, nonempty, pNum;
    LinkedRect *lr;
    HashSearch hs;
    HashEntry *he;
    TileTypeBitMask ExtraObsLayersMask;

    int defSimpleBlockageFunc();	/* Forward declaration */
    int defblockageVisit();

    defobsdata.def = rootDef;
    defobsdata.nlayers = 0;

    TTMaskZero(&ExtraObsLayersMask);

    /* Blockages are done by layer.  Create one blockage per route	*/
    /* layer, and ignore vias.						*/

    numblocks = 0;
    if (LefInfo.ht_table != (HashEntry **) NULL)
    {
	HashStartSearch(&hs);
	while (he = HashNext(&LefInfo, &hs))
	{
	    lefl = (lefLayer *)HashGetValue(he);
	    if (lefl != NULL)
		if ((lefl->lefClass == CLASS_ROUTE) || (lefl->lefClass == CLASS_VIA))
		    numblocks++;
	}

	defobsdata.nlayers = numblocks;
	defobsdata.blockMasks = (TileTypeBitMask *)mallocMagic(numblocks *
			sizeof(TileTypeBitMask));
	defobsdata.blockData = (LinkedRect **)mallocMagic(numblocks *
			sizeof(LinkedRect *));
	defobsdata.baseNames = (char **)mallocMagic(numblocks *
			sizeof(char *));

	if (numblocks > 0)
	{
	    numblocks = 0;
	    HashStartSearch(&hs);
	    while (he = HashNext(&LefInfo, &hs))
	    {
		lefl = (lefLayer *)HashGetValue(he);
		if ((lefl != NULL) && ((lefl->lefClass == CLASS_ROUTE) ||
				(lefl->lefClass == CLASS_VIA)))
		{
		    char *llayer;
		    if (lefl->lefClass == CLASS_ROUTE)
			llayer = lefl->canonName;
		    else
			llayer = MagicToLefTable[lefl->type].lefName;

		    defobsdata.baseNames[numblocks] = llayer;
		    TTMaskSetOnlyType(&defobsdata.blockMasks[numblocks], lefl->type);
		    if (lefl->obsType != -1)
		    {
		    	TTMaskSetType(&defobsdata.blockMasks[numblocks], lefl->obsType);
			/* If the obstruction type is not an active electrical
			 * type, then add it to the mask of layers to search
			 * separately from extracted nodes.
			 */
			if (!TTMaskHasType(&ExtCurStyle->exts_activeTypes,
					lefl->obsType))
			    TTMaskSetType(&ExtraObsLayersMask, lefl->obsType);
		    }
		    defobsdata.blockData[numblocks] = NULL;
		    numblocks++;
		}
	    }
	}
    }
    if (numblocks > 0)
	EFVisitNodes(defblockageVisit, (ClientData)&defobsdata);

    /* If obstruction types are marked as non-electrical layers (which
     * normally they are), then they will not be extracted as nodes.
     * Do a separate search on obstruction areas and add them to the
     * list of blockages.  To be considered, the layer must have a
     * corresponding LEF route or via layer type, that LEF record must
     * define an obstruction type, and that obstruction type must not
     * be in the active layer list.
     */

    for (pNum = PL_SELECTBASE; pNum < DBNumPlanes; pNum++)
    {
    	DBSrPaintArea((Tile *)NULL, rootDef->cd_planes[pNum], &TiPlaneRect,
		&ExtraObsLayersMask, defSimpleBlockageFunc,
		(ClientData)&defobsdata);
    }

    /* Quick check for presence of data to write */
 
    nonempty = 0;
    for (i = 0; i < numblocks; i++)
	if (defobsdata.blockData[i] != NULL)
	    nonempty++;

    if (nonempty > 0)
    {
    	fprintf(f, "BLOCKAGES %d ;\n", nonempty);

    	for (i = 0; i < numblocks; i++)
    	{
	    if (defobsdata.blockData[i] == NULL) continue;
	    fprintf(f, "   - LAYER %s", defobsdata.baseNames[i]);
	    for (lr = defobsdata.blockData[i]; lr; lr = lr->r_next)
	    {
	    	fprintf(f, "\n      RECT ( %.10g %.10g ) ( %.10g %.10g )",
				(float)(lr->r_r.r_xbot * oscale),
				(float)(lr->r_r.r_ybot * oscale),
				(float)(lr->r_r.r_xtop * oscale),
				(float)(lr->r_r.r_ytop * oscale));
	    	freeMagic(lr);
	    }
            fprintf(f, " ;\n");
    	}
    	fprintf(f, "END BLOCKAGES\n\n");
    }
    freeMagic(defobsdata.blockData);
    freeMagic(defobsdata.blockMasks);
    freeMagic(defobsdata.baseNames);

}

int
defblockageVisit(node, res, cap, defobsdata)
    EFNode *node;
    int res;
    EFCapValue cap;
    DefObsData *defobsdata;
{
    CellDef *def = defobsdata->def;
    TileType magictype;
    TileTypeBitMask tmask;
    int defBlockageGeometryFunc();	/* Forward declaration */

    /* For regular nets, only count those nodes having port	*/
    /* connections.  For special nets, only count those nodes	*/
    /* that were marked with the EF_SPECIAL flag while counting	*/
    /* nets.							*/

    if ((node->efnode_flags & EF_PORT) || (node->efnode_flags & EF_SPECIAL))
	return 0;

    magictype = DBTechNameType(EFLayerNames[node->efnode_type]);
    TTMaskZero(&tmask);
    TTMaskSetMask(&tmask, &DBConnectTbl[magictype]);

    /* Avoid attempting to extract an implicit substrate into DEF */
    if (node->efnode_type == TT_SPACE) return 0;

    DBSrConnect(def, &node->efnode_loc, &tmask, DBConnectTbl,
		&TiPlaneRect, defBlockageGeometryFunc,
		(ClientData)defobsdata);

    return 0;
}

/* Callback function for generating a linked list of blockage geometry	*/
/* for a net.								*/

int
defBlockageGeometryFunc(tile, plane, defobsdata)
    Tile *tile;			/* Tile being visited */
    int plane;			/* Plane of the tile being visited */
    DefObsData *defobsdata;	/* Data passed to this function */
{
    TileType ttype = TiGetTypeExact(tile);
    TileType loctype;
    Rect r;
    LinkedRect *lr;
    int i;

    if (IsSplit(tile))
	loctype = (ttype & TT_SIDE) ? SplitRightType(tile) : SplitLeftType(tile); 
    else
	loctype = ttype;

    if (loctype == TT_SPACE) return 0;

    /* Dissolve stacked contacts */

    if (loctype >= DBNumUserLayers)
    {
	TileTypeBitMask *rMask;
	TileType rtype;

	rMask = DBResidueMask(loctype);
	for (rtype = TT_TECHDEPBASE; rtype < DBNumUserLayers; rtype++)
	    if (TTMaskHasType(rMask, rtype))
		if (DBPlane(rtype) == plane)
		{
		    loctype = rtype;
		    break;
		}
	if (rtype == DBNumUserLayers)
	    return 0;
    }

    for (i = 0; i < defobsdata->nlayers; i++)
	if (TTMaskHasType(&(defobsdata->blockMasks[i]), loctype))
	    break;

    if (i < defobsdata->nlayers)
    {
    	TiToRect(tile, &r);

	lr = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
	lr->r_next = defobsdata->blockData[i];
	lr->r_type = loctype;
	lr->r_r = r;
	defobsdata->blockData[i] = lr;
    }

    return 0;
}

/* Callback function for generating a linked list of blockage geometry	*/
/* pulled from all non-electrical obstruction types.			*/

int
defSimpleBlockageFunc(tile, defobsdata)
    Tile *tile;			/* Tile being visited */
    DefObsData *defobsdata;	/* Data passed to this function */
{
    TileType ttype = TiGetTypeExact(tile);
    TileType loctype;
    Rect r;
    LinkedRect *lr;
    int i;

    if (IsSplit(tile))
	loctype = (ttype & TT_SIDE) ? SplitRightType(tile) : SplitLeftType(tile); 
    else
	loctype = ttype;

    if (loctype == TT_SPACE) return 0;

    for (i = 0; i < defobsdata->nlayers; i++)
	if (TTMaskHasType(&(defobsdata->blockMasks[i]), loctype))
	    break;

    if (i < defobsdata->nlayers)
    {
    	TiToRect(tile, &r);

	lr = (LinkedRect *)mallocMagic(sizeof(LinkedRect));
	lr->r_next = defobsdata->blockData[i];
	lr->r_type = loctype;
	lr->r_r = r;
	defobsdata->blockData[i] = lr;
    }

    return 0;
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

/* Callback function used by defWriteComponents for array members */

int
arrayDefFunc(use, transform, x, y, defdata)
    CellUse *use;		/* CellUse for array element */
    Transform *transform;	/* Transform from use to parent */
    int x, y;			/* Indices of element */
    DefData *defdata;
{
    int sx = use->cu_xhi - use->cu_xlo;
    int sy = use->cu_yhi - use->cu_ylo;
    char idx[32];
    Rect box, rect, *r, bbrect, defrect;
    int xoff, yoff;

    idx[0] = 0;

    if (sy) sprintf(idx, "%d%s", y, sx ? "," : "");
    if (sx) sprintf(idx + strlen(idx), "%d", x);

    r = &use->cu_def->cd_bbox;

    xoff = yoff = 0;
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

    GeoTransRect(transform, r, &box);

    fprintf(defdata->f, "   - %s[%s] %s\n      + PLACED ( %.10g %.10g ) %s ;\n",
		use->cu_id, idx, use->cu_def->cd_name,
		(float)(box.r_xbot) * defdata->scale,
		(float)(box.r_ybot) * defdata->scale,
		defTransPos(&use->cu_transform));
    return 0;
}

/* Callback function used by defWriteComponents */

int
defComponentFunc(cellUse, defdata)
    CellUse *cellUse;
    DefData *defdata;
{
    FILE *f = defdata->f;
    float oscale = defdata->scale;
    char *nameroot;
    Rect *r, rect, bbrect, defrect;
    int xoff, yoff;

    /* Ignore any cellUse that does not have an identifier string. */
    if (cellUse->cu_id == NULL) return 0;

    if (cellUse->cu_xlo != cellUse->cu_xhi || cellUse->cu_ylo != cellUse->cu_yhi) {
	/* expand the array */
	DBArraySr(cellUse, &cellUse->cu_bbox, arrayDefFunc, defdata);
	return 0;
    }

    /* In case the cd_name contains a path component (it's not supposed to),	*/
    /* remove it.								*/

    nameroot = strrchr(cellUse->cu_def->cd_name, '/');
    if (nameroot != NULL)
	nameroot++;
    else
	nameroot = cellUse->cu_def->cd_name;

    r = &cellUse->cu_def->cd_bbox;

    xoff = yoff = 0;
    if (cellUse->cu_def->cd_flags & CDFIXEDBBOX)
    {
	char *propval;
	bool found;

	propval = DBPropGet(cellUse->cu_def, "FIXED_BBOX", &found);
	if (found)
	{
	    if (sscanf(propval, "%d %d %d %d", &rect.r_xbot, &rect.r_ybot,
			&rect.r_xtop, &rect.r_ytop) == 4)
	    {
		r = &rect;
		GeoTransRect(&cellUse->cu_transform, &rect, &bbrect);
		GeoTransRect(&cellUse->cu_transform, &cellUse->cu_def->cd_bbox, &defrect);
		xoff = bbrect.r_xbot - defrect.r_xbot;
		yoff = bbrect.r_ybot - defrect.r_ybot;
	    }
	}
    }

    fprintf(f, "   - %s %s\n      + PLACED ( %.10g %.10g ) %s ;\n",
		cellUse->cu_id, nameroot,
		(float)(cellUse->cu_bbox.r_xbot - r->r_ll.p_x + xoff)
		* oscale,
		(float)(cellUse->cu_bbox.r_ybot - r->r_ll.p_y + yoff)
		* oscale,
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
defMakeInverseLayerMap(do_vias)
    bool do_vias;
{
    LefMapping *lefMagicToLefLayer;
    lefLayer *lefl;
    TileType i;
    char *lefname;

    lefMagicToLefLayer = (LefMapping *)mallocMagic(DBNumTypes
		* sizeof(LefMapping));
    memset(lefMagicToLefLayer, 0, sizeof(LefMapping) * TT_TECHDEPBASE);
    for (i = TT_TECHDEPBASE; i < DBNumTypes; i++)
    {
	lefname = defGetType(i, &lefl, do_vias);
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
DefWriteCell(def, outName, allSpecial, units, analRetentive)
    CellDef *def;		/* Cell being written */
    char *outName;		/* Name of output file, or NULL. */
    bool allSpecial;		/* Treat all nets as SPECIALNETS? */
    int units;			/* Force units to this value (default 1000) */
    bool analRetentive;		/* Force compatibility with stupid tools */
{
    char *filename, *filename1, *filename2;
    char line[2048];
    FILE *f, *f2;		/* Break output file into parts */
    NetCount nets;
    int total, numrules;
    float scale;
    HashTable defViaTable;

    LefMapping *lefMagicToLefLayer;
    int i;
    lefLayer *lefl;
    HashEntry *he;
    HashSearch hs;

    /* Note that "1" corresponds to "1000" in the header UNITS line,	*/
    /* or units of nanometers.  10 = centimicrons, 1000 = microns.	*/

    scale = CIFGetOutputScale(1000) * units;

    if (!strcmp(def->cd_name, UNNAMED))
    {
	TxError("Please name the cell before generating DEF.\n");
	return;
    }

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
    filename1 = StrDup((char **)NULL, filename);

    defWriteHeader(def, f, scale, units);

    HashInit(&defViaTable, 256, HT_STRINGKEYS);

    lefMagicToLefLayer = defMakeInverseLayerMap(LAYER_MAP_VIAS);

    /* Vias---magic contact areas are reported as vias. */
    total = defCountVias(def, lefMagicToLefLayer, &defViaTable, scale);
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
    total = defCountPins(def);
    fprintf(f, "PINS %d ;\n", total);
    if (total > 0)
	defWritePins(f, def, lefMagicToLefLayer, scale);
    fprintf(f, "END PINS\n\n");

    /* Count the number of nets and "special" nets */
    nets = defCountNets(def, allSpecial);

    /* Not done yet with output, so keep this file open. . . */

    f2 = lefFileOpen(def, outName, ".def_part", "w", &filename);

    if (f2 == NULL)
    {
#ifdef MAGIC_WRAPPER
	TxError("Cannot open output file %s (%s).\n", filename,
		strerror(errno));
#else
	TxError("Cannot open output file: ");
	perror(filename);
#endif
	/* If part 2 cannot be opened, remove part 1 */
	fclose(f);
	unlink(filename1);
        freeMagic(filename1);
	return;
    }
    filename2 = StrDup((char **)NULL, filename);

    /* "Special" nets---nets matching $GND, $VDD, or $globals(*) 	*/
    if (nets.special > 0)
    {
	fprintf(f2, "SPECIALNETS %d ;\n", nets.special);
	defWriteNets(f2, def, scale, lefMagicToLefLayer, &defViaTable,
		(allSpecial) ?  ALL_SPECIAL : DO_SPECIAL);
	fprintf(f2, "END SPECIALNETS\n\n");
    }

    /* "Regular" nets */
    if (nets.regular > 0)
    {
	fprintf(f2, "NETS %d ;\n", nets.regular);
	defWriteNets(f2, def, scale, lefMagicToLefLayer, &defViaTable, DO_REGULAR);
	fprintf(f2, "END NETS\n\n");
    }
    fclose(f2);

    /* Now that nets have been written, the nondefault rules can be generated */

    /* Nondefault rules */
    numrules = LefNonDefaultRules.ht_nEntries;
    if (numrules > 0)
    {
	LefRules *nrules;
	lefRule *rule;

	fprintf(f, "NONDEFAULTRULES %d ;\n", numrules);
	HashStartSearch(&hs);
	while (he = HashNext(&LefNonDefaultRules, &hs))
	{
	    nrules = (LefRules *)HashGetValue(he);
	    fprintf(f, "  - %s", nrules->name);

	    if (analRetentive)
	    {
		/* Some tools can crash or throw an error if all layers
		 * are not represented in the non-default rule, which
		 * is an anal retentive interpretation of the DEF spec.
		 */
		if (LefInfo.ht_table != (HashEntry **)NULL)
		{
		    HashSearch hs2;
		    HashEntry *he2;
		    lefLayer *lefl2;

		    HashStartSearch(&hs2);
		    while (he2 = HashNext(&LefInfo, &hs2))
		    {
			lefl2 = (lefLayer *)HashGetValue(he2);
			if (lefl2->lefClass == CLASS_ROUTE)
			{
			    /* Avoid duplicate entries per route layer */
			    if (lefl2->refCnt < 0) continue;
			    lefl2->refCnt = -lefl2->refCnt;

			    /* Ignore obstruction layers */
			    if (lefl2->type == -1) continue;

			    /* Only output rules here for routing layers that
			     * are not represented in the non-default ruleset.
			     */
	    		    for (rule = nrules->rule; rule; rule = rule->next)
				if (lefl2->type == rule->lefInfo->type)
				    break;
			    if (rule != NULL) continue;
			    fprintf(f, "\n     + LAYER %s WIDTH %.10g",
					lefl2->canonName, 
					(float)(lefl2->info.route.width) * scale);
			}
		    }

		    /* Put the reference counts back to the way they were */
		    HashStartSearch(&hs2);
		    while (he2 = HashNext(&LefInfo, &hs2))
		    {
			lefl2 = (lefLayer *)HashGetValue(he2);
			if (lefl2->refCnt < 0)
			    lefl2->refCnt = -lefl2->refCnt;
		    }
		}
	    }

	    for (rule = nrules->rule; rule; rule = rule->next)
	    {
		fprintf(f, "\n     + LAYER %s WIDTH %.10g",
			nrules->rule->lefInfo->canonName, 
			((float)nrules->rule->width * scale));
		if (nrules->rule->extend > 0)
	    	    fprintf(f, " WIREEXT %.10g", (float)nrules->rule->extend / 2.0);
	    }
	    fprintf(f, " ;\n");
	}
	fprintf(f, "END NONDEFAULTRULES\n\n");
    }

    /* Append contents of file with NETS and SPECIALNETS sections */

    f2 = lefFileOpen(def, outName, ".def_part", "r", &filename);
    if (f2 == NULL)
    {
	/* This should not happen because the file was just written. . . */
#ifdef MAGIC_WRAPPER
	TxError("Cannot open input file %s (%s).\n", filename,
		strerror(errno));
#else
	TxError("Cannot open input file: ");
	perror(filename);
#endif
	/* If part 2 cannot be opened, remove part 1 */
	fclose(f);
	unlink(filename1);
        freeMagic(filename1);
        freeMagic(filename2);
	return;
    }
    while (fgets(line, sizeof line, f2) != NULL) fprintf(f, "%s", line);
    fclose(f2);

    /* Blockages */
    defWriteBlockages(f, def, scale, lefMagicToLefLayer);

    fprintf(f, "END DESIGN\n\n");
    fclose(f);

    /* Remove the temporary file of nets */
    unlink(filename2);

    freeMagic(filename1);
    freeMagic(filename2);

    if (nets.has_nets) {
	EFFlatDone(NULL);
	EFDone(NULL);
    }

    /* To do:  Clean up nondefault rules tables */

    freeMagic((char *)lefMagicToLefLayer);
    HashKill(&defViaTable);
    lefRemoveGeneratedVias();
}

