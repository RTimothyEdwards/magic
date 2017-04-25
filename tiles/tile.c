/*
 * tile.c --
 *
 * Basic tile manipulation
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

#ifndef	lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/tiles/tile.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif	/* not lint */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/geometry.h"
#include "tiles/tile.h"

/*
 * Debugging version of TiSetBody() macro in tile.h
 * Includes sanity check that a tile at "infinity"
 * is not being set to a type other than space.
 */
/*
void
TiSetBody(tp, b)
   Tile *tp;
   ClientData b;
{
   if (b != (ClientData)0 && b != (ClientData)(-1))
	if (RIGHT(tp) == INFINITY || TOP(tp) == INFINITY ||
		LEFT(tp) == MINFINITY || BOTTOM(tp) == MINFINITY)
	    TxError("Error:  Tile at infinity set to non-space value %d\n", (int)b);
   tp->ti_body = b;
}
*/

/*
 * Rectangle that defines the maximum extent of any plane.
 * No tile created by the user should ever extend outside of
 * this area.
 */

global Rect TiPlaneRect = { MINFINITY+2, MINFINITY+2, INFINITY-2, INFINITY-2 };

#ifdef HAVE_SYS_MMAN_H

global Tile *TileStoreFreeList = NULL;
global Tile *TileStoreFreeList_end = NULL;

/* The new Tile Allocation scheme (Magic 8.0) */

static void *_block_begin = NULL;
static void *_current_ptr = NULL;
static void *_block_end = NULL;

#endif /* HAVE_SYS_MMAN_H */


/*
 * --------------------------------------------------------------------
 *
 * TiNewPlane --
 *
 * Allocate and initialize a new tile plane.
 *
 * Results:
 *	A newly allocated Plane with all corner stitches set
 *	appropriately.
 *
 * Side effects:
 *	Adjusts the corner stitches of the Tile supplied to
 *	point to the appropriate bounding tile in the newly
 *	created Plane.
 *
 * --------------------------------------------------------------------
 */

Plane *
TiNewPlane(tile)
    Tile *tile;/* Tile to become initial tile of plane.
			 * May be NULL.
			 */
{
    Plane *newplane;
    static Tile *infinityTile = (Tile *) NULL;

    newplane = (Plane *) mallocMagic((unsigned) (sizeof (Plane)));
    newplane->pl_top = TiAlloc();
    newplane->pl_right = TiAlloc();
    newplane->pl_bottom = TiAlloc();
    newplane->pl_left = TiAlloc();

    /*
     * Since the lower left coordinates of the TR and RT
     * stitches of a tile are used to determine its upper right,
     * we must give the boundary tiles a meaningful TR and RT.
     * To make certain that these tiles don't have zero width
     * or height, we use a dummy tile at (INFINITY+1,INFINITY+1).
     */

    if (infinityTile == (Tile *) NULL)
    {
	infinityTile = TiAlloc();
	LEFT(infinityTile) = INFINITY+1;
	BOTTOM(infinityTile) = INFINITY+1;
    }

    if (tile)
    {
	RT(tile) = newplane->pl_top;
	TR(tile) = newplane->pl_right;
	LB(tile) = newplane->pl_bottom;
	BL(tile) = newplane->pl_left;
    }

    LEFT(newplane->pl_bottom) = MINFINITY;
    BOTTOM(newplane->pl_bottom) = MINFINITY;
    RT(newplane->pl_bottom) = tile;
    TR(newplane->pl_bottom) = newplane->pl_right;
    LB(newplane->pl_bottom) = BADTILE;
    BL(newplane->pl_bottom) = newplane->pl_left;
    TiSetBody(newplane->pl_bottom, -1);

    LEFT(newplane->pl_top) = MINFINITY;
    BOTTOM(newplane->pl_top) = INFINITY;
    RT(newplane->pl_top) = infinityTile;
    TR(newplane->pl_top) = newplane->pl_right;
    LB(newplane->pl_top) = tile;
    BL(newplane->pl_top) = newplane->pl_left;
    TiSetBody(newplane->pl_top, -1);

    LEFT(newplane->pl_left) = MINFINITY;
    BOTTOM(newplane->pl_left) = MINFINITY;
    RT(newplane->pl_left) = newplane->pl_top;
    TR(newplane->pl_left) = tile;
    LB(newplane->pl_left) = newplane->pl_bottom;
    BL(newplane->pl_left) = BADTILE;
    TiSetBody(newplane->pl_left, -1);

    LEFT(newplane->pl_right) = INFINITY;
    BOTTOM(newplane->pl_right) = MINFINITY;
    RT(newplane->pl_right) = newplane->pl_top;
    TR(newplane->pl_right) = infinityTile;
    LB(newplane->pl_right) = newplane->pl_bottom;
    BL(newplane->pl_right) = tile;
    TiSetBody(newplane->pl_right, -1);

    newplane->pl_hint = tile;
    return (newplane);
}

/*
 * --------------------------------------------------------------------
 *
 * TiFreePlane --
 *
 * Free the storage associated with a tile plane.
 * Only the plane itself and its four border tiles are deallocated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 * --------------------------------------------------------------------
 */

void
TiFreePlane(plane)
    Plane *plane;	/* Plane to be freed */
{
    TiFree(plane->pl_left);
    TiFree(plane->pl_right);
    TiFree(plane->pl_top);
    TiFree(plane->pl_bottom);
    freeMagic((char *) plane);
}

/*
 * --------------------------------------------------------------------
 *
 * TiToRect --
 *
 * Convert a tile to a rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets *rect to the bounding box for the supplied tile.
 *
 * --------------------------------------------------------------------
 */

void
TiToRect(tile, rect)
    Tile *tile; /* Tile whose bounding box is to be stored in *rect */
    Rect *rect; /* Pointer to rect to be set to bounding box */
{
    rect->r_xbot = LEFT(tile);
    rect->r_xtop = RIGHT(tile);
    rect->r_ybot = BOTTOM(tile);
    rect->r_ytop = TOP(tile);
}

/*
 * --------------------------------------------------------------------
 *
 * TiSplitX --
 *
 * Given a tile and an X coordinate, split the tile into two
 * along a line running vertically through the given coordinate.
 *
 * Results:
 *	Returns the new tile resulting from the splitting, which
 *	is the tile occupying the right-hand half of the original
 *	tile.
 *
 * Side effects:
 *	Modifies the corner stitches in the database to reflect
 *	the presence of two tiles in place of the original one.
 *
 * --------------------------------------------------------------------
 */

Tile *
TiSplitX(tile, x)
    Tile *tile;	/* Tile to be split */
    int x;		/* X coordinate of split */
{
    Tile *newtile;
    Tile *tp;

    ASSERT(x > LEFT(tile) && x < RIGHT(tile), "TiSplitX");

    newtile = TiAlloc();
    TiSetClient(newtile, CLIENTDEFAULT);
    TiSetBody(newtile, 0);

    LEFT(newtile) = x;
    BOTTOM(newtile) = BOTTOM(tile);
    BL(newtile) = tile;
    TR(newtile) = TR(tile);
    RT(newtile) = RT(tile);

    /*
     * Adjust corner stitches along the right edge
     */

    for (tp = TR(tile); BL(tp) == tile; tp = LB(tp))
	BL(tp) = newtile;
    TR(tile) = newtile;

    /*
     * Adjust corner stitches along the top edge
     */

    for (tp = RT(tile); LEFT(tp) >= x; tp = BL(tp))
	LB(tp) = newtile;
    RT(tile) = tp;

    /*
     * Adjust corner stitches along the bottom edge
     */

    for (tp = LB(tile); RIGHT(tp) <= x; tp = TR(tp))
	/* nothing */;
    LB(newtile) = tp;
    while (RT(tp) == tile)
    {
	RT(tp) = newtile;
	tp = TR(tp);
    }

    return (newtile);
}

/*
 * --------------------------------------------------------------------
 *
 * TiSplitY --
 *
 * Given a tile and a Y coordinate, split the tile into two
 * along a horizontal line running through the given coordinate.
 *
 * Results:
 *	Returns the new tile resulting from the splitting, which
 *	is the tile occupying the top half of the original
 *	tile.
 *
 * Side effects:
 *	Modifies the corner stitches in the database to reflect
 *	the presence of two tiles in place of the original one.
 *
 * --------------------------------------------------------------------
 */

Tile *
TiSplitY(tile, y)
    Tile *tile;	/* Tile to be split */
    int y;		/* Y coordinate of split */
{
    Tile *newtile;
    Tile *tp;

    ASSERT(y > BOTTOM(tile) && y < TOP(tile), "TiSplitY");

    newtile = TiAlloc();
    TiSetClient(newtile, CLIENTDEFAULT);
    TiSetBody(newtile, 0);

    LEFT(newtile) = LEFT(tile);
    BOTTOM(newtile) = y;
    LB(newtile) = tile;
    RT(newtile) = RT(tile);
    TR(newtile) = TR(tile);

    /*
     * Adjust corner stitches along top edge
     */

    for (tp = RT(tile); LB(tp) == tile; tp = BL(tp))
	LB(tp) = newtile;
    RT(tile) = newtile;

    /*
     * Adjust corner stitches along right edge
     */

    for (tp = TR(tile); BOTTOM(tp) >= y; tp = LB(tp))
	BL(tp) = newtile;
    TR(tile) = tp;

    /*
     * Adjust corner stitches along left edge
     */

    for (tp = BL(tile); TOP(tp) <= y; tp = RT(tp))
	/* nothing */;
    BL(newtile) = tp;
    while (TR(tp) == tile)
    {
	TR(tp) = newtile;
	tp = RT(tp);
    }

    return (newtile);
}

/*
 * --------------------------------------------------------------------
 *
 * TiSplitX_Left --
 *
 * Given a tile and an X coordinate, split the tile into two
 * along a line running vertically through the given coordinate.
 * Intended for use when plowing to the left.
 *
 * Results:
 *	Returns the new tile resulting from the splitting, which
 *	is the tile occupying the left-hand half of the original
 *	tile.
 *
 * Side effects:
 *	Modifies the corner stitches in the database to reflect
 *	the presence of two tiles in place of the original one.
 *
 * --------------------------------------------------------------------
 */

Tile *
TiSplitX_Left(tile, x)
    Tile *tile;	/* Tile to be split */
    int x;		/* X coordinate of split */
{
    Tile *newtile;
    Tile *tp;

    ASSERT(x > LEFT(tile) && x < RIGHT(tile), "TiSplitX");

    newtile = TiAlloc();
    TiSetClient(newtile, CLIENTDEFAULT);
    TiSetBody(newtile, 0);

    LEFT(newtile) = LEFT(tile);
    LEFT(tile) = x;
    BOTTOM(newtile) = BOTTOM(tile);

    BL(newtile) = BL(tile);
    LB(newtile) = LB(tile);
    TR(newtile) = tile;
    BL(tile) = newtile;

    /* Adjust corner stitches along the left edge */
    for (tp = BL(newtile); TR(tp) == tile; tp = RT(tp))
	TR(tp) = newtile;

    /* Adjust corner stitches along the top edge */
    for (tp = RT(tile); LEFT(tp) >= x; tp = BL(tp))
	/* nothing */;
    RT(newtile) = tp;
    for ( ; LB(tp) == tile; tp = BL(tp))
	LB(tp) = newtile;

    /* Adjust corner stitches along the bottom edge */
    for (tp = LB(tile); RIGHT(tp) <= x; tp = TR(tp))
	RT(tp) = newtile;
    LB(tile) = tp;

    return (newtile);
}

/*
 * --------------------------------------------------------------------
 *
 * TiSplitY_Bottom --
 *
 * Given a tile and a Y coordinate, split the tile into two
 * along a horizontal line running through the given coordinate.
 * Used when plowing down.
 *
 * Results:
 *	Returns the new tile resulting from the splitting, which
 *	is the tile occupying the bottom half of the original
 *	tile.
 *
 * Side effects:
 *	Modifies the corner stitches in the database to reflect
 *	the presence of two tiles in place of the original one.
 *
 * --------------------------------------------------------------------
 */

Tile *
TiSplitY_Bottom(tile, y)
    Tile *tile;	/* Tile to be split */
    int y;		/* Y coordinate of split */
{
    Tile *newtile;
    Tile *tp;

    ASSERT(y > BOTTOM(tile) && y < TOP(tile), "TiSplitY");

    newtile = TiAlloc();
    TiSetClient(newtile, CLIENTDEFAULT);
    TiSetBody(newtile, 0);

    LEFT(newtile) = LEFT(tile);
    BOTTOM(newtile) = BOTTOM(tile);
    BOTTOM(tile) = y;

    RT(newtile) = tile;
    LB(newtile) = LB(tile);
    BL(newtile) = BL(tile);
    LB(tile) = newtile;

    /* Adjust corner stitches along bottom edge */
    for (tp = LB(newtile); RT(tp) == tile; tp = TR(tp))
	RT(tp) = newtile;

    /* Adjust corner stitches along right edge */
    for (tp = TR(tile); BOTTOM(tp) >= y; tp = LB(tp))
	/* nothing */;
    TR(newtile) = tp;
    for ( ; BL(tp) == tile; tp = LB(tp))
	BL(tp) = newtile;

    /* Adjust corner stitches along left edge */
    for (tp = BL(tile); TOP(tp) <= y; tp = RT(tp))
	TR(tp) = newtile;
    BL(tile) = tp;

    return (newtile);
}

/*
 * --------------------------------------------------------------------
 *
 * TiJoinX --
 *
 * Given two tiles sharing an entire common vertical edge, replace
 * them with a single tile occupying the union of their areas.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The first tile is simply relinked to reflect its new size.
 *	The second tile is deallocated.  Corner stitches in the
 *	neighboring tiles are updated to reflect the new structure.
 *	If the hint tile pointer in the supplied plane pointed to
 *	the second tile, it is adjusted to point instead to the
 *	first.
 *
 * --------------------------------------------------------------------
 */

void
TiJoinX(tile1, tile2, plane)
    Tile *tile1;	/* First tile, remains allocated after call */
    Tile *tile2;	/* Second tile, deallocated by call */
    Plane *plane;	/* Plane in which hint tile is updated */
{
    Tile *tp;

    /*
     * Basic algorithm:
     *
     *	Update all the corner stitches in the neighbors of tile2
     *	to point to tile1.
     *	Update the corner stitches of tile1 along the shared edge
     *	to be those of tile2.
     *	Change the bottom or left coordinate of tile1 if appropriate.
     *	Deallocate tile2.
     */

    ASSERT(BOTTOM(tile1)==BOTTOM(tile2) && TOP(tile1)==TOP(tile2), "TiJoinX");
    ASSERT(LEFT(tile1)==RIGHT(tile2) || RIGHT(tile1)==LEFT(tile2), "TiJoinX");

    /*
     * Update stitches along top of tile
     */

    for (tp = RT(tile2); LB(tp) == tile2; tp = BL(tp))
	LB(tp) = tile1;

    /*
     * Update stitches along bottom of tile
     */

    for (tp = LB(tile2); RT(tp) == tile2; tp = TR(tp))
	RT(tp) = tile1;

    /*
     * Update stitches along either left or right, depending
     * on relative position of the two tiles.
     */

    ASSERT(LEFT(tile1) != LEFT(tile2), "TiJoinX");
    if (LEFT(tile1) < LEFT(tile2))
    {
	for (tp = TR(tile2); BL(tp) == tile2; tp = LB(tp))
	    BL(tp) = tile1;
	TR(tile1) = TR(tile2);
	RT(tile1) = RT(tile2);
    }
    else
    {
	for (tp = BL(tile2); TR(tp) == tile2; tp = RT(tp))
	    TR(tp) = tile1;
	BL(tile1) = BL(tile2);
	LB(tile1) = LB(tile2);
	LEFT(tile1) = LEFT(tile2);
    }

    if (plane->pl_hint == tile2)
	plane->pl_hint = tile1;
    TiFree(tile2);
}

/*
 * --------------------------------------------------------------------
 *
 * TiJoinY --
 *
 * Given two tiles sharing an entire common horizontal edge, replace
 * them with a single tile occupying the union of their areas.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The first tile is simply relinked to reflect its new size.
 *	The second tile is deallocated.  Corner stitches in the
 *	neighboring tiles are updated to reflect the new structure.
 *	If the hint tile pointer in the supplied plane pointed to
 *	the second tile, it is adjusted to point instead to the
 *	first.
 *
 * --------------------------------------------------------------------
 */

void
TiJoinY(tile1, tile2, plane)
    Tile *tile1;	/* First tile, remains allocated after call */
    Tile *tile2;	/* Second tile, deallocated by call */
    Plane *plane;	/* Plane in which hint tile is updated */
{
    Tile *tp;

    /*
     * Basic algorithm:
     *
     *	Update all the corner stitches in the neighbors of tile2
     *	to point to tile1.
     *	Update the corner stitches of tile1 along the shared edge
     *	to be those of tile2.
     *	Change the bottom or left coordinate of tile1 if appropriate.
     *	Deallocate tile2.
     */

    ASSERT(LEFT(tile1)==LEFT(tile2) && RIGHT(tile1)==RIGHT(tile2), "TiJoinY");
    ASSERT(TOP(tile1)==BOTTOM(tile2) || BOTTOM(tile1)==TOP(tile2), "TiJoinY");

    /*
     * Update stitches along right of tile.
     */

    for (tp = TR(tile2); BL(tp) == tile2; tp = LB(tp))
	BL(tp) = tile1;

    /*
     * Update stitches along left of tile.
     */

    for (tp = BL(tile2); TR(tp) == tile2; tp = RT(tp))
	TR(tp) = tile1;

    /*
     * Update stitches along either top or bottom, depending
     * on relative position of the two tiles.
     */

    ASSERT(BOTTOM(tile1) != BOTTOM(tile2), "TiJoinY");
    if (BOTTOM(tile1) < BOTTOM(tile2))
    {
	for (tp = RT(tile2); LB(tp) == tile2; tp = BL(tp))
	    LB(tp) = tile1;
	RT(tile1) = RT(tile2);
	TR(tile1) = TR(tile2);
    }
    else
    {
	for (tp = LB(tile2); RT(tp) == tile2; tp = TR(tp))
	    RT(tp) = tile1;
	LB(tile1) = LB(tile2);
	BL(tile1) = BL(tile2);
	BOTTOM(tile1) = BOTTOM(tile2);
    }

    if (plane->pl_hint == tile2)
	plane->pl_hint = tile1;
    TiFree(tile2);
}

#ifdef HAVE_SYS_MMAN_H

/* MMAP the tile store */
static signed char
mmapTileStore()
{
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_ANON | MAP_PRIVATE;
    unsigned long map_len = TILE_STORE_BLOCK_SIZE; 	

    _block_begin = mmap(NULL, map_len, prot, flags, -1, 0);
    if (_block_begin == MAP_FAILED)
    {
	TxError("TileStore: Unable to mmap ANON SEGMENT\n");
	_exit(1);
    }
    _block_end = (void *) ((unsigned long) _block_begin + map_len);
    _current_ptr = _block_begin;
    return 0;
}

Tile *
getTileFromTileStore()
{
    Tile *_return_tile = NULL;

    if (!_block_begin && !_block_end)
    {
	mmapTileStore();
    }

    /* Check if we can get the tile from the 
     * Free list
     */

    if (TileStoreFreeList)
    {
	_return_tile = TileStoreFreeList;
	TileStoreFreeList = (Tile *)TileStoreFreeList->ti_client;
	return _return_tile;
    }

    /* Get it from the mmap */

    if (((unsigned long)_current_ptr + sizeof(Tile)) 
		 > (unsigned long)_block_end)
    {
	 mmapTileStore();
    }
    _current_ptr  = (void *)((unsigned long)_current_ptr + sizeof(Tile));
	
    if ((unsigned long)_current_ptr > (unsigned long) _block_end)
    {
	fprintf(stderr,"TileStore: internal assertion failure...");
	_exit(1);
    }
    return (Tile *)((unsigned long)_current_ptr - sizeof(Tile));
}

static void
TileStoreFree(ptr)
    Tile *ptr;
{
    if (!TileStoreFreeList_end || !TileStoreFreeList)
    {
	TileStoreFreeList_end = ptr;
	ptr->ti_client = (unsigned long)0;
	TileStoreFreeList = TileStoreFreeList_end;
    }
    else
    {
	TileStoreFreeList_end->ti_client = (unsigned long)ptr;
	TileStoreFreeList_end = ptr;
	TileStoreFreeList_end->ti_client = (unsigned long) 0;
    }
}

Tile *
TiAlloc()
{
    Tile *newtile;

    newtile = getTileFromTileStore();
    TiSetClient(newtile, CLIENTDEFAULT);
    TiSetBody(newtile, 0);
    return (newtile);
}

void
TiFree(tp)
    Tile *tp;
{
    TileStoreFree(tp);
}

#else

/*
 * --------------------------------------------------------------------
 *
 * TiAlloc ---
 *
 *	Memory allocation for tiles
 *
 * Results:
 *	Pointer to an initialized memory location for a tile.
 *
 * --------------------------------------------------------------------
 */

Tile *
TiAlloc()
{
    Tile *newtile;

    newtile = (Tile *) mallocMagic((unsigned) (sizeof (Tile)));
    TiSetClient(newtile, CLIENTDEFAULT);
    TiSetBody(newtile, 0);
    return (newtile);
}

/*
 * --------------------------------------------------------------------
 *
 * TiFree ---
 *
 *	Release memory allocation for tiles
 *
 * Results:
 *	None.
 *
 * --------------------------------------------------------------------
 */

void
TiFree(tp)
    Tile *tp;
{
    freeMagic((char *)tp);
}

#endif /* !HAVE_SYS_MMAN_H */

/* ==================================================================== */
/*									*/
/*			DEBUGGING PROCEDURES				*/
/*									*/
/* ==================================================================== */

void
tiPrint(tp)
    Tile *tp;
{
    printf("tp=%p LL=(%d,%d) body=0x%"DLONG_PREFIX"x\n",
	tp, LEFT(tp), BOTTOM(tp), (dlong) tp->ti_body);
    printf("BL=%p LB=%p RT=%p TR=%p\n", BL(tp), LB(tp), RT(tp), TR(tp));
}

void
tiPrintAll(tp)
    Tile *tp;
{
    tiPrint(tp);
    printf("UR=(%d,%d)\n", RIGHT(tp), TOP(tp));

    /* The following is for plowing debugging */
    printf("LEAD=%d\n", (int) tp->ti_client);
}
