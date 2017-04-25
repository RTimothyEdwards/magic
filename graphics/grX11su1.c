/* grX11su1.c -
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
 * This file contains primitive functions to manipulate an X window system
 * Included here are initialization and closing
 * functions, and several utility routines used by the other X
 * modules.
 */

/* #define HIRESDB */		/* debugging only */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "utils/magic.h"
#include "utils/magsgtty.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "textio/txcommands.h"
#include "utils/signals.h"
#include "graphics/glyphs.h"
#include "graphics/graphics.h"
#include "windows/windows.h"
#include "graphics/graphicsInt.h"
#include "utils/utils.h"
#include "utils/hash.h"
#include "grX11Int.h"
#include "utils/paths.h"

extern char  *DBWStyleType;

Display      *grXdpy;
int	      grXscrn;
Colormap      grXcmap;
Visual       *grVisual;
unsigned int  grClass;
GR_CURRENT    grCurrent= {0,0,0,0,0,0};
GR_DISPLAY    grDisplay;
GC 	      grGCFill, grGCText,grGCDraw;
GC	      grGCCopy, grGCGlyph, grGCStipple;

unsigned long grPixels[256];
unsigned long grPlanes[256];
XColor        colors[256*3];	/* Unique colors used by Magic */
Pixmap 	      *grX11Stipples;
HashTable     grX11WindowTable;

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
#endif  /* CYGWIN */
#endif

/* locals */

int pipeRead, pipeWrite;	/* As seen from parent */
#ifdef HAVE_PTHREADS
extern int writePipe;
extern int readPipe;	/* As seen from child */
#endif

typedef struct {
    char dashlist[8];
    int  dlen;
} LineStyle;

static LineStyle LineStyleTab[256];

int Xhelper;

#define visual_table_len  7

#define grMagicToXs(n) (DisplayHeight(grXdpy,grXscrn)-(n))
#define grXsToMagic(n) (DisplayHeight(grXdpy,grXscrn)-(n))

/* This is kind of a long story, and very kludgy, but the following
 * things need to be defined as externals because of the way lint
 * libraries are made by taking this module and changing all procedures
 * names "Xxxx" to "Grxxx".  The change is only done at the declaration
 * of the procedure, so we need these declarations to handle uses
 * of those names, which don't get modified.  Check out the Makefile
 * for details on this.
 */

extern void GrX11Close(), GrX11Flush();
extern bool GrX11Init(), GrX11Create();
extern void GrX11Delete(), GrX11Configure(), GrX11Raise(), GrX11Lower();
extern void GrX11Lock(), GrX11Unlock(), GrX11IconUpdate();
extern void grXWStdin();
extern bool grx11GetCursorPos();



/*---------------------------------------------------------
 * grxSetWMandC:
 *	This is a local routine that resets the value of the current
 *	write mask and color, if necessary.
 *
 * Results:	None.
 *
 * Side Effects:    None.
 *---------------------------------------------------------
 */

void
grx11SetWMandC (mask, c)
    int mask;			/* New value for write mask */
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

    GR_X_FLUSH_BATCH();
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
 * grxSetLineStyle:
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
grx11SetLineStyle (style)
    int style;			/* New stipple pattern for lines. */
{
    static int oldStyle = -1;
    LineStyle *linestyle;
    int xstyle;

    style &= 0xFF;
    if (style == oldStyle) return;
    oldStyle = style;
    GR_X_FLUSH_BATCH();

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
 * grxSetSPattern:
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
grx11SetSPattern (sttable, numstipples)
    int **sttable;		/* Table of patterns */
    int numstipples;		/* Number of stipples */
{
    Pixmap p;
    int i, x, y, pat;

    grX11Stipples = (Pixmap *)mallocMagic(numstipples * sizeof(Pixmap));
    for (i = 0; i < numstipples; i++)
    {
	p = XCreatePixmap(grXdpy, XDefaultRootWindow(grXdpy), 8, 8, 1);
	if (grGCStipple == NULL) {
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
	grX11Stipples[i] = p;
    }
}


/*---------------------------------------------------------
 * grxSetStipple:
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
grx11SetStipple (stipple)
    int stipple;			/* The stipple number to be used. */
{
    static int oldStip = -1;
    if (stipple == oldStip) return;
    oldStip = stipple;
    GR_X_FLUSH_BATCH();
    if (stipple == 0 || stipple > grNumStipples) {
	XSetFillStyle(grXdpy, grGCFill, FillSolid);
    } else {
	if (grX11Stipples[stipple] == 0) MainExit(1);
	XSetStipple(grXdpy, grGCFill, grX11Stipples[stipple]);
	XSetFillStyle(grXdpy, grGCFill, FillStippled);
    }
}


/*---------------------------------------------------------
 * GrX11Init:
 *
 *	GrXInit initializes the graphics display.  The depth
 *	of the display is queried from the server, and the
 *	"best" visual selected.  The environment variable
 *	"MAGIC_COLOR" can override this choice.  A colormap
 *	is selected based on the visual type, but will be
 *	filled in later.
 *
 * Results: TRUE if successful.
 *---------------------------------------------------------
 */

bool
GrX11Init(dispType)
    char *dispType;
{
    int i,j;
    XVisualInfo grvisual_info, *grvisual_get, grtemplate;
    VisualID defpsvid;
    int defpsindex = -1;
    int gritems, gritems_list, grcolorCount;

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

#ifdef HAVE_PTHREADS
    XInitThreads();
#endif

    grXdpy = XOpenDisplay(NULL); 
    if (grXdpy == NULL)
    {
    	 TxError("Couldn't open display; check DISPLAY variable\n");
	 return FALSE;
    }
    grXscrn = XDefaultScreen(grXdpy);

    grCurrent.window = XDefaultRootWindow(grXdpy);

   /*
    * The idea here is to first try allocating the required
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

    grXcmap = XDefaultColormap(grXdpy, grXscrn);

    /* Discover properties of the X11 Server.  */

    grVisual = XDefaultVisual(grXdpy, grXscrn);
    defpsvid = XVisualIDFromVisual(grVisual);
    grtemplate.screen = grXscrn;
    grtemplate.depth = 0; 
    grvisual_get = XGetVisualInfo(grXdpy, VisualScreenMask, &grtemplate, &gritems);
    if(grvisual_get == NULL)
    {
	TxPrintf("Could not obtain Visual Info from Server %s. "
		"Will attempt default.\n", getenv("DISPLAY"));

	/* Try to default to 8-bit pseudocolor.  May not be a great idea. */
	/* grDisplay.red/green/blue_mask not used.			  */

	grDisplay.depth = 8;
	grDisplay.colorCount = 1 << grDisplay.depth;
    }
    else
    {
#ifdef HIRESDB
	TxPrintf("Server Vendor: %s\n", ServerVendor(grXdpy));
	TxPrintf("Vendor Release: %d\n", VendorRelease(grXdpy));
	TxPrintf("Protocol Version: %d\n", ProtocolVersion(grXdpy));
	TxPrintf("Protocol Revision: %d\n", ProtocolRevision(grXdpy));
	TxPrintf("HOSTTYPE: %s\n", getenv("HOSTTYPE"));
	TxPrintf("XGetVisualInfo returned visuals list of length %d:\n", gritems);
	TxPrintf("Default VisualID 0x%x\n", defpsvid);
#endif  /* HIRESDB */

	gritems_list = gritems;
	for (gritems = 0; gritems < gritems_list; gritems++)
	{
	    j = grvisual_get[gritems].class;
	    if (j < 0 || j > 5) {
		TxPrintf("Unknown visual class index: %d\n", j);
		j = 6;
	    }
#ifdef HIRESDB
            TxPrintf("Found Visual Class %s, ID 0x%x with:\n     "
			"depth %d, colormap_size %d, bits_per_rgb %d.\n",
			visual_type[j], grvisual_get[gritems].visualid,
			grvisual_get[gritems].depth,
			grvisual_get[gritems].colormap_size,
			grvisual_get[gritems].bits_per_rgb);

            if (grvisual_get[gritems].class == 4)
		TxPrintf("     TrueColor masks: red %06x, green %06x, blue %06x\n",
			grvisual_get[gritems].red_mask,
			grvisual_get[gritems].green_mask,
			grvisual_get[gritems].blue_mask);

#endif  /* HIRESDB */

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
	/* number of reserved colors, as these depend on the terminal X	 */
	/* server, NOT on the machine running magic.			 */
	/* Note:  ought to use strtod() in place of atoi()...		 */

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
	    if (gritems == -1) {
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
	if (gritems == -1)
	{
	    TxPrintf("None of TrueColor 15, 16, or 24, or PseudoColor 8 found. "
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
	grDisplay.depth	= grvisual_get[gritems].depth;
	grDisplay.colorCount = grcolorCount;
	grDisplay.red_mask   = grvisual_get[gritems].red_mask;
	grDisplay.green_mask = grvisual_get[gritems].green_mask;
	grDisplay.blue_mask  = grvisual_get[gritems].blue_mask;
    }
    XFree(grvisual_get);
    grDisplay.planeCount = grDisplay.depth;
    grDisplay.realColors = grDisplay.colorCount;

    /* "planeCount" is the number of display planes.  "depth" is the	*/
    /* number of planes magic uses, which may be different.  In		*/
    /* particular, magic uses 7 planes in an 8-plane visual.		*/

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
	  /*
	   * Ok, we tried to be nice; now lets whack the default colormap
	   * and put in one of our own.
	   */
	{
	    int actualColors = grcolorCount;
	    int usableColors = actualColors - color_reserved;

	    if (usableColors > 256) usableColors = 256;
	    if (grClass != 4)
		TxPrintf("Unable to allocate %d planes in default colormap; "
				"making a new one.\n", grDisplay.planeCount);
	    if (grDisplay.planeCount <= 8)
	    {
		grDisplay.basepixel = color_base;
		grXcmap = XCreateColormap(grXdpy,grCurrent.window,
				grVisual, AllocAll);

	    }
	    else
	    {
		grDisplay.basepixel = 0;
		grXcmap = XCreateColormap(grXdpy,grCurrent.window,
				grVisual, AllocNone);
	    }

	    for (j = 0; j < grDisplay.planeCount; j++)
		grDisplay.planes[j] = 1 << j;
	    status = 1;
	    for (i = 0; i < usableColors; i++) colors[i].pixel = i;
	    XQueryColors(grXdpy, XDefaultColormap(grXdpy, 
			grXscrn), colors, usableColors);
	    if (grDisplay.planeCount <= 8)
		XStoreColors(grXdpy, grXcmap, colors, usableColors);
	    grDisplay.realColors = (grDisplay.basepixel
			+ grDisplay.colorCount > usableColors)?  usableColors
			- grDisplay.basepixel: grDisplay.colorCount;
	    if ((grDisplay.realColors != grDisplay.colorCount)
			&& (grDisplay.planeCount <= 8))
	    {
		TxPrintf("Only %d contiguous colors were available.\n",
				grDisplay.realColors);
		grDisplay.colorCount = grDisplay.realColors;
	    }
	}
	     			
	if (grXcmap == 0 || status == 0) 
	{
	    TxError( "X11 setup: Unable to allocate %d planes\n",
			grDisplay.planeCount);
	    MainExit(1);
	}
    }

    /* There is a non-obvious mapping between plane depth and the names */
    /* of the corresponding style and cmap filenames to load for each	*/
    /* display type.							*/

    switch(grDisplay.depth) {
	case 0: case 1:
	    grDStyleType = "bw";
	    grCMapType = NULL;
	    /* This must be called here;  because in B/W the colormap	*/
	    /* is useless, it will not be called by the style file	*/
	    /* load procedure.						*/
	    GrX11SetCMap();
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

    HashInit(&grX11WindowTable,8,HT_WORDKEYS);
    return grx11LoadFont();
}


/*---------------------------------------------------------
 * GrXClose --
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	In the non-pthreads version, kills the helper
 *	process.  In the pthreads version, detaches the
 *	X11 helper thread.
 *---------------------------------------------------------
 */

void
GrX11Close ()
{
    if (grXdpy == NULL) return;
    TxDelete1InputDevice(pipeRead);
    close(pipeRead);
#ifndef HAVE_PTHREADS
    kill(Xhelper, SIGKILL);
    WaitPid (Xhelper, 0);
#endif
    if (grGCStipple != NULL) {
	XFreeGC(grXdpy, grGCStipple);
	grGCStipple = NULL;
    }
    XCloseDisplay(grXdpy);
#ifdef HAVE_PTHREADS
    xloop_end();
#endif
}


/*---------------------------------------------------------
 * GrXFlush:
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
GrX11Flush ()
{
   XFlush(grXdpy);
}


/*
 * ---------------------------------------------------------------------------
 *
 * grXStdin --
 *
 *      Handle the stdin device for the X driver.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Adds events to the data queue.
 *
 * ---------------------------------------------------------------------------
 */

void
grX11Stdin()
{
    TxInputEvent *event;
    XEvent	xevent;
    HashEntry	*entry;
    read(pipeRead, &xevent, sizeof(XEvent));
    switch (xevent.type) 
    {
	case ButtonPress:
	case ButtonRelease:
	    {
		XButtonEvent *ButtonEvent = (XButtonEvent *) &xevent;

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
		switch(xevent.type) {
		case ButtonRelease:
		    event->txe_buttonAction = TX_BUTTON_UP;
		    break;
		case ButtonPress:
		    event->txe_buttonAction = TX_BUTTON_DOWN;
		    break;
		}

	        grCurrent.window = ButtonEvent->window;
		entry = HashLookOnly(&grX11WindowTable,grCurrent.window);
	        grCurrent.mw= (entry)?(MagWindow *)HashGetValue(entry):0;

		event->txe_p.p_x = ButtonEvent->x;
		event->txe_p.p_y = grXToMagic(ButtonEvent->y);
		event->txe_wid = grCurrent.mw->w_wid;
		TxAddEvent(event);
	    }
	    break;
	case KeyPress:
	    {
		XKeyPressedEvent *KeyPressedEvent = (XKeyPressedEvent *) &xevent;
		int c;

	        event = TxNewEvent();

	        grCurrent.window = KeyPressedEvent->window;
		entry = HashLookOnly(&grX11WindowTable,grCurrent.window);
	        grCurrent.mw= (entry)?(MagWindow *)HashGetValue(entry):0;

    		read(pipeRead, &c, sizeof(int));
		if (c == (int)'\r') c = (int)'\n';
		event->txe_button = TX_CHARACTER;
		event->txe_ch = c;
		event->txe_buttonAction = TX_KEY_DOWN;
		event->txe_p.p_x = KeyPressedEvent->x;
		event->txe_p.p_y = grXToMagic(KeyPressedEvent->y);
		event->txe_wid = grCurrent.mw->w_wid;
		TxAddEvent(event);
	    } 
	    break;
	case Expose:
	    {
		    XExposeEvent *ExposeEvent = (XExposeEvent*) &xevent;
		    Rect screenRect;
		    MagWindow	*w;
		    
	            grCurrent.window = ExposeEvent->window;
		    entry = HashLookOnly(&grX11WindowTable,grCurrent.window);
	            w = (entry)?(MagWindow *)HashGetValue(entry):0;
	            grCurrent.mw=w;

		    screenRect.r_xbot = ExposeEvent->x;
            	    screenRect.r_xtop = ExposeEvent->x+ExposeEvent->width;
            	    screenRect.r_ytop = 
			 	w->w_allArea.r_ytop-ExposeEvent->y;
            	    screenRect.r_ybot = w->w_allArea.r_ytop - 
		    		(ExposeEvent->y + ExposeEvent->height);

		    if (w->w_backingStore != (ClientData)NULL)
		    {
			Rect surface;
			(*GrGetBackingStorePtr)(w, &screenRect);
			WindScreenToSurface(w, &screenRect, &surface);
			DBWHLRedrawPrepWindow(w, &surface);
			WindDrawBorder(w, &screenRect);
		    }
		    else
			WindAreaChanged(w, &screenRect);
                    WindUpdate();
            }
	    break;
	case ConfigureNotify:
	    {
		    XConfigureEvent *ConfigureEvent = (XConfigureEvent*) &xevent;
		    Rect screenRect;
		    MagWindow	*w;
		    
	            grCurrent.window = ConfigureEvent->window;
		    entry = HashLookOnly(&grX11WindowTable,grCurrent.window);
	            w = (entry)?(MagWindow *)HashGetValue(entry):0;
	            grCurrent.mw=w;

		    screenRect.r_xbot = ConfigureEvent->x;
            	    screenRect.r_xtop = ConfigureEvent->x+
			 		ConfigureEvent->width;
            	    screenRect.r_ytop = grXsToMagic(ConfigureEvent->y);
            	    screenRect.r_ybot = 
			 	grXsToMagic(ConfigureEvent->y+
					    ConfigureEvent->height);
		 
		    WindReframe(w,&screenRect,FALSE,FALSE);
		    WindRedisplay(w);
            }
            break;
	case VisibilityNotify:
	    {
		XVisibilityEvent *VisEvent = (XVisibilityEvent*) &xevent;
		MagWindow	*w;

		entry = HashLookOnly(&grX11WindowTable, VisEvent->window);
		w = (entry)?(MagWindow *)HashGetValue(entry):0;

		switch(VisEvent->state)
		{
		    case VisibilityUnobscured:
			w->w_flags &= ~WIND_OBSCURED;
			if (w->w_backingStore == (ClientData)NULL)
			{
			    grx11CreateBackingStore(w);
			    if (w->w_backingStore != (ClientData)NULL)
			    {
				WindAreaChanged(w, &w->w_allArea);
				WindUpdate();
			    }
			}
			break;
		    case VisibilityPartiallyObscured:
		    case VisibilityFullyObscured:
			w->w_flags |= WIND_OBSCURED;
			break;
		}
	    }
	    break;
	case CreateNotify:
		{
	    	    XAnyEvent *anyEvent = (XAnyEvent*) &xevent;
		    MagWindow	*w;
		    
	            grCurrent.window = anyEvent->window;
		    entry = HashLookOnly(&grX11WindowTable, grCurrent.window);
	            w = (entry)?(MagWindow *)HashGetValue(entry):0;

/* The line above is defintely NOT a good idea. w == 0 causes address
   exception. Why X11 is generating an event for a non-existent
   window is another question... ***mdg***                             */

                    if(w == 0) {printf("CreateNotify: w = %d.\n", w); break;}
		    SigDisableInterrupts();
	    	    WindView(w);
		    SigEnableInterrupts();
		}
            break;

	default:
	    break;

     }
}


/*---------------------------------------------------------
 * x11SetDisplay:
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
x11SetDisplay (dispType, outFileName, mouseFileName)
    char *dispType;		/* arguments not used by X */
    char *outFileName;
    char *mouseFileName;
{
    int fildes[2],fildes2[2];
    char *fullname;
    FILE* f;
    bool execFailed = FALSE;

    WindPackageType = WIND_X_WINDOWS;
    WindScrollBarWidth = 14;
    grCursorType = "color";

    /* Set up helper process */
    pipe(fildes);
    pipe(fildes2);
    pipeRead = fildes[0];
    pipeWrite = fildes2[1];

    TxAdd1InputDevice(pipeRead, grX11Stdin, (ClientData) NULL);

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
    } else {
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

    GrLockPtr = GrX11Lock;
    GrUnlockPtr = GrX11Unlock;
    GrInitPtr = GrX11Init;
    GrClosePtr = GrX11Close;
    GrSetCMapPtr = GrX11SetCMap;

    GrEnableTabletPtr = GrX11EnableTablet;
    GrDisableTabletPtr = GrX11DisableTablet;
    GrSetCursorPtr = GrX11SetCursor;
    GrTextSizePtr = GrX11TextSize;
    GrDrawGlyphPtr = GrX11DrawGlyph;
    GrReadPixelPtr = GrX11ReadPixel;
    GrFlushPtr = GrX11Flush;

    GrCreateWindowPtr = GrX11Create;
    GrDeleteWindowPtr = GrX11Delete;
    GrConfigureWindowPtr = GrX11Configure;
    GrOverWindowPtr = GrX11Raise;
    GrUnderWindowPtr = GrX11Lower;
    GrUpdateIconPtr = GrX11IconUpdate; 
    GrGetCursorPosPtr = grx11GetCursorPos;
    GrGetCursorRootPosPtr = grx11GetCursorRootPos;

    /* local indirections */
    grSetSPatternPtr = grx11SetSPattern;
    grPutTextPtr = grx11PutText;
    grFontTextPtr = grx11FontText;
    grDefineCursorPtr = grx11DefineCursor;
    GrBitBltPtr = GrX11BitBlt;

    GrFreeBackingStorePtr = grx11FreeBackingStore;
    GrCreateBackingStorePtr = grx11CreateBackingStore;
    GrGetBackingStorePtr = grx11GetBackingStore;
    GrPutBackingStorePtr = grx11PutBackingStore;
    GrScrollBackingStorePtr = grx11ScrollBackingStore;

    grDrawGridPtr = grx11DrawGrid;
    grDrawLinePtr = grx11DrawLine;
    grSetWMandCPtr = grx11SetWMandC;
    grFillRectPtr = grx11FillRect;
    grSetStipplePtr = grx11SetStipple;
    grSetLineStylePtr = grx11SetLineStyle;
    grSetCharSizePtr = grx11SetCharSize;
    grFillPolygonPtr = grx11FillPolygon;
    
    if (execFailed)
    {
	TxError("Execution failed!\n");
	return FALSE;
    }

    TxAdd1InputDevice(fileno(stdin), grXWStdin, (ClientData) NULL);

    if (!GrX11Init(dispType))
    {
	return FALSE;
    }
    GrScreenRect.r_xtop = DisplayWidth(grXdpy,grXscrn);
    GrScreenRect.r_ytop = DisplayHeight(grXdpy,grXscrn);

    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * grXWStdin --
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

void
grXWStdin(fd, cdata)
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
 * GrX11Create --
 *      Create a new window under the X window system.
 *	Bind X window to Magic Window w.
 *
 * Results:
 *	Success/Failure
 *
 * Side Effects:
 *      Window created, window ID send to Xhelper.
 *
 * ----------------------------------------------------------------------------
 */

bool
GrX11Create(w, name)
    MagWindow *w;
    char *name;
{
    Window wind;
    static int firstWindow = 1;
    XSizeHints	*xsh;
    HashEntry	*entry;
    char	*windowplace;
    char	*option = (firstWindow)?"window":"newwindow";
    int		x      = w->w_frameArea.r_xbot;
    int		y      = grMagicToXs(w->w_frameArea.r_ytop);
    int		width  = w->w_frameArea.r_xtop - w->w_frameArea.r_xbot;
    int		height = w->w_frameArea.r_ytop - w->w_frameArea.r_ybot;
    unsigned long        attribmask = CWBackPixel | CWBorderPixel | CWColormap;
    XSetWindowAttributes grAttributes;
    int     grDepth;

    WindSeparateRedisplay(w);
    xsh = XAllocSizeHints();
    /* ASSERT(xsh!=0, "failed XAllocSizeHints"); */
    if (windowplace=XGetDefault(grXdpy,"magic",option))
    {
	 XParseGeometry(windowplace,&x,&y,
       (unsigned int *)&width,(unsigned int *)&height);
	 w->w_frameArea.r_xbot = x;
	 w->w_frameArea.r_xtop = x+width;
	 w->w_frameArea.r_ytop = grXsToMagic(y);
	 w->w_frameArea.r_ybot = grXsToMagic(y+height);
	 WindReframe(w,&(w->w_frameArea),FALSE,FALSE);
	 xsh->flags = USPosition | USSize;
    }
    else
    {
    	 xsh->flags = PPosition|PSize;
    }
    grAttributes.background_pixel = WhitePixel(grXdpy,grXscrn);
    grAttributes.border_pixel = BlackPixel(grXdpy,grXscrn);
    grAttributes.colormap = grXcmap;
    grDepth = grDisplay.depth;
    if(grClass == 3) grDepth = 8;  /* Needed since grDisplay.depth is reset
				     to 7 if Pseudocolor      */
#ifdef HIRESDB
    TxPrintf("x %d y %d width %d height %d depth %d class %d mask %d\n",
      x,y,width,height, grDepth, grClass, attribmask);
#endif  /* HIRESDB */
    if ( wind = XCreateWindow(grXdpy,  XDefaultRootWindow(grXdpy),
    		x, y, width, height, 0, grDepth, InputOutput, grVisual,
                attribmask, &grAttributes))
    {
#ifdef	sun
	/* Hint's for Sun's implementation of X11 (News/X11) */
        {
	    XWMHints wmhints;
	    wmhints.flags = InputHint;
	    wmhints.input = TRUE;
	    XSetWMHints(grXdpy, wind, &wmhints);
        }
#endif	/* sun */

	/*
	 * Signal xhelper to poll window.
	 */
	grCurrent.window = wind; 
	/*
	 * Define window cursor and complete initialization.
	 */
	xsh->x = w->w_frameArea.r_xbot;
	xsh->y = grMagicToXs(w->w_frameArea.r_ytop);
	xsh->width = w->w_frameArea.r_xtop - w->w_frameArea.r_xbot;
	xsh->height= w->w_frameArea.r_ytop - w->w_frameArea.r_ybot;
	XSetStandardProperties(grXdpy, wind, (name == NULL) ? "magic"
			: name, "magic", None, 0, 0, xsh);
        XMapWindow(grXdpy, grCurrent.window);
	XSync(grXdpy,0);
	XFree(xsh);
	if (firstWindow)
	{
	     firstWindow = 0;
             grGCFill = XCreateGC(grXdpy, grCurrent.window, 0, 0);
             grGCDraw = XCreateGC(grXdpy, grCurrent.window, 0, 0);
             grGCText = XCreateGC(grXdpy, grCurrent.window, 0, 0);
             grGCCopy = XCreateGC(grXdpy, grCurrent.window, 0, 0);
             grGCGlyph = XCreateGC(grXdpy, grCurrent.window, 0, 0);
        }
	XSetPlaneMask(grXdpy,grGCGlyph,AllPlanes);
	grCurrent.window = wind; 
	grCurrent.mw = w;
	w->w_grdata = (ClientData) wind;
	
	entry = HashFind(&grX11WindowTable,grCurrent.window);
	HashSetValue(entry,w);

        XDefineCursor(grXdpy, grCurrent.window,grCurrent.cursor);
	GrX11IconUpdate(w,w->w_caption); 

#ifdef HAVE_PTHREADS
	xloop_create(wind);
#else
	XSync(grXdpy,0);

        write( pipeWrite, (char *) &wind, sizeof(Window));
	kill( Xhelper, SIGTERM);
#endif
	sleep(1); /* wait for Xhelper to for Expose Events; */
		  /* the window new doesn't get painted initially    */
		  /* otherwise.					     */
/*        printf("Create call completed.\n");  */
	return 1;
    }
    else
	TxError("Could not open new X window\n");

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrXDelete --
 *      Destroy an X window.
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
GrX11Delete(w)
    MagWindow *w;
{
    Window xw;
    HashEntry	*entry;

    xw = (Window) w->w_grdata;
    entry = HashLookOnly(&grX11WindowTable,xw);
    HashSetValue(entry,NULL);
    
    XDestroyWindow(grXdpy,xw);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrXConfigure --
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
GrX11Configure(w)
    MagWindow *w;
{
    XMoveResizeWindow(grXdpy,(Window) w->w_grdata,
	    w->w_frameArea.r_xbot, grMagicToXs(w->w_frameArea.r_ytop),
		w->w_frameArea.r_xtop - w->w_frameArea.r_xbot,
		    w->w_frameArea.r_ytop - w->w_frameArea.r_ybot);
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrXRaise --
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
GrX11Raise(w)
    MagWindow *w;
{
    XRaiseWindow(grXdpy, (Window) w->w_grdata );
}

/*
 * ----------------------------------------------------------------------------
 *
 * GrXLower --
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
GrX11Lower(w)
    MagWindow *w;
{
    XLowerWindow(grXdpy, (Window) w->w_grdata );
}


/*
 * ----------------------------------------------------------------------------
 *
 * GrX11Lock --
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
GrX11Lock(w, flag)
    MagWindow *w;
    bool flag;
{
    grSimpleLock(w, flag);
    if ( w != GR_LOCK_SCREEN )
    {
	grCurrent.mw = w;
	grCurrent.window = (Window) w->w_grdata;
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * GrX11Unlock --
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
GrX11Unlock(w)
    MagWindow *w;
{
    GR_X_FLUSH_BATCH();
    grSimpleUnlock(w);
}


/*
 *-------------------------------------------------------------------------
 *
 * GrX11IconUpdate -- updates the icon text with the window script
 *
 * Results: none
 *
 * Side Effects: changes the icon text
 *
 *-------------------------------------------------------------------------
 */

void
GrX11IconUpdate(w,text)
	MagWindow	*w;
	char		*text;

{
     Window	wind = (Window)(w->w_grdata);
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
