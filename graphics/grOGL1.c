/* grOGL1.c -
 *
 * This file contains primitive functions for OpenGL running under
 * an X window system (using GLUT).
 * Included here are initialization and closing
 * functions, and several utility routines used by the other X
 * modules.
 */

#include <GL/gl.h>
#include <GL/glx.h>

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include "utils/magic.h"
#include "utils/magsgtty.h"
#include "utils/geometry.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "graphics/graphicsInt.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/signals.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "grOGLInt.h"
#include "utils/paths.h"

GLubyte 	**grOGLStipples;
HashTable	grOGLWindowTable;
Display 	*grXdpy;
GLXContext	grXcontext;
int		grXscrn;
int		pipeRead, pipeWrite;
int		Xhelper;
Visual		*grVisual;

#ifdef HAVE_PTHREADS
extern int writePipe;
extern int readPipe;        /* As seen from child */
#endif

OGL_CURRENT oglCurrent= {(XFontStruct *)NULL, 0,0,0,0, (Window)0, (MagWindow *)NULL};

/* This is kind of a long story, and very kludgy, but the following
 * things need to be defined as externals because of the way lint
 * libraries are made by taking this module and changing all procedures
 * names "Xxxx" to "Grxxx".  The change is only done at the declaration
 * of the procedure, so we need these declarations to handle uses
 * of those names, which don't get modified.  Check out the Makefile
 * for details on this.
 */

extern void GrOGLClose(), GrOGLFlush();
extern void GrOGLDelete(), GrOGLConfigure(), GrOGLRaise(), GrOGLLower();
extern void GrOGLLock(), GrOGLUnlock(), GrOGLIconUpdate();
extern bool GrOGLInit(), GrOGLCreate();
extern void grOGLWStdin();


/*---------------------------------------------------------
 * groglSetWMandC:
 *	This is a local routine that resets the value of the current
 *	write mask and color, if necessary.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *
 * Errors:		None.
 *---------------------------------------------------------
 */

void
groglSetWMandC (mask, c)
    int mask;			/* New value for write mask */
    int c;			/* New value for current color */
{
    static int oldMask = -1;
    static int oldColor = -1;

    int lr, lb, lg;
    GLfloat fr, fb, fg;
    GLfloat aval = 0.75;		/* Alpha value */

    if (mask == -65) mask = 127;		/* All planes */
    if (mask == oldMask && c == oldColor) return;

    GR_X_FLUSH_BATCH();

    GrGetColor(c, &lr, &lb, &lg);

    fr = ((GLfloat)lr / 255);
    fg = ((GLfloat)lg / 255);
    fb = ((GLfloat)lb / 255);

    if (mask == 127)
       glDisable(GL_BLEND);
    else {
       /* Calculate a "supercolor", out of normal color range, but which  */
       /* results in the desired color after a blend with the background. */

       fr = fr * 2 - 0.8;
       fg = fg * 2 - 0.8;
       fb = fb * 2 - 0.8;

       glEnable(GL_BLEND);
       glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glColor4f(fr, fb, fg, aval); 

    oldMask = mask;
    oldColor = c;
}


/*---------------------------------------------------------
 * groglSetLineStyle:
 *	This local routine sets the current line style.
 *
 * Results:	None.
 *
 * Side Effects:
 *	A new line style is output to the display.
 *
 *---------------------------------------------------------
 */

void
groglSetLineStyle (style)
    int style;			/* New stipple pattern for lines. */
{
    static int oldStyle = -1;
    GLushort glstyle;

    style &= 0xFF;
    if (style == oldStyle) return;
    oldStyle = style;
    GR_X_FLUSH_BATCH();

    switch (style) {
    case 0xFF:
    case 0x00:
	glDisable(GL_LINE_STIPPLE);
	break;
    default:
	glstyle = style | (style << 8);
	glEnable(GL_LINE_STIPPLE);
	glLineStipple(1, glstyle);
    }
}


/*---------------------------------------------------------
 * groglSetSPattern:
 *	xSetSPattern associates stipple patterns with
 *	OpenGL stipples.  This is a local routine
 *	called from grStyle.c.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
groglSetSPattern (sttable, numstipples)
    int **sttable;			/* The table of patterns */
    int numstipples;			/* Number of stipples */
{
    int i, j, k, n;
    GLubyte *pdata;

    grOGLStipples = (GLubyte **)mallocMagic(numstipples * sizeof(GLubyte *));
    for (k = 0; k < numstipples; k++)
    {
	pdata = (GLubyte *)mallocMagic(128 * sizeof(GLubyte));
	n = 0;

	/* expand magic's default 8x8 stipple to OpenGL's 32x32 */

	for (i = 0; i < 32; i++)
	    for (j = 0; j < 4; j++)
		pdata[n++] = (GLubyte)sttable[k][i % 8];

	grOGLStipples[k] = pdata;
    }
}


/*---------------------------------------------------------
 * groglSetStipple:
 *	This routine sets the Xs current stipple number.
 *
 * Results: None.
 *
 * Side Effects:
 *	The current clipmask in the X is set to stipple,
 *	if it wasn't that already.
 *---------------------------------------------------------
 */

void
groglSetStipple (stipple)
    int stipple;			/* The stipple number to be used. */
{
    static int oldStip = -1;
    if (stipple == oldStip) return;
    oldStip = stipple;
    GR_X_FLUSH_BATCH();
    if (stipple == 0 || stipple > grNumStipples) {
	glDisable(GL_POLYGON_STIPPLE);
    } else {
	if (grOGLStipples[stipple] == (GLubyte *)NULL) MainExit(1);
	glEnable(GL_POLYGON_STIPPLE);
	glPolygonStipple(grOGLStipples[stipple]);
    }
}


/*---------------------------------------------------------
 * GrOGLInit:
 *	GrOGLInit initializes the graphics display and clears its screen.
 *	Files must have been previously opened with GrSetDisplay();
 *
 * Results: TRUE if successful.
 *---------------------------------------------------------
 */

bool
GrOGLInit()
{
    XVisualInfo *grVisualInfo;
    static int	attributeList[] = { GLX_RGBA, None, None };
    static char *OGLCMapType = "OpenGL";

    grCMapType = OGLCMapType;
    grDStyleType = OGLCMapType;

#ifdef HAVE_PTHREADS
    XInitThreads();
#endif

    grXdpy = XOpenDisplay(NULL);
    if (grXdpy == NULL)
    {   
	TxError("Couldn't open display; check DISPLAY variable\n");
	return FALSE;
    }
    grXscrn = DefaultScreen(grXdpy);
    grVisualInfo = glXChooseVisual(grXdpy, grXscrn, attributeList);
    if (!grVisualInfo)
    {
	/* Try for a double-buffered configuration (added by Holger Vogt) */
	attributeList[1] = GLX_DOUBLEBUFFER;
	grVisualInfo = glXChooseVisual(grXdpy, grXscrn, attributeList);
	if (!grVisualInfo)
	{
	    TxError("No suitable visual!\n");
	    MainExit(1);
	}
    }
    grXscrn = grVisualInfo->screen;
    grVisual = grVisualInfo->visual;
    oglCurrent.depth = grVisualInfo->depth;

    /* Note: The last parameter is GL_TRUE for direct rendering. 	*/
    /* Direct rendering has a speedup advantange for raw rendering, but	*/
    /* disallows X11 operations such as XCopyArea, which prevents the	*/
    /* implementation of backing store and fast screen refreshes.  So	*/
    /* we force an indirect rendering context through the X server.	*/

    grXcontext = glXCreateContext(grXdpy, grVisualInfo, NULL, GL_FALSE);

    /* Basic GL parameters */

    glLineWidth(1.0);
    glShadeModel (GL_FLAT);
    glPixelStorei(GL_PACK_LSB_FIRST, TRUE);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    HashInit(&grOGLWindowTable,8,HT_WORDKEYS);

    /* Globally-accessed variables */
    grNumBitPlanes = oglCurrent.depth;
    grBitPlaneMask = (1 << oglCurrent.depth) - 1;

    if (grVisualInfo != NULL) XFree(grVisualInfo);
    return groglPreLoadFont();
}

/*---------------------------------------------------------
 * GrOGLClose:
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Helper process/thread killed.
 *---------------------------------------------------------
 */

void
GrOGLClose()
{
    if (grXdpy == NULL) return;
    TxDelete1InputDevice(pipeRead);
    close(pipeRead);
#ifndef HAVE_PTHREADS
    kill(Xhelper, SIGKILL);
    do {} while (wait(0) != Xhelper);
#endif
    XCloseDisplay(grXdpy);
#ifdef HAVE_PTHREADS
    xloop_end();
#endif
}


/*---------------------------------------------------------
 * GrOGLFlush:
 * 	Flush output to display.
 *
 *	Flushing is done automatically the next time input is read,
 *	so this procedure should not be used very often.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
GrOGLFlush()
{
   GR_X_FLUSH_BATCH();
   glFlush();
   glFinish();
}

/*------------------------------------------------------*/
/* Translate event coordinate to window coordinate	*/
/* (y is flipped relative to the window top)		*/
/*------------------------------------------------------*/

#define glTransYs(n) (DisplayHeight(grXdpy, grXscrn)-(n))

/*
int
glTransYs(int wy)
{
   int my;
   GLint vparms[4];

   glGetIntegerv(GL_VIEWPORT, vparms);
   my = vparms[3] - wy;

   return my;
}
*/

/*
 *----------------------------------------------------------------------
 * Set the OpenGL viewport (projection matrix) for the current window
 *----------------------------------------------------------------------
 */
int
oglSetProjection(llx, lly, width, height)
    int llx, lly, width, height;
{
    glXMakeCurrent(grXdpy, (GLXDrawable)oglCurrent.window, grXcontext);

#ifndef OGL_SERVER_SIDE_ONLY
    /* For batch-processing lines and rectangles */
    glEnableClientState(GL_VERTEX_ARRAY);
#endif

    /* Force draw to front buffer (in case of double-buffered config) */
    glDrawBuffer(GL_FRONT);		/* added by Holger Vogt */

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glViewport((GLsizei)llx, (GLsizei)lly, (GLsizei) width, (GLsizei) height);

    /* scale to fit window */

#ifdef OGL_INVERT_Y
    glScalef(1.0 / (float)(width >> 1), -1.0 / (float)(height >> 1), 1.0);
#else
    glScalef(1.0 / (float)(width >> 1), 1.0 / (float)(height >> 1), 1.0);
#endif

    /* magic origin maps to window center; move to window origin */

    glTranslated(-(GLsizei)(width >> 1), -(GLsizei)(height >> 1), 0);
}

/*----------------------------------------------------------------------*/
/* pipehandler() is the callback set up by TxAdd1Input.  The purpose is */
/* to comply with magic's interrupt-driven protocol;  magic blocks on   */
/* select() until an input arrives at a registered file descriptor, 	*/
/* then executes the callback associated with the fd, then checks to	*/
/* see if the Tx event queue size grew.  X11Handler passes info along	*/
/* to X11Stdin() in the X11 version;  that's because the X11 XtMainLoop */
/* is a macro and can be re-implemented using XNextEvent() calls.	*/
/*----------------------------------------------------------------------*/

void
pipehandler()
{
   TxInputEvent	*event;
   XEvent	xevent;
   HashEntry	*entry;
   MagWindow	*mw;

   read(pipeRead, &xevent, sizeof(XEvent));

   switch(xevent.type) {
      case KeyPress:	{	/* Keyboard Callback Function */

	int		wx, wy, ky;
	int 		key;
	XKeyPressedEvent *KeyPressedEvent = (XKeyPressedEvent *) &xevent;

	entry = HashLookOnly(&grOGLWindowTable, KeyPressedEvent->window);
	mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;

	event = TxNewEvent();

	read(pipeRead, &key, sizeof(int));

	if (key == (int)'\015') key = (int)'\n';  /* Linefeed to Return	*/

	event->txe_button = TX_CHARACTER;
	event->txe_ch = key;
	event->txe_buttonAction = TX_KEY_DOWN;
	event->txe_p.p_x = KeyPressedEvent->x;
	event->txe_p.p_y = glTransY(mw, KeyPressedEvent->y);
	event->txe_wid = mw ? mw->w_wid : WIND_UNKNOWN_WINDOW;
	TxAddEvent(event);
	} break;

      case ButtonPress:
      case ButtonRelease: {	/* Mouse Callback Function */

	XButtonEvent *ButtonEvent = (XButtonEvent *) &xevent;

	entry = HashLookOnly(&grOGLWindowTable, ButtonEvent->window);
	mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;

	event = TxNewEvent();
	switch (ButtonEvent->button) {
	    case Button1:
	    	event->txe_button = TX_LEFT_BUTTON;
	    	break;
	    case Button2:
	        event->txe_button = TX_MIDDLE_BUTTON;
	        break;
	    case Button3:
	    	event->txe_button = TX_RIGHT_BUTTON;
	    	break;
	    case Button4:
	    	event->txe_button = TX_BUTTON_4;
	    	break;
	    case Button5:
	    	event->txe_button = TX_BUTTON_5;
	    	break;
   	}
	switch(ButtonEvent->type) {
	    case ButtonRelease:
	    	event->txe_buttonAction = TX_BUTTON_UP;
	    	break;
	    case ButtonPress:
	    	event->txe_buttonAction = TX_BUTTON_DOWN;
	    	break;
	}

	event->txe_p.p_x = ButtonEvent->x;
	event->txe_p.p_y = glTransY(mw, ButtonEvent->y);
	event->txe_wid = mw ? mw->w_wid : WIND_UNKNOWN_WINDOW;

	TxAddEvent(event);
	} break;

      case ConfigureNotify:	{	/* Reshape/Resize Callback Function */

	XConfigureEvent *ConfigureEvent = (XConfigureEvent *) &xevent;
	XEvent		discard;
	Rect		screenRect;
	int		width, height;

	width = ConfigureEvent->width;
	height = ConfigureEvent->height;

	entry = HashLookOnly(&grOGLWindowTable, ConfigureEvent->window);
	mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;

	screenRect.r_xbot = ConfigureEvent->x;
	screenRect.r_xtop = ConfigureEvent->x + width;
	screenRect.r_ybot = glTransYs(ConfigureEvent->y);
	screenRect.r_ytop = glTransYs(ConfigureEvent->y + height);
		 
	SigDisableInterrupts();

	/* Redraw the window */

	WindReframe(mw, &screenRect, FALSE, FALSE);
	WindRedisplay(mw);
	SigEnableInterrupts();

	} break;

	case VisibilityNotify:		{
	    XVisibilityEvent *VisEvent = (XVisibilityEvent*) &xevent;

	    entry = HashLookOnly(&grOGLWindowTable, VisEvent->window);
	    mw = (entry)?(MagWindow *)HashGetValue(entry):0;

	    switch(VisEvent->state)
	    {
		case VisibilityUnobscured:
		    mw->w_flags &= ~WIND_OBSCURED;
		    if (mw->w_backingStore == (ClientData)NULL)
		    {
			groglCreateBackingStore(mw);
			if (mw->w_backingStore != (ClientData)NULL)
			{
			    WindAreaChanged(mw, &mw->w_allArea);
			    WindUpdate();
			}
		    }
		    break;
		case VisibilityPartiallyObscured:
		case VisibilityFullyObscured:
		    mw->w_flags |= WIND_OBSCURED;
		    break;
	    }
	} break;

      case Expose:	{	/* (Re)Display Callback Function */

	Rect		screenRect;
	XEvent		discard;
	XExposeEvent	*ExposeEvent = (XExposeEvent*) &xevent;

	entry = HashLookOnly(&grOGLWindowTable, ExposeEvent->window);
	mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;

	screenRect.r_xbot = ExposeEvent->x;
	screenRect.r_xtop = ExposeEvent->x + ExposeEvent->width;
	screenRect.r_ytop = mw->w_allArea.r_ytop - ExposeEvent->y;
	screenRect.r_ybot = mw->w_allArea.r_ytop -
			(ExposeEvent->y + ExposeEvent->height);

	if (mw->w_backingStore != (ClientData)NULL)
	{
	    Rect surface;
	    (*GrGetBackingStorePtr)(mw, &screenRect);
	    WindScreenToSurface(mw, &screenRect, &surface);
	    DBWHLRedrawPrepWindow(mw, &surface);
	    WindDrawBorder(mw, &screenRect);
	}	
	else
	    WindAreaChanged(mw, &screenRect);
	WindUpdate(mw);

	} break;

      case CreateNotify:	{
	
   	XAnyEvent 	*AnyEvent = (XAnyEvent *) &xevent;

	entry = HashLookOnly(&grOGLWindowTable, AnyEvent->window);
	mw = (entry)?(MagWindow *)HashGetValue(entry):0;

	SigDisableInterrupts();
	WindView(mw);
	SigEnableInterrupts();
	} break;
   }
}

/*---------------------------------------------------------
 * oglSetDisplay:
 *	This routine sets the appropriate parameters so that
 *	Magic will work with glX.
 *
 * Results:  success / fail
 *
 *---------------------------------------------------------
 */

bool
oglSetDisplay (dispType, outFileName, mouseFileName)
    char *dispType;		/* arguments not used by X */
    char *outFileName;
    char *mouseFileName;
{
    int fildes[2], fildes2[2];
    char	*planecount;
    char 	*fullname;
    FILE* 	f;
    bool	execFailed = FALSE;

    WindPackageType = WIND_X_WINDOWS;	/* This works okay. */

    grCursorType = "bw";
    
    WindScrollBarWidth = 14;

    pipe(fildes);
    pipe(fildes2);
    pipeRead = fildes[0];
    pipeWrite = fildes2[1];

    TxAdd1InputDevice(pipeRead, pipehandler, (ClientData) NULL);

#ifdef HAVE_PTHREADS
    writePipe = fildes[1];
    readPipe = fildes2[0];
#else
#ifdef CYGWIN
    f = PaOpen(X11HELP_PROG, "r", ".exe",
                HELPER_PATH, (char *) NULL, &fullname);
#else
    f = PaOpen(X11HELP_PROG, "r", (char *) NULL,
                HELPER_PATH, (char *) NULL, &fullname);
#endif
    if (f == NULL) {
        int error;
        TxError("Couldn't find helper process %s in search path \"%s\"\n",
            X11HELP_PROG, HELPER_PATH);
        error = 0;
        write(fildes[1], &error, 4);
        return FALSE;
    }
    else {
        fclose(f);
    }
    
    FORK(Xhelper);
    if (Xhelper == 0) {    /* Child process */
        char argv[2][100];

        sprintf(argv[0], "%s", fullname);
        sprintf(argv[1], "%d %d", fildes2[0],fildes[1]);
        if (execl(argv[0], argv[0], argv[1], 0) != 0)
        {
            execFailed = TRUE;
            TxError("Couldn't execute helper process \"%s\".\n", fullname);
            TxFlush();
            /* we're the child process -- don't muck things up by returning */
            _exit(656);  /* see vfork man page for reason for _exit() */
        }
    };
    sleep(1);
#endif

    /* Set up the procedure values in the indirection table. */

    GrPixelCorrect = 0;

    GrLockPtr = GrOGLLock;
    GrUnlockPtr = GrOGLUnlock;
    GrInitPtr = GrOGLInit;
    GrClosePtr = GrOGLClose;
    GrSetCMapPtr = GrOGLSetCMap;

    GrEnableTabletPtr = GrOGLEnableTablet;
    GrDisableTabletPtr = GrOGLDisableTablet;
    GrSetCursorPtr = GrOGLSetCursor;
    GrTextSizePtr = GrOGLTextSize;
    GrDrawGlyphPtr = GrOGLDrawGlyph;
    GrReadPixelPtr = GrOGLReadPixel;
    GrFlushPtr = GrOGLFlush;

    GrCreateWindowPtr = GrOGLCreate;
    GrDeleteWindowPtr = GrOGLDelete;
    GrConfigureWindowPtr = GrOGLConfigure;
    GrOverWindowPtr = GrOGLRaise;
    GrUnderWindowPtr = GrOGLLower;
    GrUpdateIconPtr = GrOGLIconUpdate; 
    GrBitBltPtr = GrOGLBitBlt;

    GrFreeBackingStorePtr = groglFreeBackingStore;
    GrCreateBackingStorePtr = groglCreateBackingStore;
    GrGetBackingStorePtr = groglGetBackingStore;
    GrPutBackingStorePtr = groglPutBackingStore;
    GrScrollBackingStorePtr = groglScrollBackingStore;

    /* local indirections */
    grSetSPatternPtr = groglSetSPattern;
    grPutTextPtr = groglPutText;
#ifdef VECTOR_FONTS
    grFontTextPtr = groglFontText;
#endif
    grDefineCursorPtr = groglDefineCursor;
    grDrawGridPtr = groglDrawGrid;
    grDrawLinePtr = groglDrawLine;
    grSetWMandCPtr = groglSetWMandC;
    grFillRectPtr = groglFillRect;
    grSetStipplePtr = groglSetStipple;
    grSetLineStylePtr = groglSetLineStyle;
    grSetCharSizePtr = groglSetCharSize;
    grFillPolygonPtr = groglFillPolygon;

    if (execFailed) {
        TxError("Execution failed!\n");
        return FALSE;
    }

    TxAdd1InputDevice(fileno(stdin), grOGLWStdin, (ClientData) NULL);

    if(!GrOGLInit()){
	return FALSE;
    }
    GrScreenRect.r_xbot = 0;
    GrScreenRect.r_ybot = 0;
    GrScreenRect.r_xtop = DisplayWidth(grXdpy,grXscrn);
    GrScreenRect.r_ytop = DisplayHeight(grXdpy,grXscrn);

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * grOGLWStdin --
 *      Handle the stdin device for X window interface.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Adds events to the event queue.
 *
 * ----------------------------------------------------------------------------
 */
 /*ARGSUSED*/

void
grOGLWStdin(fd, cdata)
   int fd;
   ClientData cdata;
{
   int ch;
   TxInputEvent *event;

   event = TxNewEvent();
   ch = getc(stdin);
   if (ch == EOF)
      event->txe_button = TX_EOF;
   else
      event->txe_button = TX_CHARACTER;
   event->txe_ch = ch;
   event->txe_buttonAction = 0;
   event->txe_wid = WIND_NO_WINDOW;
   event->txe_p.p_x = GR_CURSOR_X;
   event->txe_p.p_y = GR_CURSOR_Y;
   TxAddEvent(event);
}


/*
 * ----------------------------------------------------------------------------
 *
 * GrOGLCreate --
 *      Create a new window under OpenGL
 *	Bind OpenGL window to Magic Window w.
 *
 * Results:
 *	Success/Fail
 *
 * Side Effects:
 *      Window created, window ID send to OGLhelper.
 *
 * ----------------------------------------------------------------------------
 */

bool
GrOGLCreate(w, name)
    MagWindow *w;
    char *name;
{
   Window	wind;
   HashEntry	*entry;
   static int	firstWindow = 1;
   XSizeHints	*xsh;
   char		*windowplace;
   char		*option = (firstWindow)?"window":"newwindow";
   bool		result = TRUE;
   int		x     = w->w_frameArea.r_xbot;
   int		y     = glTransYs(w->w_frameArea.r_ytop);
   unsigned int	width = w->w_frameArea.r_xtop - w->w_frameArea.r_xbot;
   unsigned int	height = w->w_frameArea.r_ytop - w->w_frameArea.r_ybot;
   unsigned long attribmask = CWBackPixel | CWBorderPixel | CWColormap;
   XSetWindowAttributes	grAttributes;
   XConfigureEvent xevent;

   WindSeparateRedisplay(w);
   xsh = XAllocSizeHints();
   if (windowplace=XGetDefault(grXdpy,"magic",option)) {
       XParseGeometry(windowplace,&x,&y,&width,&height);
       w->w_frameArea.r_xbot = x;
       w->w_frameArea.r_xtop = x+width;
       w->w_frameArea.r_ytop = glTransYs(y);
       w->w_frameArea.r_ybot = glTransYs(y+height);
       WindReframe(w,&(w->w_frameArea),FALSE,FALSE);
       xsh->flags = USPosition | USSize;
   }
   else {
       xsh->flags = PPosition|PSize;
   }

   grAttributes.colormap = XCreateColormap(grXdpy, RootWindow(grXdpy, grXscrn),
		grVisual, AllocNone);
   grAttributes.background_pixel = WhitePixel(grXdpy, grXscrn);
   grAttributes.border_pixel = BlackPixel(grXdpy,grXscrn);

   if (wind = XCreateWindow(grXdpy,  RootWindow(grXdpy, grXscrn),
                x, y, width, height, 0, oglCurrent.depth, InputOutput,
		grVisual, attribmask, &grAttributes)) {
	xsh->x = w->w_frameArea.r_xbot;
	xsh->y = glTransYs(w->w_frameArea.r_ytop);
	xsh->width = w->w_frameArea.r_xtop - w->w_frameArea.r_xbot;
	xsh->height= w->w_frameArea.r_ytop - w->w_frameArea.r_ybot;
	XSetStandardProperties(grXdpy, wind, (name == NULL) ? "magic" : name,
			"magic", None, 0, 0, xsh);

	XMapWindow(grXdpy, wind);

	oglCurrent.window = wind;
	oglCurrent.mw = w;
        glXMakeCurrent(grXdpy, (GLXDrawable)wind, grXcontext);

	w->w_grdata = (ClientData) wind;
	entry = HashFind(&grOGLWindowTable, wind);
	HashSetValue(entry,w);
	XDefineCursor(grXdpy, wind, oglCurrent.cursor);
   	GrOGLIconUpdate(w, w->w_caption);

#ifdef HAVE_PTHREADS
        xloop_create(wind);
#else
	XSync(grXdpy,0);

	write(pipeWrite, (char *) &wind, sizeof(Window));
	kill( Xhelper, SIGTERM);
#endif

	if (firstWindow)
	{
	    firstWindow = 0;
	    result = groglLoadFont();
	}

	/* Force a StructureNotify event to get X11Helper to paint the window */
	usleep(600);
	xevent.type = ConfigureNotify;
	xevent.width = width;
	xevent.height = height;
	xevent.x = xsh->x;
	xevent.y = xsh->y;
	xevent.window = wind;
	XSendEvent(grXdpy, wind, FALSE, StructureNotifyMask, (XEvent *)&xevent);
	XFree(xsh);
	return result;
    }
    else {
	TxError("Could not open new X window\n");
	result = FALSE;
    }
    return result;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrOGLDelete --
 *      Destroy an X window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      Window destroyed.
 *
 * ----------------------------------------------------------------------------
 */

void
GrOGLDelete(w)
    MagWindow *w;
{
    int xw;
    HashEntry	*entry;

    xw = (Window) w->w_grdata;
    entry = HashLookOnly(&grOGLWindowTable,xw);
    HashSetValue(entry,NULL);

    XDestroyWindow(grXdpy, xw);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrOGLConfigure --
 *      Full Screen function
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Window reconfigured to w->w_frameArea.
 *
 * ----------------------------------------------------------------------------
 */

void
GrOGLConfigure(w)
    MagWindow *w;
{
    XMoveResizeWindow(grXdpy,(Window) w->w_grdata,
            w->w_frameArea.r_xbot, glTransYs(w->w_frameArea.r_ytop),
                w->w_frameArea.r_xtop - w->w_frameArea.r_xbot,
                    w->w_frameArea.r_ytop - w->w_frameArea.r_ybot);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrOGLRaise --
 *      Raise a window to the top of the screen such that nothing
 *	obscures it.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Window raised.
 *
 * ----------------------------------------------------------------------------
 */

void
GrOGLRaise(w)
    MagWindow *w;
{
    XRaiseWindow(grXdpy, (Window) w->w_grdata );
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrOGLLower --
 *      Lower a window below all other X windows.
 *	obscures it.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Window lowered.
 *
 * ----------------------------------------------------------------------------
 */

void
GrOGLLower(w)
    MagWindow *w;
{
    XLowerWindow(grXdpy, (Window) w->w_grdata );
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrOGLLock --
 *      Lock a window and set global variables "oglCurrent.window"
 *	and "oglCurrent.mw" to reference the locked window.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *      Window locked.
 *
 * ----------------------------------------------------------------------------
 */

void
GrOGLLock(w, flag)
    MagWindow *w;
    bool flag;
{
    grSimpleLock(w, flag);
    if ( w != GR_LOCK_SCREEN )
    {
	oglCurrent.mw = w;
	oglCurrent.window = (Window) w->w_grdata;

	oglSetProjection(w->w_allArea.r_xbot, w->w_allArea.r_ybot,
			 w->w_allArea.r_xtop - w->w_allArea.r_xbot,
			 w->w_allArea.r_ytop - w->w_allArea.r_ybot);
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrOGLUnlock --
 *      Unlock a window, flushing stuff out to the display.
 *
 * Results:
 *	Display update.
 *
 * Side Effects:
 *      Window unlocked.
 *
 * ----------------------------------------------------------------------------
 */

void
GrOGLUnlock(w)
    MagWindow *w;
{
    GrOGLFlush();
    grSimpleUnlock(w);
}


/*
 *-------------------------------------------------------------------------
 *
 * GrOGLIconUpdate -- updates the icon text with the window script
 *
 * Results: none
 *
 * Side Effects: changes the icon text
 *
 *-------------------------------------------------------------------------
 */

void
GrOGLIconUpdate(w,text)
	MagWindow	*w;
	char		*text;
{
     Window wind = (Window) w->w_grdata;
     XClassHint	class;
     char	*brack;
     
     if (w->w_grdata == (ClientData)NULL) return;
     class.res_name = "magic";
     class.res_class = "magic";
     XSetClassHint( grXdpy, wind, &class);

     if (brack = strchr(text,'['))
     {
     	  brack--;
	  *brack = 0;
	  XSetIconName(grXdpy,wind,text);
	  XStoreName(grXdpy,wind,text);
     	  *brack = ' ';
	  return;
     }
     if (brack = strrchr(text,' ')) text = brack+1;
     XSetIconName(grXdpy,wind,text);
     XStoreName(grXdpy,wind,text);
}
