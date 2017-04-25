/* NMundo.c -
 *
 *	Provides procedures and data structures to make net-list
 *	modifications undo-able.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMundo.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "netmenu/netmenu.h"
#include "netmenu/nmInt.h"
#include "utils/undo.h"
#include "utils/utils.h"

/* Handle for our kind of undo operation. */

static UndoType nmUndoClientID;

/* The following structure describes undo information about a
 * single net-list undo event.
 */

typedef struct
{
    int nmue_type;		/* Type of undo event;  see the header for
				 * NMUndo for a description.
				 */
    char *nmue_term;		/* A name of a terminal or new net-list. */
    char *nmue_curNet;		/* Name of current net or net-list. */
    char nmue_storage[4];	/* Used to store the actual strings for
				 * nmue_term and nmue_curNet.  May have
				 * any length.
				 */
} NMUndoEvent;

/* The following variable merely records whether any net-list-related
 * undo events have been processed.
 */

bool nmUndoCalled = FALSE;

/*
 * ----------------------------------------------------------------------------
 *
 * NMUndo --
 *
 * 	Records an undo event.  Type selects which of three net-list
 *	modifications is being recorded:
 *	NMUE_ADD:	term was added to the net of curNet
 *	NMUE_REMORE:	term was removed from net of curNet
 *	NMUE_SELECT:	the net of term replaces the net of curNet as
 *			current net.
 *	NMUE_NETLIST:	term is used as the name of a new current net-list
 *			which replaces the current net-list, given by
 *			curNet.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An event is added to the undo list, so that the modifications
 *	can be undone and/or redone later.
 *
 * ----------------------------------------------------------------------------
 */

void
NMUndo(term, curNet, type)
    char *term;			/* A name of a terminal. */
    char *curNet;		/* The name of the current net-list. */
    int type;			/* The type of thing that is being logged */
{
    NMUndoEvent *u;
    int l1, l2;

    if (term != NULL) l1 = strlen(term);
    else l1 = 0;
    if (curNet != NULL) l2 = strlen(curNet);
    else l2 = 0;

    u = (NMUndoEvent *) UndoNewEvent(nmUndoClientID,
	(unsigned) (sizeof(NMUndoEvent) + l1 + l2 + 2));
    if (u == NULL) return;

    u->nmue_type = type;
    if (term != NULL)
    {
	u->nmue_term = u->nmue_storage;
	(void) strcpy(u->nmue_term, term);
    }
    else u->nmue_term = NULL;
    if (curNet != NULL)
    {
	u->nmue_curNet = u->nmue_storage + l1 + 1;
        (void) strcpy(u->nmue_curNet, curNet);
    }
    else u->nmue_curNet = NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmUndoForw --
 * nmUndoBack --
 *
 * 	These procedures play net-list undo events forward or backwards.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A net is modified or the current net is changed.
 *
 * ----------------------------------------------------------------------------
 */

void
nmUndoForw(u)
    NMUndoEvent *u;		/* Pointer to an undo event. */
{
    nmUndoCalled = TRUE;
    switch (u->nmue_type)
    {
	case NMUE_ADD:
	    (void) NMAddTerm(u->nmue_term, u->nmue_curNet);
	    break;
	case NMUE_REMOVE:
	    (void) NMDeleteTerm(u->nmue_term);
	    break;
	case NMUE_SELECT:
	    (void) NMSelectNet(u->nmue_term);
	    break;
	case NMUE_NETLIST:
	    (void) NMNewNetlist(u->nmue_term);
	    break;
    }
}

void
nmUndoBack(u)
    NMUndoEvent *u;		/* Pointer to an undo event. */
{
    nmUndoCalled = TRUE;
    switch(u->nmue_type)
    {
	case NMUE_ADD:
	    (void) NMDeleteTerm(u->nmue_term);
	    break;
	case NMUE_REMOVE:
	    (void) NMAddTerm(u->nmue_term, u->nmue_curNet);
	    break;
	case NMUE_SELECT:
	    (void) NMSelectNet(u->nmue_curNet);
	    break;
	case NMUE_NETLIST:
	    (void) NMNewNetlist(u->nmue_curNet);
	    break;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmUndoDone --
 *
 * 	This procedure is called at the very end of undoing or
 *	redoing something.  It just redisplays the current net
 *	if there have been any net-list-related events processed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current net is redisplayed.
 *
 * ----------------------------------------------------------------------------
 */

void
nmUndoDone()
{
    if (nmUndoCalled)
    {
	UndoDisable();
	NMSelectNet(NMCurNetName);
	UndoEnable();
    }
    nmUndoCalled = FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMUndoInit --
 *
 * 	Sets up this module to handle undo-ing and redo-ing of net-list
 *	stuff.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff gets initialized in the undo package.
 *
 * ----------------------------------------------------------------------------
 */

void
NMUndoInit()
{
    nmUndoClientID = UndoAddClient((void (*)()) NULL, nmUndoDone,
	(UndoEvent *(*)()) NULL, (int (*)()) NULL,
	nmUndoForw, nmUndoBack, "net-list");
}
