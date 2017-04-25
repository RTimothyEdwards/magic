/* grTk1.c
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 * This file contains primitive functions to manipulate an X window system
 * Included here are initialization and closing
 * functions, and several utility routines used by the other X
 * modules.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

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
#include "tiles/tile.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "database/database.h"
#include "drc/drc.h"
#include "utils/macros.h"
#include "grTkInt.h"
#include "utils/paths.h"
#include "graphics/grTkCommon.h"

GR_CURRENT grCurrent = {
	(Tk_Font)0, 0, 0, 0, 0, 0,
	(MagWindow *)NULL
};

GR_DISPLAY grDisplay;

GC grGCFill, grGCText, grGCDraw, grGCCopy, grGCGlyph, grGCStipple;
Visual   *grVisual;
int      grClass;
Colormap grXcmap;

extern char *DBWStyleType;
unsigned long grPixels[256];
unsigned long grPlanes[256];
XColor   colors[256*3];	/* Unique colors used by Magic */

Pixmap *grTkStipples;
HashTable	grTkWindowTable;
/* locals */

typedef struct {
    char dashlist[8];
    int  dlen;
} LineStyle;

static LineStyle LineStyleTab[256];

#define grMagicToXs(n) (DisplayHeight(grXdpy,grXscrn)-(n))
#define grXsToMagic(n) (DisplayHeight(grXdpy,grXscrn)-(n))

extern bool GrTkInstalledCMap;

/* machine-dependent constants - see below */
#ifdef __APPLE__
#define X_COLORMAP_BASE		128
#define X_COLORMAP_RESERVED	4
#else
#if defined(CYGWIN)
#define X_COLORMAP_BASE		128
#define X_COLORMAP_RESERVED	0
#else
#define X_COLORMAP_BASE		0
#define X_COLORMAP_RESERVED	2
#endif
#endif

/* This is kind of a long story, and very kludgy, but the following
 * things need to be defined as externals because of the way lint
 * libraries are made by taking this module and changing all procedures
 * names "Xxxx" to "Grxxx".  The change is only done at the declaration
 * of the procedure, so we need these declarations to handle uses
 * of those names, which don't get modified.  Check out the Makefile
 * for details on this.
 */
extern void GrTkClose(), GrTkFlush();
extern void GrTkDelete(),GrTkConfigure(),GrTkRaise(),GrTkLower();
extern void GrTkLock(),GrTkUnlock(),GrTkIconUpdate();
extern bool GrTkInit();
extern bool GrTkEventPending(), GrTkCreate(), grtkGetCursorPos();
extern int  GrTkWindowId();
extern char *GrTkWindowName();


/*---------------------------------------------------------
 * grtkSetWMandC:
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
grtkSetWMandC (mask, c)
    long mask;			/* New value for write mask */
    int c;			/* New value for current color */
{
    static int oldC = -1;
    static int oldM = -1;

    c = grPixels[c];
    if(grDisplay.depth <= 8) {
      mask = grPlanes[mask];
      if (mask == -65) mask = AllPlanes;
    }
    else {
      mask = AllPlanes;
    }
    if (oldC == c && oldM == mask) return;

    GR_TK_FLUSH_BATCH();
    XSetPlaneMask(grXdpy,grGCFill,mask);
    XSetPlaneMask(grXdpy,grGCDraw,mask);
    XSetPlaneMask(grXdpy,grGCText,mask);
    XSetForeground(grXdpy,grGCFill,c);
    XSetForeground(grXdpy,grGCDraw,c);
    XSetForeground(grXdpy,grGCText,c);
    oldC = c;
    oldM = mask;
}


/*---------------------------------------------------------
 * grtkSetLineStyle:
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
grtkSetLineStyle (style)
    int style;			/* New stipple pattern for lines. */
{
    static int oldStyle = -1;
    LineStyle *linestyle;
    int xstyle;

    style &= 0xFF;
    if (style == oldStyle) return;
    oldStyle = style;
    GR_TK_FLUSH_BATCH();

    switch (style) {
    case 0xFF:
    case 0x00:
	xstyle = LineSolid;
	break;
    default:
	xstyle = LineOnOffDash;
	linestyle = &LineStyleTab[style];
	if (linestyle->dlen == 0) {

	    /* translate style to an X11 dashlist */

	    char *e;
	    int cnt,offset,cur,new,curnew,i,match;

	    e = linestyle->dashlist;
	    cnt = 0;
	    offset = 1;
	    cur = 0;
	    for (i = 7; i >= 0; i--) {
		new = (style >> i) & 1;
		curnew = (cur << 1) | new;
		switch (curnew) {
		case 0:
		case 3:
		    cnt++;
		    break;
		case 1:
		    if (cnt > 0) *e++ = cnt; else offset = 0;
		    cnt = 1;
		    break;
		case 2:
		    *e++ = cnt;
		    cnt = 1;
		    break;
		}
		cur = new;
	    }
	    *e++ = cnt;
	    cnt = e - linestyle->dashlist;
	    if (offset) {
		cur = e[0];
		for (i = 0; i < cnt-1; i++) e[i] = e[i+1];
		e[cnt-1] = cur;
	    }
	    match = 1;
	    do {
		if (cnt % 2) break;
		for (i = 0; i < cnt/2; i++) {
		    if (e[i] != e[cnt/2 + i]) match = 0;
		}
		if (match == 0) break;
		cnt = cnt/2;
	    } while (match);
	    linestyle->dlen = cnt;
	}
	XSetDashes(grXdpy, grGCDraw, 0,
		   linestyle->dashlist, linestyle->dlen);
    }
#ifdef	OLD_XFREE
    /* Bypass bug in XFree-2.x server */
    XSetLineAttributes(grXdpy, grGCDraw, 1,
		       xstyle, CapNotLast, JoinMiter);
#else
    XSetLineAttributes(grXdpy, grGCDraw, 0,
		       xstyle, CapNotLast, JoinMiter);
#endif
}


/*---------------------------------------------------------
 * grtkSetSPattern:
 *	xSetSPattern associates a stipple pattern with a given
 *	stipple number.  This is a local routine called from
 *	grStyle.c .
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
grtkSetSPattern (sttable, numstipples)
    int **sttable;			/* The table of patterns */
    int numstipples;			/* Number of stipples */
{
    Tk_Window tkwind;
    Window xwid;
    Pixmap p;
    int i, x, y, pat;

    tkwind = Tk_MainWindow(magicinterp);

    /* This routine may be executed before Tk tells X11 to create the	*/
    /* main window.  So if no X11 window ID is registered, force the	*/
    /* window creation and update.					*/

    if (tkwind == 0 || Tk_WindowId(tkwind) == 0)
	Tk_MakeWindowExist(tkwind);
    xwid = Tk_WindowId(tkwind);

    grTkStipples = (Pixmap *)mallocMagic(numstipples * sizeof(Pixmap));
    for (i = 0; i < numstipples; i++)
    {
	p = Tk_GetPixmap(grXdpy, xwid, 8, 8, 1);
	if (grGCStipple == NULL) {
	    /* grGCStipple = Tk_GetGC(tkwind, 0, 0); */
	    grGCStipple = XCreateGC(grXdpy, p, 0, 0);
	}
	for (y = 0; y < 8; y++) {
	    pat = sttable[i][y];
	    for (x = 0; x < 8; x++) {
		XSetForeground(grXdpy, grGCStipple, pat & 1);
		XDrawPoint(grXdpy, p, grGCStipple, x, y);
		pat >>= 1;
	    }
	}
	grTkStipples[i] = p;
    }
}


/*---------------------------------------------------------
 * grtkSetStipple:
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
grtkSetStipple (stipple)
    int stipple;			/* The stipple number to be used. */
{
    static int oldStip = -1;
    if (stipple == oldStip) return;
    oldStip = stipple;
    GR_TK_FLUSH_BATCH();
    if (stipple == 0 || stipple > grNumStipples) {
	XSetFillStyle(grXdpy, grGCFill, FillSolid);
    } else {
	if (grTkStipples[stipple] == 0) MainExit(1);
	XSetStipple(grXdpy, grGCFill, grTkStipples[stipple]);
	XSetFillStyle(grXdpy, grGCFill, FillStippled);
    }
}


/*---------------------------------------------------------
 * GrTkInit:
 *
 *	GrTkInit initializes the graphics display.  The depth
 *	of the display is queried from the server, and the
 *	"best" visual selected.  The environment variable
 *	"MAGIC_COLOR" can override this choice.  A colormap
 *	is selected based on the visual type, but will be
 *	filled in later.
 *
 * Results: TRUE if successful.
 *---------------------------------------------------------
 */

#define visual_table_len  7

bool
GrTkInit(dispType)
    char *dispType;
{
    int i,j;
    XVisualInfo grvisual_info, *grvisual_get, grtemplate;
    VisualID defpsvid;
    int defpsindex = -1;
    int gritems, gritems_list, grcolorCount;
    bool rstatus;
    Window xwind;

    const char *visual_type[] = {
        "StaticGrey",
        "GreyScale",
        "StaticColor",
        "PseudoColor",
        "TrueColor",
        "DirectColor",
        "UNKNOWN"
    };

    int visual_table[visual_table_len];
    char *log_color, *env_str;
    int color_base, color_reserved;
    int status;

    grCurrent.window = Tk_MainWindow(magicinterp);

    if (grCurrent.window == NULL)
    {
    	 TxError("No Top-Level Tk window available. . . is Tk running?\n");
	 return FALSE;
    }

    grXdpy = Tk_Display(grCurrent.window);
    grDisplay.depth = Tk_Depth(grCurrent.window);
    grCurrent.windowid = Tk_WindowId(grCurrent.window);
    grXscrn = Tk_ScreenNumber(grCurrent.window);

    /* The idea here is to first try allocating the required
     * planes out of the default colormap.  This is the kindest,
     * gentlest thing to do because it doesn't cause all the other
     * windows to go technicolor when the cursor is in a magic window.
     * If this fails, we go ahead and allocate a colormap specifically
     * for magic.  The problem now is using this colormap in such
     * a way that the other windows' colors get mangled the least.
     * Unfortunately, doing this is X-server dependent.  This is where
     * the constants above come in.  X_COLORMAP_BASE indicates
     * which part of the colormap (assuming the number of planes
     * required is less than the number of planes in the display)
     * to fill in the colors magic requires.  X_COLORMAP_RESERVED
     * tells how many high-end colors the server won't let us touch;
     * if we even try to query these colors, we get an X error.
     * If, starting at X_COLORMAP_BASE, the number of colors required
     * would push us into the top X_COLORMAP_RESERVED colors, then
     * we won't be able to set all the colors the user wanted us
     * to set.  The top colors will remain identical to those
     * in the default colormap.
     */
	
    grXcmap = XDefaultColormap(grXdpy,grXscrn);

    /* Discover properties of Server.  */

    grVisual = XDefaultVisual(grXdpy, grXscrn);
    defpsvid = XVisualIDFromVisual(grVisual);
    grtemplate.screen = grXscrn;
    grtemplate.depth = 0; 
    grvisual_get = XGetVisualInfo(grXdpy, VisualScreenMask, &grtemplate, &gritems);
    if (grvisual_get == NULL)
    {
	TxPrintf("Could not obtain Visual Info from Server %s. Will attempt default.\n",
              getenv("DISPLAY"));
	grDisplay.depth = 8;
	grDisplay.colorCount = 1 << grDisplay.depth;
    }
    else
    {
	gritems_list = gritems;
	for (gritems = 0; gritems < gritems_list; gritems++)
	{
	    j = grvisual_get[gritems].class;
	    if ( j < 0 || j > 5)
	    {
		TxPrintf("Unknown visual class index: %d\n", j);
		j = 6;
	    }
	    if ((grvisual_get[gritems].class == 3) &&
			(grvisual_get[gritems].visualid == defpsvid))
		defpsindex = gritems;
         }

	/* Unfortunately, the list returned by Xservers has classes in
	 * random order.  Therefore, a search is needed to find a good
	 * choice. The only currently supported classes are PseudoColor
	 * at depth 8 and TrueColor at depth 15, 16, and 24.  It is likely
	 * that TrueColor depths 8 through 32 will work, but these have
	 * not been tested.  In addition, it has been discovered that some
	 * SUN systems "offer" more than one Pseudocolor at depth 8, but
	 * with differing colormap sizes.  There is nothing about how to
	 * handle this in the X11 documentation, so the search below chooses
	 * the "first" class. The class with 256 colors seems preferable and
	 * works at present. The second Pseudocolor in the list gives a
	 * BatMatch reject from the server, so it is useless. Basing the
	 * selection on 256 colors might be effective, but might conflict in
	 * other cases... As usual X11 is just guesswork.  At present the
	 * preferred order is: PseudoColor at 8, then TrueColor at 24,
	 * then TrueColor at 16, ... Unless this is overridden by the
	 * MAGIC_COLOR environment variable, which can be: bw, 8, 16, 24
	 */

	for (j = 0; j < visual_table_len; j++)
	    visual_table[j] = -1;
	for (j = 0; j < gritems_list; j++)
	{
	    if ((grvisual_get[j].class == 0) && (grvisual_get[j].depth == 8)
			&& (visual_table[1] == -1))
		visual_table[1] = j; /* StaticGrey */
	    if ((grvisual_get[j].class == 1) && (grvisual_get[j].depth == 8)
			&& (visual_table[2] == -1))
		visual_table[2] = j; /* GreyScale */
	    if ((grvisual_get[j].class == 3) && (grvisual_get[j].depth == 8)
			&& (visual_table[3] == -1))
		visual_table[3] = j; /* Pseudocolor */
	    if ((grvisual_get[j].class == 4) && (grvisual_get[j].depth == 15)
			&& (visual_table[4] == -1))
		visual_table[4] = j; /* TrueColor */
	    if ((grvisual_get[j].class == 4) && (grvisual_get[j].depth == 16)
			&& (visual_table[5] == -1))
		visual_table[5] = j; /* TrueColor */
	    if ((grvisual_get[j].class == 4) && (grvisual_get[j].depth == 24)
			&& (visual_table[6] == -1))
		visual_table[6] = j; /* TrueColor */
	}
	if (defpsindex != -1)
	    visual_table[3] = defpsindex;
	log_color = getenv("MAGIC_COLOR");

	if ((log_color == NULL) && (dispType != NULL) && (dispType[0] != 'X'))
	    log_color = dispType;

	 /* Allow environment variables to override the colormap base and */
	 /* number of reserved colors, as these depend on the terminal X  */
	 /* server, NOT on the machine running magic.			  */
	 /* Note:  ought to use strtod() in place of atoi().		  */

	env_str = getenv("X_COLORMAP_BASE");
	if (env_str != NULL)
	    color_base = (int)atoi(env_str);
	else
	    color_base = X_COLORMAP_BASE;
	env_str = getenv("X_COLORMAP_RESERVED");
	if (env_str != NULL)
	    color_reserved = (int)atoi(env_str);
	else
	    color_reserved = X_COLORMAP_RESERVED;
	 
	gritems = -1;
	if (log_color != NULL)
	{
	    if (strncmp(log_color, "8", 1) == 0)  gritems = visual_table[3];
	    if (strncmp(log_color, "15", 2) == 0) gritems = visual_table[4];
	    if (strncmp(log_color, "16", 2) == 0) gritems = visual_table[5];
	    if (strncmp(log_color, "24", 2) == 0) gritems = visual_table[6];
	    if (gritems == -1)
	    {
		printf("The visual mode %s is not available. Sorry.\n", log_color);
		XFree(grvisual_get);
		MainExit(1);
	    }
	}
	else
	{
	    if (visual_table[3] != -1)      gritems = visual_table[3];
	    else if (visual_table[6] != -1) gritems = visual_table[6];
	    else if (visual_table[5] != -1) gritems = visual_table[5];
	    else if (visual_table[4] != -1) gritems = visual_table[4];
	}
	if (gritems == -1) {
	    TxPrintf("None of TrueColor 15, 16 or 24, or PseudoColor 8 found. "
			"Cannot initialize DISPLAY %s\n", getenv("DISPLAY"));
	    XFree(grvisual_get);
	    MainExit(1);
	}
	else
	{
	    TxPrintf("Using %s, VisualID 0x%x depth %d\n",
		visual_type[grvisual_get[gritems].class],
		grvisual_get[gritems].visualid,
		grvisual_get[gritems].depth);
	}
	grClass         = grvisual_get[gritems].class;
	grVisual        = grvisual_get[gritems].visual;
	grcolorCount    = grvisual_get[gritems].colormap_size;
	grDisplay.depth = grvisual_get[gritems].depth;
	grDisplay.red_mask      = grvisual_get[gritems].red_mask;
	grDisplay.green_mask    = grvisual_get[gritems].green_mask;
	grDisplay.blue_mask     = grvisual_get[gritems].blue_mask;
	grDisplay.colorCount    = grcolorCount;
    }
    XFree(grvisual_get);
    grDisplay.planeCount = grDisplay.depth;
    grDisplay.realColors = grDisplay.colorCount;

    if (grDisplay.planeCount == 8)
    {
	grDisplay.depth = 7;
	grDisplay.planeCount = 7;      /* This resets to old 7-plane mode */
	grDisplay.colorCount = 1 << (grDisplay.planeCount);
	grDisplay.realColors = grDisplay.colorCount;
    }
    if (grDisplay.depth)
    {
	status = 0;
	if (grClass != 4)
	    status= XAllocColorCells(grXdpy, grXcmap, TRUE, grDisplay.planes,	
			grDisplay.planeCount, &grDisplay.basepixel, 1); 
	if (status == 0) 
	{
	  /*
	   * Ok, we tried to be nice; now lets whack the default colormap
	   * and put in one of our own.
	   */

	    int actualColors = grcolorCount;
	    int usableColors = actualColors - color_reserved;

	    /* Need the window ID.  If grCurrent.window is not a valid window, */
	    /* Use the default Tk main window.					*/

	    xwind = grCurrent.windowid;
	    if (xwind == 0)
	    {
		xwind = Tk_WindowId(Tk_MainWindow(magicinterp));
		if (xwind == 0)  
		    xwind = DefaultRootWindow(grXdpy);
	    }

	    if (usableColors > 256) usableColors = 256;
	    if (grClass != 4)
		TxPrintf("Unable to allocate %d planes in default colormap; "
			 "making a new one.\n", grDisplay.planeCount);
#ifdef MAGIC_WRAPPER
	    if (grClass == 3) GrTkInstalledCMap = TRUE;
#endif
	    if (grDisplay.planeCount <= 8)
	    {
		grDisplay.basepixel = color_base;
		grXcmap = XCreateColormap(grXdpy, xwind, grVisual, AllocAll);
	    }
	    else {
		grDisplay.basepixel = 0;
		grXcmap = XCreateColormap(grXdpy, xwind, grVisual, AllocNone);
	    }

	    for (j = 0; j < grDisplay.planeCount; j++) grDisplay.planes[j] = 1 << j;
	    status = 1;
	    for (i = 0; i < usableColors; i++) colors[i].pixel = i;
	    XQueryColors(grXdpy, XDefaultColormap(grXdpy, grXscrn),
				colors, usableColors);
	    if (grDisplay.planeCount <= 8)
		XStoreColors(grXdpy, grXcmap, colors, usableColors);
	    grDisplay.realColors = (grDisplay.basepixel +
			grDisplay.colorCount > usableColors) ?
			usableColors - grDisplay.basepixel: grDisplay.colorCount;
	    if ((grDisplay.realColors != grDisplay.colorCount)
			&& (grDisplay.planeCount <= 8))
	    {
		TxPrintf("Only %d contiguous colors were available.\n",
			grDisplay.realColors);
		grDisplay.colorCount = grDisplay.realColors;
	    }
	}
	     			
	if (grXcmap == 0 || status ==0) 
	{
	    TxError( "Tk/X11 setup: Unable to allocate %d planes\n",
		grDisplay.planeCount);
	    MainExit(1);
	}
    }

    /* There is a non-obvious mapping between plane depth and the names */
    /* of the corresponding style and cmap filenames to load for each   */
    /* display type.                                                    */

    switch(grDisplay.depth) {
        case 0: case 1:
            grDStyleType = "bw";
            grCMapType = NULL;
            /* This must be called here;  because in B/W the colormap   */
            /* is useless, it will not be called by the style file      */
            /* load procedure.                                          */
            GrTkSetCMap();
            break;
        case 7: case 8:
            grDStyleType = "7bit";
            grCMapType = "7bit";
            break;
        default:
            grDStyleType = "24bit";
            grCMapType = "24bit";
            break;
    }

    /* Globally-accessed variables */
    grNumBitPlanes = grDisplay.depth;
    grBitPlaneMask = (1 << grDisplay.depth) - 1;

    HashInit(&grTkWindowTable,8,HT_WORDKEYS);
    rstatus = grTkLoadFont();
    return rstatus;
}

/*---------------------------------------------------------
 * GrTkClose:
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Frees fonts from the Tk font cache.
 *---------------------------------------------------------
 */

void
GrTkClose ()
{
    int i;

    if (grXdpy == NULL) return;

    XFreeGC(grXdpy, grGCStipple);
    grGCStipple = NULL;

    /* Free Tk fonts from the font cache */
    grTkFreeFonts();

    /* XCloseDisplay(grXdpy); */  /* Pop down Tk window but let Tcl/Tk	*/
				  /* do XCloseDisplay()			*/
}


/*---------------------------------------------------------
 * GrTkFlush:
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
GrTkFlush ()
{
    GR_TK_FLUSH_BATCH();
}


/*
 * ---------------------------------------------------------------------------
 *
 * MagicEventProc ---
 *
 *	Tk Event Handler
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Calls functions in response to Tk events.
 *
 * ---------------------------------------------------------------------------
 */

void
MagicEventProc(clientData, xevent)
    ClientData clientData;
    XEvent *xevent;
{
    HashEntry	*entry;
    Tk_Window wind = (Tk_Window)clientData;
    MagWindow *mw;
    unsigned char LocRedirect = TxInputRedirect;

    XKeyPressedEvent *KeyPressedEvent = (XKeyPressedEvent *) xevent;
    KeySym keysym;
    int nbytes;

    /* Keys and Buttons:  Determine expansion of macros or redirect
     * keys to the terminal/console.
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

		entry = HashLookOnly(&grTkWindowTable, (char *)wind);
		mw = (entry)?(MagWindow *)HashGetValue(entry):0;

		/* Hack---non-wrapper versions of magic have	*/
		/* fixed handlers for the frame.		*/
		if (mw && (mw->w_flags & WIND_SCROLLBARS))
		    if (WindButtonInFrame(mw, ButtonEvent->x,
				grXToMagic(ButtonEvent->y),
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

		entry = HashLookOnly(&grTkWindowTable, (char *)wind);
		mw = (entry)?(MagWindow *)HashGetValue(entry):0;

keys_and_buttons:
		grCurrent.window = wind;
		grCurrent.windowid = Tk_WindowId(wind);
		grCurrent.mw = mw;

		keymod = (LockMask | ControlMask | ShiftMask)
				& KeyPressedEvent->state;

#ifdef __APPLE__
		if (KeyPressedEvent->state & (Mod1Mask | Mod2Mask |
				Mod3Mask | Mod4Mask | Mod5Mask))
		    keymod |= Mod1Mask;
#else
		keymod |= (Mod1Mask & KeyPressedEvent->state);
#endif

		if (nbytes == 0)
		{
		    /* No ASCII equivalent */
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
		    if (xevent->type == KeyPress && inChar[idx] == 3)
		    {
			/* Ctrl-C interrupt */
			if (SigInterruptPending)
			    MainExit(0);		/* double Ctrl-C */
			else
			    sigOnInterrupt(0);		/* Set InterruptPending */
			TxError("Tk Caught Ctrl-C Interrupt\n");
			TxFlush();
			/* MainExit(0); */
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
			/* Translate Control-H to BackSpace */
			if ((keymod & ControlMask) && (keysym == XK_h))
			    keysym = XK_BackSpace;

			switch (keysym)
			{
			    case XK_Return:
			        TxSetPoint(KeyPressedEvent->x,
					grXToMagic(KeyPressedEvent->y),
					grCurrent.mw->w_wid);
				TxInputRedirect = TX_INPUT_PROCESSING;
				Tcl_EvalEx(consoleinterp, "::tkcon::Eval .text",
						19, 0);
				TxInputRedirect = TX_INPUT_NORMAL;
				TxSetPrompt('%');

				Tcl_SaveResult(magicinterp, &state);
				Tcl_EvalEx(magicinterp, "history event 0", 15,
						0);
				MacroDefine(mw->w_client, (int)'.',
					Tcl_GetStringResult(magicinterp), NULL,
					FALSE);
				Tcl_RestoreResult(magicinterp, &state);
				break;
			    case XK_Up:
				Tcl_EvalEx(consoleinterp, "::tckon::Event -1",
						17, 0);
				break;
			    case XK_Down:
				Tcl_EvalEx(consoleinterp, "::tckon::Event 1",
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
				Tcl_EvalEx(consoleinterp, ".text delete insert-1c ; "
					".text see insert", 41, 0);
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
		    else if (LocRedirect == TX_INPUT_REDIRECTED)
		    {
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
			    if (tl != 0) MacroDefine(mw->w_client, (int)'.',
					TxBuffer, NULL, FALSE);
			    TxInputRedirect = TX_INPUT_NORMAL;
			    TxSetPoint(KeyPressedEvent->x,
					grXToMagic(KeyPressedEvent->y),
					grCurrent.mw->w_wid);
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

			macroDef = MacroSubstitute(macroDef, "%W", Tk_PathName(wind));

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
				 * and calls Tcl_Eval().
				 */

				TxSetPoint(KeyPressedEvent->x,
					grXToMagic(KeyPressedEvent->y),
					grCurrent.mw->w_wid);
				TxParseString(macroDef, NULL, NULL);
			    }
			    freeMagic(macroDef);
			}
		    }
		}
	    } 
	    break;
	case VisibilityNotify:
	    {
		XVisibilityEvent *VisEvent = (XVisibilityEvent*) xevent;

		entry = HashLookOnly(&grTkWindowTable, (char *)wind);
		mw = (entry)?(MagWindow *)HashGetValue(entry):0;

		switch(VisEvent->state)
		{
		    case VisibilityUnobscured:
			mw->w_flags &= ~WIND_OBSCURED;
			if (mw->w_backingStore == (ClientData)NULL)
			{
			    grtkCreateBackingStore(mw);
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

		entry = HashLookOnly(&grTkWindowTable, (char *)wind);
		mw = (entry)?(MagWindow *)HashGetValue(entry):0;
		grCurrent.window = wind;
		grCurrent.windowid = Tk_WindowId(wind);
		grCurrent.mw = mw;

		screenRect.r_xbot = ExposeEvent->x;
            	screenRect.r_xtop = ExposeEvent->x+ExposeEvent->width;
            	screenRect.r_ytop = mw->w_allArea.r_ytop-ExposeEvent->y;
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
	case ConfigureNotify:
	    {
		XConfigureEvent *ConfigureEvent = (XConfigureEvent*) xevent;
		Rect screenRect;
		bool need_resize;
		    
		entry = HashLookOnly(&grTkWindowTable, (char *)wind);
		mw = (entry)?(MagWindow *)HashGetValue(entry):0;
	        grCurrent.window = wind;
		grCurrent.windowid = Tk_WindowId(wind);
		grCurrent.mw = mw;

		screenRect.r_xbot = ConfigureEvent->x;
            	screenRect.r_xtop = ConfigureEvent->x + ConfigureEvent->width;
            	screenRect.r_ytop = grXsToMagic(ConfigureEvent->y);
            	screenRect.r_ybot = grXsToMagic(ConfigureEvent->y+
					    ConfigureEvent->height);

		need_resize = (screenRect.r_xbot != mw->w_screenArea.r_xbot ||
			screenRect.r_xtop != mw->w_screenArea.r_xtop ||
			screenRect.r_ybot != mw->w_screenArea.r_ybot ||
			screenRect.r_ytop != mw->w_screenArea.r_ytop);

		WindReframe(mw, &screenRect, FALSE, FALSE);
		WindRedisplay(mw);
		if (need_resize) grtkCreateBackingStore(mw);
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
 * x11SetDisplay:
 *	This routine sets the appropriate parameters so that
 *	Magic will work with the X display under Tcl/Tk.
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
 * Side Effects:
 *---------------------------------------------------------
 */

bool
x11SetDisplay (dispType, outFileName, mouseFileName)
    char *dispType;
    char *outFileName;
    char *mouseFileName;
{
    char *planecount;
    char *fullname;
    FILE* f;
    bool execFailed = FALSE;
    int x, y, width, height;

    WindPackageType = WIND_X_WINDOWS;
    TxInputRedirect = TX_INPUT_NORMAL;
    grCursorType = "color";
    WindScrollBarWidth = 14;

    /* Set up the procedure values in the indirection table. */

    GrLockPtr = GrTkLock;
    GrUnlockPtr = GrTkUnlock;
    GrInitPtr = GrTkInit;
    GrClosePtr = GrTkClose;
    GrSetCMapPtr = GrTkSetCMap;

    GrEnableTabletPtr = GrTkEnableTablet;
    GrDisableTabletPtr = GrTkDisableTablet;
    GrSetCursorPtr = GrTkSetCursor;
    GrTextSizePtr = GrTkTextSize;
    GrDrawGlyphPtr = GrTkDrawGlyph;
    GrReadPixelPtr = GrTkReadPixel;
    GrFlushPtr = GrTkFlush;

    GrCreateWindowPtr = GrTkCreate;
    GrDeleteWindowPtr = GrTkDelete;
    GrConfigureWindowPtr = GrTkConfigure;
    GrOverWindowPtr = GrTkRaise;
    GrUnderWindowPtr = GrTkLower;
    GrUpdateIconPtr = GrTkIconUpdate; 
    GrEventPendingPtr = GrTkEventPending;
    GrWindowIdPtr = GrTkWindowId;
    GrWindowNamePtr = GrTkWindowName;
    GrGetCursorPosPtr = grtkGetCursorPos;
    GrGetCursorRootPosPtr = grtkGetCursorRootPos;

    /* local indirections */
    grSetSPatternPtr = grtkSetSPattern;
    grPutTextPtr = grtkPutText;
    grFontTextPtr = grtkFontText;
    grDefineCursorPtr = grTkDefineCursor;
    grFreeCursorPtr = grTkFreeCursors;
    GrBitBltPtr = GrTkBitBlt;
    grDrawGridPtr = grtkDrawGrid;
    grDrawLinePtr = grtkDrawLine;
    grSetWMandCPtr = grtkSetWMandC;
    grFillRectPtr = grtkFillRect;
    grSetStipplePtr = grtkSetStipple;
    grSetLineStylePtr = grtkSetLineStyle;
    grSetCharSizePtr = grtkSetCharSize;
    grFillPolygonPtr = grtkFillPolygon;

    GrFreeBackingStorePtr = grtkFreeBackingStore;
    GrCreateBackingStorePtr = grtkCreateBackingStore;
    GrGetBackingStorePtr = grtkGetBackingStore;
    GrPutBackingStorePtr = grtkPutBackingStore;
    GrScrollBackingStorePtr = grtkScrollBackingStore;
    
    if (execFailed) {
	TxError("Execution failed!\n");
	return FALSE;
    }

    if (!GrTkInit(dispType)) {
	return FALSE;
    };

    Tk_GetVRootGeometry(Tk_MainWindow(magicinterp), &x, &y, &width, &height);
    GrScreenRect.r_xbot = x;
    GrScreenRect.r_ybot = y;
    GrScreenRect.r_xtop = x + width;
    GrScreenRect.r_ytop = y + height;

    return Tk_MainWindow(magicinterp) ? TRUE : FALSE;
}

extern void MakeWindowCommand();


/*
 * ----------------------------------------------------------------------------
 *
 * GrTkCreate --
 *      Create a new window under Tk.
 *	Bind Tk window to Magic Window w.
 *
 * Results:
 *	Success/Failure
 *
 * Side Effects:
 *      Window created and mapped.
 *
 * ----------------------------------------------------------------------------
 */

bool
GrTkCreate(w, name)
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
    int		y      = grMagicToXs(w->w_frameArea.r_ytop);
    int		width  = w->w_frameArea.r_xtop - w->w_frameArea.r_xbot;
    int		height = w->w_frameArea.r_ytop - w->w_frameArea.r_ybot;
    unsigned long        attribmask = CWBackPixel | CWBorderPixel;
    XSetWindowAttributes grAttributes;
    int     grDepth;

    GrTkFlush();

    WindSeparateRedisplay(w);

    sprintf(windowname, ".magic%d", WindowNumber + 1);
    if (windowplace = XGetDefault(grXdpy, "magic", windowname))
    {
	XParseGeometry(windowplace,&x,&y,
		(unsigned int *)&width,(unsigned int *)&height);
	w->w_frameArea.r_xbot = x;
	w->w_frameArea.r_xtop = x+width;
	w->w_frameArea.r_ytop = grXsToMagic(y);
	w->w_frameArea.r_ybot = grXsToMagic(y+height);
	WindReframe(w,&(w->w_frameArea),FALSE,FALSE);
    }
    grAttributes.background_pixel = WhitePixel(grXdpy,grXscrn);
    grAttributes.border_pixel = BlackPixel(grXdpy,grXscrn);

    grDepth = grDisplay.depth;
    if(grClass == 3) grDepth = 8;  /* Needed since grDisplay.depth is reset
				     to 7 if Pseudocolor      */

    if (tktop = Tk_MainWindow(magicinterp))
    {
	if (!WindowNumber)
	{
	    /* To do: deal with grVisual---destroy and recreate	*/
	    /* top frame if necessary 				*/

	    if (Tk_WindowId(tktop) == 0)
	    {
	        Tk_SetWindowVisual(tktop, grVisual, grDepth, grXcmap);
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
	return 0;	/* Failure condition */

    /* Last parameter "" indicates a top-level window in the space of	*/
    /* the parent.							*/

    if (name == NULL)
        tkwind = Tk_CreateWindowFromPath(magicinterp, tktop, windowname, "");
    else
        tkwind = Tk_CreateWindowFromPath(magicinterp, tktop, name, NULL);

    /* TxError("Created window named \"%s\", tkwind = 0x%x\n",
		windowname, tkwind); */

    if (tkwind != 0)
    {
	grCurrent.window = tkwind;
	grCurrent.mw = w;
	w->w_grdata = (ClientData) tkwind;

	entry = HashFind(&grTkWindowTable, (char *)tkwind);
	HashSetValue(entry,w);

	/* set the window attributes, since Tk doesn't do this in the	*/
	/* Tk_CreateWindowFromPath() function.				*/
	
	Tk_ChangeWindowAttributes(tkwind, attribmask, &grAttributes);

	/* ensure that the visual is what we wanted, if possible to change */

        Tk_SetWindowVisual(tkwind, grVisual, grDepth, grXcmap);

	/* map the window, if necessary */

	Tk_MapWindow(tkwind);

	/* use x, y, width, height to size and position the window */

	Tk_GeometryRequest(tkwind, width, height);
	/* Tk_MoveResizeWindow(tkwind, x, y, width, height); */

	wind = Tk_WindowId(tkwind);
	grCurrent.windowid = wind;

	if (!WindowNumber)
	{
             grGCFill = XCreateGC(grXdpy, wind, 0, 0);
             grGCDraw = XCreateGC(grXdpy, wind, 0, 0);
             grGCText = XCreateGC(grXdpy, wind, 0, 0);
             grGCCopy = XCreateGC(grXdpy, wind, 0, 0);
             grGCGlyph = XCreateGC(grXdpy, wind, 0, 0);
        }

	XSetPlaneMask(grXdpy, grGCGlyph, AllPlanes);
        Tk_DefineCursor(tkwind, grCurrent.cursor);
	GrTkIconUpdate(w, w->w_caption); 

	/*----------------------------------------------------------------------*/
	/* If we're using TkCon in a PseudoColor visual, we need to set the	*/
	/* console's colormap to match the magic window.  TkCon need know	*/
	/* nothing about this action (i.e., don't call Tk_SetWindowColormap()).	*/
	/* Make a call to a routine to reassign colors to match magic's solid	*/
	/* colors.								*/
	/*----------------------------------------------------------------------*/

	if (TxTkConsole && !WindowNumber)
	{
	    Window parent, rret, *clist;
	    unsigned int iret;

	    if (Tk_Visual(tktop) == Tk_Visual(tkwind))
	    {
	        XQueryTree(grXdpy, Tk_WindowId(tktop), &rret, &parent, &clist, &iret);

	        /* Overlay plane windows won't have the same visual in both the */
	        /* console and the layout windows;  in such case, there should  */
	        /* be no need to set the colormap.				*/

		XSetWindowColormap(grXdpy, parent, grXcmap);

	        if (clist != NULL) XFree(clist);
	    }
	    else
		GrTkInstalledCMap = FALSE;

	    Tcl_EvalEx(consoleinterp, "catch repaintconsole", 20, 0);
	}
	WindowNumber++;

	/* set up Tk event handler to start processing */

	Tk_CreateEventHandler(tkwind, ExposureMask | StructureNotifyMask
		| ButtonPressMask | KeyPressMask | VisibilityChangeMask,
		(Tk_EventProc *)MagicEventProc, (ClientData) tkwind);

	/* set up commands to be passed expressly to this window */

	MakeWindowCommand((name == NULL) ? windowname : name, w);

	/*--------------------------------------------------------------*/
	/* In a PseudoColor visual + GUI wrapper environment, we need	*/
	/* to make sure that the GUI window (layout's parent) has the	*/
	/* same colormap as the layout.  Tk need know nothing about	*/
	/* this sleight-of-hand.					*/
	/*--------------------------------------------------------------*/

	if ((grClass == 3) && !Tk_IsTopLevel(tkwind))
	{
	    Tk_Window tklayout = tkwind;
	    Window parent, rret, *clist;
	    unsigned int iret;

	    while (!Tk_IsTopLevel(tklayout)) tklayout = Tk_Parent(tklayout);
	    XQueryTree(grXdpy, Tk_WindowId(tklayout), &rret, &parent, &clist, &iret);

	    /* Avoid trying to set the colormap of a window with an	*/
	    /* incompatible visual (see above).				*/

	    if (Tk_Visual(tklayout) == Tk_Visual(tkwind))
	    {
		XSetWindowColormap(grXdpy, parent, grXcmap);
		Tk_SetWindowColormap(tklayout, grXcmap);
	    }
	    else
	    {
		GrTkInstalledCMap = FALSE;
		TxError("Warning:  Cannot match colormap of wrapper to layout.\n");
	    }

	    if (clist != NULL) XFree(clist);
	}

	/* Install the colormap in all associated windows, for pseudocolor */
	/* visuals, if required 					   */

	if (grClass == 3) XInstallColormap(grXdpy, grXcmap);

	return 1;
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
 * GrTkDelete --
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
GrTkDelete(w)
    MagWindow *w;
{
    Tk_Window xw;
    HashEntry	*entry;

    xw = (Tk_Window) w->w_grdata;
    entry = HashLookOnly(&grTkWindowTable, (char *)xw);
    HashSetValue(entry,NULL);

    Tcl_DeleteCommand(magicinterp, Tk_PathName(xw));

    Tk_DestroyWindow(xw);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrTkConfigure --
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
GrTkConfigure(w)
    MagWindow *w;
{
    if (w->w_flags & WIND_OFFSCREEN) return;

    Tk_MoveResizeWindow((Tk_Window)w->w_grdata,
	    w->w_frameArea.r_xbot, grMagicToXs(w->w_frameArea.r_ytop),
		w->w_frameArea.r_xtop - w->w_frameArea.r_xbot,
		    w->w_frameArea.r_ytop - w->w_frameArea.r_ybot);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrTkRaise --
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
GrTkRaise(w)
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
 * GrTkLower --
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
GrTkLower(w)
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
 * GrTkLock --
 *      Lock a window and set global variables "grCurrent.window"
 *	and "grCurrent.mw" to reference the locked window.
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
GrTkLock(w, flag)
    MagWindow *w;
    bool flag;
{

    grSimpleLock(w, flag);
    if ( w != GR_LOCK_SCREEN )
    {
	grCurrent.mw = w;
	if (w->w_flags & WIND_OFFSCREEN)
	{
	    grCurrent.window = NULL;
	    grCurrent.windowid = (Pixmap) w->w_grdata;
	}
	else
	{
	    grCurrent.window = (Tk_Window) w->w_grdata;
	    grCurrent.windowid = Tk_WindowId(grCurrent.window);
	}
    }
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrTkUnlock --
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
GrTkUnlock(w)
    MagWindow *w;
{
    GR_TK_FLUSH_BATCH();
    grSimpleUnlock(w);
}


/*
 *-------------------------------------------------------------------------
 * GrTkEventPending -- check for pending graphics events.
 *	Here we use the X11 check for window events, because Tcl/Tk doesn't
 *	allows peeking into its event queue without executing whatever is
 *	in the queue.
 *
 * Results:
 *	TRUE if an event is in the event queue.
 *
 * Side effects:
 *	Hopefully none (put back the event!)
 *
 *-------------------------------------------------------------------------
 */

bool
GrTkEventPending()
{
   Tk_Window	tkwind = grCurrent.window;
   Window	wind = grCurrent.windowid;
   XEvent	genEvent;
   bool		retval;

   if (wind == 0) return FALSE;

   retval = XCheckWindowEvent(grXdpy, wind, ExposureMask
		| StructureNotifyMask | ButtonPressMask
		| KeyPressMask, &genEvent); 
   if (retval) XPutBackEvent(grXdpy, &genEvent);
   return retval;
}

/*
 *-------------------------------------------------------------------------
 *
 * GrTkIconUpdate -- updates the icon text with the window script
 *
 * Results: none
 *
 * Side Effects: changes the icon text
 *
 *-------------------------------------------------------------------------
 */

void
GrTkIconUpdate(w, text)		/* See Blt code */
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

/*
 *-------------------------------------------------------------------------
 * GrTkWindowId --
 *	Get magic's ID number from the indicated MagWindow structure
 *
 * Results:
 *	The window ID number
 *
 * Side effects:
 *	None.
 *-------------------------------------------------------------------------
 */

int
GrTkWindowId(tkname)
    char *tkname;
{
    Tk_Window tkwind;
    MagWindow *mw;
    HashEntry *entry;
    int id = 0;

    tkwind = Tk_NameToWindow(magicinterp, tkname, Tk_MainWindow(magicinterp));
    if (tkwind != NULL)
    {
	entry = HashLookOnly(&grTkWindowTable, (char *)tkwind);
	mw = (entry) ? (MagWindow *)HashGetValue(entry) : 0;
	if (mw) id = mw->w_wid;
    }
    return id;
}

