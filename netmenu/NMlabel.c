/* NMlabel.c -
 *
 *	This file contains routines that handle the label portion
 *	of net-list menus.  The routines do things like reading
 *	labels from the terminal, placing labels in the design,
 *	re-orienting label text, and incrementing numbers inside
 *	of labels.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/netmenu/NMlabel.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "utils/main.h"
#include "dbwind/dbwind.h"
#include "netmenu/nmInt.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "commands/commands.h"
#include "utils/styles.h"
#include "utils/malloc.h"

/* The user can enter several labels at once, so he doesn't have
 * to keep going back and forth between the mouse and the keyboard.
 * We store the labels in a big array.  At any given time, only one
 * of them is visible in the menu;  only it can be placed in the
 * design.  There are menu buttons for going to the next or previous
 * label.
 */

#define MAXLABELS 100
char * nmLabelArray[MAXLABELS];	/* Holds pointers to all labels
				 * entered together.
				 */
int nmCurLabel;			/* Index of current label. */
int nmNum1 = -1;		/* First two numbers in current label. */
int nmNum2 = -1;
char nmNum1String[12];		/* String equivalents of nmNum1 and nmNum2. */
char nmNum2String[12];


/*
 * ----------------------------------------------------------------------------
 *
 * nmGetNums --
 *
 * 	Picks one or two positive decimal numbers out of a string of text.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The string is scanned for positive decimal numbers.  If any are
 *	found, the first one is put in num1 and the second one is put in
 *	num2.  If there aren't two numbers, num1 and/or num2 will be
 *	set to -1.  This routine ignores "-" signs, so the numbers are
 *	always positive.
 *
 * ----------------------------------------------------------------------------
 */

void
nmGetNums(string, num1, num2)
    char *string;		/* String to scan. */
    int *num1, *num2;		/* Pointers to places to store numbers. */
{
    char *p;
    bool gotNum = FALSE;
    bool gotDigit = FALSE;
    int num = 0;

    *num1 = *num2 = -1;
    for (p = string; ; p++)
    {
	if (isdigit(*p))
	{
	    num = (10*num) + *p - '0';
	    gotDigit = TRUE;
	}
	else if (gotDigit)
	{
	    if (gotNum)
	    {
		*num2 = num;
		return;
	    }
	    *num1 = num;
	    gotDigit = FALSE;
	    gotNum = TRUE;
	    num = 0;
	}
	if (*p == 0) return;
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmPutNums --
 *
 * 	This routine generates a new string by replaces the first two
 *	numbers in an old string with new numbers.
 *
 * Results:
 *	The return value is a pointer to a string that is identical
 *	to src, except that if there are decimal numbers in src, the
 *	first two of those are replaced with the string equivalents
 *	of num1 and num2.  For example, if src is "bus[14,22]" and
 *	num1 is 3 and num2 is 177, the return value will be "bus[3,177]".
 *	Note:  the result is a statically allocated string, so the
 *	caller should copy it before calling this routine again.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

char *
nmPutNums(src, num1, num2)
    char *src;			/* String on which substitution is
				 * to be done. */
    int num1, num2;		/* Numbers to substitute. A number less
				 * than zero means keep the old value.
				 */

{
    static char *result = NULL;
    static int resultLength = 0;
    char num1String[12], num2String[12];
    int spaceNeeded;
    char *pSrc, *pResult;

    /* First, translate the numbers into strings.  Then, make sure
     * we have enough space to store the result.  Allocate a new
     * result area if there isn't enough space.  The space computation
     * is a bit conservative, but simpler that way.
     */
    
    (void) sprintf(num1String, "%d", num1);
    (void) sprintf(num2String, "%d", num2);
    spaceNeeded = strlen(num1String) + strlen(num2String) + strlen(src) + 1;
    if (resultLength < spaceNeeded)
    {
	if (result != NULL) freeMagic(result);
	result = mallocMagic((unsigned) spaceNeeded);
	resultLength = spaceNeeded;
    }

    /* Now scan through the source string.  Copy everything up
     * to the first number into the result.
     */
    
    pSrc = src;
    pResult = result;
    while (!isdigit(*pSrc))
	if ((*pResult++ = *pSrc++) == 0) return result;

    /* Now copy num1String into result and skip the number in
     * the original string.  Or, if num1 is less than zero,
     * then just copy the number from src.
     */
    
    if (num1 < 0)
    {
	while (isdigit(*pSrc)) *pResult++ = *pSrc++;
    }
    else
    {
	while (isdigit(*pSrc)) pSrc += 1;
	(void) strcpy(pResult, num1String);
	while (isdigit(*pResult)) pResult += 1;
    }

    /* Copy more non-digits from source to destination. */

    while (!isdigit(*pSrc))
	if ((*pResult++ = *pSrc++) == 0) return result;
    
    /* Copy the second number. */

    if (num2 < 0)
    {
	while (isdigit(*pSrc)) *pResult++ = *pSrc++;
    }
    else
    {
	while (isdigit(*pSrc)) pSrc += 1;
	(void) strcpy(pResult, num2String);
	while (isdigit(*pResult)) pResult += 1;
    }

    /* Copy the rest of the source to the destination. */

    while (TRUE)
	if ((*pResult++ = *pSrc++) == 0) return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmSetCurrentLabel --
 *
 * 	This is a utility procedure called when the current label changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Number information is extracted from the label, and the menu
 *	is redisplayed.
 *
 * ----------------------------------------------------------------------------
 */

void
nmSetCurrentLabel()
{
    nmGetNums(nmLabelArray[nmCurLabel], &nmNum1, &nmNum2);
    if (nmNum1 >= 0) (void) sprintf(nmNum1String, "%d", nmNum1);
    else nmNum1String[0] = 0;
    if (nmNum2 >= 0) (void) sprintf(nmNum2String, "%d", nmNum2);
    else nmNum2String[0] = 0;

    /* Set up the menu buttons to refer to this information, and
     * redisplay relevant stuff.
     */
    
    NMLabelButton.nmb_text = nmLabelArray[nmCurLabel];
    NMNum1Button.nmb_text = nmNum1String;
    NMNum2Button.nmb_text = nmNum2String;
    if (NMWindow != NULL)
    {
	(void) NMredisplay(NMWindow, &NMLabelButton.nmb_area, (Rect *) NULL);
	(void) NMredisplay(NMWindow, &NMNum1Button.nmb_area, (Rect *) NULL);
	(void) NMredisplay(NMWindow, &NMNum2Button.nmb_area, (Rect *) NULL);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMGetLabels --
 *
 * 	Reads in a bunch of labels from the terminal, and makes the
 *	first of them "current".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The old group of labels is re-allocated, and replaced by a new
 *	group read from the terminal.  The first of the new group is
 *	made the current label in the net-list menu.
 *
 * ----------------------------------------------------------------------------
 */

void
NMGetLabels()
{
#define MAXLENGTH 200
    char line[MAXLENGTH];		/* Holds label temporarily. */
    int i;

    TxPrintf("Enter labels, one per line, terminated by a blank line:\n");
    for (i = 0; i < MAXLABELS; i++)
    {
	if (TxGetLine(line, MAXLENGTH) == NULL) line[0] = 0;
	if (line[0] == 0)
	{
	    /* All done.  If we got any labels at all, null out all
	     * the remaining old labels.
	     */
	    
	    if (i == 0)
	    {
		TxPrintf("No new labels given, so I'll keep the old ones.\n");
		return;
	    }
	    for ( ; i < MAXLABELS; i++)
		(void) StrDup(&(nmLabelArray[i]), (char *) NULL);
	    break;
	}
	(void) StrDup(&(nmLabelArray[i]), line);
    }

    /* Make the first label current, and extract its two numbers. */

    nmCurLabel = 0;
    nmSetCurrentLabel();
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMNextLabel and NMPrevLabel --
 *
 * 	These procedures are invoked in response to button pushes
 *	over the label button.  They make the next or previous label
 *	in the list to be the current one.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current label is changed.
 *
 * ----------------------------------------------------------------------------
 */

void
NMNextLabel()
{
    if (nmLabelArray[nmCurLabel] == NULL)
    {
	TxError("Use the left button to enter labels first.\n");
	return;
    }
    if ((nmCurLabel == MAXLABELS-1) || (nmLabelArray[nmCurLabel+1] == NULL))
	nmCurLabel = 0;
    else nmCurLabel += 1;
    nmSetCurrentLabel();
}

void
NMPrevLabel()
{
    if (nmLabelArray[nmCurLabel] == NULL)
    {
	TxError("Use the left button to enter labels first.\n");
	return;
    }
    if (nmCurLabel == 0)
    {
	for (nmCurLabel = MAXLABELS-1; nmLabelArray[nmCurLabel] == NULL;
	    nmCurLabel -= 1) /* null loop body */;
    }
    else nmCurLabel -= 1;
    nmSetCurrentLabel();
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMChangeNum --
 *
 * 	This procedure increments or decrements one of the two numbers
 *	we've extracted from the current label.  It is called in response
 *	to a button push inside one of the number buttons.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	One of the numbers is incremented or decremented, and the label
 *	information gets redisplayed.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMChangeNum(window,cmd, nmButton, point)
    MagWindow *window;		/* Window in which button was pushed. */
    NetButton *nmButton;	/* NetButton where button was pushed. */
    TxCommand *cmd;		/* Exact command invoked. */
    Point *point;		/* Cursor position in surface coords. */

{
    int *pNum;

    /* Figure out which number is involved. */

    if (nmButton == &NMNum1Button) pNum = &nmNum1;
    else pNum = &nmNum2;

    /* Increment or decrement the number. */

    if (*pNum < 0)
    {
	TxError("That number doesn't exist!\n");
	return;
    }
    if (cmd->tx_button != TX_LEFT_BUTTON) *pNum += 1;
    else
    {
	if (*pNum == 0)
	{
	    TxError("Can't decrement past zero.\n");
	    return;
	}
	*pNum -= 1;
    }

    /* Fill in the new numbers in the string, and update the screen. */

    (void) StrDup(&(nmLabelArray[nmCurLabel]),
	nmPutNums(nmLabelArray[nmCurLabel], nmNum1, nmNum2));
    nmSetCurrentLabel();
}

/*
 * ----------------------------------------------------------------------------
 *
 * nmGetPos --
 *
 * 	This is a utility procedure to determine which label position
 *	was selected on a button push.
 *
 * Results:
 *	The return value is a label position.  This is determined by
 *	dividing the button area into 9 sectors and using the sector
 *	number to select a label position.  The position is transformed
 *	into edit cell coordinates before returning.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

int
nmGetPos(button, point)
    NetButton *button;		/* Button where the mouse was clicked. */
    Point *point;		/* Exact surface position within the button. */
{
    /* The following table maps from sectors to label positions. */

    static int pos[] =
    {
	GEO_SOUTHWEST, 	GEO_SOUTH,	GEO_SOUTHEAST,
	GEO_WEST,	GEO_CENTER,	GEO_EAST,
	GEO_NORTHWEST,	GEO_NORTH,	GEO_NORTHEAST
    };
    int x, y, tmp;

    /* Divide the button area into thirds in x and y, and figure out
     * which sector contains the point.
     */
    
    tmp = (button->nmb_area.r_xtop - button->nmb_area.r_xbot + 1)/3;
    if (point->p_x <= button->nmb_area.r_xbot + tmp) x = 0;
    else if (point->p_x >= button->nmb_area.r_xtop - tmp) x = 2;
    else x = 1;
    tmp = (button->nmb_area.r_ytop - button->nmb_area.r_ybot + 1)/3;
    if (point->p_y <= button->nmb_area.r_ybot + tmp) y = 0;
    else if (point->p_y >= button->nmb_area.r_ytop - tmp) y = 2;
    else y = 1;
    return GeoTransPos(&RootToEditTransform, pos[3*y + x]);
}


/*
 * ----------------------------------------------------------------------------
 *
 * NMPutLabel --
 *
 * 	This procedure is invoked when the left button is pushed over
 *	the button for placing labels.  It places the current label
 *	in the design at the position of the box, and with an orientation
 *	determined by the place where the button was pushed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A label is added to the edit cell.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMPutLabel(window, cmd, nmButton, point)
    MagWindow *window;		/* Window where mouse button was pushed.
				 * (ignored) */
    NetButton *nmButton;	/* Menu button activated. */
    TxCommand *cmd;		/* Complete info about command. (ignored) */
    Point *point;		/* Cursor position in surface coords. */
{
    int pos;
    char *text;

    text = nmLabelArray[nmCurLabel];
    if ((text == NULL) || (*text == 0))
    {
	TxError("Enter some text first (left-button the label entry).\n");
	return;
    }

    pos = nmGetPos(nmButton, point);
    CmdLabelProc(text, -1, 1, 0, 0, 0, pos, 0, (TileType)-1);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMReOrientLabel --
 *
 * 	This procedure is invoked by button pushes inside the net-list
 *	menu.  It resets the text positions of all labels touching the
 *	box.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Label positions are changed.
 *
 * ----------------------------------------------------------------------------
 */

	/* ARGSUSED */
void
NMReOrientLabel(window, cmd, nmButton, point)
    MagWindow *window;		/* Window that was buttoned. (ignored) */
    NetButton *nmButton;	/* Button that was clicked. */
    TxCommand *cmd;		/* Detailed info on command (ignored). */
    Point *point;		/* Cursor position in surface coords. */
{
    int pos;
    Rect editArea;

    /* Make sure that the box exists. */

    if (!ToolGetEditBox(&editArea)) return;
    pos = nmGetPos(nmButton, point);
    DBReOrientLabel(EditCellUse->cu_def, &editArea, pos);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMFindLabel --
 *
 * 	Use the current label as a search pattern and create feedback
 *	areas for all instances of labels with that pattern.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	New feedback areas get created.
 *
 * ----------------------------------------------------------------------------
 */

void
NMFindLabel()
{
    char *pattern;

    pattern = nmLabelArray[nmCurLabel];
    if ((pattern == NULL) || (*pattern == 0))
    {
	TxError("Enter some text first (left-button the label entry).\n");
	return;
    }
    NMShowLabel(pattern, (TileTypeBitMask *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * NMShowLabel --
 *
 * 	This procedure creates feedback for each instance of a label
 *	matching pattern that lies underneath the box.  This procedure
 *	looks at all cells in the hierarchy, expanded or not.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Feedback areas are created and displayed.
 *
 * ----------------------------------------------------------------------------
 */

void
NMShowLabel(pattern, pMask)
    char *pattern;			/* Pattern to be searched for. */
    TileTypeBitMask *pMask;
{
    SearchContext scx;
    MagWindow *w;
    int nmlLabelFunc();

    w = ToolGetBoxWindow(&scx.scx_area, (int *) NULL);
    if (w == NULL)
    {
	TxError("There's no box!  Please use the box to select\n");
	TxError("the area to search for a label match.\n");
	return;
    }

    scx.scx_use = (CellUse *) w->w_surfaceID;
    scx.scx_trans = GeoIdentityTransform;
    if (pMask == NULL)
	pMask = &DBAllTypeBits;

    (void) DBSearchLabel(&scx, pMask, 0, pattern,
	nmlLabelFunc, (ClientData) scx.scx_use->cu_def);
}

	/* ARGSUSED */
int
nmlLabelFunc(scx, label, tpath, rootDef)
    SearchContext *scx;			/* Describes state of search. */
    Label *label;			/* Pointer to label found. */
    TerminalPath *tpath;		/* Not used. */
    CellDef *rootDef;			/* Root def for search. */
{
    char mesg[2048];
    Rect rootArea;
    int left;

    /* Finish generating the full hierarchical name */
    left = tpath->tp_last - tpath->tp_next - 1;
    (void) strncpy(tpath->tp_next, label->lab_text, left);
    tpath->tp_next[left] = '\0';
    (void) sprintf(mesg, "%s;%s",
	DBTypeShortName(label->lab_type), tpath->tp_first);

    GeoTransRect(&scx->scx_trans, &label->lab_rect, &rootArea);
    GEO_EXPAND(&rootArea, 1, &rootArea);
    DBWFeedbackAdd(&rootArea, mesg, rootDef, 1,
	STYLE_PALEHIGHLIGHTS);
    return 0;
}
