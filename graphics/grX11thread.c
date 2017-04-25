/* grX11thread.c -
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 * This file contains functions to run the X11 event loop as a separate
 * thread.  Used by X11 and OpenGL running under X11.  Compiled only if
 * option "THREADS" is set in "make config".
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <pthread.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "graphics/graphicsInt.h"
#include "grX11Int.h"

Display *grXdpy;

int writePipe;		/* As seen from child */
int readPipe;

pthread_t xloop_thread = 0;

/*------------------------------------------------------*/
/* ParseEvent is copied from X11Helper.c		*/
/*------------------------------------------------------*/

/*
 *--------------------------------------------------------------
 * ParseEvent(XEvent *event) --
 *
 *   Pass XEvents on to the magic main process by writing into the
 *   connecting pipe.  Keystrokes must be handled such that magic
 *   can treat untranslated keyboard input from stdin the same way
 *   that it treats translated keyboard input through X11.  
 *   Use XLookupString() to get an ASCII character out of the
 *   keycode, but also pass back the event structure so we can
 *   pull out key modifier information in grX11Stdin().
 *
 *   Pass the event and key value back through the I/O pipe so that
 *   the keypress triggers the select() mechanism in magic's textio
 *   routine.
 *
 * Results:
 *	None.
 *
 * Side effects: 
 *	pipe I/O
 *
 *--------------------------------------------------------------
 */

void
ParseEvent (event, parentID)
    XEvent *event;
    int parentID;
{
    if (event->type == KeyPress) 
    {
	XKeyPressedEvent *KeyPressedEvent = (XKeyPressedEvent *) event;
	char inChar[10], c, *p;
	KeySym keysym;
	int keywstate;	/* KeySym with prepended state information */
	int keymod;
	int nbytes;

	nbytes = XLookupString(KeyPressedEvent, inChar, sizeof(inChar), &keysym, NULL);

	if (IsModifierKey(keysym)) return;  /* Don't send modifiers as events */

	keymod = (LockMask | ControlMask | ShiftMask)
				& KeyPressedEvent->state;

#ifdef __APPLE__
	if (KeyPressedEvent->state & (Mod1Mask | Mod2Mask | Mod3Mask
				| Mod4Mask | Mod5Mask))
	    keymod |= Mod1Mask;
#else
	keymod |= (Mod1Mask & KeyPressedEvent->state);
#endif

	if (nbytes == 0)  	/* No ASCII equivalent to string */
	{
	    keywstate = (keymod << 16) | (keysym & 0xffff);

	    write(writePipe, event, sizeof(XEvent));
	    write(writePipe, &keywstate, sizeof(int));
	}
	else if (!strncmp(XKeysymToString(keysym), "KP_", 3))
	{
	    /* keypad key (special case---would like to		*/
	    /* differentiate between shift-KP-# and # itself)	*/
	    keymod &= ~ShiftMask;
	    keywstate = (keymod << 16) | (keysym & 0xffff);
	    write(writePipe, event, sizeof(XEvent));
	    write(writePipe, &keywstate, sizeof(int));
	}
	else			/* For keys with ASCII equivalent */
	{
	    /* If Control or Shift is used alone, it should be removed from  */
	    /* the modifier list, since the ASCII value subsumes the Control */
	    /* and Shift functions.  However, Control + Shift + key should   */
	    /* be retained, as it cannot be expressed in ASCII.		     */

	    if (!(keymod & (LockMask | Mod1Mask))) {
		if (!(keymod & ControlMask))
		    keymod &= ~ShiftMask;
		else if (!(keymod & ShiftMask))
		    keymod &= ~ControlMask;
	    }

	    p = inChar;
	    while (nbytes--)
	    {
		if ((c = *p++) == 3)	/* Ctrl-C interrupt */
		{
		    kill(parentID, SIGINT);
		} 
		else
		{
		    /* When Control modifier is present, use the capital */
		    /* letter value instead of the control value.	 */

		    if ((keymod & ControlMask) && (c < 32))
			c += 'A' - 1;

		    keywstate = (keymod << 16) | ((int)c & 0xff);

		    write(writePipe, event, sizeof(XEvent));
		    write(writePipe, &keywstate, sizeof(int));
		}
	    }
	}
    }
    else if (event->type == DestroyNotify)
    {
	/* do nothing? */
    }
    else  /* All event types other than KeyPress */
    {
	write(writePipe, event, sizeof(XEvent));
    }
}

/*---------------------------------------------------------
 * xloop_begin:  Threaded version of XHelper7 (X11Helper.c)
 *
 *---------------------------------------------------------
 */

void
xloop_begin(window)
    Window window;
{
    XEvent xevent;
    int parentID;

    parentID = (int)getppid();

    /* Set this thread to cancel immediately when requested */
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    XLockDisplay(grXdpy);
    XSelectInput(grXdpy, window,
	    KeyPressMask|ButtonPressMask|ButtonReleaseMask|
	    ExposureMask|StructureNotifyMask|
	    VisibilityChangeMask|OwnerGrabButtonMask);
    XSync(grXdpy,1);
    XUnlockDisplay(grXdpy);

    while (1) {
	XNextEvent(grXdpy, &xevent);
	XLockDisplay(grXdpy);
	ParseEvent(&xevent, parentID);
	XUnlockDisplay(grXdpy);
    }
}  

/*----------------------------------------------------------------------
 * xloop_create --
 *	Function creates a thread for the X event loop.
 *
 * Results:
 *	Returns 0 on success of pthread_create, non-zero on failure.
 *
 * Side Effects:
 *	Thread crated
 *----------------------------------------------------------------------
 */

int
xloop_create(window)
    Window window;
{
    int status = 0;

    XLockDisplay(grXdpy);
    XSelectInput(grXdpy, window,
		KeyPressMask|ButtonPressMask|ButtonReleaseMask|ExposureMask|
		StructureNotifyMask|OwnerGrabButtonMask);
    XSync(grXdpy,1);
    XUnlockDisplay(grXdpy);

    if (xloop_thread == (pthread_t)0)
        status = pthread_create(&xloop_thread, NULL, (void *)xloop_begin,
		(void *)window);

    return status;
}

/*
 *----------------------------------------------------------------------
 * xloop_end --
 *	Kill the X event loop thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread destroyed.
 *----------------------------------------------------------------------
 */

void
xloop_end()
{
    if (xloop_thread != (pthread_t)0)
	pthread_cancel(xloop_thread);

    /* For whatever reason, calling this used to work, now just	*/
    /* causes magic to hang. . . 				*/
    /* pthread_join(xloop_thread, NULL); */
}

/*----------------------------------------------------------------------*/
