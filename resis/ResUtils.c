
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResUtils.c,v 1.3 2010/06/24 12:37:56 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "utils/malloc.h"
#include "textio/textio.h"
#include "extract/extract.h"
#include "extract/extractInt.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/stack.h"
#include "utils/tech.h"
#include "textio/txcommands.h"
#include "resis/resis.h"


/*
 * ---------------------------------------------------------------------------
 *
 * ResFirst -- Checks to see if tile is a contact. If it is, allocate a
 *	contact structure.
 *
 *
 * Results: Always returns NULL (in the form of a Region pointer)
 *
 * Side effects:
 *	Memory is allocated by ResFirst.
 *	We cons the newly allocated region onto the front of the existing
 *	region list.
 *
 *
 * -------------------------------------------------------------------------
 */

ExtRegion *
ResFirst(tile, arg)
    Tile *tile;
    FindRegion *arg;
{
    ResContactPoint *reg;
    TileType t;
    int i;

    if (IsSplit(tile))
    {
	t = (SplitSide(tile)) ? SplitRightType(tile) : SplitLeftType(tile);
    }
    else
	t = TiGetType(tile);

    if (DBIsContact(t))
    {
	reg = (ResContactPoint *) mallocMagic((unsigned) (sizeof(ResContactPoint)));
	reg->cp_center.p_x = (LEFT(tile) + RIGHT(tile)) >> 1;
	reg->cp_center.p_y = (TOP(tile) + BOTTOM(tile)) >> 1;
	reg->cp_status = FALSE;
	reg->cp_type = t;
	reg->cp_width = RIGHT(tile) - LEFT(tile);
	reg->cp_height = TOP(tile) - BOTTOM(tile);
	for (i = 0; i < LAYERS_PER_CONTACT; i++)
	{
	    reg->cp_tile[i] = (Tile *) NULL;
	    reg->cp_cnode[i] = (resNode *) NULL;
	}
	reg->cp_currentcontact = 0;
	reg->cp_rect.r_ll.p_x = tile->ti_ll.p_x;
	reg->cp_rect.r_ll.p_y = tile->ti_ll.p_y;
	reg->cp_rect.r_ur.p_x = RIGHT(tile);
	reg->cp_rect.r_ur.p_y = TOP(tile);
	reg->cp_contactTile = tile;
	/* Prepend it to the region list */
	reg->cp_nextcontact = (ResContactPoint *) arg->fra_region;
	arg->fra_region = (ExtRegion *) reg;
    }
    return((ExtRegion *) NULL);
}

/*
 *--------------------------------------------------------------------------
 *
 * resMultiPlaneTerm --
 *
 * Callback function to set a junk field
 *
 *--------------------------------------------------------------------------
 */

int
resMultiPlaneTerm(Tile *tile, tileJunk *junk2)
{
    tileJunk *Junk;
    
    Junk = resAddField(tile);
    Junk->tj_status |= RES_TILE_SD;
    junk2->sourceEdge |= OTHERPLANE;
    return 0;
}

/*
 *--------------------------------------------------------------------------
 *
 * resSubstrateTerm --
 *
 * Callback function to set a junk field
 *
 *--------------------------------------------------------------------------
 */

int
resSubstrateTerm(Tile *tile)
{
    tileJunk *Junk;
    
    Junk = resAddField(tile);
    Junk->tj_status |= RES_TILE_SUBS;
    return 0;
}

/*
 *--------------------------------------------------------------------------
 *
 * ResEach--
 *
 * ResEach calls ResFirst unless this is the first contact, in which case it
 * has alreay been processed
 *
 * results: returns 0
 *
 * Side Effects: see ResFirst
 *
 * -------------------------------------------------------------------------
 */

int
ResEach(tile, pNum, arg)
    Tile	*tile;
    int		pNum;
    FindRegion	*arg;
{

    if (((ResContactPoint *)(arg->fra_region))->cp_contactTile != tile)
    {
	ResFirst(tile, arg);
    }
    return(0);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResAddPlumbing-- Each tile is a tileJunk structure associated with it
 * to keep track of various things used by the extractor. ResAddPlumbing
 * adds this structure and sets the tile's ClientData field to point to it.
 * If the tile is a device, then a device structure is also added;
 * all connected device tiles are enumerated and their deviceList
 * fields set to the new structure.
 *
 * Results: always returns 0
 *
 * Side Effects:see above
 *
 *-------------------------------------------------------------------------
 */

int
ResAddPlumbing(tile, arg)
    Tile	*tile;
    ClientData	*arg;
{
    tileJunk		*Junk, *junk2;
    static Stack	*resDevStack = NULL;
    TileType		loctype, t1;
    Tile		*tp1, *tp2, *source;
    resDevice		*resDev;
    ExtDevice		*devptr;
    TileTypeBitMask	locDevSubsMask;

    if (resDevStack == NULL)
     	resDevStack = StackNew(64);

    if (tile->ti_client == (ClientData) CLIENTDEFAULT)
    {
	if (IsSplit(tile))
	    loctype = (SplitSide(tile)) ? SplitRightType(tile) :
			SplitLeftType(tile);
	else
	    loctype = TiGetTypeExact(tile);

	devptr = ExtCurStyle->exts_device[loctype];
     	junk2 = resAddField(tile);
	if (TTMaskHasType(&(ExtCurStyle->exts_deviceMask), loctype))
	{
	    int i, nterms, pNum;
	    Rect r;

	    /* Count SD terminals of the device */
	    nterms = 0;
	    for (i = 0;; i++)
	    {
		if (TTMaskIsZero(&(devptr->exts_deviceSDTypes[i]))) break;
		nterms++;
	    }
	    if (nterms < devptr->exts_deviceSDCount)
		nterms = devptr->exts_deviceSDCount;

	    /* resDev terminals includes device identifier (e.g., gate) and
	     * substrate, so add two to nterms.
	     */
	    nterms += 2;

   	    resDev = (resDevice *) mallocMagic((unsigned)(sizeof(resDevice)));
	    resDev->rd_nterms = nterms;
	    resDev->rd_terminals = (resNode **) mallocMagic(nterms * sizeof(resNode *));
	    for (i = 0; i != nterms; i++)
	    	resDev->rd_terminals[i] = (resNode *) NULL;

            resDev->rd_tile = tile;
	    resDev->rd_inside.r_ll.p_x = LEFT(tile);
	    resDev->rd_inside.r_ll.p_y = BOTTOM(tile);
	    resDev->rd_inside.r_ur.p_x = RIGHT(tile);
	    resDev->rd_inside.r_ur.p_y = TOP(tile);
	    resDev->rd_devtype = loctype;
            resDev->rd_tiles = 0;
	    resDev->rd_length = 0;
	    resDev->rd_width = 0;
	    resDev->rd_perim = 0;
	    resDev->rd_area = 0;
	    resDev->rd_status = 0;
            resDev->rd_nextDev = (resDevice *) *arg;
	    *arg = (ClientData)resDev;
	    junk2->deviceList =  resDev;
	    junk2->tj_status |= RES_TILE_DEV;

	    for (i = 0; i < nterms - 2; i++)
	    {
		source = NULL;
		/* find diffusion (if present) to be source contact */

		/* top */
		for (tp2 = RT(tile); RIGHT(tp2) > LEFT(tile); tp2 = BL(tp2))
		{
		    if TTMaskHasType(&(devptr->exts_deviceSDTypes[i]),
				TiGetBottomType(tp2))
		    {
			junk2->sourceEdge |= TOPEDGE;
			source = tp2;
			Junk = resAddField(source);
			Junk->tj_status |= RES_TILE_SD;
			break;
		    }
		}

		/* bottom */
		if (source == NULL)
		for (tp2 = LB(tile); LEFT(tp2) < RIGHT(tile); tp2 = TR(tp2))
		{
		    if TTMaskHasType(&(devptr->exts_deviceSDTypes[i]),
				TiGetTopType(tp2))
		    {
			junk2->sourceEdge |= BOTTOMEDGE;
			source = tp2;
			Junk = resAddField(source);
			Junk->tj_status |= RES_TILE_SD;
			break;
		    }
		}

		/* right */
		if (source == NULL)
		for (tp2 = TR(tile); TOP(tp2) > BOTTOM(tile); tp2 = LB(tp2))
		{
		    if TTMaskHasType(&(devptr->exts_deviceSDTypes[i]),
				TiGetLeftType(tp2))
		    {
			junk2->sourceEdge |= RIGHTEDGE;
			source = tp2;
			Junk = resAddField(source);
			Junk->tj_status |= RES_TILE_SD;
			break;
		    }
		}

		/* left */
		if (source == NULL)
		for (tp2 = BL(tile); BOTTOM(tp2) < TOP(tile); tp2 = RT(tp2))
		{
		    if TTMaskHasType(&(devptr->exts_deviceSDTypes[i]),
				TiGetRightType(tp2))
		    {
			source = tp2;
			Junk = resAddField(source);
			Junk->tj_status |= RES_TILE_SD;
			junk2->sourceEdge |= LEFTEDGE;
			break;
		    }
		}

		/* other plane (in ResUse) */
		if (source == NULL)
		{
		    TiToRect(tile, &r);
		    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
		    {
			if (TTMaskIntersect(&DBPlaneTypes[pNum],
				&(devptr->exts_deviceSDTypes[i])))
			    DBSrPaintArea((Tile *)NULL, 
				    ResUse->cu_def->cd_planes[pNum],
				    &r, &(devptr->exts_deviceSDTypes[i]),
				    resMultiPlaneTerm, (ClientData)junk2);
		    }
		}

		/* We need to know whether a given diffusion tile connects to
		 * the source or to the drain of a device.  A single
		 * diffusion tile is marked, and all connecting diffusion tiles
		 * are enumerated and called the source.  Any other SD tiles
		 * are assumed to be the drain.  BUG: this does not work
		 * correctly with multi SD structures.
		 */

		if (source != (Tile *) NULL)
		{
		    STACKPUSH((ClientData)source, resDevStack);
		}
	    }
	    while (!StackEmpty(resDevStack))
	    {
	       	tp1 = (Tile *) STACKPOP(resDevStack);
		if (IsSplit(tp1))
		{
		    t1 = (SplitSide(tp1)) ? SplitRightType(tp1) :
				SplitLeftType(tp1);
		}
		else
		    t1 = TiGetTypeExact(tp1);

		/* top */
		for (tp2 = RT(tp1); RIGHT(tp2) > LEFT(tp1); tp2 = BL(tp2))
		{
		    if (TiGetBottomType(tp2) == t1)
		    {
		        tileJunk *j = resAddField(tp2);
			if ((j->tj_status & RES_TILE_SD) == 0)
			{
			    j->tj_status |= RES_TILE_SD;
	            	    STACKPUSH((ClientData)tp2, resDevStack);
			}
		    }
		}
		/* bottom */
		for (tp2 = LB(tp1); LEFT(tp2) < RIGHT(tp1); tp2 = TR(tp2))
		{
		    if (TiGetTopType(tp2) == t1)
		    {
		        tileJunk *j = resAddField(tp2);
			if ((j->tj_status & RES_TILE_SD) == 0)
			{
			    j->tj_status |= RES_TILE_SD;
	            	    STACKPUSH((ClientData)tp2, resDevStack);
			}
		    }
		}
		/* right */
		for (tp2 = TR(tp1); TOP(tp2) > BOTTOM(tp1); tp2 = LB(tp2))
		{
		    if (TiGetLeftType(tp2) == t1)
		    {
		        tileJunk *j = resAddField(tp2);
			if ((j->tj_status & RES_TILE_SD) == 0)
			{
			    j->tj_status |= RES_TILE_SD;
	            	    STACKPUSH((ClientData)tp2, resDevStack);
			}
		    }
		}
		/* left */
		for (tp2 = BL(tp1); BOTTOM(tp2) < TOP(tp1); tp2 = RT(tp2))
		{
		    if (TiGetRightType(tp2) == t1)
		    {
		        tileJunk *j = resAddField(tp2);
			if ((j->tj_status & RES_TILE_SD) == 0)
			{
			    j->tj_status |= RES_TILE_SD;
	            	    STACKPUSH((ClientData)tp2, resDevStack);
			}
		    }
		}
	    }

	    /* Find device substrate */

	    TTMaskZero(&locDevSubsMask);
	    TTMaskSetMask(&locDevSubsMask, &(devptr->exts_deviceSubstrateTypes));
	    TTMaskClearType(&locDevSubsMask, TT_SPACE);

	    TiToRect(tile, &r);
	    for (pNum = PL_TECHDEPBASE; pNum < DBNumPlanes; pNum++)
	    {
		if (TTMaskIntersect(&DBPlaneTypes[pNum], &locDevSubsMask))
		    DBSrPaintArea((Tile *)NULL, 
				    ResUse->cu_def->cd_planes[pNum],
				    &r, &locDevSubsMask,
				    resSubstrateTerm, (ClientData)NULL);
	    }

	    /* find rest of device; search for source edges */

	    STACKPUSH((ClientData)tile, resDevStack);
	    while (!StackEmpty(resDevStack))
	    {
	       	tileJunk *j0;

		tp1 = (Tile *) STACKPOP(resDevStack);
		if (IsSplit(tp1))
		{
		    t1 = (SplitSide(tp1)) ? SplitRightType(tp1) :
				SplitLeftType(tp1);
		}
		else
		    t1 = TiGetTypeExact(tp1);

		devptr = ExtCurStyle->exts_device[t1];
		j0 = (tileJunk *) tp1->ti_client;
		/* top */
		for (tp2 = RT(tp1); RIGHT(tp2) > LEFT(tp1); tp2 = BL(tp2))
		{
		    if ((TiGetBottomType(tp2) == t1) &&
			      (tp2->ti_client == (ClientData) CLIENTDEFAULT))
		    {
     	  		Junk = resAddField(tp2);
			STACKPUSH((ClientData)tp2, resDevStack);
	       		Junk->deviceList = resDev;
	       		Junk->tj_status |= RES_TILE_DEV;

			/* Update device position to point to the lower-leftmost tile */
			if ((tp2->ti_ll.p_x < resDev->rd_inside.r_ll.p_x) ||
				((tp2->ti_ll.p_x == resDev->rd_inside.r_ll.p_x) &&
				(tp2->ti_ll.p_y < resDev->rd_inside.r_ll.p_y)))
			{
			    resDev->rd_inside.r_ll.p_x = LEFT(tp2);
			    resDev->rd_inside.r_ll.p_y = BOTTOM(tp2);
			    resDev->rd_inside.r_ur.p_x = RIGHT(tp2);
			    resDev->rd_inside.r_ur.p_y = TOP(tp2);
			}
		    }
	      	    else if TTMaskHasType(&(devptr->exts_deviceSDTypes[0]),
				TiGetBottomType(tp2))
		    {
			Junk = resAddField(tp2);
			if (Junk->tj_status & RES_TILE_SD)
			       	    j0->sourceEdge |= TOPEDGE;
		    }
		}
		/* bottom */
		for (tp2 = LB(tp1); LEFT(tp2) < RIGHT(tp1); tp2 = TR(tp2))
		{
		    if ((TiGetTopType(tp2) == t1) &&
			      (tp2->ti_client == (ClientData) CLIENTDEFAULT))
		    {
     	  		Junk = resAddField(tp2);
			STACKPUSH((ClientData)tp2, resDevStack);
	       		Junk->deviceList =  resDev;
	       		Junk->tj_status |= RES_TILE_DEV;

			/* Update device position to point to the lower-leftmost tile */
			if ((tp2->ti_ll.p_x < resDev->rd_inside.r_ll.p_x) ||
				((tp2->ti_ll.p_x == resDev->rd_inside.r_ll.p_x) &&
				(tp2->ti_ll.p_y < resDev->rd_inside.r_ll.p_y)))
			{
			    resDev->rd_inside.r_ll.p_x = LEFT(tp2);
			    resDev->rd_inside.r_ll.p_y = BOTTOM(tp2);
			    resDev->rd_inside.r_ur.p_x = RIGHT(tp2);
			    resDev->rd_inside.r_ur.p_y = TOP(tp2);
			}
		    }
	      	    else if TTMaskHasType(&(devptr->exts_deviceSDTypes[0]),
				TiGetTopType(tp2))
		    {
			Junk = resAddField(tp2);
			if (Junk->tj_status & RES_TILE_SD)
			    j0->sourceEdge |= BOTTOMEDGE;
		    }
		}
		/* right */
		for (tp2 = TR(tp1); TOP(tp2) > BOTTOM(tp1); tp2 = LB(tp2))
		{
		    if ((TiGetLeftType(tp2) == t1) &&
			      (tp2->ti_client == (ClientData) CLIENTDEFAULT))
		    {
			Junk = resAddField(tp2);
			STACKPUSH((ClientData)tp2, resDevStack);
	       		Junk->deviceList =  resDev;
	       		Junk->tj_status |= RES_TILE_DEV;

			/* Update device position to point to the lower-leftmost tile */
			if ((tp2->ti_ll.p_x < resDev->rd_inside.r_ll.p_x) ||
				((tp2->ti_ll.p_x == resDev->rd_inside.r_ll.p_x) &&
				(tp2->ti_ll.p_y < resDev->rd_inside.r_ll.p_y)))
			{
			    resDev->rd_inside.r_ll.p_x = LEFT(tp2);
			    resDev->rd_inside.r_ll.p_y = BOTTOM(tp2);
			    resDev->rd_inside.r_ur.p_x = RIGHT(tp2);
			    resDev->rd_inside.r_ur.p_y = TOP(tp2);
			}
		    }
	      	    else if TTMaskHasType(&(devptr->exts_deviceSDTypes[0]),
				TiGetLeftType(tp2))
		    {
     	  		Junk = resAddField(tp2);
			if (Junk->tj_status & RES_TILE_SD)
			    j0->sourceEdge |= RIGHTEDGE;
		    }
		}
		/* left */
		for (tp2 = BL(tp1); BOTTOM(tp2) < TOP(tp1); tp2 = RT(tp2))
		{
		    if ((TiGetRightType(tp2) == t1) &&
			      (tp2->ti_client == (ClientData) CLIENTDEFAULT))
		    {
     	  		Junk = resAddField(tp2);
			STACKPUSH((ClientData)tp2, resDevStack);
	       		Junk->deviceList =  resDev;
	       		Junk->tj_status |= RES_TILE_DEV;

			/* Update device position to point to the lower-leftmost tile */
			if ((tp2->ti_ll.p_x < resDev->rd_inside.r_ll.p_x) ||
				((tp2->ti_ll.p_x == resDev->rd_inside.r_ll.p_x) &&
				(tp2->ti_ll.p_y < resDev->rd_inside.r_ll.p_y)))
			{
			    resDev->rd_inside.r_ll.p_x = LEFT(tp2);
			    resDev->rd_inside.r_ll.p_y = BOTTOM(tp2);
			    resDev->rd_inside.r_ur.p_x = RIGHT(tp2);
			    resDev->rd_inside.r_ur.p_y = TOP(tp2);
			}
		    }
	      	    else if TTMaskHasType(&(devptr->exts_deviceSDTypes[0]),
				TiGetRightType(tp2))
		    {
     	  		Junk = resAddField(tp2);
			if (Junk->tj_status & RES_TILE_SD)
			    j0->sourceEdge |= LEFTEDGE;
		    }
		}
	    }

	    /* unmark all tiles marked as being part of source */

	    if (source != (Tile *) NULL)
	    {
	        tileJunk *j = (tileJunk *) source->ti_client;

		STACKPUSH((ClientData)source, resDevStack);
		j->tj_status &= ~RES_TILE_SD;
	    }
	    while (!StackEmpty(resDevStack))
	    {
	        tp1 = (Tile *) STACKPOP(resDevStack);
		if (IsSplit(tp1))
		{
		    t1 = (SplitSide(tp1)) ? SplitRightType(tp1) :
				SplitLeftType(tp1);
		}
		else
		    t1 = TiGetTypeExact(tp1);

		/* top */
		for (tp2 = RT(tp1); RIGHT(tp2) > LEFT(tp1); tp2 = BL(tp2))
		{
		    tileJunk *j2 = (tileJunk *) tp2->ti_client;
		    if (TiGetBottomType(tp2) == t1)
		    {
			if (j2->tj_status & RES_TILE_SD)
			{
			    j2->tj_status &= ~RES_TILE_SD;
	            	    STACKPUSH((ClientData)tp2, resDevStack);
			}
		    }
		}
		/* bottom */
		for(tp2 = LB(tp1); LEFT(tp2) < RIGHT(tp1); tp2 = TR(tp2))
		{
		    tileJunk *j2 = (tileJunk *) tp2->ti_client;
		    if (TiGetTopType(tp2) == t1)
		    {
			if (j2->tj_status & RES_TILE_SD)
			{
			    j2->tj_status &= ~RES_TILE_SD;
	           	    STACKPUSH((ClientData)tp2, resDevStack);
			}
		    }
		}
		/* right */
		for (tp2 = TR(tp1); TOP(tp2) > BOTTOM(tp1); tp2 = LB(tp2))
		{
		    tileJunk *j2 = (tileJunk *) tp2->ti_client;
		    if (TiGetLeftType(tp2) == t1)
		    {
			if (j2->tj_status & RES_TILE_SD)
			{
			    j2->tj_status &= ~RES_TILE_SD;
	            	    STACKPUSH((ClientData)tp2, resDevStack);
			}
		    }
		}
		/* left */
		for (tp2 = BL(tp1); BOTTOM(tp2) < TOP(tp1); tp2 = RT(tp2))
		{
		    tileJunk *j2 = (tileJunk *) tp2->ti_client;
		    if (TiGetRightType(tp2) == t1)
		    {
			if (j2->tj_status & RES_TILE_SD)
			{
			    j2->tj_status &= ~RES_TILE_SD;
	            	    STACKPUSH((ClientData)tp2, resDevStack);
			}
		    }
		}
	    }
	}
    }
    return(0);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResRemovePlumbing-- Removes and deallocates all the tileJunk fields.
 *
 * Results: returns 0
 *
 * Side Effects: frees up memory; resets tile->ti_client fields to CLIENTDEFAULT
 *
 *-------------------------------------------------------------------------
 */

int
ResRemovePlumbing(tile, arg)
    Tile	*tile;
    ClientData	*arg;

{

    if (tile->ti_client != (ClientData) CLIENTDEFAULT)
    {
	freeMagic(((char *)(tile->ti_client)));
	tile->ti_client = (ClientData) CLIENTDEFAULT;
    }
    return(0);
}


/*
 *-------------------------------------------------------------------------
 *
 * ResPreProcessDevices-- Given a list of all the device tiles and
 * a list of all the devices, this procedure calculates the width and
 * length.  The width is set equal to the sum of all edges that touch
 * diffusion divided by 2. The length is the remaining perimeter divided by
 * 2*tiles.  The perimeter and area fields of device structures are also
 * fixed.
 *
 * Results: none
 *
 * Side Effects: sets length and width of devices. "ResDevTile"
 * structures are freed.
 *
 *-------------------------------------------------------------------------
 */

void
ResPreProcessDevices(TileList, DeviceList, Def)
    ResDevTile		*TileList;
    resDevice		*DeviceList;
    CellDef		*Def;
{
    Tile	*tile;
    ResDevTile	*oldTile;
    tileJunk	*tstruct;
    TileType	tt, residue;
    int		pNum;

    while (TileList != (ResDevTile *) NULL)
    {
	tt = TileList->type;
	if (DBIsContact(tt))
	{
	    /* Find which residue of the contact is a device. */
	    TileTypeBitMask ttresidues;

	    DBFullResidueMask(tt, &ttresidues);

	    for (residue = TT_TECHDEPBASE; residue < DBNumUserLayers; residue++)
	    {
		if (TTMaskHasType(&ttresidues, residue))
		{
		    if (TTMaskHasType(&ExtCurStyle->exts_deviceMask, residue))
		    {
			pNum = DBPlane(residue);
			break;
		    }
		}
	    }
	}
	else
	    pNum = DBPlane(tt);		/* always correct for non-contact types */

	tile = (Def->cd_planes[pNum])->pl_hint;
	GOTOPOINT(tile, &(TileList->area.r_ll));

	tt = TiGetType(tile);
	tstruct = (tileJunk *) tile->ti_client;

	if ((tstruct == (tileJunk *)CLIENTDEFAULT) ||
		    (tstruct->deviceList == NULL) ||
		    !TTMaskHasType(&ExtCurStyle->exts_deviceMask, tt))
	{
	    TxError("Bad Device Location at %d,%d\n",
			TileList->area.r_ll.p_x,
			TileList->area.r_ll.p_y);
	}
	else if ((tstruct->tj_status & RES_TILE_MARK) == 0)
	{
	    resDevice	*rd = tstruct->deviceList;

	    tstruct->tj_status |= RES_TILE_MARK;
	    rd->rd_perim += TileList->perim;
	    rd->rd_length += TileList->overlap;
	    rd->rd_area += (TileList->area.r_xtop - TileList->area.r_xbot)
			* (TileList->area.r_ytop - TileList->area.r_ybot);
	    rd->rd_tiles++;
	}
	oldTile = TileList;
	TileList = TileList->nextDev;
	freeMagic((char *)oldTile);
    }

    for (; DeviceList != NULL; DeviceList = DeviceList->rd_nextDev)
    {
     	int width  = DeviceList->rd_perim;
	int length = DeviceList->rd_length;
	if (DeviceList->rd_tiles != 0)
	{
	    if (length)
	    {
	        DeviceList->rd_length = (float) length /
			((float)((DeviceList->rd_tiles) << 1));
	        DeviceList->rd_width = (width-length) >> 1;
	    }
	    else
	    {
	       	double perimeter = DeviceList->rd_perim;
		double area = DeviceList->rd_area;

		perimeter /= 4.0;

		DeviceList->rd_width = perimeter +
			sqrt(perimeter * perimeter-area);
		DeviceList->rd_length = (DeviceList->rd_perim
			- 2 * DeviceList->rd_width) >> 1;
	    }
	}
    }
}


/*
 *-------------------------------------------------------------------------
 *
 * ResAddToQueue-- adds new nodes to list of nodes requiring processing.
 *
 * Side Effects: nodes are added to list (i.e they have their linked list
 *	pointers modified.)
 *
 *-------------------------------------------------------------------------
 */

void
ResAddToQueue(node, list)
   resNode  *node, **list;
{
   node->rn_more = *list;
   node->rn_less = NULL;
   if (*list) (*list)->rn_less = node;
   *list = node;
}

/*
 *-------------------------------------------------------------------------
 *
 * ResRemoveFromQueue-- removes node from queue. Complains if it notices
 *	that the node isn't in the supplied list.
 *
 * Results: none
 *
 * Side Effects: modifies nodelist
 *
 *-------------------------------------------------------------------------
 */

void
ResRemoveFromQueue(node, list)
    resNode	*node, **list;
{
    if (node->rn_less != NULL)
    {
     	node->rn_less->rn_more = node->rn_more;
    }
    else
    {
     	if (node != (*list))
	{
	    TxError("Error: Attempt to remove node from wrong list\n");
	}
	else
	{
	    *list = node->rn_more;
	}
    }
    if (node->rn_more != NULL)
    {
     	node->rn_more->rn_less = node->rn_less;
    }
    node->rn_more = NULL;
    node->rn_less = NULL;
}

tileJunk *
resAddField(tile)
    Tile    *tile;
{
    tileJunk *Junk;
    if ((Junk = (tileJunk *)tile->ti_client) == (tileJunk *) CLIENTDEFAULT)
    {
     	Junk = (tileJunk *) mallocMagic((unsigned) (sizeof(tileJunk)));
	ResJunkInit(Junk);
	tile->ti_client = (ClientData) Junk;
    }
    return Junk;
}
