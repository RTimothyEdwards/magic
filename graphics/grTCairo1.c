/* grTCairo1.c
 *
 * Copyright (C) 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 * This file contains primitive functions for OpenGL running under
 * an X window system in a Tcl/Tk interpreter environment
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

/*
#include <GL/gl.h>
#include <GL/glx.h>
*/
#include <cairo/cairo-xlib.h>

#include "tcltk/tclmagic.h"
#include "utils/main.h"
#include "utils/magic.h"
#include "utils/malloc.h"
#include "utils/magsgtty.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/signals.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "utils/macros.h"
//#include "graphics/grTCairoInt.h"
#include "graphics/grTCairoInt.h"
#include "utils/paths.h"
#include "graphics/grTkCommon.h"

uint8_t			**grTCairoStipples;
HashTable		grTCairoWindowTable;
//GLXContext	grXcontext;
cairo_surface_t *grCairoSurface;
cairo_t 		*grCairoContext;
XVisualInfo		*grVisualInfo;

//TCairo_CURRENT tcairoCurrent= {(Tk_Font)0, 0, 0, 0, 0,
//	(Tk_Window)0, (Window)0, (MagWindow *)NULL};

TCAIRO_CURRENT tcairoCurrent = {(Tk_Font)0, 0, 0, 0, 0,
                                (Tk_Window)0, (Window)0, (MagWindow *)NULL
                               };

/* This is kind of a long story, and very kludgy, but the following
 * things need to be defined as externals because of the way lint
 * libraries are made by taking this module and changing all procedures
 * names "Xxxx" to "Grxxx".  The change is only done at the declaration
 * of the procedure, so we need these declarations to handle uses
 * of those names, which don't get modified.  Check out the Makefile
 * for details on this.
 */

extern void GrTCairoClose(), GrTCairoFlush();
extern void GrTCairoDelete(), GrTCairoConfigure(), GrTCairoRaise(), GrTCairoLower();
extern void GrTCairoLock(), GrTCairoUnlock(), GrTCairoIconUpdate();
extern bool GrTCairoInit();
extern bool GrTCairoEventPending(), GrTCairoCreate(), grtcairoGetCursorPos();
extern int  GrTCairoWindowId();
extern char *GrTkWindowName();

extern void tcairoSetProjection();

/*---------------------------------------------------------
 * grtcairoSetWMandC:
 *	This is a local routine that resets the value of the current
 *	write alpha (mask) and color, if necessary.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *
 * Errors:		None.
 *---------------------------------------------------------
 */

void
grtcairoSetWMandC (mask, c)
int mask;			/* New value for write mask */
int c;			/* New value for current color */
{
	static int oldColor = -1;
	static int oldMask = -1;

	int lr, lb, lg;
	//GLfloat fr, fb, fg;
	//GLfloat aval; 	 /* Alpha default value was 0.75 */
	float fr, fb, fg, aval;

	if (mask == -65) mask = 127;	/* All planes */
	if (mask == oldMask && c == oldColor) return;

	//GR_TCairo_FLUSH_BATCH();
	GR_TCAIRO_FLUSH_BATCH();

	GrGetColor(c, &lr, &lb, &lg);

	fr = ((float)lr / 255);
	fg = ((float)lg / 255);
	fb = ((float)lb / 255);
	aval = ((float)mask / 127.0);

	/*
		if (mask == 127)
		{
			glDisable(GL_BLEND);
			aval = 1.0;
		}
		else
		{
			//Calculate a "supercolor", outside of the normal color range,
			//but which results in the desired color after a blend with
			//the background color.

			fr = fr * 2 - 0.8;
			fg = fg * 2 - 0.8;
			fb = fb * 2 - 0.8;

			aval = (GLfloat)mask / 127.0;	// mask translates to alpha in
			//the OpenGL version.

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	*/

	//glColor4f(fr, fb, fg, aval);

	cairo_set_source_rgba(grCairoContext, fr, fg, fb, aval);

	oldColor = c;
	oldMask = mask;
}


/*---------------------------------------------------------
 * grtcairoSetLineStyle:
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
grtcairoSetLineStyle (style)
int style;			/* New stipple pattern for lines. */
{
	// unimplemented for cairo
	/*
	static int oldStyle = -1;
	GLushort glstyle;

	style &= 0xFF;
	if (style == oldStyle) return;
	oldStyle = style;
	GR_TCairo_FLUSH_BATCH();

	switch (style) {
	case 0xFF:
	case 0x00:
		glDisable(GL_LINE_STIPPLE);
		break;
	default:
		glstyle = style | (style << 8);
		glEnable(GL_LINE_STIPPLE);
		glLineStipple(1, glstyle);
		break;
	}
	*/
	;
}


/*---------------------------------------------------------
 * grtcairoSetSPattern:
 *	tcairoSetSPattern associates a stipple pattern with a given
 *	stipple number.  This is a local routine called from
 *	grStyle.c .
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

cairo_pattern_t **stipplePatterns;

void
grtcairoSetSPattern (sttable, numstipples)
int **sttable;			/* The table of patterns */
int numstipples;			/* Number of stipples */
{
	int i, j, k, n;
	uint8_t *pdata;

	stipplePatterns = (cairo_pattern_t **)mallocMagic(sizeof(cairo_pattern_t *) * numstipples);

	grTCairoStipples = (uint8_t **)mallocMagic(numstipples * sizeof(uint8_t *));
	for (k = 0; k < numstipples; k++)
	{
		pdata = (uint8_t *)mallocMagic(128 * sizeof(uint8_t));
		n = 0;

		/* expand magic's default 8x8 stipple to OpenGL's 32x32 */

		for (i = 0; i < 32; i++) {
			for (j = 0; j < 4; j++) {
				pdata[n++] = (uint8_t)sttable[k][i % 8];
			}
		}

		grTCairoStipples[k] = pdata;
		stipplePatterns[k] = cairo_pattern_create_for_surface(cairo_image_surface_create_for_data(pdata, CAIRO_FORMAT_A1, 32, 32,
		                     cairo_format_stride_for_width(CAIRO_FORMAT_A1, 32)));
	}
}


/*---------------------------------------------------------
 * grtcairoSetStipple:
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
grtcairoSetStipple (stipple)
int stipple;			/* The stipple number to be used. */
{
	static int oldStip = -1;
	if (stipple == oldStip) return;
	oldStip = stipple;
	GR_TCAIRO_FLUSH_BATCH();
	if (stipple == 0 || stipple > grNumStipples) {
		//glDisable(GL_POLYGON_STIPPLE);
		cairo_set_source_rgb(grCairoContext, 0, 0, 0);
	} else {
		if (stipplePatterns[stipple] == (cairo_pattern_t *)NULL) MainExit(1);
		//glEnable(GL_POLYGON_STIPPLE);
		//glPolygonStipple(grTCairoStipples[stipple]);
		cairo_pattern_set_extend(stipplePatterns[stipple], CAIRO_EXTEND_REPEAT);
		cairo_pattern_set_filter(stipplePatterns[stipple], CAIRO_FILTER_NEAREST);
		cairo_set_source(grCairoContext, stipplePatterns[stipple]);
		cairo_mask_surface(grCairoContext, grCairoSurface, 0.0, 0.0);
	}
}


/*------------------------------------------------------------------------
 * GrTCairoInit:
 *	GrTCairoInit initializes the graphics display and clears its screen.
 *	Files must have been previously opened with GrSetDisplay();
 *
 * Results: TRUE if successful.
 *
 * Notes: When 3D rendering is compiled in, we search for a double-buffered
 *	configuration first, because it generates the smoothest graphics,
 *	and fall back on a single-buffered configuration if necessary.
 *	For normal, 2D-only rendering, we look for a single-buffered
 *	configuration first because we don't use the back buffer, so a
 *	double-buffered configuration just wastes space.
 *------------------------------------------------------------------------
 */

bool
GrTCairoInit ()
{
	bool rstatus;
	/*
	#ifdef THREE_D
		static int attributeList[] = { GLX_RGBA, GLX_DOUBLEBUFFER, None };
	#else
		static int attributeList[] = { GLX_RGBA, None, None };
	#endif
	*/

	tcairoCurrent.window = Tk_MainWindow(magicinterp); // XDefaultRootWindow(grXdpy) ??
	if (tcairoCurrent.window == NULL)
	{
		TxError("No Top-Level Tk window available. . . is Tk running?\n");
		return FALSE;
	}

	tcairoCurrent.windowid = Tk_WindowId(tcairoCurrent.window);
	grXdpy = Tk_Display(tcairoCurrent.window);
	tcairoCurrent.depth = Tk_Depth(tcairoCurrent.window);

	grXscrn = DefaultScreen(grXdpy);

	//grVisualInfo = glXChooseVisual(grXdpy, grXscrn, attributeList);
	XVisualInfo grtemplate;
	int gritems;
	grtemplate.screen = grXscrn;
	grtemplate.depth = 0;
	grVisualInfo = XGetVisualInfo(grXdpy, VisualScreenMask, &grtemplate, &gritems);

	if (!grVisualInfo)
	{
		TxError("No suitable visual!\n");
		return FALSE;
		/*
		// Try for a double-buffered configuration
		#ifdef THREE_D
		attributeList[1] = None;
		#else
		attributeList[1] = GLX_DOUBLEBUFFER;
		#endif
		grVisualInfo = glXChooseVisual(grXdpy, grXscrn, attributeList);
		if (!grVisualInfo)
		{
		TxError("No suitable visual!\n");
		return FALSE;
		}
		*/
	}

	grXscrn = grVisualInfo->screen;
	tcairoCurrent.depth = grVisualInfo->depth;

	/* TRUE = Direct rendering, FALSE = Indirect rendering */
	/* (note that direct rendering may not be able to deal with pixmaps) */
	//grXcontext = glXCreateContext(grXdpy, grVisualInfo, NULL, GL_FALSE);

	grCairoSurface = cairo_xlib_surface_create(grXdpy, tcairoCurrent.windowid, grVisualInfo->visual, Tk_Width(tcairoCurrent.window), Tk_Height(tcairoCurrent.window));
	grCairoContext = cairo_create(grCairoSurface);

	/* Basic GL parameters */
	/*
	glLineWidth(1.0);
	glShadeModel (GL_FLAT);
	glPixelStorei(GL_PACK_LSB_FIRST, TRUE);
	*/
	cairo_set_line_width(grCairoContext, 1.0);
	cairo_set_source_rgb(grCairoContext, 0, 0, 0);

	/* OpenGL sets its own names for colormap and dstyle file types */
	grCMapType = "OpenGL";
	grDStyleType = "OpenGL";

	/* Globally-accessed variables */
	grNumBitPlanes = tcairoCurrent.depth;
	grBitPlaneMask = (1 << tcairoCurrent.depth) - 1;

	HashInit(&grTCairoWindowTable, 8, HT_WORDKEYS);

	return grTkLoadFont();
}

/*---------------------------------------------------------
 * GrTCairoClose:
 *
 * Results:	None.
 *
 * Side Effects:
 *---------------------------------------------------------
 */

void
GrTCairoClose ()
{
	if (grXdpy == NULL) return;
	if (grVisualInfo != NULL) XFree(grVisualInfo);

	grTkFreeFonts();
	/* Pop down Tk window but let Tcl/Tk */
	/* do XCloseDisplay()		 */
}


/*---------------------------------------------------------
 * GrTCairoFlush:
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
GrTCairoFlush ()
{
	GR_TCAIRO_FLUSH_BATCH();
	//glFlush();
	//glFinish();
}

/*
 *---------------------------------------------------------
 */

//static GLXPixmap glpmap = None;
Pixmap cairopmap = (Pixmap)NULL;

#define glTransYs(n) (DisplayHeight(grXdpy, grXscrn)-(n))

/*
 *---------------------------------------------------------
 * Set the OpenGL viewport (projection matrix) for a window
 *---------------------------------------------------------
 */

void
tcairoSetProjection(llx, lly, width, height)
int llx, lly, width, height;
{
	if (tcairoCurrent.mw->w_flags & WIND_OFFSCREEN)
	{
		/*
		if (glpmap != None) glXDestroyGLXPixmap(grXdpy, glpmap);
		glpmap = glXCreateGLXPixmap(grXdpy, grVisualInfo,
				(Pixmap)tcairoCurrent.windowid);
		glXMakeCurrent(grXdpy, (GLXDrawable)glpmap, grXcontext);
		*/
		cairopmap = XCreatePixmap(grXdpy, tcairoCurrent.windowid, width, height, tcairoCurrent.depth);
		grCairoSurface = cairo_xlib_surface_create(grXdpy, cairopmap, grVisualInfo->visual, width, height);
	}
	else {
		//glXMakeCurrent(grXdpy, (GLXDrawable)tcairoCurrent.windowid, grXcontext);
		grCairoSurface = cairo_xlib_surface_create(grXdpy, tcairoCurrent.windowid, grVisualInfo->visual, Tk_Width(tcairoCurrent.window), Tk_Height(tcairoCurrent.window));
	}
	grCairoContext = cairo_create(grCairoSurface);

// #ifndef Cairo_SERVER_SIDE_ONLY
// 	 For batch-processing lines and rectangles
// 	glEnableClientState(GL_VERTEX_ARRAY);
// #endif

	/* Because this tends to result in thick lines, it has been moved	*/
	/* the line drawing routine so it can be enabled for individual	*/
	/* lines.								*/
	/* glEnable(GL_LINE_SMOOTH); */

	/* Force draw to front buffer (in case of double-buffered config) */
	//glDrawBuffer(GL_FRONT);

	//glMatrixMode(GL_PROJECTION);
	//glLoadIdentity();
	cairo_identity_matrix(grCairoContext);

	//glViewport((GLsizei)llx, (GLsizei)lly, (GLsizei) width, (GLsizei) height);
	// cairo equivalent??

	/* scale to fit window */
	/*
	#ifdef CAIRO_INVERT_Y
		//glScalef(1.0 / (float)(width >> 1), -1.0 / (float)(height >> 1), 1.0);
		//cairo_scale(grCairoContext, 1.0 / (float)(width >> 1), -1.0 / (float)(height >> 1));
		cairo_scale(grCairoContext, width, -height);
		//cairo_scale(grCairoContext, 1, -1);
	#else
		//glScalef(1.0 / (float)(width >> 1), 1.0 / (float)(height >> 1), 1.0);
		cairo_scale(grCairoContext, width, height);
		//cairo_scale(grCairoContext, 1, 1);
	#endif
	*/
	cairo_scale(grCairoContext, width, -height);

	/* magic origin maps to window center; move to window origin */

	//glTranslated(-(GLsizei)(width >> 1), -(GLsizei)(height >> 1), 0);
	cairo_translate(grCairoContext, -(int)(width >> 1), -(int)(height >> 1));

	/* Remaining transformations are done on the modelview matrix */

	//glMatrixMode(GL_MODELVIEW);
	//glLoadIdentity();
	cairo_identity_matrix(grCairoContext);
}


/*
 * ---------------------------------------------------------------------------
 *
 * TCairoEventProc ---
 *
 *	Tk Event Handler
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Calls functions in response to X11 events.
 *
 * ---------------------------------------------------------------------------
 */

void
TCairoEventProc(clientData, xevent)
ClientData clientData;
XEvent *xevent;
{
	TxInputEvent *event;
	HashEntry	*entry;
	Tk_Window tkwind = (Tk_Window)clientData;
	Window wind;
	MagWindow *mw;
	unsigned char LocRedirect = TxInputRedirect;

	XKeyPressedEvent *KeyPressedEvent = (XKeyPressedEvent *) xevent;
	KeySym keysym;
	int nbytes;

	/* Keys and Buttons:  Determine expansion of macros or redirect
	 * keys to the terminal or console.
	 */

	switch (xevent->type)
	{
	case ButtonPress:
	{
		XButtonEvent *ButtonEvent = (XButtonEvent *) xevent;
		int txbutton;

		switch (ButtonEvent->button) {
		case Button1:
			txbutton = TX_LEFT_BUTTON;
			keysym = XK_Pointer_Button1;
			break;
		case Button2:
			txbutton = TX_MIDDLE_BUTTON;
			keysym = XK_Pointer_Button2;
			break;
		case Button3:
			txbutton = TX_RIGHT_BUTTON;
			keysym = XK_Pointer_Button3;
			break;
		case Button4:
			txbutton = TX_BUTTON_4;
			keysym = XK_Pointer_Button4;
			break;
		case Button5:
			txbutton = TX_BUTTON_5;
			keysym = XK_Pointer_Button5;
			break;
		}
		nbytes = 0;

		entry = HashLookOnly(&grTCairoWindowTable, (char *)tkwind);
		mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;

		if (mw && (mw->w_flags & WIND_SCROLLBARS))
			if (WindButtonInFrame(mw, ButtonEvent->x,
			                      grXtransY(mw, ButtonEvent->y),
			                      txbutton))
				break;

		goto keys_and_buttons;
	}
	break;
	case KeyPress:
	{
		int keywstate, keymod, idx, idxmax;
		char inChar[10];
		Tcl_Channel outChannel = Tcl_GetStdChannel(TCL_STDOUT);

		nbytes = XLookupString(KeyPressedEvent, inChar, sizeof(inChar),
		                       &keysym, NULL);

		if (IsModifierKey(keysym)) break;	/* Don't handle modifiers */

		entry = HashLookOnly(&grTCairoWindowTable, (char *)tkwind);
		mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;

keys_and_buttons:

		keymod = (LockMask | ControlMask | ShiftMask)
		         & KeyPressedEvent->state;
#ifdef __APPLE__
		if (KeyPressedEvent->state & (Mod1Mask | Mod2Mask |
		                              Mod3Mask | Mod4Mask | Mod5Mask))
			keymod |= Mod1Mask;
#else
		keymod |= (Mod1Mask & KeyPressedEvent->state);
#endif

		if (nbytes == 0)	/* No ASCII equivalent */
		{
			keywstate = (keymod << 16) | (keysym & 0xffff);
		}
		else if (!strncmp(XKeysymToString(keysym), "KP_", 3))
		{
			/* keypad key (special case---would like to		*/
			/* differentiate between shift-KP-# and # itself)	*/
			keymod &= ~ShiftMask;
			keywstate = (keymod << 16) | (keysym & 0xffff);
			nbytes = 0;
		}
		else			/* ASCII-valued character */
		{
			if (!(keymod & (LockMask | Mod1Mask))) {
				if (!(keymod & ControlMask))
					keymod &= ~ShiftMask;
				else if (!(keymod & ShiftMask))
					keymod &= ~ControlMask;
			}
		}

		idxmax = (nbytes == 0) ? 1 : nbytes;
		for (idx = 0; idx < idxmax; idx++)
		{
			if (inChar[idx] == 3)	/* Ctrl-C interrupt */
			{
				if (SigInterruptPending)
					MainExit(0);	/* double Ctrl-C */
				else
					sigOnInterrupt(0);	/* Set InterruptPending */
				break;
			}
			else if (nbytes > 0)
			{
				if ((keymod & ControlMask) && (inChar[idx] < 32))
					inChar[idx] += 'A' - 1;

				keywstate = (keymod << 16) | ((int)inChar[idx] & 0xff);
			}

			/* Allow buttons to bypass the console and be	*/
			/* treated as macros.				*/

			if (LocRedirect == TX_INPUT_REDIRECTED)
			{
				switch (keysym)
				{
				case XK_Pointer_Button1:
				case XK_Pointer_Button2:
				case XK_Pointer_Button3:
				case XK_Pointer_Button4:
				case XK_Pointer_Button5:
					LocRedirect = TX_INPUT_NORMAL;;
					break;
				}
			}

			if ((LocRedirect == TX_INPUT_REDIRECTED) && TxTkConsole)
			{
				Tcl_SavedResult state;
				static char outstr[] = "::tkcon::Insert .text \"x\" ";

				switch (keysym)
				{
				case XK_Return:
					TxSetPoint(KeyPressedEvent->x,
					           grXtransY(mw, KeyPressedEvent->y),
					           mw->w_wid);
					TxInputRedirect = TX_INPUT_PROCESSING;
					Tcl_EvalEx(consoleinterp, "::tkcon::Eval .text",
					           19, 0);
					TxInputRedirect = TX_INPUT_NORMAL;
					TxSetPrompt('%');

					Tcl_SaveResult(magicinterp, &state);
					Tcl_EvalEx(magicinterp, "history event 0", 15, 0);
					MacroDefine(mw->w_client, (int)'.',
					            Tcl_GetStringResult(magicinterp), NULL,
					            FALSE);
					Tcl_RestoreResult(magicinterp, &state);
					break;
				case XK_Up:
					Tcl_EvalEx(consoleinterp, "::tkcon::Event -1",
					           17, 0);
					break;
				case XK_Down:
					Tcl_EvalEx(consoleinterp, "::tkcon::Event 1",
					           16, 0);
					break;
				case XK_Left:
					Tcl_EvalEx(consoleinterp, ".text mark set insert "
					           "insert-1c ; .text see insert", 50, 0);
					break;
				case XK_Right:
					Tcl_EvalEx(consoleinterp, ".text mark set insert "
					           "insert+1c ; .text see insert", 50, 0);
					break;
				case XK_BackSpace: case XK_Delete:
					Tcl_EvalEx(consoleinterp, ".text delete insert-1c ;"
					           ".text see insert", 40, 0);
					break;
				case XK_quotedbl: case XK_backslash: case XK_bracketleft:
					outstr[23] = '\\';
					outstr[24] = inChar[idx];
					outstr[25] = '\"';
					Tcl_EvalEx(consoleinterp, outstr, 26, 0);
					outstr[24] = '\"';
					outstr[25] = '\0';
				default:
					outstr[23] = inChar[idx];
					Tcl_EvalEx(consoleinterp, outstr, 25, 0);
					break;
				}
			}
			else if (LocRedirect == TX_INPUT_REDIRECTED) {
				int tl;
				if (TxBuffer == NULL)
				{
					TxBuffer = Tcl_Alloc(2);
					*TxBuffer = '\0';
					tl = 0;
				}
				else
				{
					tl = strlen(TxBuffer);
					TxBuffer = Tcl_Realloc(TxBuffer, tl + 2);
				}
				if (keysym == XK_BackSpace || keysym == XK_Delete)
				{
					if (tl >= 0)
					{
						if (tl > 0)
						{
							*(TxBuffer + tl - 1) = '\0';
							TxPrintf("\b");
						}
						TxPrintf(" \b");
						TxFlushOut();
					}
				}
				else if (keysym == XK_Return)
				{
					*(TxBuffer + tl) = '\n';
					*(TxBuffer + tl + 1) = '\0';
					if (tl != 0) MacroDefine(mw->w_client,
						                         XK_period, TxBuffer, NULL, FALSE);
					TxInputRedirect = TX_INPUT_NORMAL;
					TxSetPoint(KeyPressedEvent->x,
					           grXtransY(mw, KeyPressedEvent->y),
					           mw->w_wid);
					TxPrintf("\n");
					TxFlushOut();
					Tcl_NotifyChannel(Tcl_GetStdChannel(TCL_STDIN),
					                  TCL_READABLE);
				}
				else
				{
					*(TxBuffer + tl) = *(inChar + idx);
					*(TxBuffer + tl + 1) = '\0';
					TxPrintf("%c", *(inChar + idx));
					TxFlushOut();
				}
			}
			else
			{
				bool iMacro;
				char *macroDef;

				macroDef = MacroRetrieve(mw->w_client, keywstate, &iMacro);

				/* Special handling:  An imacro beginning with ':'	*/
				/* sets the prompt to ':' and moves to the next char.	*/

				if (macroDef != NULL && *macroDef == ':' && iMacro)
				{
					if (TxTkConsole)
						TxSetPrompt(':');
					else
					{
						TxPrintf("\b\b: ");
						TxFlushOut();
					}
					memmove(macroDef, macroDef + 1, strlen(macroDef + 1) + 1);
				}

				macroDef = MacroSubstitute(macroDef, "%W", Tk_PathName(tkwind));

				if (macroDef == NULL)
				{
					if (keysym != XK_Return)
					{
						char *vis = MacroName(keywstate);
						TxError("Unknown macro or short command: '%s'\n", vis);

						freeMagic(vis);
					}
					/* Print Carriage Return & Put back Tcl/Tk prompt */
					TxParseString("", NULL, NULL);
				}
				else
				{
					int sl = strlen(macroDef);

					if (iMacro)
					{
						/* Echo macro to interpreter, then redirect keys */

						if (TxTkConsole)
						{
							char *outstring = Tcl_Alloc(sl + 20);
							sprintf(outstring, ".text insert end \"%s\"",
							        macroDef);
							Tcl_EvalEx(consoleinterp, outstring, -1, 0);
							Tcl_Free(outstring);
						}
						else
						{
							TxBuffer = Tcl_Alloc(sl + 1);
							strcpy(TxBuffer, macroDef);
							TxPrintf("%s", macroDef);
							TxFlushOut();
						}
						TxInputRedirect = TX_INPUT_REDIRECTED;
					}
					else
					{
						/* TxParseString is defined by tcltk/tclmagic.c
						 * and calls Tcl_Eval()
						 */

						TxSetPoint(KeyPressedEvent->x,
						           grXtransY(mw, KeyPressedEvent->y),
						           mw->w_wid);
						TxParseString(macroDef, NULL, NULL);
					}
					freeMagic(macroDef);
				}
			}
		}
	}
	break;
	case ConfigureNotify:
	{
		XConfigureEvent *ConfigureEvent = (XConfigureEvent*) xevent;
		Rect	screenRect;
		int width, height;
		bool result, need_resize;

		width = ConfigureEvent->width;
		height = ConfigureEvent->height;

		entry = HashLookOnly(&grTCairoWindowTable, (char *)tkwind);
		mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;

		screenRect.r_xbot = ConfigureEvent->x;
		screenRect.r_xtop = ConfigureEvent->x + width;
		screenRect.r_ytop = glTransYs(ConfigureEvent->y);
		screenRect.r_ybot = glTransYs(ConfigureEvent->y + height);

		need_resize = (screenRect.r_xbot != mw->w_screenArea.r_xbot ||
		               screenRect.r_xtop != mw->w_screenArea.r_xtop ||
		               screenRect.r_ybot != mw->w_screenArea.r_ybot ||
		               screenRect.r_ytop != mw->w_screenArea.r_ytop);

		/* Redraw the window */

		WindReframe(mw, &screenRect, FALSE, FALSE);
		WindRedisplay(mw);
		if (need_resize) (*GrCreateBackingStorePtr)(mw);
	}
	break;
	case VisibilityNotify:
	{
		XVisibilityEvent *VisEvent = (XVisibilityEvent*) xevent;

		entry = HashLookOnly(&grTCairoWindowTable, (char *)tkwind);
		mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;

		switch (VisEvent->state)
		{
		case VisibilityUnobscured:
			mw->w_flags &= ~WIND_OBSCURED;
			if (mw->w_backingStore == (ClientData)NULL)
			{
				(*GrCreateBackingStorePtr)(mw);
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
	}
	break;
	case Expose:
	{
		XExposeEvent *ExposeEvent = (XExposeEvent*) xevent;
		Rect screenRect;

		entry = HashLookOnly(&grTCairoWindowTable, (char *)tkwind);
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
		WindUpdate();
	}
	break;

	case MapNotify:
	case UnmapNotify:
	case DestroyNotify:	/* Do nothing */
		break;

	default:
		TxError("Tk Event: Unknown (%d)\n", xevent->type);
		TxFlush();
		break;
	}
}



/*---------------------------------------------------------
 * cairoSetDisplay:
 *	This routine sets the appropriate parameters so that
 *	Magic will work with the X display.
 *
 *      Under Xlib, all input events (mouse and keyboard) are
 *	sent to one queue which has to be polled to discover
 *	whether there is any input or not.  To fit the Magic
 *	interrupt-driven input model, a helper process is
 *	spawned which reads and blocks on the event queue,
 *	sending SIGIO's to Magic when it detects input.  The
 *	input read in the helper process is then sent to Magic
 *	via a communication pipe.
 *
 * Results:  success / fail
 *
 * Side Effects:	Sets up the pipe.
 *---------------------------------------------------------
 */

bool
cairoSetDisplay (dispType, outFileName, mouseFileName)
char *dispType;
char *outFileName;
char *mouseFileName;
{
	char *planecount;
	char *fullname;
	FILE* f;
	bool execFailed = FALSE;
	int x, y, width, height;

	WindPackageType = WIND_X_WINDOWS;  /* to be changed? */
	TxInputRedirect = TX_INPUT_NORMAL;

	grCursorType = "color";
	WindScrollBarWidth = 14;

	/* Set up the procedure values in the indirection table. */

	GrPixelCorrect = 0;

	GrLockPtr = GrTCairoLock;
	GrUnlockPtr = GrTCairoUnlock;
	GrInitPtr = GrTCairoInit;
	GrClosePtr = GrTCairoClose;
	GrSetCMapPtr = GrTCairoSetCMap;

	GrEnableTabletPtr = GrTCairoEnableTablet;
	GrDisableTabletPtr = GrTCairoDisableTablet;
	GrSetCursorPtr = GrTCairoSetCursor;
	GrTextSizePtr = GrTCairoTextSize;
	GrDrawGlyphPtr = GrTCairoDrawGlyph;
	GrReadPixelPtr = GrTCairoReadPixel;
	GrFlushPtr = GrTCairoFlush;

	GrCreateWindowPtr = GrTCairoCreate;
	GrDeleteWindowPtr = GrTCairoDelete;
	GrConfigureWindowPtr = GrTCairoConfigure;
	GrOverWindowPtr = GrTCairoRaise;
	GrUnderWindowPtr = GrTCairoLower;
	GrUpdateIconPtr = GrTCairoIconUpdate;
	GrEventPendingPtr = GrTCairoEventPending;
	GrWindowIdPtr = GrTCairoWindowId;
	GrWindowNamePtr = GrTkWindowName;		/* from grTkCommon.c */
	GrGetCursorPosPtr = grtcairoGetCursorPos;
	GrGetCursorRootPosPtr = grtcairoGetCursorRootPos;

	/* local indirections */
	grSetSPatternPtr = grtcairoSetSPattern;
	grPutTextPtr = grtcairoPutText;
#ifdef VECTOR_FONTS
	grFontTextPtr = grtcairoFontText;
#endif
	grDefineCursorPtr = grTkDefineCursor;
	grFreeCursorPtr = grTkFreeCursors;
	GrBitBltPtr = GrTCairoBitBlt;
	grDrawGridPtr = grtcairoDrawGrid;
	grDrawLinePtr = grtcairoDrawLine;
	grSetWMandCPtr = grtcairoSetWMandC;
	grFillRectPtr = grtcairoFillRect;
	grSetStipplePtr = grtcairoSetStipple;
	grSetLineStylePtr = grtcairoSetLineStyle;
	grSetCharSizePtr = grtcairoSetCharSize;
	grFillPolygonPtr = grtcairoFillPolygon;
	/*
	#ifdef X11_BACKING_STORE
		GrFreeBackingStorePtr = grtkFreeBackingStore;
		GrCreateBackingStorePtr = grtkCreateBackingStore;
		GrGetBackingStorePtr = grtkGetBackingStore;
		GrPutBackingStorePtr = grtkPutBackingStore;
		GrScrollBackingStorePtr = grtkScrollBackingStore;
	#else
	*/
	GrFreeBackingStorePtr = grtcairoFreeBackingStore;
	GrCreateBackingStorePtr = grtcairoCreateBackingStore;
	GrGetBackingStorePtr = grtcairoGetBackingStore;
	GrPutBackingStorePtr = grtcairoPutBackingStore;
	GrScrollBackingStorePtr = grtcairoScrollBackingStore;
//#endif

	if (execFailed) {
		TxError("Execution failed!\n");
		return FALSE;
	}

	if (!GrTCairoInit()) {
		return FALSE;
	};

	Tk_GetVRootGeometry(Tk_MainWindow(magicinterp), &x, &y, &width, &height);
	GrScreenRect.r_xbot = x;
	GrScreenRect.r_ybot = y;
	GrScreenRect.r_xtop = width + x;
	GrScreenRect.r_ytop = height + y;

	return Tk_MainWindow(magicinterp) ? TRUE : FALSE;
}

extern void MakeWindowCommand();

/*
 * ----------------------------------------------------------------------------
 *
 * GrTCairoCreate --
 *      Create a new window under the X window system.
 *	Bind X window to Magic Window w.
 *
 * Results:
 *	Success/Fail
 *
 * Side Effects:
 *      Window created, window ID send to Xhelper.
 *
 * ----------------------------------------------------------------------------
 */

bool
GrTCairoCreate(w, name)
MagWindow *w;
char *name;
{
	Tk_Window tkwind, tktop;
	Window wind;
	static int WindowNumber = 0;
	HashEntry	*entry;
	char	*windowplace;
	char	windowname[10];
	int		x      = w->w_frameArea.r_xbot;
	int		y      = glTransYs(w->w_frameArea.r_ytop);
	int		width  = w->w_frameArea.r_xtop - w->w_frameArea.r_xbot;
	int		height = w->w_frameArea.r_ytop - w->w_frameArea.r_ybot;
	unsigned long        attribmask = CWBackPixel | CWBorderPixel | CWColormap;
	XSetWindowAttributes grAttributes;

	WindSeparateRedisplay(w);

	sprintf(windowname, ".magic%d", WindowNumber + 1);
	if (windowplace = XGetDefault(grXdpy, "magic", windowname))
	{
		XParseGeometry(windowplace, &x, &y,
		               (unsigned int *)&width, (unsigned int *)&height);
		w->w_frameArea.r_xbot = x;
		w->w_frameArea.r_xtop = x + width;
		w->w_frameArea.r_ytop = glTransYs(y);
		w->w_frameArea.r_ybot = glTransYs(y + height);
		WindReframe(w, &(w->w_frameArea), FALSE, FALSE);
	}

	grAttributes.colormap = XCreateColormap(grXdpy, RootWindow(grXdpy, grXscrn),
	                                        grVisualInfo->visual, AllocNone);
	grAttributes.background_pixel = WhitePixel(grXdpy, grXscrn);
	grAttributes.border_pixel = BlackPixel(grXdpy, grXscrn);

	if (tktop = Tk_MainWindow(magicinterp))
	{
		if (!WindowNumber)
		{
			/* To do: deal with grVisualInfo---destroy and recreate top	*/
			/* frame if necessary					*/

			if (Tk_WindowId(tktop) == 0)
			{
				Tk_SetWindowVisual(tktop, grVisualInfo->visual,
				                   tcairoCurrent.depth, grAttributes.colormap);
			}
			else
			{
				/* The Top-level window has already been mapped.  We can't mess */
				/* with it's visual.  If the title is "wish", we'll assume that */
				/* nobody else is claiming it, and unmap it.		    	*/

				if (!strcmp(Tk_Name(tktop), "wish")) Tk_UnmapWindow(tktop);
			}
		}
	}
	else
		return 0;  /* failure */

	/* Last parameter "" indicates a top-level window in the space of	*/
	/* the parent.							*/

	if (name == NULL)
		tkwind = Tk_CreateWindowFromPath(magicinterp, tktop, windowname, "");
	else
		tkwind = Tk_CreateWindowFromPath(magicinterp, tktop, name, NULL);

	/* TxError("Creating window named \"%s\", tkwind = 0x%x\n",
		windowname, tkwind); TxFlush(); */

	if (tkwind != 0)
	{
		bool result;

		GrTCairoFlush();

		tcairoCurrent.window = tkwind;
		tcairoCurrent.mw = w;

		w->w_grdata = (ClientData) tkwind;

		entry = HashFind(&grTCairoWindowTable, (char *)tkwind);
		HashSetValue(entry, w);

		/* ensure that the visual is what we wanted, if possible to change */

		Tk_SetWindowVisual(tkwind, grVisualInfo->visual, tcairoCurrent.depth,
		                   grAttributes.colormap);

		/* map the window, if necessary */

		Tk_MapWindow(tkwind);

		/* use x, y, width, height to size and position the window */

		Tk_GeometryRequest(tkwind, width, height);
		/* Tk_MoveResizeWindow(tkwind, x, y, width, height); */

		wind = Tk_WindowId(tkwind);
		tcairoCurrent.windowid = wind;
		//glXMakeCurrent(grXdpy, (GLXDrawable)wind, grXcontext);
		grCairoSurface = cairo_xlib_surface_create(grXdpy, tcairoCurrent.windowid, grVisualInfo->visual, Tk_Width(tcairoCurrent.window), Tk_Height(tcairoCurrent.window));
		grCairoContext = cairo_create(grCairoSurface);

		Tk_DefineCursor(tkwind, tcairoCurrent.cursor);
		GrTCairoIconUpdate(w, w->w_caption);

		WindowNumber++;

		/* execute all Tk events up to current */

		while (Tcl_DoOneEvent(TCL_DONT_WAIT) != 0);

		/* set up Tk event handler to start processing */

		Tk_CreateEventHandler(tkwind, ExposureMask | StructureNotifyMask
		                      | ButtonPressMask | KeyPressMask | VisibilityChangeMask,
		                      (Tk_EventProc *)TCairoEventProc, (ClientData) tkwind);

		/* set up commands to be passed expressly to this window */

		MakeWindowCommand((name == NULL) ? windowname : name, w);

		return (WindowNumber == 1) ? grtcairoLoadFont() : 1;
	}
	else
	{
		TxError("Could not open new Tk window\n");
	}

	return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrTCairoDelete --
 *      Destroy a Tk/OpenGL window.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *      Window destroyed.
 *
 * ----------------------------------------------------------------------------
 */

void
GrTCairoDelete(w)
MagWindow *w;
{
	Tk_Window xw;
	HashEntry	*entry;

	xw = (Tk_Window) w->w_grdata;
	entry = HashLookOnly(&grTCairoWindowTable, (char *)xw);
	HashSetValue(entry, NULL);

	Tcl_DeleteCommand(magicinterp, Tk_PathName(xw));
	Tk_DestroyWindow(xw);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrTCairoConfigure --
 *      Resize/ Move an existing X window.
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
GrTCairoConfigure(w)
MagWindow *w;
{
	if (w->w_flags & WIND_OFFSCREEN) return;

	Tk_MoveResizeWindow((Tk_Window)w->w_grdata,
	                    w->w_frameArea.r_xbot, glTransYs(w->w_frameArea.r_ytop),
	                    w->w_frameArea.r_xtop - w->w_frameArea.r_xbot,
	                    w->w_frameArea.r_ytop - w->w_frameArea.r_ybot);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrTCairoRaise --
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
GrTCairoRaise(w)
MagWindow *w;
{
	Tk_Window tkwind;

	if (w->w_flags & WIND_OFFSCREEN) return;

	tkwind = (Tk_Window)w->w_grdata;
	Tk_RestackWindow(tkwind, Above, NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrTCairoLower --
 *      Lower a window below all other Tk windows.
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
GrTCairoLower(w)
MagWindow *w;
{
	Tk_Window tkwind;

	if (w->w_flags & WIND_OFFSCREEN) return;

	tkwind = (Tk_Window)w->w_grdata;
	Tk_RestackWindow(tkwind, Below, NULL);
}


/*
 * ----------------------------------------------------------------------------
 *
 * GrTCairoLock --
 *      Lock a window and set global variables "tcairoCurrent.window"
 *	and "tcairoCurrent.mw" to reference the locked window.
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
GrTCairoLock(w, flag)
MagWindow *w;
bool flag;
{
	Window wind;

	grSimpleLock(w, flag);
	if ( w != GR_LOCK_SCREEN )
	{
		tcairoCurrent.mw = w;

		if (w->w_flags & WIND_OFFSCREEN)
		{
			tcairoCurrent.window = (Tk_Window) NULL;
			tcairoCurrent.windowid = (Pixmap) w->w_grdata;
		}
		else
		{
			tcairoCurrent.window = (Tk_Window) w->w_grdata;
			tcairoCurrent.windowid = Tk_WindowId(tcairoCurrent.window);
		}

		tcairoSetProjection(w->w_allArea.r_xbot, w->w_allArea.r_ybot,
		                    w->w_allArea.r_xtop - w->w_allArea.r_xbot,
		                    w->w_allArea.r_ytop - w->w_allArea.r_ybot);
	}
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrTCairoUnlock --
 *      Unlock a window, flushing stuff out to the display.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *      Window unlocked.
 *	Display update.
 *
 * ----------------------------------------------------------------------------
 */

void
GrTCairoUnlock(w)
MagWindow *w;
{
	/* GR_TCairo_FLUSH_BATCH(); */
	GrTCairoFlush();	/* (?) Adds glFlush and glFinish to the above. */
	grSimpleUnlock(w);
}


/*
 *-------------------------------------------------------------------------
 * GrTCairoEventPending --
 *	check for pending graphics events.
 *      Here we use the X11 check for window events, because Tcl/Tk doesn't
 *      allows peeking into its event queue without executing whatever is
 *      in the queue.
 *
 * Results:
 *	TRUE if an event is waiting in the event queue.
 *
 * Side effects:
 *	None, hopefully (put back the event!)
 *
 *-------------------------------------------------------------------------
 */

bool
GrTCairoEventPending()
{
	Window       wind = tcairoCurrent.windowid;
	XEvent       genEvent;
	bool         retval;

	XSync(grXdpy, FALSE); /* Necessary, or it won't catch mouse/key events */
	retval = XCheckWindowEvent(grXdpy, wind, ExposureMask
	                           | StructureNotifyMask | ButtonPressMask
	                           | KeyPressMask, &genEvent);
	if (retval) XPutBackEvent(grXdpy, &genEvent);
	return retval;
}

/*
 *-------------------------------------------------------------------------
 *
 * GrTCairoIconUpdate -- updates the icon text with the window script
 *
 * Results: none
 *
 * Side Effects: changes the icon text
 *
 *-------------------------------------------------------------------------
 */

void
GrTCairoIconUpdate(w, text)		/* See Blt code */
MagWindow	*w;
char	*text;
{
	Tk_Window	tkwind;
	Window	wind;
	XClassHint	class;
	char	*brack;

	if (w->w_flags & WIND_OFFSCREEN) return;

	tkwind = (Tk_Window)(w->w_grdata);
	if (tkwind == NULL) {
		tkwind = Tk_MainWindow(magicinterp);
		if (tkwind == NULL) return;
	}
	wind = Tk_WindowId(tkwind);
	if (wind == 0) return;

	class.res_name = "magic";
	class.res_class = "magic";

	XSetClassHint( grXdpy, wind, &class);
	if (text)
	{
		if (brack = strchr(text, '['))
		{
			brack--;
			*brack = 0;
			XSetIconName(grXdpy, wind, text);
			XStoreName(grXdpy, wind, text);
			*brack = ' ';
			return;
		}
		if (brack = strrchr(text, ' ')) text = brack + 1;
		XSetIconName(grXdpy, wind, text);
		XStoreName(grXdpy, wind, text);
	}
}

/*
 *-------------------------------------------------------------------------
 * GrTCairoWindowId --
 *	Get magic's ID number from the indicated MagWindow structure
 *
 * Results:
 *	The window ID number.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

int
GrTCairoWindowId(tkname)
char *tkname;
{
	Tk_Window tkwind;
	MagWindow *mw;
	HashEntry *entry;
	int id = 0;

	tkwind = Tk_NameToWindow(magicinterp, tkname, Tk_MainWindow(magicinterp));
	if (tkwind != NULL)
	{
		entry = HashLookOnly(&grTCairoWindowTable, (char *)tkwind);
		mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;
		if (mw) {
			id = mw->w_wid;
		}
	}
	return id;
}
