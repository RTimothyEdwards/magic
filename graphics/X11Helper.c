/* X11Helper.c --  This "helper" process, forked off by the magic process,
 *   looks like a kludge (and is), but is the only way to ensure that X11
 *   is able to run its event-driven protocol (through calls to XNextEvent)
 *   while magic runs its interrupt-driven protocol (through calls to
 *   select()).  Both are infinite (blocking) loops and must execute in
 *   parallel.  Xlib is not thread-safe, so running this as a thread
 *   instead of a forked process is a bad idea (confuses X11 and locks up).
 *
 *   In magic version 7.1, the executable name for X11Helper has been
 *   changed to XHelper7 to avoid compatibility conflicts with earlier
 *   versions of magic.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

/* 
 * Portability stuff
 */

#if     (defined(MIPSEB) && defined(SYSTYPE_BSD43)) || ibm032
# define        SIG_RETURNS_INT
#endif

/* Some machines have signal handlers returning an int, while other machines
 * have it returning a void.  If you have a machine that requires ints put 
 * it in the list of machines in utils/magic.h.
 */
#ifdef	SIG_RETURNS_INT
#define	sigRetVal	int
#else
#define	sigRetVal	void
#endif

void sigSetAction(int, sigRetVal (*)(int));
sigRetVal TimeOut(int);
void SetTimeOut();
void ParseEvent(XEvent *);

/*
 * Declaration of global variables and procedure prototypes
 */

int readPipe,writePipe;	/* pipe file descriptor to magic process */
int parentID;		/* process id of parent */
Display *grXdpy;	/* X11 display */

sigRetVal MapWindow();

/*
 * Main program:
 *   This infinite loop is a rewrite of XtMainLoop() which includes both
 *   the ability to handle interrupts and communicate with magic's main
 *   process via the I/O pipe.  The program exits when killed by the
 *   GrClose() routine in magic.
 *
 *   This process will look every TIMEOUT minutes to see if its parent process
 *   (magic) still exists.  This keeps the helper processes from becoming
 *   an orphan process if magic crashes or otherwise fails to execute
 *   GrClose() before exiting.
 */

#define TIMEOUT	10	/* timeout period (minutes) */

int
main (argc, argv)
    int argc;
    char **argv;
{
    XEvent xevent;

    if (argc <= 1) {
	fprintf(stderr, "%s: expecting two file-descriptor numbers.\n\
	    \t(This program should be run only by magic itself.)\n", argv[0]);
	exit(1);
    }

    sscanf(argv[1], "%d %d", &readPipe,&writePipe);
    grXdpy = XOpenDisplay(0);
    if (grXdpy == (Display *) 0) {
	fprintf(stderr,"%s: XOpenDisplay failed.\n", argv[0]);
	exit(1);
    }

    parentID = getppid();

/*     sigSetAction(SIGINT,  SIG_IGN); */
/*     sigSetAction(SIGQUIT, SIG_IGN); */
    sigSetAction(SIGTERM, MapWindow); 
#ifdef SIGTSTP
    sigSetAction(SIGTSTP, SIG_IGN);
#endif
#ifdef SIGCONT
    sigSetAction(SIGCONT, SIG_IGN);
#endif

    SetTimeOut();	/* set timer for TIMEOUT minutes */

    while (1)
    {
	XNextEvent(grXdpy, &xevent);
	ParseEvent(&xevent);
    }
}

/*
 * ParseEvent(XEvent *event):
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
 */

void
ParseEvent (event)
    XEvent *event;
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
#ifndef USE_IO_PROBE
	    kill(parentID, SIGIO);
#endif
	}
	else if (!strncmp(XKeysymToString(keysym), "KP_", 3))
	{
	    /* keypad key (special case---would like to		*/
	    /* differentiate between shift-KP-# and # itself)	*/
	    keymod &= ~ShiftMask;
	    keywstate = (keymod << 16) | (keysym & 0xffff);
	    write(writePipe, event, sizeof(XEvent));
	    write(writePipe, &keywstate, sizeof(int));
#ifndef USE_IO_PROBE
	    kill(parentID, SIGIO);
#endif
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
#ifndef USE_IO_PROBE
		    kill(parentID, SIGIO);
#endif
		}
	    }
	}
    }
    else if (event->type == DestroyNotify)
    {
	/* If this is the last window to go, it will be too late to catch */
	/* (i.e., read on pipe will never happen).			  */
	/* Reset timer for 1 second and check presence of parent	  */

	struct itimerval checktimer;

	getitimer(ITIMER_REAL, &checktimer);

	if (checktimer.it_value.tv_sec == 0 && checktimer.it_value.tv_usec == 0)
	    TimeOut(0);
	else	/* Set timer to 1 second */
	{
	    checktimer.it_value.tv_sec = 1;
	    checktimer.it_value.tv_usec = 0;
	}
	setitimer(ITIMER_REAL, &checktimer, NULL);
    }
    else  /* All event types other than KeyPress */
    {
	write(writePipe, event, sizeof(XEvent));
#ifndef USE_IO_PROBE
	kill(parentID, SIGIO);
#endif
    }
}

/*
 * SetTimeOut():
 *   Initiate an interval timer for an interval of TIMEOUT minutes.
 */

void
SetTimeOut()
{
    struct itimerval checktimer;

    checktimer.it_value.tv_sec = 60 * TIMEOUT;
    checktimer.it_value.tv_usec = 0;

    setitimer(ITIMER_REAL, &checktimer, NULL);

    sigSetAction(SIGALRM, TimeOut); /* Timeout signal handler */
}

/*
 * TimeOut():
 *   Check parent process to see if it still exists.  Commit suicide if
 *   orphaned (it's a harsh, harsh world).  Reset the timer for another
 *   TIMEOUT minutes, otherwise.
 */

sigRetVal
TimeOut(int signo)
{
    int tmpid;

    if ((tmpid = getppid()) != parentID)
    {
 	fprintf(stderr, X11HELP_PROG ": parent (ID %d) not found.  Exiting.\n",
		parentID);
	exit(1);
    }
    SetTimeOut(); 	/* Renew the timer and signal handler */
}
 
/*
 * MapWindow():
 *   On startup of any new magic window, magic writes the X11 window ID
 *   into the communication pipe and manually generates a SIGTERM signal.
 *   The interrupt handler calls MapWindow() on receiving SIGTERM, and
 *   the window ID is read from the I/O pipe.  MapWindow() then activates
 *   the window by declaring what events it will interpret for that
 *   window (keystrokes, mouse button, and expose and resize events)
 */

sigRetVal
MapWindow(int signo)
{
    Window window;

    if (read(readPipe, (char *)&window, sizeof(Window)) == sizeof(Window))
    {
	XSelectInput(grXdpy, window,
		     KeyPressMask|ButtonPressMask|ButtonReleaseMask|
		     ExposureMask|StructureNotifyMask|
		     VisibilityChangeMask|OwnerGrabButtonMask);
	XSync(grXdpy,1);
    }
    else
	fprintf(stderr, X11HELP_PROG ": read on pipe failed\n");
}

/*
 * This code duplicated from signals/signals.c so that this program is
 * independent of the rest of the magic code.
 */

void
sigSetAction(int signo, sigRetVal (*handler)(int))
{
#if defined(SYSV) || defined(CYGWIN)
  struct sigaction sa;

  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(signo, &sa, (struct sigaction *)NULL);
#else
  struct sigvec sv;

  sv.sv_handler = handler;
  sv.sv_mask    = 0;
  sv.sv_flags   = 0;
  sigvec(signo, &sv, (struct sigvec *)NULL);
#endif
}
