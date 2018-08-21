/* CIFreadcell.c -
 *
 *	This file contains more routines to parse CIF files.  In
 *	particular, it contains the routines to handle cells,
 *	both definitions and calls, and user-defined features
 *	like labels.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/cif/CIFrdcl.c,v 1.5 2010/08/25 17:33:55 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/undo.h"
#include "database/database.h"
#include "cif/CIFint.h"
#include "cif/CIFread.h"
#include "cif/cif.h"
#include "calma/calma.h"
#include "utils/utils.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "drc/drc.h"

/* The following variable is made available to the outside world,
 * and is the cell definition currently being modified.
 */

CellDef *cifReadCellDef;

/*
 * The following hash table is used internally to keep track of
 * of all the cells we've seen definitions for or calls on.
 * The hash table entries contain pointers to cellDefs, and
 * are indexed by CIF cell number.  If the CDAVAILABLE bit is
 * set it means we've read the cell's contents.  If not set, it
 * means that the cell has been called but not yet defined.
 */
HashTable CifCellTable;

/* The following variable is used to save and restore current
 * paint layer information so that we can resume the correct
 * layer after a subcell definition.
 */

Plane *cifOldReadPlane = NULL;

/* The following boolean is TRUE if a subcell definition is being
 * read.  FALSE means we're working on the EditCell.
 */

bool cifSubcellBeingRead;

/* The following two collections of planes are used to hold CIF
 * information while cells are being read in (one set for the
 * outermost, unnamed cell, and one for the current subcell).
 * When a cell is complete, then geometrical operations are
 * performed on the layers and stuff is painted into Magic.
 */

Plane *cifEditCellPlanes[MAXCIFRLAYERS];
Plane *cifSubcellPlanes[MAXCIFRLAYERS];
Plane **cifCurReadPlanes = cifEditCellPlanes;	/* Set of planes currently
						 * in force.
						 */
TileType cifCurLabelType = TT_SPACE;	/* Magic layer on which to put '94'
					 * labels that aren't identified by
					 * type.
					 */

/* Structure used when flattening the CIF hierarchy on read-in */

typedef struct {
    Plane *plane;
    Transform *trans;
} CIFCopyRec;

/* The following variable is used to hold a subcell id between
 * the 91 statement and the (immediately-following?) call statement.
 * The string this points to is dynamically allocated, so it must
 * also be freed explicitly.
 */

char *cifSubcellId = NULL;

/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadCellInit --
 *
 * 	This procedure initializes the data structures in this
 *	module just prior to reading a CIF file.
 *
 *	If ptrkeys is 0, the keys used in this hash table will
 *	be strings; if it is 1, the keys will be CIF numbers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cell hash table is initialized, and things are set up
 *	to put information in the EditCell first.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFReadCellInit(ptrkeys)
    int ptrkeys;
{
    int i;

    HashInit(&CifCellTable, 32, ptrkeys);
    cifReadCellDef = EditCellUse->cu_def;
    cifSubcellBeingRead = FALSE;
    cifCurReadPlanes = cifEditCellPlanes;
    for (i = 0; i < MAXCIFRLAYERS; i += 1)
    {
	if (cifEditCellPlanes[i] == NULL)
	    cifEditCellPlanes[i] = DBNewPlane((ClientData) TT_SPACE);
	if (cifSubcellPlanes[i] == NULL)
	    cifSubcellPlanes[i] = DBNewPlane((ClientData) TT_SPACE);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifForgetCell --
 *
 * 	This local procedure is used to find a cell in the subcell
 *	table and remove its CellDef entry. 
 *
 * Results:
 *	FALSE if no such entry was found, otherwise TRUE.
 *
 * Side effects:
 *	Mucks with the CIF cell name hash table.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifForgetCell(cifNum)
    int cifNum;
{
    HashEntry *h;

    h = HashLookOnly(&CifCellTable, (char *)(spointertype)cifNum);
    if (h == NULL)
	return FALSE;
    else if (HashGetValue(h) == 0)
	return FALSE;

    HashSetValue(h, 0);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifUniqueCell --
 *
 *	Attempt to find a cell in the CIF subcell name hash table.
 *	If one exists, rename its definition so that it will not
 *	be overwritten when the cell is redefined.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
cifUniqueCell(cifNum)
   int cifNum;
{
    HashEntry *h;
    CellDef *def, *testdef;
    char name[17];
    int reused = 0;

    h = HashLookOnly(&CifCellTable, (char *)(spointertype)cifNum);

    if ((h == NULL) || HashGetValue(h) == 0)
    {
	/* Cell was deleted with "DD".  Don't rename anything */
	return;
    }

    sprintf(name, "%d", cifNum);
    def = DBCellLookDef(name);
    if (def == (CellDef *)NULL)
	return;

    /* Cell may have been called but not yet defined---this is okay. */
    else if ((def->cd_flags & CDAVAILABLE) == 0)
        return;

    testdef = def;
    while (testdef != NULL)
    {
	sprintf(name, "%d_%d", cifNum, ++reused);
	testdef = DBCellLookDef(name);
    }
    DBCellRenameDef(def, name);

    h = HashFind(&CifCellTable, (char *)(spointertype)cifNum);
    HashSetValue(h, 0);

    CIFReadError("Warning: cell definition %d reused.\n", cifNum);
}
	
/*
 * ----------------------------------------------------------------------------
 *
 * cifFindCell --
 *
 * 	This local procedure is used to find a cell in the subcell
 *	table, and create a new subcell if there isn't already
 *	one there.  If a new subcell is created, its CDAVAILABLE
 *	is left FALSE.
 *
 * Results:
 *	The return value is a pointer to the definition for the
 *	cell whose CIF number is cifNum.
 *
 * Side effects:
 *	A new CellDef may be created.
 *
 * ----------------------------------------------------------------------------
 */

CellDef *
cifFindCell(cifNum)
    int cifNum;			/* The CIF number of the desired cell. */
{
    HashEntry *h;
    CellDef *def, *testdef;
    int reused;

    h = HashFind(&CifCellTable, (char *)(spointertype)cifNum);

    if (HashGetValue(h) == 0)
    {
	char name[15];

	sprintf(name, "%d", cifNum);
	def = DBCellLookDef(name);
	if (def == NULL)
	{
	    def = DBCellNewDef(name, (char *) NULL);

	    /* Tricky point:  call DBReComputeBbox here to make SURE
	     * that the cell has a valid bounding box.  Otherwise,
	     * if the cell is used in a parent before being defined
	     * then it will cause a core dump.
	     */

	    DBReComputeBbox(def);
	}
	HashSetValue(h, def);
    }
    return (CellDef *) HashGetValue(h);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFScalePlanes --
 *
 *   Scale all of the CIF planes by the amount (scalen / scaled)
 *
 * ----------------------------------------------------------------------------
 */

void
CIFScalePlanes(scalen, scaled, planearray)
    int scalen;
    int scaled;
    Plane **planearray;
{
    int pNum;
    Plane *newplane;

    for (pNum = 0; pNum < MAXCIFRLAYERS; pNum++)
    {
	if (planearray[pNum] != NULL)
	{
	    newplane = DBNewPlane((ClientData) TT_SPACE);
	    DBClearPaintPlane(newplane);
	    dbScalePlane(planearray[pNum], newplane, pNum,
			scalen, scaled, TRUE);
	    DBFreePaintPlane(planearray[pNum]);
	    TiFreePlane(planearray[pNum]);
	    planearray[pNum] = newplane;
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFInputRescale --
 *
 *	Scale all CIF distances by n / d.  Normally, rescaling is done in
 *	the upward direction (n > 1, d = 1) in response to a value in the
 *	CIF input that does not divide evenly into the database units of
 *	the CIF planes.  Currently there is no call to CIFInputRescale
 *	with d > 1, but it is left for the possibility that scalefactors
 *	could be restored after finishing a subcell definition, possibly
 *	preventing integer overflow in large designs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Many.  Adjusts the CIF input style scalefactor (CIF vs. magic
 *	units), multiplier (CIF units vs. centimicrons), distances of all
 *	CIF boolean operations, and the scale of all existing planes in
 *	the current CIF database.
 *
 * ----------------------------------------------------------------------------
 */
 
void
CIFInputRescale(n, d)
    int n, d;
{
    CIFReadStyle *istyle = cifCurReadStyle;
    CIFReadLayer *cl;
    CIFOp *op;
    int i;

    /* 2-step process for efficiency */

    if (n > 1)
    {
	istyle->crs_scaleFactor *= n;
	istyle->crs_multiplier *= n;

	for (i = 0; i < istyle->crs_nLayers; i++)
	{
	    cl = istyle->crs_layers[i];
	    for (op = cl->crl_ops; op != NULL; op = op->co_next)
		if (op->co_distance)
		    op->co_distance *= n;
	}
    }

    if (d > 1)
    {
	istyle->crs_scaleFactor /= d;
	istyle->crs_multiplier /= d;

	for (i = 0; i < istyle->crs_nLayers; i++)
	{
	    cl = istyle->crs_layers[i];
	    for (op = cl->crl_ops; op != NULL; op = op->co_next)
		if (op->co_distance)
		    op->co_distance /= d;
	}
    }

    CIFScalePlanes(n, d, cifEditCellPlanes);
    if (cifEditCellPlanes != cifSubcellPlanes)
	CIFScalePlanes(n, d, cifSubcellPlanes);

    CIFReadWarning("CIF style %s: units rescaled by factor of %d / %d\n",
		istyle->crs_name, n, d);
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseStart --
 *
 * 	Parse the beginning of a symbol (cell) definition.
 *	ds ::= D { blank } S integer [ sep integer sep integer ]
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	Set up information for the new cell, including the CIF
 *	planes and creating a Magic cell (if one doesn't exist
 *	already).
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseStart()
{
    int		number;
    
    if (cifSubcellBeingRead)
    {
	CIFReadError("definition start inside other definition; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    if (cifSubcellId != NULL)
    {
	CIFReadError("pending call identifier %s discarded.\n", cifSubcellId);
	(void) StrDup(&cifSubcellId, (char *) NULL);
    }

    /* Take the `S'. */

    TAKE();
    if (!CIFParseInteger(&number))
    {
	CIFReadError("definition start, but no symbol number; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    else if (number < 0)
    {
	CIFReadError("illegal negative symbol number; definition ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }

    if (!CIFParseInteger(&cifReadScale1))
    {
	cifReadScale1 = 1;
	cifReadScale2 = 1;
    }
    else
    {
	cifReadScale1 *= cifCurReadStyle->crs_multiplier;  /* Units not centimicrons */

	if (!CIFParseInteger(&cifReadScale2))
	{
	    CIFReadError(
	        "only one of two scale factors given; ignored.\n");
	    cifReadScale1 = 1;
	    cifReadScale2 = 1;
	}
    }

    if (cifReadScale1 <= 0 || cifReadScale2 <= 0)
    {
	CIFReadError("Illegal scale %d / %d changed to 1 / 1\n",
		cifReadScale1, cifReadScale2);
	cifReadScale1 = 1;
	cifReadScale2 = 1;
    }


    /*
     * Set up the cell definition.
     */

    cifUniqueCell(number);
    cifReadCellDef = cifFindCell(number);
    DBCellClearDef(cifReadCellDef);
    DBCellSetAvail(cifReadCellDef);

    cifOldReadPlane = cifReadPlane;
    cifReadPlane = (Plane *) NULL;
    cifSubcellBeingRead = TRUE;
    cifCurReadPlanes = cifSubcellPlanes;
    return TRUE;
}


/*
 * ----------------------------------------------------------------------------
 * cifCheckPaintFunc ---
 *
 *	Callback function for checking if any paint has been generated
 *	on the CIF target plane for a "copyup" layer.
 * ----------------------------------------------------------------------------
 */

int cifCheckPaintFunc(tile, clientData)
    Tile *tile;
    ClientData clientData;
{
    return 1;
}

/* Callback function for copying paint from one CIF cell into another */

int
cifCopyPaintFunc(tile, cifCopyRec)
    Tile *tile;
    CIFCopyRec *cifCopyRec;
{
    int pNum;
    TileType dinfo;
    Rect sourceRect, targetRect;
    Transform *trans = cifCopyRec->trans;
    Plane *plane = cifCopyRec->plane;

    dinfo = TiGetTypeExact(tile);

    if (trans)
    {
        TiToRect(tile, &sourceRect);
        GeoTransRect(trans, &sourceRect, &targetRect);
	if (IsSplit(tile))
	    dinfo = DBTransformDiagonal(TiGetTypeExact(tile), trans);
    }
    else
        TiToRect(tile, &targetRect);

    DBNMPaintPlane(plane, dinfo, &targetRect, CIFPaintTable,
                (PaintUndoInfo *)NULL);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFPaintCurrent --
 *
 * 	This procedure does geometrical processing on the current
 *	set of CIF planes, and paints the results into the current
 *	CIF cell.
 *
 * Results:
 *	Return 0
 *
 * Side effects:
 *	Lots of information gets added to the current Magic cell.
 *
 * ----------------------------------------------------------------------------
 */

int
CIFPaintCurrent()
{
    Plane *plane, *swapplane;
    int i;

    for (i = 0; i < cifCurReadStyle->crs_nLayers; i++)
    {
	TileType type;
	extern int cifPaintCurrentFunc();	/* Forward declaration. */
	CIFOp *op;

	plane = CIFGenLayer(cifCurReadStyle->crs_layers[i]->crl_ops,
	    &TiPlaneRect, (CellDef *) NULL, cifCurReadPlanes);
	
	/* Generate a paint/erase table, then paint from the CIF
	 * plane into the current Magic cell.
	 */
	type = cifCurReadStyle->crs_layers[i]->crl_magicType;

	if (cifCurReadStyle->crs_layers[i]->crl_flags & CIFR_TEMPLAYER)
	{
	    op = cifCurReadStyle->crs_layers[i]->crl_ops;
	    while (op)
	    {
		if (op->co_opcode == CIFOP_COPYUP) break;
		op = op->co_next;
	    }

	    /* Quick check to see if anything was generated	*/
	    /* on this layer.					*/

	    if (op && (DBSrPaintArea((Tile *)NULL, plane, &TiPlaneRect,
			&DBAllButSpaceBits, cifCheckPaintFunc,
			(ClientData)NULL) == 1))
	    {
		/* Copy-up function */

		int pNum;
		Plane *newplane;
		Plane **parray;
		extern char *(cifReadLayers[MAXCIFRLAYERS]);

		if (cifReadCellDef->cd_flags & CDFLATGDS)
		    parray = (Plane **)cifReadCellDef->cd_client;
		else
		{
		    parray = (Plane **)mallocMagic(MAXCIFRLAYERS * sizeof(Plane *));
		    cifReadCellDef->cd_flags |= CDFLATGDS;
		    cifReadCellDef->cd_flags &= ~CDFLATTENED;
		    cifReadCellDef->cd_client = (ClientData)parray;
		    for (pNum = 0; pNum < MAXCIFRLAYERS; pNum++)
			parray[pNum] = NULL;
		}

		for (pNum = 0; pNum < MAXCIFRLAYERS; pNum++)
		{
		    if (TTMaskHasType(&op->co_cifMask, pNum))
		    {
			CIFCopyRec cifCopyRec;

			newplane = parray[pNum];
			if (newplane == NULL)
		 	{
			    newplane = DBNewPlane((ClientData) TT_SPACE);
			    DBClearPaintPlane(newplane);
			}

			cifCopyRec.plane = newplane;
			cifCopyRec.trans = NULL;

			DBSrPaintArea((Tile *)NULL, plane, &TiPlaneRect,
				&DBAllButSpaceBits, cifCopyPaintFunc,
				&cifCopyRec);

			parray[pNum] = newplane;
		    }
		}
	    }

	    /* Swap planes */
	    swapplane = cifCurReadPlanes[type];
	    cifCurReadPlanes[type] = plane;
	    plane = swapplane;
	}
	else
	{
	    DBSrPaintArea((Tile *) NULL, plane, &TiPlaneRect,
			&CIFSolidBits, cifPaintCurrentFunc,
			(ClientData)type);
	}
	
	/* Recycle the plane, which was dynamically allocated. */

	DBFreePaintPlane(plane);
	TiFreePlane(plane);
    }

    /* Now go through all the current planes and zero them out. */

    for (i = 0; i < MAXCIFRLAYERS; i++)
	DBClearPaintPlane(cifCurReadPlanes[i]);

    return 0;
}

/* Below is the search function invoked for each CIF tile type
 * found for the current layer.
 */

int
cifPaintCurrentFunc(tile, type)
    Tile *tile;			/* Tile of CIF information. */
    TileType type;		/* Magic type to be painted. */
{
    Rect area;
    int pNum;
    int savescale;
    bool snap_type = COORD_EXACT;

    /* Contact types are allowed to be on half-lambda spacing, and are	*/
    /* snapped to the nearest magic coordinate if the result is a	*/
    /* fractional magic coordinate.					*/

    if (DBIsContact(type)) snap_type = COORD_HALF_U;

    /* Compute the area of the CIF tile, then scale it into
     * Magic coordinates.
     */

    TiToRect(tile, &area);
    area.r_xtop = CIFScaleCoord(area.r_xtop, snap_type);
    savescale = cifCurReadStyle->crs_scaleFactor;
    area.r_ytop = CIFScaleCoord(area.r_ytop, snap_type);
    if (snap_type == COORD_HALF_U) snap_type = COORD_HALF_L;
    if (savescale != cifCurReadStyle->crs_scaleFactor)
    {
	area.r_xtop *= (savescale / cifCurReadStyle->crs_scaleFactor);
	savescale = cifCurReadStyle->crs_scaleFactor;
    }
    area.r_xbot = CIFScaleCoord(area.r_xbot, snap_type);
    if (savescale != cifCurReadStyle->crs_scaleFactor)
    {
	area.r_xtop *= (savescale / cifCurReadStyle->crs_scaleFactor);
	area.r_ytop *= (savescale / cifCurReadStyle->crs_scaleFactor);
	savescale = cifCurReadStyle->crs_scaleFactor;
    }
    area.r_ybot = CIFScaleCoord(area.r_ybot, snap_type);
    if (savescale != cifCurReadStyle->crs_scaleFactor)
    {
	area.r_xtop *= (savescale / cifCurReadStyle->crs_scaleFactor);
	area.r_ytop *= (savescale / cifCurReadStyle->crs_scaleFactor);
	area.r_xbot *= (savescale / cifCurReadStyle->crs_scaleFactor);
    }

    /* Check for degenerate areas (from rescale limiting) before painting */
    if ((area.r_xbot == area.r_xtop) || (area.r_ybot == area.r_ytop))
	return 0;

    for (pNum = PL_PAINTBASE; pNum < DBNumPlanes; pNum++)
	if (DBPaintOnPlane(type, pNum))
	{
	    DBNMPaintPlane(cifReadCellDef->cd_planes[pNum], TiGetTypeExact(tile),
		    &area, DBStdPaintTbl(type, pNum), (PaintUndoInfo *) NULL);
	}

    return  0;		/* To keep the search alive. */
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseFinish --
 *
 * 	This procedure is called at the end of a cell definition.
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	Process the CIF planes and paint the results into the Magic
 *	cell.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseFinish()
{
    if (!cifSubcellBeingRead)
    {
	CIFReadError("definition finish without definition start; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    if (cifSubcellId != NULL) 
    {
	CIFReadError("pending call identifier %s discarded.\n", cifSubcellId);
	(void) StrDup(&cifSubcellId, (char *) NULL);
    }
	
    /* Take the `F'. */

    TAKE();

    /* Do the geometrical processing and paint this material back into
     * the appropriate cell of the database.  Then restore the saved
     * layer info.
     */
    
    CIFPaintCurrent();

    DBAdjustLabels(cifReadCellDef, &TiPlaneRect);
    DBReComputeBbox(cifReadCellDef);
    cifReadCellDef = EditCellUse->cu_def;
    cifReadPlane = cifOldReadPlane;
    cifReadScale1 = 1;
    cifReadScale2 = 1;
    cifSubcellBeingRead = FALSE;
    cifCurReadPlanes = cifEditCellPlanes;
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseDelete --
 *
 * 	This procedure is called to handle delete-symbol statements.
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	The mapping between numbers and cells is modified to eliminate
 *	some symbols.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseDelete()
{
    int		number;

    /* Take the `D'. */

    TAKE();
    if (!CIFParseInteger(&number))
    {
	CIFReadError("definition delete, but no symbol number; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }

    /* Unlink the hash entry from its target definition */
    cifForgetCell(number);

    CIFSkipToSemi();
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifParseName --
 *
 * 	Parse a name, which is a string of alphabetics, numerics,
 *	or underscores, possibly preceded by whitespace.
 *
 * Results:
 *	The return value is a pointer to the name read from the
 *	CIF file.  This is a statically-allocated area, so the
 *	caller should copy out of this area before invoking this
 *	procedure again.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
cifParseName()
{
    char	ch;
    char	*bufferp;
    static char	buffer[128];

    /* Skip white space. */

    for (ch = PEEK() ; ch == ' ' || ch == '\t' ; ch = PEEK())
	TAKE();
    
    /* Read the string. */

    bufferp = &buffer[0];
    for (ch = PEEK() ; (! isspace(ch)) && ch != ';' ; ch = PEEK())
    {
	*bufferp++ = TAKE();
    }
    *bufferp = '\0';
    return buffer;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifParseUser9 --
 *
 * 	This procedure processes user extension 9: the name of the
 *	current symbol (cell) definition.
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	The current CIF symbol is renamed from its default "cifxx" name
 *	to the given name.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifParseUser9()
{
    char *name;

    name = cifParseName();
    if (!DBCellRenameDef(cifReadCellDef, name))
    {
	CIFReadError("%s already exists, so cell from CIF is named %s.\n",
		name, cifReadCellDef->cd_name);
    }
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseCall --
 *
 * 	This procedure processes subcell uses.  The syntax of a call is
 *	call ::= C integer transform
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	A subcell is added to the current Magic cell we're generating.
 *
 * ----------------------------------------------------------------------------
 */

bool
CIFParseCall()
{
    int		called;
    Transform	transform;
    CellUse 	*use;
    CellDef	*def;
    
    /* Take the `C'. */

    TAKE();
    if (!CIFParseInteger(&called))
    {
	CIFReadError("call, but no symbol number; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }

    /* Get optional transformation. */

    (void) CIFParseTransform(&transform);

    def = cifFindCell(called);

    /* avoid recursion
     */

    if (DBIsAncestor(def, cifReadCellDef))
    {
	CIFReadError("attempt to place cell use inside its own definition!\n");
	CIFSkipToSemi();
	return FALSE;
    }

    /* Find the use and add it to the current cell.  Give it an
     * id also.
     */

    use = DBCellNewUse(def, cifSubcellId);
    (void) DBLinkCell(use, cifReadCellDef);
    DBSetTrans(use, &transform);
    DBPlaceCell(use, cifReadCellDef);

    (void) StrDup(&cifSubcellId, (char *) NULL);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifParseUser91 --
 *
 * 	This procedure handles 91 user commands, which provide id's
 *	for following cell calls.  The syntax is:
 *	91 ::= 91 blanks name
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	The identifier is saved until the call is read.  Then it is
 *	used as the identifier for the cell.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifParseUser91()
{
    if (cifSubcellId != NULL)
    {
	CIFReadError("91 command with identifier %s pending; %s discarded.\n" ,
	    cifSubcellId , cifSubcellId);
    }
    (void) StrDup(&cifSubcellId, cifParseName());
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifParseUser94 --
 *
 * 	This procedure parses 94 user commands, which are labelled
 *	points.  The syntax is:
 *	94 ::= 94 blanks name point
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	A label is added to the current cell.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifParseUser94()
{
    Rect rectangle;
    char *name = NULL;
    TileType type;
    int layer, flags, i;
    int savescale;

    (void) StrDup(&name, cifParseName());
    if (! CIFParsePoint(&rectangle.r_ll, 1))
    {
	CIFReadError("94 command, but no location; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }

    /* Scale the coordinates, then make the location into a
     * rectangle.
     */

    rectangle.r_xbot = CIFScaleCoord(rectangle.r_xbot, COORD_ANY);
    savescale = cifCurReadStyle->crs_scaleFactor;
    rectangle.r_ybot = CIFScaleCoord(rectangle.r_ybot, COORD_ANY);
    if (savescale != cifCurReadStyle->crs_scaleFactor)
	rectangle.r_xbot *= (savescale / cifCurReadStyle->crs_scaleFactor);

    rectangle.r_ur = rectangle.r_ll;

    /* Get a layer, lookup the layer, then add the label to the
     * current cell.  Tricky business: in order for the default
     * label location to be computed
     */
    
    CIFSkipBlanks();
    if (PEEK() != ';')
    {
	char *lname = cifParseName();
	layer = CIFReadNameToType(lname, FALSE);
	if (layer < 0)
	{
	    CIFReadError("label attached to unknown layer %s.\n",
		    lname);
	    type = TT_SPACE;
	}
	else {
	    type = cifCurReadStyle->crs_labelLayer[layer];
	}
    } else {
	type = cifCurLabelType;

	/* Should do this better, by defining cifCurLabelFlags. . . */
	layer = -1;
	for (i = 0; i < cifCurReadStyle->crs_nLayers; i++)
	    if (cifCurReadStyle->crs_labelLayer[i] == type) {
		layer = i;
		break;
	    }
    }
    if (type >=0 )
    {
	if (layer >= 0 && cifCurReadStyle->crs_labelSticky[layer])
	    flags = LABEL_STICKY;
	else
	    flags = 0;
    	(void) DBPutLabel(cifReadCellDef, &rectangle, -1, name, type, flags);
    }
    freeMagic(name);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * cifParseUser95 --
 *
 * 	This procedure parses 95 user commands, which are labelled
 *	points.  The syntax is:
 *	95 ::= 95 blanks name length width point
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	An area label is added to the current cell.
 *
 * ----------------------------------------------------------------------------
 */

bool
cifParseUser95()
{
    /* Modified by BIM 1/8/2018 */
    Rect rectangle;
    Point size, center, lowerleft, upperright;
    char *name = NULL;
    TileType type;
    int layer, i;
    int savescale;

    (void) StrDup(&name, cifParseName());

    if (! CIFParsePoint(&size, 1))
    {
	CIFReadError("95 command, but no size; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }
    
    savescale = cifCurReadStyle->crs_scaleFactor;

    /* The center coordinates returned are in CIF units *2              */
    /* the values will be halved later before conversion to magic units */
    
    if (! CIFParsePoint(&center, 2))
    {
	CIFReadError("95 command, but no location; ignored.\n");
	CIFSkipToSemi();
	return FALSE;
    }

    /* If reading the center causes a CIF input scale to be redefined,	*/
    /* then the length and width must also be changed.			*/

    if (savescale != cifCurReadStyle->crs_scaleFactor)
    {
	size.p_x *= (cifCurReadStyle->crs_scaleFactor / savescale);
	size.p_y *= (cifCurReadStyle->crs_scaleFactor / savescale);
    }

    /* Scale the coordinates and create the rectangular area.		*/
    /* Explicitly calculate lowerleft and upperright using CIF units *2 */
    /* so that half-lambda centers are resolved before remapping to	*/
    /* magic coordinates.						*/

    lowerleft.p_x = center.p_x - size.p_x;
    lowerleft.p_y = center.p_y - size.p_y;
    
    upperright.p_x = center.p_x + size.p_x;
    upperright.p_y = center.p_y + size.p_y;
    
    if ((lowerleft.p_x % 2 == 0) && (lowerleft.p_y % 2 == 0)) {

      /* if possible convert values to CIF units by dividing by two */

      lowerleft.p_x /= 2;
      lowerleft.p_y /= 2;

      upperright.p_x /= 2;
      upperright.p_y /= 2;

    } else {

      /* if division by two would create inaccuracy then rescale to accommodate */

      CIFInputRescale(2, 1);

    }
    
    /* now scale each of the co-ordinates in turn */
    
    lowerleft.p_x = CIFScaleCoord(lowerleft.p_x, COORD_ANY);
    savescale = cifCurReadStyle->crs_scaleFactor;

    lowerleft.p_y = CIFScaleCoord(lowerleft.p_y, COORD_ANY);
    if (savescale != cifCurReadStyle->crs_scaleFactor)
    {
	lowerleft.p_x *= (savescale / cifCurReadStyle->crs_scaleFactor);
	savescale = cifCurReadStyle->crs_scaleFactor;
    }

    upperright.p_x = CIFScaleCoord(upperright.p_x, COORD_ANY);
    if (savescale != cifCurReadStyle->crs_scaleFactor)
    {
	lowerleft.p_x *= (savescale / cifCurReadStyle->crs_scaleFactor);
	lowerleft.p_y *= (savescale / cifCurReadStyle->crs_scaleFactor);
	savescale = cifCurReadStyle->crs_scaleFactor;
    }

    upperright.p_y = CIFScaleCoord(upperright.p_y, COORD_ANY);
    if (savescale != cifCurReadStyle->crs_scaleFactor)
    {
	lowerleft.p_x *= (savescale / cifCurReadStyle->crs_scaleFactor);
	lowerleft.p_y *= (savescale / cifCurReadStyle->crs_scaleFactor);
	upperright.p_x *= (savescale / cifCurReadStyle->crs_scaleFactor);
    }

    rectangle.r_xbot = lowerleft.p_x;
    rectangle.r_ybot = lowerleft.p_y;
    rectangle.r_xtop = upperright.p_x;
    rectangle.r_ytop = upperright.p_y;

    /* Get a layer, lookup the layer, then add the label to the
     * current cell.  Tricky business: in order for the default
     * label location to be computed
     */
    CIFSkipBlanks();
    if (PEEK() != ';')
    {
	char *name = cifParseName();
	layer = CIFReadNameToType(name, FALSE);
	if (layer < 0)
	{
	    CIFReadError("label attached to unknown layer %s.\n",
		    name);
	    type = TT_SPACE;
	}
	else type = cifCurReadStyle->crs_labelLayer[layer];
    }
    else {
	type = TT_SPACE;
	layer = -1;
	for (i = 0; i < cifCurReadStyle->crs_nLayers; i++)
	    if (cifCurReadStyle->crs_labelLayer[i] == type) {
		layer = i;
		break;
	    }
    }
    if (type >=0 )
    {
	int flags;
	if (layer >= 0 && cifCurReadStyle->crs_labelSticky[layer])
	    flags = LABEL_STICKY;
	else
	    flags = 0;
    	(void) DBPutLabel(cifReadCellDef, &rectangle, -1, name, type, flags);
    }

    freeMagic(name);
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFParseUser --
 *
 * 	This procedure is called to process user-defined statements.
 *	The syntax is user ::= digit usertext.
 *
 * Results:
 *	TRUE is returned if the parse completed successfully, and
 *	FALSE is returned otherwise.
 *
 * Side effects:
 *	Depends on the user command.
 *
 * ----------------------------------------------------------------------------
 */
bool
CIFParseUser()
{
    char	ch;

    ch = TAKE();
    switch (ch)
    {
	case '9':
		ch = PEEK();
		switch (ch)
		{
		    case '1':
			(void) TAKE();
			return cifParseUser91();
		    case '4':
			(void) TAKE();
			return cifParseUser94();
		    case '5':
			(void) TAKE();
			return cifParseUser95();
		    default:
			if (isspace(ch)) return cifParseUser9();
		}
	default:
		CIFReadError("unimplemented user extension; ignored.\n");
		CIFSkipToSemi();
		return FALSE;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * CIFReadCellCleanup --
 *
 * 	This procedure is called after processing the CIF file.
 *	It performs various cleanup functions on the cells that
 *	have been read in.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The area of each cell is DRC'ed and redisplayed.  Error
 *	messages are output for any cells whose contents weren't
 *	in the CIF file.  An error message is also output if
 *	we're still in the middle of reading a subcell.
 *
 * ----------------------------------------------------------------------------
 */

void
CIFReadCellCleanup(type)
    int type;		 // 0 = CIF, 1 = GDS, because routine is used by both
{
    HashEntry *h;
    HashSearch hs;
    CellDef *def;
    MagWindow *window;
    int flags;

    if (cifSubcellBeingRead)
    {
	if (type == 0)
	    CIFReadError("CIF ended partway through a symbol definition.\n");
	else
	    calmaReadError("GDS ended partway through a symbol definition.\n");
	(void) CIFParseFinish();
    }

    HashStartSearch(&hs);
    while (TRUE)
    {
	h = HashNext(&CifCellTable, &hs);
	if (h == NULL) break;

	def = (CellDef *) HashGetValue(h);
	if (def == NULL)
	{
	    if (type == 0)
		CIFReadError("cell table has NULL entry (Magic error).\n");
	    else
		calmaReadError("cell table has NULL entry (Magic error).\n");
	    continue;
	}
	flags = def->cd_flags;
	if (!(flags & CDAVAILABLE))
	{
	    if (type == 0)
		CIFReadError("cell %s was used but not defined.\n", def->cd_name);
	    else
		calmaReadError("cell %s was used but not defined.\n", def->cd_name);
        }
	def->cd_flags &= ~CDPROCESSEDGDS;

	if ((type == 0 && CIFNoDRCCheck == FALSE) ||
			(type == 1 && CalmaNoDRCCheck == FALSE))
	    DRCCheckThis(def, TT_CHECKPAINT, &def->cd_bbox);
	DBWAreaChanged(def, &def->cd_bbox, DBW_ALLWINDOWS, &DBAllButSpaceBits);
	DBCellSetModified(def, TRUE);
    }

    /* Do geometrical processing on the top-level cell. */

    CIFPaintCurrent();
    DBAdjustLabels(EditCellUse->cu_def, &TiPlaneRect);
    DBReComputeBbox(EditCellUse->cu_def);
    DBWAreaChanged(EditCellUse->cu_def, &EditCellUse->cu_def->cd_bbox,
	    DBW_ALLWINDOWS, &DBAllButSpaceBits);
    DBCellSetModified(EditCellUse->cu_def, TRUE);

    /* Clean up saved CIF/GDS planes in cd_client records of cells */

    HashStartSearch(&hs);
    while (TRUE)
    {
	h = HashNext(&CifCellTable, &hs);
	if (h == NULL) break;

	def = (CellDef *) HashGetValue(h);
	if (def == NULL) continue;

	if (def->cd_flags & CDFLATGDS)
	{
	    /* These cells have been flattened and are no longer needed. */

	    int pNum;
	    Plane **cifplanes = (Plane **)def->cd_client;

	    UndoDisable();

	    for (pNum = 0; pNum < MAXCIFRLAYERS; pNum++)
	    {
	        if (cifplanes[pNum] != NULL)
		{
		    DBFreePaintPlane(cifplanes[pNum]);
		    TiFreePlane(cifplanes[pNum]);
		}
	    }
	    freeMagic((char *)def->cd_client);
	    def->cd_client = (ClientData)CLIENTDEFAULT;

	    /* If the CDFLATTENED flag was not set, then this geometry	*/
	    /* was never instantiated, and should generate a warning.	*/

	    if (!(def->cd_flags & CDFLATTENED))
		CIFReadError("%s read error:  Unresolved geometry in cell"
			" %s maps to no magic layers\n",
			(type == 0) ? "CIF" : "GDS", def->cd_name);

#if 0
	    /* Remove the cell if it has no parents, no children, and no geometry */
	    /* To-do:  Check that these conditions are valid */

	    if (def->cd_parents == (CellUse *)NULL)
	    {
		char *savename = StrDup((char **)NULL, def->cd_name);

		if (DBCellDeleteDef(def) == FALSE)
		{
		    CIFReadError("%s read error:  Unable to delete cell %s\n",
				(type == 0) ? "CIF" : "GDS", savename);
		}
		else
		{
		    if (type == 0)
			TxPrintf("CIF read:  Removed flattened cell %s\n", savename);
		    else
			TxPrintf("GDS read:  Removed flattened cell %s\n", savename);
		}
		freeMagic(savename);
	    }
#endif
	    UndoEnable();
	}
    }
    HashKill(&CifCellTable);
}
