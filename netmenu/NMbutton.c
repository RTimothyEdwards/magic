/* NMbutton.c -
 *
 *	This file contains routines that respond to button pushes
 *	in database windows when the netlist button handler is active.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMbutton.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "dbwind/dbwind.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "netmenu/nmInt.h"
#include "netmenu/netmenu.h"
#include "utils/styles.h"
#include "utils/main.h"

/* The following static holds the name of the current net. */

char *NMCurNetName = NULL;

/* Maximimum amount of storage to hold terminal name: */

#define MAXTERMLENGTH 200

/*
 * ----------------------------------------------------------------------------
 *
 * NMButtonNetList --
 *
 * 	This procedure is invoked when the button to switch netlists
 *	is clicked.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The user is asked to type in a netlist name, and this name is
 *	used as the current netlist.  If the right button was clicked,
 *	use the name of the edit cell as the name of the netlist.
 *
 * ----------------------------------------------------------------------------
 */

void
NMButtonNetList(window, cmd, nmButton, point)
    MagWindow *window;		/* Where button was clicked (not used). */
    NetButton *nmButton;	/* Data structure for button (not used). */
    TxCommand *cmd;		/* Used to figure out which button it was. */
    Point *point;		/* Not used. */
{
#define MAXLENGTH 200
    char newName[MAXLENGTH];
    if (cmd->tx_button == TX_RIGHT_BUTTON)
	NMNewNetlist(EditCellUse->cu_def->cd_name);
    else
    {
	TxPrintf("New net list name: ");
	if (TxGetLine(newName, MAXLENGTH) == NULL) newName[0] == 0;
	if (newName[0] == 0) return;
	NMNewNetlist(newName);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmButtonSetup --
 *
 * 	This procedure does miscellaneous button dirty work.  It
 *	makes sure that there's a current netlist (and prints an
 *	error if not), and finds the nearest terminal to the point
 *	location.
 *
 * Results:
 *	If everything is OK, the return value is a pointer to a
 *	statically-allocated string holding the nearest terminal
 *	location.  If there's any problem, NULL is returned.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
nmButtonSetup()
{
    Point rootPoint, editPoint;
    Rect rootArea, searchArea, tmp1, tmp2;
    int expand;
    static char termName[MAXTERMLENGTH];
    MagWindow *w;

#define SEARCHPIXELS 20

    if (NMNetListButton.nmb_text == NULL)
    {
	TxError("There's no current netlist;  please select one.\n");
	return NULL;
    }

    /* Compute an area (in edit cell coordinates) that's SEARCHPIXELS
     * around the cursor position, so that anything even close can
     * be used to find a terminal.
     */

    w = ToolGetPoint(&rootPoint, &rootArea);
    if (w == NULL) return NULL;
    if (((CellUse *)(w->w_surfaceID))->cu_def != EditRootDef)
    {
	TxError("Sorry, but you have to use a window that's being edited.\n");
	return NULL;
    }
    tmp1.r_xbot = tmp1.r_ybot = tmp1.r_ytop = 0;
    tmp1.r_xtop = SEARCHPIXELS;
    WindScreenToSurface(w, &tmp1, &tmp2);
    expand = tmp2.r_xtop - tmp2.r_xbot;
    rootArea.r_xbot -= expand;
    rootArea.r_xtop += expand;
    rootArea.r_ybot -= expand;
    rootArea.r_ytop += expand;
    GeoTransPoint(&RootToEditTransform, &rootPoint, &editPoint);
    GeoTransRect(&RootToEditTransform, &rootArea, &searchArea);

    if (!DBNearestLabel(EditCellUse, &searchArea, &editPoint, 0,
	(Rect *) NULL, termName, MAXTERMLENGTH))
    {
	TxPrintf("There's no terminal near the cursor.\n");
	return NULL;
    }

    if(strchr(termName, '/')==0)
    {
	TxPrintf("You can't route to a terminal in the Edit cell!");
	TxPrintf("  Please select one in a subcell.\n");
	return NULL;
    }
    return termName;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMButtonRight --
 *
 * 	This procedure is invoked when the right button is pushed over
 *	a database window.  It toggles the nearest terminal into or
 *	out of the current net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A terminal is added to or removed from a net, and the highlight
 *	information on the display is updated.
 *
 * ----------------------------------------------------------------------------
 */

void
NMButtonRight(w, cmd)
    MagWindow *w;			/* Window in which button was pushed. */
    TxCommand *cmd;		/* Detailed information about command. */
{
    char *name;
    extern int nmButHighlightFunc(), nmButUnHighlightFunc();
    extern int nmButCheckFunc(), nmNewRefFunc(), nmFindNetNameFunc();

    name = nmButtonSetup();
    if (name == NULL) return;

    if (NMCurNetName == NULL)
    {
	TxError("Use the left button to select a net first.\n");
	return;
    }

    /* See if this terminal is already in the current net. */
    
    if (NMEnumTerms(name, nmButCheckFunc, (ClientData) NMCurNetName))
    {
	/* In the net already.  Delete it from the net.  But first,
	 * find another terminal in the net to use as a reference
	 * for the current net.  If not, then null out the current net.
	 */
	
	if (strcmp(name, NMCurNetName) == 0)
	{
	    NMSelectNet((char *) NULL);
	    (void) NMEnumTerms(name, nmNewRefFunc, (ClientData) name);
	}
	NMUndo(name, NMCurNetName, NMUE_REMOVE);
	(void) NMDeleteTerm(name);
	(void) DBSrLabelLoc(EditCellUse, name, nmButUnHighlightFunc,
	    (ClientData) NULL);
	TxPrintf("Removing \"%s\" from net.\n", name);
    }
    else
    {
	/* Not in the net already:  add it in.  But first, see if
	 * the terminal is already in a net.  If it is, then remove
	 * it from that net.
	 */
	
	if (NMTermInList(name) != NULL)
	{
	    char *netName = name;
	    (void) NMEnumTerms(name, nmFindNetNameFunc, (ClientData) &netName);
	    if (netName != name)
	    {
		TxPrintf("\"%s\" was already in a net;", name);
		TxPrintf("  I'm removing it from the old net.\n");
	    }
	    NMUndo(name, netName, NMUE_REMOVE);
	    (void) NMDeleteTerm(name);
	}
	NMUndo(name, NMCurNetName, NMUE_ADD);
	(void) NMAddTerm(name, NMCurNetName);
	(void) DBSrLabelLoc(EditCellUse, name, nmButHighlightFunc,
	    (ClientData) NULL);
	TxPrintf("Adding \"%s\" to net.\n", name);
    }
}

/* This check function merely returns TRUE if a terminal in
 * the net being enumerated matches some other given terminal.
 */

int
nmButCheckFunc(name1, name2)
    char *name1;		/* Name of terminal in net. */
    char *name2;		/* Name of other terminal. */
{
    if (strcmp(name1, name2) == 0) return 1;
    return 0;
}

/* This function looks for a name of a terminal in a net that
 * is different from a given name.  The different name, if any,
 * is stored in the clientdata.
 */

int
nmFindNetNameFunc(name1, pname2)
    char *name1;		/* Name of terminal in net. */
    char **pname2;		/* Pointer to name to be replaced with
				 * different name, if there is a different
				 * name in the net.
				 */
{
    if (strcmp(name1, *pname2) == 0) return 0;
    *pname2 = name1;
    return 1;			/* Abort search with new name. */
}

	/* ARGSUSED */
int
nmButHighlightFunc(area, name, label, pExists)
    Rect  *area;		/* Area of the label. */
    char  *name;		/* Name of label. */
    Label *label;		/* Pointer to label */
    bool  *pExists;		/* We just set this to TRUE. */
{
    Rect rootArea;
    Point point;

    GeoTransRect(&EditToRootTransform, area, &rootArea);
    point.p_x = (rootArea.r_xbot + rootArea.r_xtop)/2;
    point.p_y = (rootArea.r_ybot + rootArea.r_ytop)/2;
    NMAddPoint(&point);
    if (pExists != NULL) *pExists = TRUE;
    return 0;
}

int
nmButUnHighlightFunc(area)
    Rect *area;			/* Area of the label. */
{
    Rect rootArea;
    Point point;

    GeoTransRect(&EditToRootTransform, area, &rootArea);
    point.p_x = (rootArea.r_xbot + rootArea.r_xtop)/2;
    point.p_y = (rootArea.r_ybot + rootArea.r_ytop)/2;
    NMDeletePoint(&point);
    return 0;
}

int
nmNewRefFunc(name, oldRef)
    char *name;			/* Name of a terminal in the net. */
    char *oldRef;		/* Name we don't want to use as net reference
				 * anymore.
				 */
{
    if (strcmp(name, oldRef) != 0)
    {
	NMSelectNet(name);
	return 1;		/* Abort search: we've found what we want. */
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMSelectNet --
 *
 * 	This routine selects a net and highlights it on the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The net containing terminal is selected.  If there used to
 *	be a net selected, it is unselected.  If name is NULL,
 *	all this procedure does is to unselect the previous net.
 *
 * ----------------------------------------------------------------------------
 */

void
NMSelectNet(name)
    char *name;			/* Gives name of terminal in net to be
				 * be selected.
				 */
{
    extern int nmSelNetFunc();

    NMUndo(name, NMCurNetName, NMUE_SELECT);
    NMCurNetName = NULL;
    NMClearPoints();

    if (name == NULL) return;

    /* If this terminal isn't already in a net, return. */

    NMCurNetName = NMTermInList(name);
    TxPrintf("Selected net is now \"%s\".\n", NMCurNetName);
    if (NMCurNetName == NULL) return;

    /* Highlight the entire net. */

    (void) NMEnumTerms(name, nmSelNetFunc, (ClientData) NULL);
}

/* For each terminal in the net, highlight each instance of the terminal. */

int
nmSelNetFunc(name)
    char *name;
{
    bool exists;

    exists = FALSE;
    (void) DBSrLabelLoc(EditCellUse, name, nmButHighlightFunc,
	(ClientData) &exists);
    if (!exists) TxPrintf("%s: not in circuit!\n", name);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMButtonLeft --
 *
 * 	This procedure is invoked when the left button is pushed
 *	over a database window.  It selects a new net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If there is a terminal near the cursor, it is selected as the
 *	current net and is highlighted on the screen.
 *
 * ----------------------------------------------------------------------------
 */

void
NMButtonLeft(w, cmd)
    MagWindow *w;			/* Window in which button was pushed. */
    TxCommand *cmd;		/* Detailed information about the command. */
{
    char *name;

    name = nmButtonSetup();
    if (name == NULL)
    {
	NMSelectNet((char *) NULL);
	return;
    }

    /* If this terminal isn't already in a net, start a new net. */

    if (NMTermInList(name) == NULL) (void) NMAddTerm(name, name);
    NMSelectNet(name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMButtonMiddle --
 *
 * 	This procedure is invoked when the middle button is pressed in
 *	a database window.  It joins the net of the nearest terminal
 *	into the current net.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Two nets are joined together.  The highlights on the screen
 *	reflect this.
 *
 * ----------------------------------------------------------------------------
 */

void
NMButtonMiddle(w, cmd)
    MagWindow *w;		/* Window in which button was pushed. */
    TxCommand *cmd;	/* Detailed information about command. */
{
    char *name;

    name = nmButtonSetup();
    if (name == NULL) return;

    if (NMCurNetName == NULL)
    {
	TxPrintf("Use the left button to select a name first.\n");
	return;
    }

    /* Go through the new terminal's net and add everything to
     * the list of stuff to be redisplayed.  After this is done,
     * join the nets.  Note:  if the terminal isn't in any net
     * at all, make a new terminal.
     */
    
    if (NMTermInList(name) == NULL) (void) NMAddTerm(name, name);
    (void) NMEnumTerms(name, nmSelNetFunc, (ClientData) NULL);
    NMJoinNets(name, NMCurNetName);

    TxPrintf("Merging net \"%s\" into current net.\n", name);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMButtonProc --
 *
 * 	This is an alternate handler for button pushes in database
 *	windows.  It is invoked from the dbwind module when netlist
 *	editing is in progress.  This procedure just dispatches to
 *	the correct command procedure for the button.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Whatever the command procedure for the button does.
 *
 * ----------------------------------------------------------------------------
 */

void
NMButtonProc(w, cmd)
    MagWindow *w;		/* Window in which button was pushed. */
    TxCommand *cmd;	/* Detailed information about exactly what happened. */
{
    if (cmd->tx_buttonAction != TX_BUTTON_DOWN) return;
    switch (cmd->tx_button)
    {
	case TX_LEFT_BUTTON:
	    NMButtonLeft(w, cmd);
	    break;
	case TX_RIGHT_BUTTON:
	    NMButtonRight(w, cmd);
	    break;
	case TX_MIDDLE_BUTTON:
	    NMButtonMiddle(w, cmd);
	    break;
    }
}
