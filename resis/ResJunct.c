
#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/resis/ResJunct.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
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
#include "database/databaseInt.h"
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
 *-------------------------------------------------------------------------
 *
 * ResNewSDDevice -- called when a device is reached via a piece of
 *			 diffusion. (Devices  reached via poly, i.e.
 *			 gates, are handled by ResEachTile.)
 *
 * Results:none
 *
 * Side Effects: determines to which terminal (source or drain) node
 * is connected. Makes new node if node hasn't already been created .
 * Allocates breakpoint in current tile for device.
 *
 *-------------------------------------------------------------------------
 */

void
ResNewSDDevice(tile, tp, xj, yj, direction, PendingList)
    Tile 	*tile, *tp;
    int 	xj, yj, direction;
    resNode	**PendingList;
{
    resNode	*resptr;
    resDevice	*resDev;
    tElement	*tcell;
    int		newnode;
    tileJunk	*j;

    newnode = FALSE;
    j = (tileJunk *) tp->ti_client;
    resDev = j->deviceList;
    if ((j->sourceEdge & direction) != 0)
    {
	if (resDev->rd_fet_source == (resNode *) NULL)
	{
	    resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	    newnode = TRUE;
	    resDev->rd_fet_source = resptr;
	}
	else
	{
	    resptr = resDev->rd_fet_source;
	}
    }
    else
    {
	if (resDev->rd_fet_drain == (resNode *) NULL)
	{
	    resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	    newnode = TRUE;
	    resDev->rd_fet_drain = resptr;
	}
	else
	{
	    resptr = resDev->rd_fet_drain;
	}
    }
    if (newnode)
    {
	tcell = (tElement *) mallocMagic((unsigned)(sizeof(tElement)));
	tcell->te_nextt = NULL;
	tcell->te_thist = j->deviceList;
	InitializeNode(resptr, xj, yj, RES_NODE_DEVICE);
	resptr->rn_te = tcell;
	ResAddToQueue(resptr, PendingList);
    }
    NEWBREAK(resptr, tile, xj, yj, NULL);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResProcessJunction-- Called whenever a tile  connecting to the tile being
 *	worked on is found. If a junction is already present, its address is
 *      returned. Otherwise, a new junction is made.
 *
 * Results: None.
 *
 * Side Effects: Junctions may be created.
 *
 *-------------------------------------------------------------------------
 */

void
ResProcessJunction(tile, tp, xj, yj, NodeList)
    Tile 	*tile, *tp;
    int		xj, yj;
    resNode	**NodeList;
{
    ResJunction *junction;
    resNode	*resptr;
    jElement    *jcell;
    tileJunk	*j0 = (tileJunk *)tile->ti_client;
    tileJunk	*j2 = (tileJunk *)tp->ti_client;

#ifdef PARANOID
    if (tile == tp)
    {
	TxError("Junction being made between tile and itself \n");
	return;
    }
#endif
    if (j2->tj_status & RES_TILE_DONE) return;
    resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
    resptr->rn_te = (tElement *) NULL;
    junction = (ResJunction *) mallocMagic((unsigned)(sizeof(ResJunction)));
    jcell = (jElement *) mallocMagic((unsigned)(sizeof(jElement)));
    InitializeNode(resptr, xj, yj, RES_NODE_JUNCTION);
    resptr->rn_je = jcell;
    ResAddToQueue(resptr, NodeList);

    jcell->je_thisj = junction;
    jcell->je_nextj = NULL;
    junction->rj_status = FALSE;
    junction->rj_jnode = resptr;
    junction->rj_Tile[0] = tile;
    junction->rj_Tile[1] = tp;
    junction->rj_loc.p_x =xj;
    junction->rj_loc.p_y =yj;
    junction->rj_nextjunction[0] = j0->junctionList;
    j0->junctionList = junction;
    junction->rj_nextjunction[1] = j2->junctionList;
    j2->junctionList = junction;

    NEWBREAK(junction->rj_jnode,tile, junction->rj_loc.p_x, 
		    junction->rj_loc.p_y, NULL);

    NEWBREAK(junction->rj_jnode,tp, junction->rj_loc.p_x,
		    junction->rj_loc.p_y, NULL);

}
