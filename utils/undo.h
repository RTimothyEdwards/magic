/*
 * undo.h --
 *
 * Exported definitions for the undo/redo module.
 * The undo/redo module is designed to be as client-independent
 * as possible.  Communication to and from clients is by means
 * of objects, allocated by the undo package, known as UndoEvents.
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
 * rcsid "$Header: /usr/cvsroot/magic-8.0/utils/undo.h,v 1.2 2009/09/10 20:32:55 tim Exp $"
 */

#ifndef	_UNDO_H
#define	_UNDO_H

#include "utils/magic.h"

/* -------------------- Exported definitions -------------------------- */

typedef int	 UndoType;	/* Type of undo event */
typedef char	 UndoEvent;	/* Externally visible undo event */

#define	UNDOLINESIZE	300	/* Maximum number of characters in external
				 * representation of undo event.
				 */

/*
 * Procedures for manipulating undo events.
 *
 *	UndoInit	-- start up the undo package and assign a log file.
 *	UndoAddClient	-- used by a client to inform the undo package of
 *			   its existence and to obtain an UndoType used in
 *			   all future interactions with undo.
 *	UndoIsEnabled	-- returns TRUE if the undo package is turned on,
 *			   and FALSE if it is disabled.
 *	UndoNewEvent	-- returns a new UndoEvent which the client may load
 *			   with its own data.  The event is appended to the
 *			   undo log.  The client should not retain this
 *			   new event past the next call to the undo package.  
 *			   If undoing is disabled, returns NULL.
 *	UndoNext	-- used by a client to inform the undo package that
 *			   all events since the last call to UndoNext are
 *			   to be treated as a single unit by UndoForward()
 *			   and UndoBackward().
 *	UndoBackward	-- play the undo log backward N units (until the prev-
 *			   ious call to UndoNext()).
 *	UndoForward	-- play the undo log forward N units.
 *	UndoDisable	-- turn off the undo package until the next UndoEnable.
 *	UndoEnable	-- turn the undo package back on.
 *	UndoFlush	-- throw away all undo information.
 */

extern bool UndoInit(char *, char *);
extern UndoType UndoAddClient();
extern UndoEvent *UndoNewEvent(UndoType, unsigned int);
/* extern UndoEvent *UndoCopyEvent(); */
extern void UndoNext(void);
extern int UndoBackward(int), UndoForward(int);
extern void UndoDisable(void), UndoEnable(void);
extern void UndoFlush(void);
extern void UndoStackTrace(int);

/*
 * ----------------------------------------------------------------------------
 *
 * UndoIsEnabled --
 *
 * Test whether the undo package is enabled.
 *
 * Results:
 *	Returns TRUE if undoing is enabled, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

extern int UndoDisableCount;
#define	UndoIsEnabled()	(UndoDisableCount == 0)

#endif	/* _UNDO_H */
