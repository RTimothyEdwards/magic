
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
 * ResNewTermDevice --
 *
 *	Called when a device is reached via a type in the device's
 *	terminal type list (e.g., diffusion, for MOSFETs).  Note that
 *	devices reached by the device type (e.g., poly, for MOSFETs)
 *	are handled by ResEachTile.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Determines to which terminal (source or drain) node is connected.
 *	Makes new node if node hasn't already been created.  Allocates
 *	breakpoint in current tile for device.
 *
 *-------------------------------------------------------------------------
 */

void
ResNewTermDevice(
    Tile *tile,
    Tile *tp,
    int 	n,			/* Terminal index */
    int xj,
    int yj,
    int		direction,		/* Direction of current */
    resNode	**PendingList)
{
    resNode	*resptr = NULL;
    resDevice	*resDev;
    tElement	*tcell;
    int		newnode;
    resInfo	*ri;

    newnode = FALSE;

    /* Watch for invalid client data.  If found, a "Bad Device" error will
     * be generated which means "more debugging needed";  however, it will
     * not cause a segmentation violation.
     */
    if (TiGetClient(tp) == CLIENTDEFAULT) return;

    ri = (resInfo *) TiGetClientPTR(tp);
    resDev = ri->deviceList;
    if (resDev == NULL) return;			/* Shouldn't happen? */

    /* Set the terminal indicated (source or drain).  If the terminal
     * indicated is already set, then create a new breakpoint on the
     * terminal.  However, to handle cases where a device may have
     * source and drain tied to the same net, if there is an existing
     * entry and the edge direction is opposite of "direction", then
     * create a new terminal on the other side (i.e., permute source
     * and drain).
     */
 
    if (resDev->rd_terminals[2 + n] == (resNode *)NULL)
    {
	resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	newnode = TRUE;
	resDev->rd_terminals[2 + n] = resptr;
    }
    else if (((ri->sourceEdge & direction) != 0) || (resDev->rd_nterms < 4))
    {
	resptr = resDev->rd_terminals[2 + n];
    }
    else if ((n == 0) && (resDev->rd_terminals[3] == (resNode *)NULL))
    {
	resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	newnode = TRUE;
	resDev->rd_terminals[3] = resptr;
    }
    else if ((n == 1) && (resDev->rd_terminals[2] == (resNode *)NULL))
    {
	resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	newnode = TRUE;
	resDev->rd_terminals[2] = resptr;
    }

    if (newnode)
    {
	tcell = (tElement *) mallocMagic((unsigned)(sizeof(tElement)));
	tcell->te_nextt = NULL;
	tcell->te_thist = ri->deviceList;
	InitializeResNode(resptr, xj, yj, RES_NODE_DEVICE);
	resptr->rn_te = tcell;
	ResAddToQueue(resptr, PendingList);
    }
    if (resptr != NULL)
	ResNewBreak(resptr, tile, xj, yj, NULL);
}

/*
 *-------------------------------------------------------------------------
 *
 * ResNewSubDevice -- called when a device is reached via a substrate.
 *
 * Results:none
 *
 * Side Effects: Makes new node if node hasn't already been created.
 * Allocates breakpoint in current tile for device.
 *
 *-------------------------------------------------------------------------
 */

void
ResNewSubDevice(
    Tile *tile,
    Tile *tp,
    int xj,
    int yj,
    int direction,
    resNode **PendingList)
{
    resNode	*resptr;
    resDevice	*resDev;
    tElement	*tcell;
    int		newnode;
    resInfo	*ri;

    newnode = FALSE;
    ri = (resInfo *) TiGetClientPTR(tp);
    resDev = ri->deviceList;

    if (resDev == NULL) return;		/* Should not happen? */

    if (resDev->rd_fet_subs == (resNode *)NULL)
    {
	resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
	newnode = TRUE;
	resDev->rd_fet_subs = resptr;
    }
    else
	resptr = resDev->rd_fet_subs;

    if (newnode)
    {
	tcell = (tElement *) mallocMagic((unsigned)(sizeof(tElement)));
	tcell->te_nextt = NULL;
	tcell->te_thist = ri->deviceList;
	InitializeResNode(resptr, xj, yj, RES_NODE_DEVICE);
	resptr->rn_te = tcell;
	ResAddToQueue(resptr, PendingList);
    }
    ResNewBreak(resptr, tile, xj, yj, NULL);
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
ResProcessJunction(
    Tile *tile,
    Tile *tp,
    int xj,
    int yj,
    resNode **NodeList)
{
    ResJunction *junction;
    resNode	*resptr;
    jElement    *jcell;
    resInfo	*ri0 = (resInfo *)TiGetClientPTR(tile);
    resInfo	*ri2 = (resInfo *)TiGetClientPTR(tp);

#ifdef PARANOID
    if (tile == tp)
    {
	TxError("Junction being made between tile and itself \n");
	return;
    }
#endif
    if (ri2->ri_status & RES_TILE_DONE) return;
    resptr = (resNode *) mallocMagic((unsigned)(sizeof(resNode)));
    resptr->rn_te = (tElement *) NULL;
    junction = (ResJunction *) mallocMagic((unsigned)(sizeof(ResJunction)));
    jcell = (jElement *) mallocMagic((unsigned)(sizeof(jElement)));
    InitializeResNode(resptr, xj, yj, RES_NODE_JUNCTION);
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
    junction->rj_nextjunction[0] = ri0->junctionList;
    ri0->junctionList = junction;
    junction->rj_nextjunction[1] = ri2->junctionList;
    ri2->junctionList = junction;

    ResNewBreak(junction->rj_jnode, tile, junction->rj_loc.p_x, 
		    junction->rj_loc.p_y, NULL);

    ResNewBreak(junction->rj_jnode, tp, junction->rj_loc.p_x,
		    junction->rj_loc.p_y, NULL);

}
