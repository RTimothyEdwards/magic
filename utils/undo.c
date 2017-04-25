/*
 * undo.c --
 *
 * Undo/redo module.
 *
 * The undo package records a series of invertible editing events
 * in a log maintained in main memory.
 *
 * The current state may be rewound back toward the time the editing
 * session began, and it may be replayed forward as well.
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
static char rcsid[] __attribute__ ((unused)) = "$Header: /usr/cvsroot/magic-8.0/utils/undo.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $";
#endif  /* not lint */

#include <stdio.h>
#include <sys/types.h>

#include "utils/magic.h"
#include "utils/utils.h"
#include "utils/malloc.h"
#include "utils/undo.h"

/* ------------------------------------------------------------------------ */

/*
 * CONFIGURATION INFORMATION
 */

#define	MAXUNDOCLIENTS	50	/* Maximum number of calls to UndoAddClient */

    /*
     * MAXCOMMANDS is the maximum number of delimited event sequences
     * ("commands") retained in main memory.  LOWCOMMANDS is a low-water
     * mark for the number of commands in memory; whenever we free up
     * commands, we do so until there are no more than LOWCOMMANDS in
     * main memory.
     *
     * >>>>> In the current implementation, these two are the same <<<<<
     */

#define	MAXCOMMANDS	1000
#define	LOWCOMMANDS	1000	/* Must be > 0 ! */

/* ------------------------------------------------------------------------ */

/*
 * The following structure describes the basic information
 * required by the undo package for each event it stores.
 * This information is NOT intended to be visible to any of
 * the clients of the undo package and is susceptible to being
 * changed arbitrarily.
 *
 * To enforce the absolute ignorance of undo's clients, when
 * we allocate an internalUndoEvent, we only give the client
 * a pointer to the iue_client part, which is of a size determined
 * by the client when it calls UndoNewEvent().  This pointer is
 * what the client sees as an (UndoEvent *) (really a (char *)).
 */

    typedef struct ue
    {
	UndoType	 iue_type;	/* Event type */
	struct ue	*iue_back;	/* Previous event on list */
	struct ue	*iue_forw;	/* Next event on list */
	int		 iue_client;	/* Client data area.  This is merely a
					 * dummy placeholder; the actual size
					 * of one of these structures is
					 * determined at the time of
					 * UndoNewEvent().
					 */
    } internalUndoEvent;

#define	UT_DELIM	(-1)

/*
 * The following macro is used to compute the number of bytes we must
 * allocate in order to give the user an UndoEvent capable of holding
 * n bytes.
 */

#define	undoSize(n)	(sizeof (struct ue) + (n) \
			    - sizeof (((struct ue *) 0)->iue_client))

/*
 * Mapping between internal and external undo event pointers.
 * When the undo package hands an (UndoEvent *) to a client, it is
 * really a pointer to the iue_client part of the structure
 * above.
 */

#define	CLIENTOFFSET	((int) &((internalUndoEvent) 0)->iue_client)
#define	undoExport(p)	((UndoEvent *) (&(p)->iue_client))
#define	undoImport(p)	((internalUndoEvent) (((char *) (p)) - CLIENTOFFSET))

/*
 * The following table is used to record the information about clients
 * of the undo package.  The number of such clients is stored in
 * undoNumClients.
 */

    typedef struct
    {
	char		  *uc_name;	/* Name (for error messages) */
	void		 (*uc_init)();	/* Called before playing log */
	void		 (*uc_done)();	/* Called after playing log */
	void		 (*uc_forw)();	/* Play event forward */
	void		 (*uc_back)();	/* Play event backward */
    } undoClient;

undoClient undoClientTable[MAXUNDOCLIENTS];
int undoNumClients = 0;

/*
 * undoState is used by UndoNewEvent() to figure out the context from which
 *	it was called in order that it may link the newly allocated event
 *	in the appropriate way.  Events are only linked in when the state
 *	is US_APPEND.  The only reason for having two other states instead
 *	of one is for ease in debugging.
 * UndoDisableCount counts the number of times UndoDisable() has been called.
 */

#define	US_APPEND	0	/* Normal state: appending to log */
#define	US_FORWARD	1	/* Playing log forward */
#define	US_BACKWARD	2	/* Playing log backward */

int undoState = US_APPEND;
global int UndoDisableCount = 0;

/*
 * Log of events kept in main memory.
 *
 *	undoLogHead	Pointer to first entry stored in main memory.
 *			    - NULL, indicating no events are in memory
 *			    - a pointer to the first event of a command
 *	undoLogTail	Pointer to last entry stored in main memory.
 *			    - Undefined (if undoLogHead == NULL)
 *			    - a pointer to a UT_DELIM event if
 *			      undoNumRecentEvents == 0
 *			    - a pointer to a non-UT_DELIM event if
 *			      undoNumRecentEvents != 0
 *	undoLogCur	Pointer to "current" event, ie, one after which
 *			next event will be added.
 *			    - NULL if at beginning of event list
 *			    - a pointer to a UT_DELIM event if
 *			      undoNumRecentEvents == 0
 *			    - a pointer to a non-UT_DELIM event if
 *			      undoNumRecentEvents != 0
 *
 *	undoNumRecentEvents
 *			Number of events written since last call to
 *			UndoNext().
 *	undoNumCommands
 *			Number of complete commands in main memory.
 */

internalUndoEvent *undoLogCur;
internalUndoEvent *undoLogHead;
internalUndoEvent *undoLogTail;
int undoNumRecentEvents;
int undoNumCommands;

/*
 * ============================================================================
 *
 *	The following collection of procedures completely defines
 *	the interface the undo package presents to its clients.
 *
 * ============================================================================
 */

extern internalUndoEvent *undoGetForw();
extern internalUndoEvent *undoGetBack();
extern void undoFreeHead();
extern void undoMemTruncate();

/*
 * ----------------------------------------------------------------------------
 *
 * UndoInit --
 *
 * Initialize the undo package.
 * Opens the given file as an undo log.
 * One of several modes may be specified.  In all cases, the log file
 * is opened so that playback will commence from the beginning of the
 * file.
 *
 *	"r"	The log file is opened for playback only; no new events
 *		may be played out to the file.
 *		This mode is intended for use in recovering from a crash
 *		without clobbering the crash log.
 *
 *	"w"	The log file is created afresh and is available to the
 *		undo package for use in logging events.
 *		This is the normal mode for starting a new session.
 *
 *	"rw"	The log file is opened for playback and logging.
 *		Any new events written before the entire log has played
 *		forward will cause the remaining events in the log to be
 *		truncated.
 *		This is the normal mode for crash recovery.
 *
 * Results:
 *	TRUE if the log file could be successfully created/opened, FALSE
 *	if an error was encountered.  In the latter case, the external
 *	variable errno is set to the UNIX error encountered.
 *
 * Side effects:
 *	Initializes all the variables in the undo package.
 *
 * ----------------------------------------------------------------------------
 */

bool
UndoInit(logFileName, mode)
    char *logFileName;	/* Name of log file.  This may contain tilde
			 * abbreviations.
			 */
    char *mode;		/* Mode for opening.  Must be "r", "rw", or "w" */
{
    UndoDisableCount = 0;
    undoLogTail = NULL;
    undoLogCur = NULL;
    undoNumRecentEvents = 0;
    undoNumCommands = 0;

    /*
     * Deallocate any events stored in main memory
     */

    while (undoLogHead != (internalUndoEvent *) NULL)
    {
	freeMagic((char *) undoLogHead);
	undoLogHead = undoLogHead->iue_forw;
    }

    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * UndoAddClient --
 *
 * Used by a client to make itself known to the undo package.
 * The client must supply the following information:
 *
 *	1. A procedure to call before starting an undo or redo
 *	   operation.
 *
 *	2. A procedure to call upon completion of an undo or
 *	   redo operation.
 *
 *	3. A procedure to parse a line in the undo log file and
 *	   return a filled-in UndoEvent (allocated, of course,
 *	   by a call to UndoNewEvent()) that is the internal
 *	   representation of the event externally represented
 *	   by the supplied line of text from the log file.
 *
 *	   UndoEvent *
 *	   readEvent(line)
 *	       char *line;
 *	   {
 *	   }
 *
 *	4. A procedure to fill in a line (null-terminated with
 *	   no newlines embedded or trailing) with the external
 *	   representation of an event based on an UndoEvent
 *	   supplied to it.  This prodedure returns the number
 *	   of bytes it stored in the line (which must be less
 *	   than a constant, UNDOLINESIZE).
 *
 *	   int
 *	   writeEvent(event, line)
 *	       UndoEvent *event;
 *	       char *line;
 *	   {
 *	   }
 *
 * If either readEvent or writeEvent are NULL, a default procedure
 * is used which simply uses the same internal and external representation
 * for the undo event (ie, a text string).
 *
 *	5. A procedure to play an event forward.
 *
 *	   void
 *	   forwEvent(event)
 *	       UndoEvent *event;
 *	   {
 *	   }
 *
 *	6. A procedure to play an event backward.
 *
 *	   void
 *	   backEvent(event)
 *	       UndoEvent *event;
 *	   {
 *	   }
 *
 *	7. A client name, for error messages.  This is a character
 *	   string.
 *
 * Results:
 *	Returns an UndoType which must be passed in future calls
 *	to UndoNewEvent().  If -1 is returned, this means that there
 *	are too many clients of the undo package.
 *
 * Side effects:
 *	Initializes local state in the undo package.
 *
 * ----------------------------------------------------------------------------
 */

UndoType
UndoAddClient(init, done, readEvent, writeEvent, forwEvent, backEvent, name)
    void (*init)();
    void (*done)();
    UndoEvent *(*readEvent)();
    int (*writeEvent)();
    void (*forwEvent)(), (*backEvent)();
    char *name;
{
    if (undoNumClients >= MAXUNDOCLIENTS)
	return ((UndoType) -1);

    undoClientTable[undoNumClients].uc_name = StrDup((char **) NULL, name);
    undoClientTable[undoNumClients].uc_forw = forwEvent;
    undoClientTable[undoNumClients].uc_back = backEvent;
    undoClientTable[undoNumClients].uc_init = init;
    undoClientTable[undoNumClients].uc_done = done;

    return (undoNumClients++);
}

/*
 * ----------------------------------------------------------------------------
 *
 * UndoFlush --
 *
 * Flush the current undo list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes everything from the undo list.
 *
 * ----------------------------------------------------------------------------
 */

void
UndoFlush()
{
    if (undoLogHead == (internalUndoEvent *) NULL)
	return;

    while (undoLogTail != undoLogHead)
    {
	freeMagic((char *) undoLogTail);
	undoLogTail = undoLogTail->iue_back;
	ASSERT(undoLogTail != (internalUndoEvent *) NULL, "UndoFlush");
    }
    freeMagic((char *) undoLogHead);

    undoLogHead = undoLogTail = undoLogCur = (internalUndoEvent *) NULL;
    undoNumCommands = 0;
    undoNumRecentEvents = 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * UndoDisable --
 *
 * Turn the undo package off.
 * Future calls to UndoNewEvent() will return NULL, and future calls
 * to UndoIsEnabled() will return FALSE, until the next call to
 * UndoEnable();
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Disables undoing until the next call to UndoEnable().
 *
 * ----------------------------------------------------------------------------
 */

void
UndoDisable()
{
    UndoDisableCount++;
}

/*
 * ----------------------------------------------------------------------------
 *
 * UndoEnable --
 *
 * Turn the undo package on.
 * Re-enables the undo package after a call to UndoDisable().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Re-enables undoing.
 *
 * ----------------------------------------------------------------------------
 */

void
UndoEnable()
{
    if (UndoDisableCount > 0)
	UndoDisableCount--;
}

/*
 * ----------------------------------------------------------------------------
 *
 * UndoNewEvent --
 *
 * Return a pointer to a new UndoEvent of the specified type and capable
 * of holding size bytes of client data.
 *
 * Results:
 *	A pointer to a new UndoEvent.
 *
 * WARNING:
 *	The pointer to the new UndoEvent must not be retained past the
 *	next call to any of the routines in the undo package, as the
 *	event is liable to be reallocated.
 *
 * Side effects:
 *	Appends the event read to the undo list in the appropriate place,
 *	depending on the context from which it was called.  If called by
 *	a client under normal circumstances, appends the event to the end
 * 	of the undo log.  If called by the readEvent() procedure during
 *	replaying of the undo log, places the event at whichever end of
 *	the log is appropriate to the operation (back/forw) being performed.
 *
 * ----------------------------------------------------------------------------
 */

UndoEvent *
UndoNewEvent(clientType, size)
    UndoType clientType;	/* Type of event to allocate */
    unsigned int size;		/* Number of bytes of client data to allocate */
{
    internalUndoEvent *iup;
    int usize;

    if (UndoDisableCount > 0)
	return ((UndoEvent *) NULL);

    usize = undoSize(size);
    iup = (internalUndoEvent *) mallocMagic((unsigned) usize);
    ASSERT(clientType >= 0 && clientType < undoNumClients, "UndoNewEvent");
    iup->iue_type = clientType;
    if (undoState == US_APPEND)
    {
	/*
	 * Normal state:
	 * Append the new event after the event pointed to by
	 * undoLogCur.
	 */
	iup->iue_forw = (internalUndoEvent *) NULL;
	iup->iue_back = undoLogCur;
	if (undoLogCur == (internalUndoEvent *) NULL)
	{
	    if (undoLogHead != (internalUndoEvent *) NULL)
		undoMemTruncate();
	    undoLogHead = undoLogCur = undoLogTail = iup;
	}
	else
	{
	    if (undoLogCur->iue_forw != (internalUndoEvent *) NULL)
		undoMemTruncate();
	    undoLogCur->iue_forw = iup;
	    undoLogCur = undoLogTail = iup;
	}
	undoNumRecentEvents++;
    }

    return (undoExport(iup));
}

/*
 * ----------------------------------------------------------------------------
 *
 * UndoNext --
 *
 * Delimit a sequence of operations to the undo package with an event
 * delimiter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends a marker to the undo list signifying the boundary of
 *	a unit of events.  UndoBackward() and UndoForward() operate in
 *	terms of complete units of this sort.
 *
 * ----------------------------------------------------------------------------
 */

void
UndoNext()
{
    internalUndoEvent *iup;
    int usize;

    if (UndoDisableCount > 0 || undoNumRecentEvents == 0)
	return;

    undoNumRecentEvents = 0;
    undoNumCommands++;
    usize = undoSize(0);
    iup = (internalUndoEvent *) mallocMagic((unsigned) usize);
    iup->iue_type = UT_DELIM;
    iup->iue_back = undoLogTail;
    iup->iue_forw = (internalUndoEvent *) NULL;
    if (undoLogTail != (internalUndoEvent *) NULL)
	undoLogTail->iue_forw = iup;
    undoLogCur = undoLogTail = iup;
    if (undoNumCommands >= MAXCOMMANDS)
	undoFreeHead();
}

/*
 * ----------------------------------------------------------------------------
 *
 * UndoBackward --
 *
 * Play the undo log backward n events.
 *
 * Results:
 *	The number of events actually played backward.  Normally, this
 *	will be equal to n unless we encounter the beginning of the log.
 *
 * Side effects:
 *	Applies the client backEvent() procedures to each event encountered
 *	in playing the log backward.
 *
 * ----------------------------------------------------------------------------
 */

int
UndoBackward(n)
    int n;		/* Number of events to unplay */
{
    internalUndoEvent *iup;
    int client, count;

#ifdef MAGIC_WRAPPER
    /* This condition appears to happen just prior to a	program	*/
    /* crash, apparently due to a race condition which is	*/
    /* difficult to reproduce.  Tcl_DoOneEvent() inside the	*/
    /* DRCContinuous routine is implicated.  Can't find the	*/
    /* error source, but refusing to execute the undo command	*/
    /* appears to prevent it.					*/
 
    if (UndoDisableCount > 0)
    {
	TxError("Attempted undo with undo disabled. . . abort function.\n");
	return 0;
    }
#endif

    /* Call the initialization routines of all clients */
    for (client = 0; client < undoNumClients; client++)
	if (undoClientTable[client].uc_init)
	    (*undoClientTable[client].uc_init)();

    iup = undoLogCur;
    undoNumRecentEvents = 0;
    UndoDisableCount++;
    for (count = 0; (count < n) && (iup != NULL); count++)
    {
	do
	{
	    if (iup->iue_type != UT_DELIM)
		if (undoClientTable[iup->iue_type].uc_back != NULL)
		    (*undoClientTable[iup->iue_type].uc_back)(undoExport(iup));

	    /* fprintf(stderr, "Undo record: type=%d back=0x%x, forw=0x%x\n",
	     *	iup->iue_type, iup->iue_back, iup->iue_forw);
	     * fprintf(stderr, "   UndoDisableCount = %d, undoNumRecentEvents = %d\n",
	     *		UndoDisableCount, undoNumRecentEvents);
	     * fflush(stderr);
	     */

	    iup = undoGetBack(iup);
	}
	while ((iup != (internalUndoEvent *) NULL) && (iup->iue_type != UT_DELIM));
    }
    UndoDisableCount--;

    undoLogCur = iup;

    /* fprintf(stderr, "UndoBackward: undoLogCur set to 0x%x\n", undoLogCur);
     * fflush(stderr);
     */

    /* Call the termination routines of all clients */
    for (client = 0; client < undoNumClients; client++)
	if (undoClientTable[client].uc_done)
	    (*undoClientTable[client].uc_done)();
    return (count);
}

/*
 * ----------------------------------------------------------------------------
 *
 * UndoForward --
 *
 * Play the undo log forward n events.
 *
 * Results:
 *	The number of events actually played forward.  Normally, this
 *	will be equal to n unless we encounter the end of the log.
 *
 * Side effects:
 *	Applies the client forwEvent() procedures to each event encountered
 *	in playing the log forward.
 *
 * ----------------------------------------------------------------------------
 */

int
UndoForward(n)
    int n;		/* Number of events to replay */
{
    internalUndoEvent *iup;
    int count, client;

    /* Call the initialization routines of all clients */
    for (client = 0; client < undoNumClients; client++)
	if (undoClientTable[client].uc_init)
	    (*undoClientTable[client].uc_init)();

    count = 0;
    iup = undoGetForw(undoLogCur);
    if (iup == NULL) goto done;

    undoNumRecentEvents = 0;
    UndoDisableCount++;
    for ( ; count < n; count++)
    {
	do
	{
	    if (iup->iue_type != UT_DELIM)
		if (undoClientTable[iup->iue_type].uc_forw != NULL)
		    (*undoClientTable[iup->iue_type].uc_forw)(undoExport(iup));
	    iup = undoGetForw(iup);
	}
	while (iup != (internalUndoEvent *) NULL && iup->iue_type != UT_DELIM);
	if (iup == (internalUndoEvent *) NULL)
	{
	    iup = undoLogTail;
	    break;
	}
    }
    UndoDisableCount--;

    undoLogCur = iup;

done:
    /* Call the termination routines of all clients */
    for (client = 0; client < undoNumClients; client++)
	if (undoClientTable[client].uc_done)
	    (*undoClientTable[client].uc_done)();
    return (count);
}

/*
 * ============================================================================
 *
 *	All of the remaining procedures in the file are invisible
 *	to the clients of the undo package and should not be used.
 *
 * ============================================================================
 */


/*
 * ----------------------------------------------------------------------------
 *
 * undoGetForw --
 *
 * Return a pointer to the next undo event in the list.
 *
 * Results:
 *	A pointer to an undo event.
 *
 * Side effects:
 *	None.
 *
 * Directly modifies:
 *	undoLogHead, undoLogTail
 *	undoNumCommands
 *	undoNumRecentEvents = 0
 *
 * Indirectly modifies:
 *	Nothing.
 *
 * ----------------------------------------------------------------------------
 */

internalUndoEvent *
undoGetForw(iup)
    internalUndoEvent *iup;
{
    if (iup != (internalUndoEvent *) NULL)
    {
	/*
	 * Return the next event in memory if there is one.
	 */
	if (iup->iue_forw != (internalUndoEvent *) NULL)
	    return (iup->iue_forw);
    }
    else
    {
	/*
	 * A NULL initial iup means to start at the very beginning of
	 * the main-memory undo list.  If there is anything there, return
	 * it; otherwise return NULL.
	 */
	if (undoLogHead != (internalUndoEvent *) NULL)
	    return (undoLogHead);
    }

    return ((internalUndoEvent *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * undoGetBack --
 *
 * Return a pointer to the previous undo event in the list.
 *
 * Results:
 *	A pointer to an undo event.
 *
 * Side effects:
 *	None.
 *
 * Directly modifies:
 *
 * Indirectly modifies:
 *
 * ----------------------------------------------------------------------------
 */

internalUndoEvent *
undoGetBack(iup)
    internalUndoEvent *iup;
{
    if (iup == (internalUndoEvent *) NULL) return (iup);
    if (iup->iue_back != (internalUndoEvent *) NULL) return (iup->iue_back);
    return ((internalUndoEvent *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * undoFreeHead --
 *
 * Free up space by throwing away events from the front of the in-memory
 * event list until the total number of in-memory commands falls below
 * LOWCOMMANDS
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deallocates events from the front of the in-memory event list.
 *	Updates undoLogHead, undoNumCommands.
 *	Guaranteed to leave undoLogHead pointing to the first event
 *	in a command (not of type UT_DELIM).
 *
 * WARNING:
 *	It is important that undoLogCur point beyond the region
 *	to be freed.  Also, it is important that the in-core list
 *	be terminated by an UT_DELIM event.
 *
 * ----------------------------------------------------------------------------
 */

void
undoFreeHead()
{
    if (undoNumCommands <= LOWCOMMANDS)
	return;

    while (undoNumCommands > LOWCOMMANDS)
    {
	do
	{
	    ASSERT(undoLogHead != undoLogCur, "undoFreeHead");
	    freeMagic((char *) undoLogHead);
	    undoLogHead = undoLogHead->iue_forw;
	    ASSERT(undoLogHead != (internalUndoEvent *) NULL, "undoFreeHead");
	}
	while (undoLogHead->iue_type != UT_DELIM);
	undoNumCommands--;
    }
    freeMagic((char *) undoLogHead);
    undoLogHead = undoLogHead->iue_forw;
    undoLogHead->iue_back = (internalUndoEvent *) NULL;
}

/*
 * ----------------------------------------------------------------------------
 *
 * undoMemTruncate --
 *
 * Delete events in memory which are later than the current event.
 * NOTE: This expects to be called only when undoNumRecentEvents == 0.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Truncates the event list so there are no events past undoLogCur.
 *
 *	Updates undoLogTail.
 *
 * ----------------------------------------------------------------------------
 */

void
undoMemTruncate()
{
    internalUndoEvent *up;

    /*
     * If there are events forward of the current event that
     * will get overwritten by the new event, delete them from
     * memory.
     */

    if (undoLogCur == (internalUndoEvent *) NULL)
    {
	/*
	 * Delete ALL events from memory
	 */
	up = undoLogHead;
	while (up != (internalUndoEvent *) NULL)
	{
	    freeMagic((char *) up);
	    up = up->iue_forw;
	}
	undoLogTail = undoLogHead = (internalUndoEvent *) NULL;
	undoNumCommands = 0;
    }
    else
    {
	ASSERT(undoLogCur->iue_type == UT_DELIM, "undoMemTruncate");
	/*
	 * Delete only some of the events in main memory.
	 */
	up = undoLogCur->iue_forw;
	while (up != (internalUndoEvent *) NULL)
	{
	    if (up->iue_type == UT_DELIM)
		undoNumCommands--;
	    freeMagic((char *) up);
	    up = up->iue_forw;
	}
	undoLogCur->iue_forw = (internalUndoEvent *) NULL;
	undoLogTail = undoLogCur;
    }

}

/*
 * ============================================================================
 *
 *			    DEBUGGING PROCEDURES
 *
 * ============================================================================
 */

void
undoPrintEvent(iup)
    internalUndoEvent *iup;
{
    char *client_name;
    if (iup->iue_type < 0)
	client_name = "(delimiter)";
    else
	client_name = undoClientTable[iup->iue_type].uc_name;

    (void) TxPrintf("0x%x: \t%s \tf=0x%x \tb=0x%x\n",
		iup, client_name, iup->iue_forw, iup->iue_back);
}

/* Print events forward from "iup".  If n is 0 or negative, print to	*/
/* the end of the stack.  Otherwise, print the next n events.		*/

void
undoPrintForw(iup, n)
    internalUndoEvent *iup;
    int n;
{
    int i = 0;

    (void) TxPrintf("head=0x%x\ttail=0x%x\tcur=0x%x\n",
		undoLogHead, undoLogTail, undoLogCur);
    if (iup == (internalUndoEvent *) NULL)
	iup = undoLogHead;
    while (iup != (internalUndoEvent *) NULL)
    {
	undoPrintEvent(iup);
	iup = iup->iue_forw;
	i++;
	if (i == n) break;
    }
}

/* Print events backward from "iup".  If n is 0 or negative, print to	*/
/* the beginning of the stack.  Otherwise, print the previous n events.	*/

void
undoPrintBack(iup, n)
    internalUndoEvent *iup;
    int n;
{
    int i = 0;

    (void) TxPrintf("head=0x%x\ttail=0x%x\tcur=0x%x\n",
		undoLogHead, undoLogTail, undoLogCur);
    if (iup == (internalUndoEvent *) NULL)
	iup = undoLogTail;
    while (iup != (internalUndoEvent *) NULL)
    {
	undoPrintEvent(iup);
	iup = iup->iue_back;
	i++;
	if (i == n) break;
    }
}

/* Called from windUndoCmd() or windRedoCmd() with n either positive, 	*/
/* or negative with 1 offset to differentiate between n = 0 (forward)	*/
/* and n = 0 (backward).						*/

void
UndoStackTrace(n)
    int n;
{
    if (n < 0)
       undoPrintBack(undoLogCur, -(n + 1));
    else
       undoPrintForw(undoLogCur, n);
}

