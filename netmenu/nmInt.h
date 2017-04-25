/*
 * nmInt.h --
 *
 * Defines things needed inside the netmenu module, but
 * not needed outside the module.
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
 *
 *
 * rcsid "$Header: /usr/cvsroot/magic-8.0/netmenu/nmInt.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $"
 */

#ifndef _NMINT_H
#define _NMINT_H

#include "windows/windows.h"

/* The declarations below define the elements displayed in the
 * net-list menu.  There are three kinds of things:
 *
 * 1. Buttons:  these are boxes, drawn with a solid color fill
 *    and a black outline and (possibly) text centered in the
 *    middle.  Each button corresponds to a command or commands.
 *
 * 2. Extra text:  this is just extra text, displayed to make the
 *    menu more readable.
 *
 * 3. Extra boxes:  more stuff, not necessarily corresponding to
 *    commands, that makes the menu more readable.
 *
 */

typedef struct
{
    char *nmb_text;		/* Text to display in button. */
    int nmb_style;		/* Style for color fill. */
    Rect nmb_area;		/* Area of button. */
    void (*nmb_leftDown)();	/* Procedure to call when left button
			         * goes down in button.  NULL means don't
			         * call anything on this transition.  The
				 * procedure gets passed four parameters:
				 * the window, the address of this NetButton,
				 * the TxCommand, and the cursor position in
				 * surface coords.  See NMcommand's code for
				 * details.
			         */
    void (*nmb_leftUp)();
    void (*nmb_middleDown)();
    void (*nmb_middleUp)();
    void (*nmb_rightDown)();
    void (*nmb_rightUp)();
} NetButton;

typedef struct
{
    char *nml_text;		/* Text to display. */
    int nml_style;		/* Style for text. */
    Rect nml_area;		/* Bounding area for text. */
} NetLabel;

typedef struct
{
    int nmr_style;		/* Style for rectangle. */
    Rect nmr_area;		/* Rectangle's area. */
} NetRect;

/* Button action procedures: */

extern void NMGetLabels();
extern void NMNextLabel();
extern void NMPrevLabel();
extern void NMChangeNum();
extern void NMPutLabel();
extern void NMReOrientLabel();
extern void NMSwitchHandler();
extern void NMButtonNetList();
extern void NMButtonDisconnect();
extern void NMButtonPrint();
extern void NMFindLabel();
extern void NMButtonProc();

/* Command routines: */

extern void NMCmdAdd(), NMCmdCleanup(), NMCmdCull(), NMCmdDnet(),
    NMCmdDterm(), NMCmdExtract(), NMCmdFindLabels(), NMCmdFlush(),
    NMCmdJoinNets(),
#ifdef ROUTE_MODULE
    NMCmdMeasure(),
#endif
    NMCmdNetlist(), NMCmdPrint(), NMCmdPushButton(), NMCmdRipup(),
    NMCmdSavenetlist(), NMCmdShownet(), NMCmdShowterms(),
    NMCmdTrace(), NMCmdVerify(), NMCmdWriteall();

extern NetButton NMButtons[];
extern MagWindow *NMWindow;

extern void NMAddPoint();
extern void NMDeletePoint();
extern void NMClearPoints();
extern void NMUpdatePoints();
extern int NMRedrawPoints();

extern int NMRedrawCell();
extern void NMShowCell();
extern void NMUnsetCell();
extern void NMShowUnderBox();
extern int NMShowRoutedNet();
extern void NMShowLabel();

extern int NMRipup();
extern int NMRipupList();
extern int NMExtract();
extern void NMSelectNet();

extern bool NMCheckWritten();
extern void NMWriteAll();

extern void NMUndo();
extern void NMUndoInit();

/* Various global variables (within this module): */

extern char * NMCurNetName;

#define NMLabelButton (NMButtons[0])
#define NMNum1Button (NMButtons[2])
#define NMNum2Button (NMButtons[3])
#define NMNetListButton (NMButtons[5])

/* Arguments for the routine NMUndo: */

#define NMUE_ADD 1
#define NMUE_REMOVE 2
#define NMUE_SELECT 3
#define NMUE_NETLIST 4

#endif /* _NMINT_H */
