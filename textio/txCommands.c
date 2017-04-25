/*
 * txCommands.c --
 *
 * 	Reads commands from devices and sends them to the window package,
 *	which sends them to a particular window. 
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
static char rcsid[] __attribute__ ((unused)) ="$Header: /usr/cvsroot/magic-8.0/textio/txCommands.c,v 1.6 2009/09/11 16:09:49 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>

#include "utils/magsgtty.h"
#include "utils/magic.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "textio/txcommands.h"
#include "textio/textioInt.h"
#include "utils/macros.h"
#include "windows/windows.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "dbwind/dbwind.h"
#include "drc/drc.h"
#include "utils/signals.h"
#include "graphics/graphics.h"
#include "utils/dqueue.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "lisp/lisp.h"

/* Turning this flag on prints out input events and commands as they
 * are processed.
 */

bool TxDebug = FALSE;

/* A mask of the file descriptors for all input devices.
 */
static fd_set txInputDescriptors;

/* A table of all input devices.
 */
static txInputDevRec txInputDevice[TX_MAX_OPEN_FILES];
static int txLastInputEntry = -1;

/* The current point -- reset by the 'setpoint' command and for each
 * interactive command.  Calls to TxClearPoint clear previous setpoints,
 *
 * Each input point is associated with a window, as windows may use 
 * different coordinate systems.
 *
 * Also, keep around the last input event.
 */
static bool txHaveCurrentPoint = FALSE;
static Point txCurrentPoint = {100, 100};
static int txCurrentWindowID = WIND_UNKNOWN_WINDOW;
TxInputEvent txLastEvent;


/* Input queues.  We have an input queue for low-level input events, and
 * a queue for assembled interactive commands and file commands.  Also, there 
 * are free lists for these structures.
 */

#ifdef MAGIC_WRAPPER
extern void send_interpreter(char *);

char *TxBuffer;			/* For use with special stdin processing */
unsigned char TxInputRedirect;	/* For redirecting Tk events to the terminal */
#endif

DQueue txInputEvents;
DQueue txFreeEvents;
int txNumInputEvents;	/* Number of events recieved by Magic so far. */

DQueue txFreeCommands;

/* A zero time value, used for doing a poll via 'select()'.
 */

static struct timeval txZeroTime;


/* Mask of buttons down, as of the last command in the queue (not the last 
 * command executed).
 */
int TxCurButtons = 0;  

/* 
 * Commands are numbered sequentially starting at zero.  This number says
 * which command we are collecting or executing.
 */

int TxCommandNumber = 0;

/*
 * The "cmd" structure is shared by lisp and eval using this pointer
 */
static TxCommand *lisp_cur_cmd = NULL;

/*
 * ----------------------------------------------------------------------------
 *
 * FD_IsZero --
 * FD_OrSet --
 *
 *	Routines for manipulating set of file descriptors.
 *
 * ----------------------------------------------------------------------------
 */

bool
FD_IsZero(fdmask) 
    fd_set fdmask;
{
    int i;
    for (i = 0; i <= TX_MAX_OPEN_FILES; i++)
	if (FD_ISSET(i, &fdmask)) return FALSE;
    return TRUE;
}

void
FD_OrSet(fdmask, dst) 
    fd_set fdmask;
    fd_set *dst;
{
    int i;
    for (i = 0; i <= TX_MAX_OPEN_FILES; i++)
	if (FD_ISSET(i, &fdmask)) FD_SET(i, dst);
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxReleaseButton --
 *
 *	Pretend that a certain button is up, even though we think that it is
 *	down.  Used only in rare circumstances, such as when SunWindows
 *	reads a button push behind our back.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
TxReleaseButton(but)
    int but;
{
    TxCurButtons &= ~but;
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxPrintEvent --
 *
 *	Print an event's contents to stderr.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Text appears on the terminal.
 *
 * ----------------------------------------------------------------------------
 */

void
TxPrintEvent(event)
    TxInputEvent *event;
{
    TxError("Input event at 0x%x\n    ", event);
    if (event->txe_button == TX_EOF) {
	TxError("EOF event");
    } else if (event->txe_button == TX_BYPASS) {
	TxError("Bypass event");
    } else if (event->txe_button == TX_CHARACTER) {
	char *strc = MacroName(event->txe_ch);
	TxError("Character '%s'", strc);
	freeMagic(strc);
    } else {
	switch (event->txe_button) {
	    case TX_LEFT_BUTTON: {TxError("Left button"); break;};
	    case TX_MIDDLE_BUTTON: {TxError("Middle button"); break;};
	    case TX_RIGHT_BUTTON: {TxError("Right button"); break;};
	    default: {TxError("UNKNOWN button"); break;};
	}
	switch (event->txe_buttonAction) {
	    case TX_BUTTON_UP: {TxError(" up"); break;};
	    case TX_BUTTON_DOWN: {TxError(" down"); break;};
	    default: {TxError(" UNKNOWN-ACTION"); break;};
	}
    }
    TxError(" at (%d, %d)\n    Window: ", event->txe_p.p_x, event->txe_p.p_y);
    switch (event->txe_wid) {
	case WIND_UNKNOWN_WINDOW: {TxError("unknown\n"); break;};
	case WIND_NO_WINDOW: {TxError("none\n"); break;};
	default: {TxError("%d\n", event->txe_wid); break;};
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxPrintCommand --
 *
 *	Print a command's contents to stderr.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Text appears on the terminal.
 *	Repaired so that unprintable text does NOT appear (March 2000, Tim)
 *
 * ----------------------------------------------------------------------------
 */

void
TxPrintCommand(cmd)
    TxCommand *cmd;
{
    int i, j;
    char TxTemp[200];

    TxError("Command at 0x%x\n    ", cmd);
    if (cmd->tx_button == TX_CHARACTER) {
	TxError("Text command with %d words: ", cmd->tx_argc);
	for (i = 0; i < cmd->tx_argc; i++) {
	    for (j = 0; cmd->tx_argv[i][j] && (j < 199); j++)
	       TxTemp[j] = isprint(cmd->tx_argv[i][j]) ? cmd->tx_argv[i][j] : '*';
	    TxTemp[j] = '\0';
	    TxError(" \"%s\"", TxTemp);
	}
    } else {
	switch (cmd->tx_button) {
	    case TX_LEFT_BUTTON: {TxError("Left button"); break;};
	    case TX_MIDDLE_BUTTON: {TxError("Middle button"); break;};
	    case TX_RIGHT_BUTTON: {TxError("Right button"); break;};
	    default: {TxError("UNKNOWN button"); break;};
	}
	switch (cmd->tx_buttonAction) {
	    case TX_BUTTON_UP: {TxError(" up"); break;};
	    case TX_BUTTON_DOWN: {TxError(" down"); break;};
	    default: {TxError(" UNKNOWN-ACTION"); break;};
	}
    }
    TxError(" at (%d, %d)\n    Window: ", cmd->tx_p.p_x, cmd->tx_p.p_y);
    switch (cmd->tx_wid) {
	case WIND_UNKNOWN_WINDOW: {TxError("unknown\n"); break;};
	case WIND_NO_WINDOW: {TxError("none\n"); break;};
	default: {TxError("%d\n", cmd->tx_wid); break;};
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxNewEvent --
 *
 *	Get a new event, ready to be filled in.
 *
 * Results:
 *	A pointer to a new event.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

TxInputEvent *
TxNewEvent()
{
    TxInputEvent *event;
    event = (TxInputEvent *) DQPopFront(&txFreeEvents);
    if (event == NULL) event = (TxInputEvent *) mallocMagic(sizeof(TxInputEvent));
    event->txe_button = TX_CHARACTER;
    event->txe_buttonAction = TX_BUTTON_UP;
    event->txe_wid = WIND_UNKNOWN_WINDOW;
    event->txe_p.p_x = GR_CURSOR_X;
    event->txe_p.p_y = GR_CURSOR_Y;
    event->txe_ch = 0;
    return event;
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxAddEvent --
 *
 *	Add a new event into our input queue.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
TxAddEvent(event)
    TxInputEvent *event;
{
    ASSERT(event != NULL, "TxAddEvent");
    DQPushRear(&txInputEvents, (ClientData) event);
    txNumInputEvents++;
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxFreeEvent --
 *
 *	Free an event.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
TxFreeEvent(event)
    TxInputEvent *event;
{
    ASSERT(event != NULL, "TxFreeEvent");
    DQPushRear(&txFreeEvents, (ClientData) event);
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxNewCommand --
 *
 *	Get a new command, ready to be filled in.
 *
 * Results:
 *	A pointer to a new command.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

TxCommand *
TxNewCommand()
{
    TxCommand *command;
    command = (TxCommand *) DQPopFront(&txFreeCommands);
    if (command == NULL) 
	command = (TxCommand *) mallocMagic(sizeof(TxCommand));
    command->tx_button = TX_CHARACTER;
    return command;
}


/*
 * ----------------------------------------------------------------------------
 *
 * TxFreeCommand --
 *
 *	Free a command.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
TxFreeCommand(command)
    TxCommand *command;
{
    ASSERT(command != NULL, "TxFreeCommand");
#ifdef MAGIC_WRAPPER
    /* No Tx Queue in MAGIC_WRAPPER; call is just to free memory */
    freeMagic((char *)command);
#else
    DQPushRear(&txFreeCommands, (ClientData) command);
#endif
}

/*
 * ----------------------------------------------------------------------------
 * TxAddInputDevice --
 * TxAdd1InputDevice --
 *
 *	Add a device which is able to deliever commands to magic.
 *	The caller must ensure that SIGIO signals will be send whenever
 *	this file descriptor becomes ready.
 *
 *	The usual form of inputProc is:
 *
 *		proc(fd, cdata)
 *		    int fd;	-- file descriptor that is ready
 *		    ClientData; -- ClientData as passed to TxAddInputDevice()
 *		{
 *		    TxInputEvent *event;
 *		    event = TxNewEvent();
 *		    -- read fd here, and fill in event
 *		    TxAddEvent(event);
 *		    -- might do this more than once for multiple events
 *		}
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies internal tables.
 * ----------------------------------------------------------------------------
 */

void
TxAddInputDevice(fdmask, inputProc, cdata)
    fd_set fdmask;		/* A mask of file descriptors that this
				 * device will handle.
				 */
    void (*inputProc)();	/* A routine to call.  This routine will
				 * be passed a single file descriptor that
				 * is ready, and should read that file and
				 * add events(s) by calling TxNewEvent() 
				 * followed by TxAddEvent().
				 */
    ClientData cdata;		/* Will be passed back to the proc whenever
				 * it is called.
				 */
{
    int i;
    TxDeleteInputDevice(fdmask);
    if (txLastInputEntry + 1 == TX_MAX_OPEN_FILES)
    {
	TxError("Too many input devices.\n");
	return;
    }
    txLastInputEntry++;
    txInputDevice[txLastInputEntry].tx_fdmask = fdmask;
    txInputDevice[txLastInputEntry].tx_inputProc = inputProc;
    txInputDevice[txLastInputEntry].tx_cdata = cdata;
    FD_OrSet(fdmask, &txInputDescriptors);
}

void
TxAdd1InputDevice(fd, inputProc, cdata)
    int fd;		
    void (*inputProc)();	
    ClientData cdata;		
{
    fd_set fs;
    FD_ZERO(&fs);
    FD_SET(fd, &fs);
    TxAddInputDevice(fs, inputProc, cdata);
}

/*
 * ----------------------------------------------------------------------------
 * TxDeleteInputDevice --
 *
 *	Delete an input device from our tables.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	modifies internal tables.
 * ----------------------------------------------------------------------------
 */

void
TxDeleteInputDevice(fdmask)
    fd_set fdmask;		/* A mask of file descriptors that are
				 * no longer active.
				 */
{
    int i;

    for (i = 0; i <= TX_MAX_OPEN_FILES; i++)
	if (FD_ISSET(i, &fdmask)) TxDelete1InputDevice(i);
}

void
TxDelete1InputDevice(fd)
    int fd;		
{
    int i, j;

    for (i = 0; i <= txLastInputEntry; i++)
    {
	FD_CLR(fd, &(txInputDevice[i].tx_fdmask));
	if (FD_IsZero(txInputDevice[i].tx_fdmask))
	{
	    for (j = i+1; j <= txLastInputEntry; j++)
		txInputDevice[j-1] = txInputDevice[j];
	    txLastInputEntry--;
	}
    }
    FD_CLR(fd, &txInputDescriptors);
}


/*
 * ----------------------------------------------------------------------------
 * TxSetPoint --
 *
 *	Set the point and window that will be used for the next command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes global set point variables.
 * ----------------------------------------------------------------------------
 */

void
TxSetPoint(x, y, wid)
    int x, y, wid;
{
    txHaveCurrentPoint = TRUE;
    txCurrentPoint.p_x = x;
    txCurrentPoint.p_y = y;
    txCurrentWindowID = wid;
}

/*
 * ----------------------------------------------------------------------------
 * TxGetPoint --
 *
 *	Return the window ID and set the point (if non-NULL) set for the
 *	next command.
 *
 * Results:
 *	integer ID of the current window, or -1 if txHaveCurrentPoint is
 *	TX_POINT_CLEAR
 *
 * Side effects:
 *	Current point goes into
 *
 * ----------------------------------------------------------------------------
 */

int
TxGetPoint(tx_p)
    Point *tx_p;
{
    if (txHaveCurrentPoint)
    {
	if (tx_p != (Point *)NULL)
	    *tx_p = txCurrentPoint;
	return txCurrentWindowID;
    }
    return -1;
}

/*
 * ----------------------------------------------------------------------------
 * TxClearPoint --
 *
 *	Clear the point that will be used for the next command, forcing it to
 *	be read from the mouse or button push.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Nullifies the effect of any previous TxSetPoint() call.
 * ----------------------------------------------------------------------------
 */

void
TxClearPoint()
{
    txHaveCurrentPoint = FALSE;
}

static FILE *txLogFile = NULL;
bool txLogUpdate;

/*
 * ----------------------------------------------------------------------------
 * TxLogCommands --
 *
 *	Log all further commands to the given file name.  If the file is NULL,
 *	turn off logging.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	File IO.
 * ----------------------------------------------------------------------------
 */

void
TxLogCommands(fileName, update)
    char *fileName;
    bool update;	/* Request a screen update after each command */
{
    if (txLogFile != NULL) 
    {
	(void) fclose(txLogFile);
	txLogFile = NULL;
    }
    if (fileName == NULL) return;

    txLogUpdate = update;
    txLogFile = fopen(fileName, "w");
    if (txLogFile == NULL) 
	TxError("Could not open file '%s' for writing.\n", fileName);
}

/*
 * ----------------------------------------------------------------------------
 * txLogCommand --
 *
 *	Log a command in the log file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
txLogCommand(cmd)
    TxCommand *cmd;
{
    static char *txButTable[] =
    {
	"left",
	"middle",
	"right",
	0
    };
    static char *txActTable[] =
    {
	"down",
	"up",
	0
    };

    if (txLogFile == (FILE *) NULL) return;

    if (cmd->tx_wid >= 0) {
	/* Command has a window associated with it. */
	fprintf(txLogFile, ":setpoint %d %d %d\n", 
	    cmd->tx_p.p_x, cmd->tx_p.p_y, cmd->tx_wid);
    } else {
	/* No window associated with the command. */
	fprintf(txLogFile, ":setpoint %d %d\n", 
	    cmd->tx_p.p_x, cmd->tx_p.p_y);
    }

    if (cmd->tx_argc > 0)
    {
	int i;
	fprintf(txLogFile, ":%s", cmd->tx_argv[0]);
	for (i = 1; i < cmd->tx_argc; i++)
	{
	    fprintf(txLogFile, " '%s'", cmd->tx_argv[i]);
	}
	fprintf(txLogFile, "\n");
    }
    else if (cmd->tx_button == TX_CHARACTER) {
	/* its a no-op command */
	return;
    }
    else {
	int but, act;

	switch(cmd->tx_button) {
	    case TX_LEFT_BUTTON: { but = 0; break; };
	    case TX_MIDDLE_BUTTON: { but = 1; break; };
	    case TX_RIGHT_BUTTON: { but = 2; break; };
	    default: {ASSERT(FALSE, "txLogCommand"); break; };
	}
	switch(cmd->tx_buttonAction) {
	    case TX_BUTTON_DOWN: { act = 0; break; };
	    case TX_BUTTON_UP: { act = 1; break; };
	    default: {ASSERT(FALSE, "txLogCommand"); break; };
	}
	    
	fprintf(txLogFile, ":pushbutton %s %s\n", 
		txButTable[but], txActTable[act]);
    }
    if (txLogUpdate)
	fprintf(txLogFile, ":updatedisplay\n");
    (void) fflush(txLogFile);
}

/*
 * ----------------------------------------------------------------------------
 *
 * TxGetInputEvent --
 *
 *	Get some events and put them into the event queue.  If returnOnSigWinch
 *	is on, we will return early if some event (such as a SigWinch) requires
 *	immediate attention.  In that case, the input event queue may be empty
 *	even if block is TRUE.
 *
 * Results:
 *	"TRUE" if it got some input.
 *
 * Side Effects:
 *	Some IO may be done, and things may appear in the event queue.
 *
 * ----------------------------------------------------------------------------
 */

bool
TxGetInputEvent(block, returnOnSigWinch)
    bool block;		/* If TRUE, we will wait for an event.  Otherwise, we
			 * just poll.
			 */
    bool returnOnSigWinch;
			/* If we get a Sig-Winch signal, should we abondon
			 * our quest to read an input event and return
			 * immediately instead?
			 */
{

    struct timeval *waitTime;
    fd_set inputs;
    int numReady;
    bool gotSome;
    int i, fd, lastNum;

    ASSERT(!FD_IsZero(txInputDescriptors), "TxGetInputEvent");

    if (block)
	waitTime = NULL;
    else
	waitTime = &txZeroTime;

    gotSome = FALSE;
    do {
	do
	{
	    if (returnOnSigWinch && SigGotSigWinch) return gotSome;
	    inputs = txInputDescriptors;
	    numReady = select(TX_MAX_OPEN_FILES, &inputs, (fd_set *)NULL, 
		(fd_set *)NULL, waitTime);
	    if (numReady <= 0) FD_ZERO(&inputs);	/* no fd is ready */
	} while ((numReady <= 0) && (errno == EINTR));

	if ((numReady < 0) && (errno != EINTR))
	{
	    perror("magic");
	}

	for (i = 0; i <= txLastInputEntry; i++)
	{
	    /* This device has data on its file descriptor, call
	     * it so that it can add events to the input queue.
	     */
	    for (fd = 0; fd < TX_MAX_OPEN_FILES; fd++) {
		if (FD_ISSET(fd, &inputs) && 
			    FD_ISSET(fd, &(txInputDevice[i].tx_fdmask))) {
		    lastNum = txNumInputEvents;
		    (*(txInputDevice[i].tx_inputProc))
			(fd, txInputDevice[i].tx_cdata);
		    FD_CLR(fd, &inputs);
		    /* Did this driver choose to add an event? */
		    if (txNumInputEvents != lastNum) gotSome = TRUE;
		}
	    }
	}
	/*
	 * At this point we have handled all the bits in 'inputs' -- almost.
	 * It is possible for an input handler to remove or add other handlers 
	 * via calls to TxDeleteDevice or TxAddDevice.  Therefore, there may be
	 * bits in inputs that haven't been turned off.
	 */
    } while (block && !gotSome);
    return gotSome;
}

#ifndef MAGIC_WRAPPER

/*
 * ----------------------------------------------------------------------------
 *
 * TxParseString --
 *
 *	Parse a string into commands, and add them to the rear of a queue.
 *	The commands in the queue should eventually be freed by the caller
 *	via TxFreeCommand().
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

void
TxParseString(str, q, event)
    char *str;			/* The string to be parsed. */
    DQueue *q;			/* Add to the tail of this queue. */
    TxInputEvent *event;	/* An event to supply the point, window ID, 
				 * etc. .  If NULL, we will use the last
				 * event processed.
				 */
{
    char *remainder;
    TxCommand *cmd;

    if (event == NULL) event = &txLastEvent;

    remainder = str;
    while (remainder != NULL) {
	/* Parse a single command. */
	cmd = TxNewCommand();
	strncpy(cmd->tx_argstring, remainder, TX_MAX_CMDLEN);
	cmd->tx_argstring[TX_MAX_CMDLEN - 1] = '\0';

	if (ParsSplit(cmd->tx_argstring, TX_MAXARGS, 
	    &(cmd->tx_argc), cmd->tx_argv, &remainder) )
	    {
		if (event == NULL) event = &txLastEvent;
		cmd->tx_button = TX_CHARACTER;
		cmd->tx_p = event->txe_p;
		cmd->tx_wid = event->txe_wid;
		DQPushRear(q, (ClientData) cmd);
	    }
	    else
	    {
		TxError("Unable to completely parse command line '%s'\n", 
		    str);
		TxFreeCommand(cmd);
		return;
	    }
    }
}

#endif  /* !MAGIC_WRAPPER */

/*
 * ----------------------------------------------------------------------------
 * txGetInteractiveCommand:
 *
 *	Get one or more commands from one of the interactive input devices, 
 *	and put it into the input queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 * ----------------------------------------------------------------------------
 */

void
txGetInteractiveCommand(block, queue)
    bool block;			/* If TRUE, then wait until we have a command.
				 * If FALSE, then get one only if input is
				 * available.
				 */
    DQueue *queue;		/* Queue to receive the new commands. */
{
    static char inputLine[TX_MAX_CMDLEN] = "";
    TxInputEvent *event;
    TxCommand *cmd;
    int ch;

    /* Get one new event and return (except for text commands, then collect
     * an entire line.
     */
    if (DQIsEmpty(&txInputEvents)) TxGetInputEvent(block, TRUE);
    if (DQIsEmpty(&txInputEvents)) return;

    event = (TxInputEvent *) DQPopFront(&txInputEvents);
    txLastEvent = *event;
    if (TxDebug) TxPrintEvent(event);
    ASSERT(event != NULL, "txGetInteractiveCommand");

    if (event->txe_button == TX_EOF) {
	/* We have an end of file.  Put it at the end of our command queue,
	 * but only if we are waiting for a command.  We don't want to
	 * put in EOF's if we are just polling.
	 */
	if (block)
	{
	    cmd = TxNewCommand();
	    cmd->tx_button = TX_EOF;
	    cmd->tx_p = event->txe_p;
	    cmd->tx_wid = event->txe_wid;
	    cmd->tx_argc = 0;
	    cmd->tx_argv[0] = NULL;
	    DQPushRear(queue, (ClientData) cmd);
	}
	TxFreeEvent(event);
    } else if ((TxCurButtons != 0) && (event->txe_button == TX_CHARACTER)) {
	/* We have a keyboard character, but buttons are still down.
	 * Push fake button up commands into the queue in front of this
	 * event, so that all buttons will be up by the time that we get
	 * to the character event.  This is needed to avoid strange things
	 * when the user pushes a buttons down on a Sun 160 (or other device
	 * with a non-Magic window package) and then releases it outside of
	 * the Magic windows.
	 */
	TxInputEvent *newEvent;
	int ourbuts;

	ourbuts = TxCurButtons;
	DQPushFront(&txInputEvents, event);
	while (ourbuts != 0) {
	    newEvent = TxNewEvent();
	    newEvent->txe_p = event->txe_p;
	    newEvent->txe_wid = event->txe_wid;
	    /* Get rightmost '1' bit of ourbuts. */
	    newEvent->txe_button = LAST_BIT_OF(ourbuts);
	    newEvent->txe_buttonAction = TX_BUTTON_UP;
	    newEvent->txe_ch = 0;
	    ourbuts &= ~newEvent->txe_button;
	    DQPushFront(&txInputEvents, newEvent);
	}
	/* Now, go around the loop and process these things just like
	 * the user had typed them.
	 */
    } else if (event->txe_button == TX_CHARACTER) {
	/* We have a text command, go into text collection mode */

	/* need to check for long command or macro, then assemble line */
	ch = event->txe_ch;
	TxFreeEvent(event);
	if ((ch == (int)TX_LONG_CMD) || (ch == (int)TX_LONG_CMD2))
	{
	    (void) TxGetLinePrompt(inputLine, TX_MAX_CMDLEN, TX_CMD_PROMPT);
	    if (inputLine[0] != '\0') MacroDefine(DBWclientID, (int)'.',
			inputLine, NULL, FALSE);
	    TxParseString(inputLine, queue, (TxInputEvent* ) NULL);
	}
	else
	{
	    bool iMacro; /* Interactive macro */
	    char *macroDef;

	    macroDef = MacroRetrieve(DBWclientID, ch, &iMacro);
	    if (macroDef == NULL)
	    {
		/* Cases macroDef may be NULL:			*/
		/* 1) Return  2) Ctrl-C  3) Invalid macro	*/

		if (ch == (int)'\n')
		{
		    if (TxInteractive) TxPrintf("%c\n", TX_PROMPT);
		}
		else
		{
		    /* not a valid command */
		    char *vis = MacroName(ch);
		    TxError("Unknown macro or short command: '%s'\n", vis);
		    freeMagic(vis);
		}
	    }
	    else
	    {
		if (iMacro)
		{
		    (void) TxGetLineWPrompt(inputLine,
					TX_MAX_CMDLEN, TX_CMD_PROMPT, macroDef);
		    if (inputLine[0] != '\0') MacroDefine(DBWclientID, (int)'.',
				inputLine, NULL, FALSE);
		    TxParseString(inputLine, queue, (TxInputEvent *) NULL);
		}
		else
		{
		    TxParseString(macroDef, queue, (TxInputEvent *) NULL);
		}
		freeMagic(macroDef);
	    }
	}
    } else if ((event->txe_button & TX_LEFT_BUTTON) ||
	       (event->txe_button & TX_MIDDLE_BUTTON) ||
	       (event->txe_button & TX_RIGHT_BUTTON)) {
	/* We have a button command, but ignore double ups & downs. */
	int oldButtons;
	oldButtons = TxCurButtons;
	if (event->txe_buttonAction == TX_BUTTON_UP)
	    TxCurButtons &= ~(event->txe_button);
	else
	    TxCurButtons |= event->txe_button;
	if (oldButtons == TxCurButtons) {
	    TxFreeEvent(event);
	} else {
	    /* We have a valid button command. */
	    cmd = TxNewCommand();
	    cmd->tx_button = event->txe_button;
	    cmd->tx_buttonAction = event->txe_buttonAction;
	    cmd->tx_p = event->txe_p;
	    cmd->tx_wid = event->txe_wid;
	    cmd->tx_argc = 0;
	    cmd->tx_argv[0] = NULL;
	    DQPushRear(queue, (ClientData) cmd);
	    TxFreeEvent(event);
	}
    } else {	/* TX_BYPASS */
	/* Ignore this event */
	TxFreeEvent(event);
    }
}

/*
 * ----------------------------------------------------------------------------
 * txGetFileCommand --
 *
 *	Get a line from a file and stick the commands into the input queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Reads the file and puts a command into the queue.
 * ----------------------------------------------------------------------------
 */

void
txGetFileCommand(f, queue)
    FILE *f;			/* File to read. */
    DQueue *queue;		/* Queue to receive the new commands. */
{
    char inputLine[TX_MAX_CMDLEN];
    char *linep;
    char *current;
    int spaceleft;

    /* Generate a line by patching together successive lines ending
     * in '\'.
     */

    do {
	current = inputLine;
	spaceleft = TX_MAX_CMDLEN - 1;

	while (TRUE)
	{
	    if (fgets(current, spaceleft, f) == NULL)
		return;

	    while (*current != 0)
	    {
		current++;
		spaceleft--;
	    }
	    if ((*(current-1) != '\n') || (*(current-2) != '\\')) goto gotline;
	    current--;
	    spaceleft++;
	}

	gotline: *current = 0;

	/* If the line is empty, or contains a hash mark as
	 * the first non-blank character, then skip over it.
	 */

	current = inputLine;
	while (isspace(*current)) current++;
    } while ((*current == 0) || (*current == '#'));

    linep = inputLine;

#ifdef MAGIC_WRAPPER
    /* Don't muck with Tcl's interpretation of semicolon as a	*/
    /* comment character.  Watch for single-colon entries for	*/
    /* backwards-compatibility with the old format, but watch	*/
    /* for the double-colon, which is a Tcl namespace marker.	*/

    if ((inputLine[0] == ':') && (inputLine[1] != ':')) linep++;
#else
    if ((inputLine[0] == ':') || (inputLine[1] == ';')) linep++;
#endif

    TxParseString(linep, queue, (TxInputEvent *) NULL);
}

#ifdef MAGIC_WRAPPER

/*
 * ----------------------------------------------------------------------------
 * TxTclDispatch:
 *
 *	Command dispatcher which relies on the Tcl interpreter for most
 *	of the work.  Replaces TxDispatch except for file input during
 *	startup.
 *
 * ----------------------------------------------------------------------------
 */

int
TxTclDispatch(clientData, argc, argv, quiet)
   ClientData clientData;
   int argc;
   char *argv[];
   bool quiet;
{
    int result;
    int n, asize;
    TxCommand *tclcmd;
    unsigned char lastdrc;

    if (argc > TX_MAXARGS)
    {
	TxError("Error: number of command arguments exceeds %d!\n", TX_MAXARGS);
	return -1;
    }

    SigIOReady = FALSE;
    if (SigInterruptOnSigIO >= 0) SigInterruptOnSigIO = 1;
    SigInterruptPending = FALSE;

    tclcmd = TxNewCommand();
    tclcmd->tx_argc = argc;

    asize = 0;
    for (n = 0; n < argc; n++)
    {
	if (asize + strlen(argv[n]) >= TX_MAX_CMDLEN)
	{
	    TxError("Error: command length exceeds %d characters!\n", TX_MAX_CMDLEN);
	    TxFreeCommand(tclcmd);
	    return -1;
	}
	strcpy(&tclcmd->tx_argstring[asize], argv[n]);
	tclcmd->tx_argv[n] = &tclcmd->tx_argstring[asize];
	asize += (1 + strlen(argv[n]));
    }
    tclcmd->tx_p = txCurrentPoint;
    if (txHaveCurrentPoint)
	tclcmd->tx_wid = txCurrentWindowID;
    else
	tclcmd->tx_wid = WIND_UNKNOWN_WINDOW;

    /* Prevent DRCContinuous from running in the middle of a command	*/
    /* (Note that Tcl_CancelIdleCall() does *not* work. . .)		*/

    lastdrc = DRCBackGround;
    if (DRCBackGround != DRC_SET_OFF) DRCBackGround = DRC_NOT_SET;

    result = WindSendCommand((MagWindow *)clientData, tclcmd, quiet);

    TxFreeCommand(tclcmd);

    // Don't update the command number on bypass commands, or else
    // the selection mechanism gets broken by the background DRC.

    if (argc > 0 && strcmp(argv[0], "*bypass")) TxCommandNumber++;

    if (SigInterruptPending)
	TxPrintf("[Interrupted]\n");

    if (result == 0) WindUpdate();

    SigInterruptPending = FALSE;
    if (SigInterruptOnSigIO >= 0) SigInterruptOnSigIO = 0;
    SigIOReady = FALSE;

    /* Force a break of DRCContinuous() so we don't run DRC on an	*/
    /* invalid database.						*/

    /* Avoid breaking on "*bypass" commands.  Technically, one could	*/
    /* set up a command to *bypass that alters the database and crashes	*/
    /* magic.  This is a total hack.  We should flag an invalid		*/
    /* database instead.						*/

    if (DRCBackGround == DRC_NOT_SET) DRCBackGround = lastdrc;
    if (argc > 0 && strcmp(argv[0], "*bypass") && strcmp(argv[0], "windownames"))
	DRCBreak();

    /* Reinstate the idle call */
    if (result == 0) Tcl_DoWhenIdle(DRCContinuous, (ClientData)NULL);
    return result;
}
#else  /* !MAGIC_WRAPPER */

/*
 * ----------------------------------------------------------------------------
 * TxDispatch:
 *
 *	Read input from the interactive devices (keyboard or mouse) or from
 *	a file.  Send a command off to WindSendCommand for processing, and
 *	then repeat.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are read and executed, one per cycle of this procedure's loop.
 * ----------------------------------------------------------------------------
 */

void
TxDispatch(f)
    FILE *f;		/* Read commands from this file instead of
			 * from the mouse or keyboard.
			 */
{
    bool inFile;
    DQueue inputCommands;	/* A queue of commands from our input
				 * sources.
				 */ 

    DQInit(&inputCommands, 4);

    inFile = (f != (FILE *) NULL);

    while (TRUE)
    {
	if (!inFile)
	{
	    /* Update the screen info of the window package and all of 
	     * its clients.  If there's already input waiting, skip this...
	     * it can get done at the end, after all pending commands have
	     * been processed.
	     */
	    if (SigInterruptOnSigIO >= 0) SigInterruptOnSigIO = 0;
	    SigInterruptPending = FALSE;
	    txGetInteractiveCommand(FALSE, &inputCommands);
	    if (DQIsEmpty(&inputCommands))
	    {
		WindUpdate();
		if (SigInterruptPending) 
		    TxPrintf("[Redisplay Interrupted]\n");
		txGetInteractiveCommand(FALSE, &inputCommands);
	    }

	    /* Give the DRC a chance to work. */
	    if (!SigGotSigWinch && DQIsEmpty(&inputCommands)) {
		if (DRCHasWork) {
		    TxSetPrompt(']');
		    TxPrompt();
		    (void) GrEnableTablet();
		    GrFlush();

		    /*  Call background DRC.
		     *  It will return if it is not enabled, there is no work to
		     *  be done, or it learns that the user has entered a 
		     *  command.
		     */
		    SigIOReady = FALSE;
		    if (SigInterruptOnSigIO >= 0) SigInterruptOnSigIO = 1;
		    SigInterruptPending = FALSE;
		    DRCContinuous();
		    TxUnPrompt();

		    SigIOReady = FALSE;
		    if (SigInterruptOnSigIO >= 0) SigInterruptOnSigIO = 0;
		    SigInterruptPending = FALSE;
		    (void) GrDisableTablet();
		    WindUpdate();
		    if (SigInterruptPending)
			TxPrintf("[DRC Redisplay Interrupted]\n");
		}
		TxSetPrompt(TX_PROMPT);
		TxPrompt();
		(void) GrEnableTablet();
		GrFlush();
		
		txGetInteractiveCommand(TRUE, &inputCommands);

		TxUnPrompt();
		(void) GrDisableTablet();
		GrFlush();
		SigInterruptPending = FALSE;
		SigIOReady = FALSE;
	    }
	}
	else
	{
	    if (feof(f)) goto done;
	    if (SigInterruptPending && inFile) 
	    {
		TxPrintf("[Read-in of command file aborted]\n");
		goto done;
	    }
	    if (!SigGotSigWinch) txGetFileCommand(f, &inputCommands);
	}

	if (SigGotSigWinch) {
	    /* WindUpdate will take care of SigWinch and set it to FALSE */
	    WindUpdate();
	}

	/* Now send off the commands in the queue. */
	while (!DQIsEmpty(&inputCommands))
	{
	    TxCommand *cmd;

	    cmd = (TxCommand *) DQPopFront(&inputCommands);
	    if (TxDebug) TxPrintCommand(cmd);
	    if (cmd->tx_button == TX_EOF)
	    {
		/* End of our input stream. */
		TxError("EOF encountered on input stream -- Magic exiting.\n");
		MainExit(1);
	    }

	    /****
	    ASSERT(cmd->tx_p.p_x > 0 && cmd->tx_p.p_x < 10000, "TxDispatch");
	    ASSERT(cmd->tx_p.p_y > 0 && cmd->tx_p.p_y < 10000, "TxDispatch");
	    ****/

	    /* force the point, if that was requested */
	    if (txHaveCurrentPoint) {
		cmd->tx_p = txCurrentPoint;
		cmd->tx_wid = txCurrentWindowID;
		if (!inFile) txHaveCurrentPoint = FALSE;
	    };

	    /****
	    ASSERT(cmd->tx_argc >= 0 && cmd->tx_argc <= TX_MAXARGS, 
		"TxDispatch");
	    if (cmd->tx_argc != 0) {
		ASSERT(cmd->tx_button == TX_CHARACTER, "TxDispatch");
	    }
	    else {
		ASSERT((cmd->tx_buttonAction == TX_BUTTON_DOWN) || \
		    (cmd->tx_buttonAction == TX_BUTTON_UP),
		    "TxDispatch");
		ASSERT((cmd->tx_button == TX_LEFT_BUTTON) || \
		    (cmd->tx_button == TX_MIDDLE_BUTTON) || \
		    (cmd->tx_button == TX_RIGHT_BUTTON) || \
		    (cmd->tx_button == TX_CHARACTER),
		    "TxDispatch");
	    };
	    ****/
	    if (!inFile && (txLogFile != NULL)) txLogCommand(cmd);
	    
	    /*
	     *  Add lisp interpreter here. We trap the character
	     *  commands and call the lisp interpreter that traps
	     *  any lisp-like behavior and returns a stream of "cmd"
	     *  functions which are then sent to the appropriate window
	     *  using the usual WindSendCommand() call.
	     *
	     *  (rajit@cs.caltech.edu)
	     */
#ifdef SCHEME_INTERPRETER
	    if (cmd->tx_button == TX_CHARACTER)
	      {
		lisp_cur_cmd = cmd;
		LispEvaluate (cmd->tx_argc, cmd->tx_argv, inFile);
		TxFreeCommand (cmd);
		lisp_cur_cmd = NULL;
	      }
	    else {
#endif
	      (void) WindSendCommand((MagWindow *) NULL, cmd, FALSE);
	      TxFreeCommand(cmd);
	      TxCommandNumber++;

#ifdef SCHEME_INTERPRETER
	    }
#endif
        }
	/* ?? */
	WindUpdate();

    } /* while */

done:
    while (!DQIsEmpty(&inputCommands))
	TxFreeCommand((TxCommand *) DQPopFront(&inputCommands));
    DQFree(&inputCommands);
}

#endif	/* !MAGIC_WRAPPER */

/*
 * ----------------------------------------------------------------------------
 *
 * txCommandsInit --
 *
 *	Initialize the things in this file
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Variables are set up.
 *
 * ----------------------------------------------------------------------------
 */

void
txCommandsInit()
{
    txZeroTime.tv_sec = 0;
    txZeroTime.tv_usec = 0;
    FD_ZERO(&txInputDescriptors);
    DQInit(&txInputEvents, 4);
    DQInit(&txFreeEvents, 4);
    DQInit(&txFreeCommands, 4);
}

#ifdef SCHEME_INTERPRETER

/*
 * ----------------------------------------------------------------------------
 *
 * txLispDispatch -- 
 *
 *	Send command to magic window.
 *
 * Results:
 *	Returns TRUE on success, FALSE on failure.
 *
 * Side Effects:
 *	whatever the command does.
 *
 * ----------------------------------------------------------------------------
 */

bool
TxLispDispatch (argc,argv,trace, inFile)
     int argc;
     char **argv;
     int trace;
     int inFile;
{
  TxCommand *cmd = lisp_cur_cmd;
  int i,j,k;
  bool ret;
  Point tx_p;
  int wid;

  cmd->tx_argc = argc;
  for (i=0,j=0; i < argc; i++) {
    cmd->tx_argv[i] = cmd->tx_argstring+j;
    k=0;
    while (cmd->tx_argstring[j] = argv[i][k])
	j++,k++;
    j++;
  }
  tx_p = cmd->tx_p;
  wid = cmd->tx_wid;
  if (txHaveCurrentPoint) {
    cmd->tx_p = txCurrentPoint;
    cmd->tx_wid = txCurrentWindowID;
    if (!inFile) txHaveCurrentPoint = FALSE;
  }
  if (trace)
    TxPrintCommand (cmd);
  ret = WindSendCommand((MagWindow *) NULL, cmd, FALSE);
  /* restore the original values */
  cmd->tx_p = tx_p;
  cmd->tx_wid = wid;
  TxCommandNumber++;
  return ret;
}

#endif	/* SCHEME_INTERPRETER */
