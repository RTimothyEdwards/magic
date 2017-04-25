/*
 * txcommands.h --
 *
 * 	Declarations for textio command routines.
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
 * rcsid $Header: /usr/cvsroot/magic-8.0/textio/txcommands.h,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 */

#ifndef _TXCOMMANDS_H
#define _TXCOMMANDS_H

#include "utils/magic.h"
#include "utils/geometry.h"

/* Structure of one Magic command.  All commands are in the same format. 
 * Commands are tagged with the point and window at which the command was 
 * invoked.
 */

/* The definitions below should be made variable.  Especially with the
 * Tcl/Tk version of magic, it is possible to auto-generate very long
 * commands such as, for example, "polygon"
 */

#define	TX_MAXARGS	200
#define TX_MAX_CMDLEN	2048

typedef struct {		/* A command -- either a button push or
				 * a textual command.
				 */
    Point tx_p;			/* The location of the pointing device
				 * when this command was invoked. 
				 */
    int tx_button;		/* The event type (button number).
				 * See below.
				 */
    int tx_buttonAction;	/* The action of the button (if any), such as 
				 * TX_BUTTON_UP, DOWN
				 */
    int tx_argc;		/* The number of textual command words,
				 * including the command itself.  0 means
				 * no textual command.
				 */
    char *tx_argv[TX_MAXARGS];	/* An array of pointers to the words (if any)
				 * that make up the command.
				 */
    int tx_wid;			/* The ID of the window that this command
				 * is destined for.  The point 'tx_p' is
				 * in this window's coordinate system.
				 * (See windows.h for window IDs.)
				 */
    char tx_argstring[TX_MAX_CMDLEN];
		 		/* The storage used for the command line.  
				 * Tx_argv[] points into this.
				 */
} TxCommand;

/* Structure of one low-level input event.  This can be either a button event
 * (such as a button down or a button up) or a keyboard event (such as a
 * character typed.
 */

typedef struct {		
    Point txe_p;		/* The point at which this action took place.*/
    int txe_wid;		/* The window that the event occured in (see 
				 * windows.h for details, may be defaulted to
				 * WIND_UNKNOWN_WINDOW by some low-level 
				 * device drivers).
				 */
    int txe_button;		/* The event type. */
    int txe_buttonAction;	/* The button action, if a button. */
    int txe_ch;			/* The character typed, if a character. */
} TxInputEvent;

/* Event types (button numbers) in the above structures, carefully chosen so 
 * there is 1 bit per event (as they are used elsewhere in masks). 
 */
#define TX_CHARACTER		0x00
#define TX_NO_BUTTON		0x00	/* for backward compat. only */
#define TX_LEFT_BUTTON		0x01
#define TX_MIDDLE_BUTTON 	0x02
#define TX_RIGHT_BUTTON		0x04
#define TX_EXPOSE		0x08	/* graphics commands for separate */
#define TX_CONFIGURE		0x10	/* graphics event loops.	  */
#define TX_CREATE		0x20
#define TX_BYPASS		0x40	/* Input line is ready, but needs
					 * and event to trigger the callback
					 */
#define TX_EOF			0x80	/* Never leaves this module, it is
					 * filtered out.
					 */
#define TX_BUTTON_4		0x100	/* For scroll wheels and such */
#define TX_BUTTON_5		0x200	/* For scroll wheels and such */

#ifdef MAGIC_WRAPPER
extern TxCommand TxCurCommand;
#endif

/* Button actions for the above structures.
 */
#define TX_BUTTON_DOWN	0 
#define TX_BUTTON_UP	1
#define TX_KEY_DOWN	2

/* procedures to help in making device command routines */

extern TxCommand *TxDeviceStdin();
extern TxCommand *TxButtonMaskToCommand();
extern void TxAddInputDevice();		/* Can read multiple file desc. */
extern void TxAdd1InputDevice();	/* Can read only 1 file desc. */
extern void TxDeleteInputDevice();	
extern void TxDelete1InputDevice();	

/* Routines to manipulate the current point.  Only really used for command
 * scripts.
 */
extern void TxSetPoint();
extern int TxGetPoint();
extern void TxClearPoint();

/* Routine to set up command logging.
 */
extern void TxLogCommands();


/* Routines for handling input events.  A typical device driver in the
 * graphics module will get one or more free events via TxNewEvent() and
 * then put them in the input queue via TxAddEvent().
 */

extern TxInputEvent *TxNewEvent();
extern void TxAddEvent();
extern void TxPrintEvent();
extern void TxFreeEvent();
extern void TxReleaseButton();

/* Routines for dealing with commands.  Usually only used within this
 * module, although they may be used elsewhere.
 */
extern void TxPrintCommand();
extern TxCommand *TxNewCommand();
extern void TxFreeCommand();
extern void TxParseString();
extern void TxDispatch();
extern int TxCommandNumber;	/* Serial number of current command. */

#ifdef MAGIC_WRAPPER
extern int TxTclDispatch();
#endif

#endif /* _TXCOMMANDS_H */
