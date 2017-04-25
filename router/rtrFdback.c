/* rtrFeedback.c -
 *
 *	Code to manage feedback from the channel router.  If there are
 *	errors during channel routing, the router reroutes in a different
 *	direction to try to reduce the number of errors.  It has to save
 *	feedback from these attempts until it knows which version of the
 *	routing is the real one, whose errors should appear in the layout.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/router/rtrFdback.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/styles.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "utils/utils.h"
#include "gcr/gcr.h"
#include "router/router.h"
#include "utils/heap.h"
#include "grouter/grouter.h"
#include "utils/netlist.h"
#include "textio/textio.h"
#include "dbwind/dbwind.h"
#include "utils/malloc.h"

/* This module maintains two linked lists of feedback information, one for
 * each of the possible attempts at channel routing.  The information consists
 * of a box covering the area to be marked with feedback, and the text of
 * a feedback message associated with the area.
 */
typedef struct rtrFb
{
    Rect            rtr_R;	/* Box for feedback paint 	*/
    char          * rtr_c;	/* Text for feedback		*/
    struct rtrFb  * rtr_Fnext;	/* Link to next feedback	*/
} RtrFB;

RtrFB * rtrFList[] = {(RtrFB *) NULL, (RtrFB *) NULL}; /* Lists of feedback */
int     rtrFNum; /* Says which list is active */


/*
 * ----------------------------------------------------------------------------
 *
 * RtrChannelError --
 *
 * 	This utility procedure is provided for use by the channel router
 *	(which isn't supposed to know anything about the rest of Magic).
 *	This procedure just enters a feedback area to describe an error
 *	that occurred at a particular location in a channel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new feedback area is created.
 *	Remembers this channel in the table of channels to be dumped
 *	at the end of routing (for debugging the channel router).
 *
 * ----------------------------------------------------------------------------
 */

void
RtrChannelError(ch, col, track, msg, net)
    GCRChannel *ch;	/* Channel where error occurred. */
    int col;		/* Column where error occurred. */
    int track;		/* Track where error occurred. */
    char *msg;		/* Message identifying error. */
    NLNet *net;		/* Net where error occurred */
{
    Rect box;
    Point old, new;
    char buff[2048];
    char * netname;

    if (net)
    {
	netname = NLNetName(net);
	if ((strlen(netname) + strlen(msg)) >= 2048)
	    netname = "too long";
	(void) sprintf(buff, "Net `%s', channel %p:  ", netname, ch);
    }
    else
    {
	(void) sprintf(buff, "channel %p: ", ch);
    }
    (void) strcat(buff, msg);
    old.p_x = col;
    old.p_y = track;
    GeoTransPoint(&ch->gcr_transform, &old, &new);
    box.r_xbot = ch->gcr_origin.p_x + (new.p_x * RtrGridSpacing) - 2;
    box.r_xtop = box.r_xbot + 4;
    box.r_ybot = ch->gcr_origin.p_y + (new.p_y * RtrGridSpacing) - 2;
    box.r_ytop = box.r_ybot + 4;

    (void) rtrFBAdd(&box, buff);
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrFBClear --
 *
 * 	Initialization for the module, on each channel invocation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory and initializes list pointers to NULL.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrFBClear()
{
    RtrFB * temp;

    for(temp=rtrFList[0]; temp!=(RtrFB *) NULL; rtrFList[0]=temp)
    {
	temp=temp->rtr_Fnext;
	freeMagic(rtrFList[0]->rtr_c);
	freeMagic((char *) rtrFList[0]);
    }
    for(temp=rtrFList[1]; temp!=(RtrFB *) NULL; rtrFList[1]=temp)
    {
	temp=temp->rtr_Fnext;
	freeMagic(rtrFList[1]->rtr_c);
	freeMagic((char *) rtrFList[1]);
    }
    rtrFNum=0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * rtrFBAdd --
 *
 * 	Save the information needed to generate a feedback rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated.  Global variables get changed.
 *
 * ----------------------------------------------------------------------------
 */

void
rtrFBAdd(r, t)
    Rect * r;
    char * t;
{
    RtrFB * new;

    new=(RtrFB *) mallocMagic((unsigned) sizeof(RtrFB));
    ASSERT(new!=(RtrFB *) NULL, "Malloc failed in rtrFBSave");
    new->rtr_R= *r;
    new->rtr_c=StrDup((char **) NULL, t);
    new->rtr_Fnext=rtrFList[rtrFNum];
    rtrFList[rtrFNum]=new;
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrFBPaint --
 *
 * 	Paint the information from the selected list into the given design.
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
RtrFBPaint(num)
    int num;	/* Selects which list to use */
{
    RtrFB *temp;

    for (temp = rtrFList[num]; temp; temp = temp->rtr_Fnext)
	DBWFeedbackAdd(&temp->rtr_R, temp->rtr_c, EditCellUse->cu_def,
		1, STYLE_PALEHIGHLIGHTS);
    rtrFBClear();
}

/*
 * ----------------------------------------------------------------------------
 *
 * RtrFBSwitch --
 *
 * 	Switch from list 0 to list 1 to record feedback patches.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes this module's global keeping track of which list.
 *
 * ----------------------------------------------------------------------------
 */

void
RtrFBSwitch()
{
    ASSERT(rtrFNum==0, "RtrFBSwitch");
    rtrFNum=1;
}
