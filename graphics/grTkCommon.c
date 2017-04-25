/* grTkCommon.c
 *
 *	Functions common to all graphics routines running under Tcl/Tk
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 */

/* We should be here if this is not set! */
#ifdef MAGIC_WRAPPER

#include <stdio.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/styles.h"
#include "utils/geometry.h"
#include "utils/hash.h"
#include "utils/malloc.h"
#include "utils/utils.h"
#include "tiles/tile.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwtech.h"
#include "utils/styles.h"
#include "graphics/graphics.h"
#include "graphics/graphicsInt.h"
#include "dbwind/dbwind.h"
#include "graphics/grTkCommon.h"
#include "graphics/glyphs.h"

/* Variables declared by grClip.c */

extern int grCurFill, grCurOutline, grCurColor;
extern void grInformDriver();

/* Global variables used by both the Tk and TOGL interfaces	*/
/* should be defined here.					*/

Display *grXdpy;
int grXscrn;
Tk_Cursor grCursors[MAX_CURSORS];
Tk_Font grTkFonts[4];

/* Used by Tk interface.  Defined for both Tk and TOGL, but 	*/
/* unused by the TOGL interface.				*/

bool GrTkInstalledCMap = FALSE;

/*---------------------------------------------------------
 * grTkLoadFont
 *	This local routine loads the Tk fonts used by Magic.
 *
 * Results:
 *	Success/Failure.
 *
 * Side Effects:
 *	Font information saved in grTkFonts array.
 *---------------------------------------------------------
 */

bool
grTkLoadFont()
{
    Tk_Window tkwind;
    int i;
    char *s;
    char *unable = "Unable to load font";

    static char *fontnames[4] = {
      TK_FONT_SMALL,
      TK_FONT_MEDIUM,
      TK_FONT_LARGE,
      TK_FONT_XLARGE };
    static char *optionnames[4] = {
      "small",
      "medium",
      "large",
      "xlarge"};

    tkwind = Tk_MainWindow(magicinterp);
    for (i = 0; i < 4; i++)
    {
    	s = XGetDefault(grXdpy, "magic", optionnames[i]);
	if (s) fontnames[i] = s;
        if ((grTkFonts[i] = Tk_GetFont(magicinterp,
			tkwind, fontnames[i])) == NULL) 
        {
	    TxError("%s %s\n", unable, fontnames[i]);
            if ((grTkFonts[i] = Tk_GetFont(magicinterp,
			tkwind, TK_DEFAULT_FONT)) == NULL) 
	    {
	        TxError("%s %s\n", unable, TK_DEFAULT_FONT);
		return FALSE;
	    }
        }
    }
    return TRUE;
}

/*---------------------------------------------------------
 * grTkFreeFonts
 *      This local routine frees the Tk font cache.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Memory deallocation.
 *---------------------------------------------------------
 */

void
grTkFreeFonts()
{
    int i;

    for (i = 0; i < 4; i++)
        Tk_FreeFont(grTkFonts[i]);
}

/*
 * ----------------------------------------------------------------------------
 * grTkFreeCursors:
 *
 *	Remove cursors from the Tk cursor cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Tk forgets about the glyph cursors.
 * ----------------------------------------------------------------------------
 */

void
grTkFreeCursors(glyphs)
    GrGlyphs *glyphs;
{
    int i;
    for (i = 0; i < glyphs->gr_num; i++)
	Tk_FreeCursor(grXdpy, grCursors[i]);
}

/*
 * ----------------------------------------------------------------------------
 * grTkDefineCursor:
 *
 *	Define a new set of cursors.  Use Tk-style cursors (portable)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given matrix is stored in the graphics display, and it can be
 *	used as the cursor by calling GrSetCursor.
 * ----------------------------------------------------------------------------
 */

typedef struct {
    unsigned char source[32];
    unsigned char mask[32];
} CursorCache;

void
grTkDefineCursor(glyphs)
    GrGlyphs *glyphs;
{
    char *fgname, *bgname;
    int glyphnum;
    Rect oldClip;
    int red, green, blue;
    Tk_Window tkwin;
    bool fg_needs_free;

    if (glyphs->gr_num <= 0) return;

    if (glyphs->gr_num > MAX_CURSORS)
    {
	TxError("magic/Tk only has room for %d cursors\n", MAX_CURSORS);
	return;
    }

    tkwin = Tk_MainWindow(magicinterp);

    /* expand clipping amount for off-screen access on the X */
    GrLock(GR_LOCK_SCREEN, FALSE);
    oldClip = grCurClip;
    grCurClip = GrScreenRect;
    grCurClip.r_ytop += 16;
    
    /* enter the glyphs */
    for (glyphnum = 0; glyphnum < glyphs->gr_num; glyphnum++) {
	int i, *p, fgstyle;
	XColor curcolor;
	GrGlyph *g;
	int x, y;
	CursorCache *glyphcache;

	g = glyphs->gr_glyph[glyphnum];
	if ((g->gr_xsize != 16) || (g->gr_ysize != 16)) {
	    TxError("Tk/OpenGL Cursors must be 16 X 16 pixels.\n");
	    return;
	}

        glyphcache = (CursorCache *)mallocMagic(sizeof(CursorCache));
	g->gr_cache = (ClientData)glyphcache;
	g->gr_free = freeMagic;
	
	/* Find the foreground and background colors of the glyph */

	p = &(g->gr_pixels[0]);
	fgstyle = STYLE_TRANSPARENT;
	fg_needs_free = FALSE;
	for (x = 0; x < 256; x++)
	{
	    if (*p != STYLE_TRANSPARENT)
	    {
		fgstyle = *p;
		GrGetColor(GrStyleTable[*p].color, &red, &green, &blue);
		curcolor.red = (unsigned short)(red << 8);
		curcolor.green = (unsigned short)(green << 8);
		curcolor.blue = (unsigned short)(blue << 8);
		curcolor.flags = DoRed | DoGreen | DoBlue;
		fgname = (char *)Tk_NameOfColor(Tk_GetColorByValue(tkwin, &curcolor));
		break;
	    }
	    p++;
	}
	if (x == 256)
	    fgname = "black";

	for (; x < 256; x++)
	{
	    if ((*p != STYLE_TRANSPARENT) && (*p != fgstyle))
	    {
		GrGetColor(GrStyleTable[*p].color, &red, &green, &blue);
		curcolor.red = (unsigned short)(red << 8);
		curcolor.green = (unsigned short)(green << 8);
		curcolor.blue = (unsigned short)(blue << 8);
		curcolor.flags = DoRed | DoGreen | DoBlue;
		bgname = StrDup((char **)NULL, fgname);
		fgname = bgname;
		fg_needs_free = TRUE;
		bgname = (char *)Tk_NameOfColor(Tk_GetColorByValue(tkwin, &curcolor));
		break;
	    }
	    p++;
	}
	if (x >= 256)
	    bgname = "white";

	/* Perform transposition on the glyph matrix since X displays
	 * the least significant bit on the left hand side.
	 */
	p = &(g->gr_pixels[0]);
	for (y = 0; y < 32; y++) {
	    i = (y & 1) ? (32 - y) : (30 - y);
	    glyphcache->source[i] = glyphcache->mask[i] = 0;
	    for (x = 0; x < 8; x++) 
	    {
		if (*p == fgstyle)
		     glyphcache->source[i] |= (1 << x);
		if (*p != STYLE_TRANSPARENT)
		     glyphcache->mask[i] |= (1 << x);
		p++;
	    }
	}
	grCursors[glyphnum] = Tk_GetCursorFromData(magicinterp,
			Tk_MainWindow(magicinterp),
			(char *)glyphcache->source, (char *)glyphcache->mask,
			16, 16, g->gr_origin.p_x, (15 - g->gr_origin.p_y),
			Tk_GetUid(fgname), Tk_GetUid(bgname));

	if (fg_needs_free) freeMagic(fgname);
    }

    /* Restore clipping */
    grCurClip = oldClip;
    GrUnlock(GR_LOCK_SCREEN);
}

/*
 *-------------------------------------------------------------------------
 * GrTkWindowName --
 *	Get the window name from the indicated MagWindow structure
 *
 * Results:
 *	The Tk path name of the window "mw" (char * string).
 *
 * Side Effects:
 *	None.
 *-------------------------------------------------------------------------
 */ 
  
char *
GrTkWindowName(mw)
    MagWindow *mw;
{    
    Tk_Window tkwind;
    char *tkname;
 
    tkwind = (Tk_Window) mw->w_grdata;
    return Tk_PathName(tkwind);
}  

/*
 * ----------------------------------------------------------------------------
 * grtkFreeBackingStore --
 *	Free up Pixmap memory for a backing store cell.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	memory Free'd
 * ----------------------------------------------------------------------------
 */

void
grtkFreeBackingStore(MagWindow *window)
{
    Pixmap pmap = (Pixmap)window->w_backingStore;
    if (pmap == (Pixmap)NULL) return;
    XFreePixmap(grXdpy, pmap);
    window->w_backingStore = (ClientData)NULL;
    /* TxPrintf("grtkFreeBackingStore called\n"); */
}

/*
 * ----------------------------------------------------------------------------
 * grtkCreateBackingStore --
 *	Create Pixmap memory for a backing store cell and copy data
 *	from the window into it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	memory Allocated.
 * 
 * ----------------------------------------------------------------------------
 */

void
grtkCreateBackingStore(MagWindow *w)
{
    Pixmap pmap;
    Tk_Window tkwind = (Tk_Window)w->w_grdata;
    Window wind;
    unsigned int width, height;

    /* ignore all windows other than layout */
    if (w->w_client != DBWclientID) return;

    /* deferred */
    if (tkwind == NULL) return;

    wind = Tk_WindowId(tkwind);
    width = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
    height = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;

    if (w->w_backingStore != (ClientData)NULL) grtkFreeBackingStore(w);

    pmap = XCreatePixmap(grXdpy, wind, width, height, Tk_Depth(tkwind));
    w->w_backingStore = (ClientData)pmap;

    /* TxPrintf("grtkCreateBackingStore area %d %d %d %d\n",
	w->w_screenArea.r_xbot, w->w_screenArea.r_ybot,
	w->w_screenArea.r_xtop, w->w_screenArea.r_ytop); */
}

/*
 * ----------------------------------------------------------------------------
 * grtkGetBackingStore --
 *	Copy data from a backing store Pixmap into the indicated window.
 *
 * Results:
 *	TRUE if backing store was copied successfully, FALSE if not.
 *
 * Side effects:
 *	Data copied into Pixmap memory.
 * 
 * ----------------------------------------------------------------------------
 */

bool
grtkGetBackingStore(MagWindow *w, Rect *area)
{
    Pixmap pmap;
    Tk_Window tkwind = (Tk_Window)w->w_grdata;
    Window wind = Tk_WindowId(tkwind);
    unsigned int width, height;
    int ybot;
    int xoff, yoff;
    GC gc;
    XGCValues gcValues;
    Rect r;

    pmap = (Pixmap)w->w_backingStore;
    if (pmap == (Pixmap)NULL)
	return FALSE;

    gcValues.graphics_exposures = FALSE;
    gc = Tk_GetGC(tkwind, GCGraphicsExposures, &gcValues);

    /* Make a local copy of area so we don't disturb the	*/
    /* original.  Expand by one pixel to deal with different	*/
    /* boundary	conditions between X11 and OpenGL.  The redraw	*/
    /* mechanism allows for at least this much slop.		*/

    GEO_EXPAND(area, 1, &r);
    GeoClip(&r, &(w->w_screenArea));

    width = r.r_xtop - r.r_xbot;
    height = r.r_ytop - r.r_ybot;
    ybot = grXtransY(w, r.r_ytop);

    xoff = w->w_screenArea.r_xbot - w->w_allArea.r_xbot;
    yoff = w->w_allArea.r_ytop - w->w_screenArea.r_ytop;

    XCopyArea(grXdpy, pmap, wind, gc, r.r_xbot - xoff,
		ybot - yoff, width, height, r.r_xbot, ybot);

    /* This is really only necessary for OpenGL, to avoid event */
    /* timing conflicts between OpenGL calls and XCopyArea.	*/
    /* It does not make sense for this to come AFTER the	*/
    /* XCopyArea() command.  Yet that is what works.  So I'm	*/
    /* sure that I do not entirely understand the situation.	*/

    (*GrFlushPtr)();

    /* TxPrintf("grtkGetBackingStore %d %d %d %d\n",
		r.r_xbot, r.r_ybot, r.r_xtop, r.r_ytop); */
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * grtkScrollBackingStore --
 *	Enable fast scrolling by shifting part of the backing store
 *	from one position to another, with the amount of shift indicated
 *	by the X and/or Y value of the indicated point.
 *
 * Results:
 *	TRUE on success, FALSE on failure.
 *
 * Side effects:
 *	Data shifted in Pixmap memory.
 * 
 * ----------------------------------------------------------------------------
 */

bool
grtkScrollBackingStore(MagWindow *w, Point *shift)
{
    Pixmap pmap;
    Tk_Window tkwind = (Tk_Window)w->w_grdata;
    unsigned int width, height;
    int xorigin, yorigin, xshift, yshift;
    GC gc;
    XGCValues gcValues;

    pmap = (Pixmap)w->w_backingStore;
    if (pmap == (Pixmap)NULL)
    {
	TxPrintf("grtkScrollBackingStore %d %d failure\n",
		shift->p_x, shift->p_y);
	return FALSE;
    }

    gcValues.graphics_exposures = FALSE;
    gc = Tk_GetGC(tkwind, GCGraphicsExposures, &gcValues);

    width = w->w_screenArea.r_xtop - w->w_screenArea.r_xbot;
    height = w->w_screenArea.r_ytop - w->w_screenArea.r_ybot;
    xorigin = 0;
    yorigin = 0;
    xshift = shift->p_x;
    yshift = -shift->p_y;

    if (xshift > 0)
	width -= xshift;
    else if (xshift < 0)
    {
	width += xshift;
	xorigin = -xshift;
	xshift = 0;
    }
    if (yshift > 0)
	height -= yshift;
    else if (yshift < 0)
    {
	height += yshift;
	yorigin = -yshift;
	yshift = 0;
    }

    XCopyArea(grXdpy, pmap, pmap, gc, xorigin, yorigin, width, height,
		xshift, yshift);

    /* TxPrintf("grtkScrollBackingStore %d %d\n", shift->p_x, shift->p_y); */
    return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * grtkPutBackingStore --
 *	Copy data from the window into backing store.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Graphics drawing into the window.
 * ----------------------------------------------------------------------------
 */

void
grtkPutBackingStore(MagWindow *w, Rect *area)
{
    Pixmap pmap = (Pixmap)w->w_backingStore;
    Tk_Window tkwind = (Tk_Window)w->w_grdata;
    Window wind = Tk_WindowId(tkwind);
    unsigned int width, height;
    int ybot, xbot, xoff, yoff;
    GC gc;
    XGCValues gcValues;

    if (pmap == (Pixmap)NULL) return;

    /* Attempting to write backing store into an obscured	*/
    /* window immediately invalidates everything in backing	*/
    /* store.  This is extreme, but is much simpler and under	*/
    /* normal conditions faster than tracking all obscured	*/
    /* areas separately.					*/

    if (w->w_flags & WIND_OBSCURED)
    {
	grtkFreeBackingStore(w);
	w->w_backingStore = (ClientData)NULL;
	return;
    }

    width = area->r_xtop - area->r_xbot;
    height = area->r_ytop - area->r_ybot;
    ybot = grXtransY(w, area->r_ytop);
    xbot = area->r_xbot;

    gcValues.graphics_exposures = FALSE;
    gc = Tk_GetGC(tkwind, GCGraphicsExposures, &gcValues);

    xoff = w->w_screenArea.r_xbot - w->w_allArea.r_xbot;
    yoff = w->w_allArea.r_ytop - w->w_screenArea.r_ytop;

    /* This area may need to be expanded by a pixel and/or	*/
    /* expanded by GrPixelCorrect to compensate for the OpenGL	*/
    /* coordinate system.					*/

    if (GrPixelCorrect == 0)
    {
	height--;
	width--;
	xbot++;
    }

    XCopyArea(grXdpy, wind, pmap, gc, xbot, ybot, width, height,
		xbot - xoff, ybot - yoff);

    /* TxPrintf("grtkPutBackingStore %d %d %d %d\n",
		xbot, area->r_ybot, area->r_xtop, area->r_ytop); */
}

/*
 *---------------------------------------------------------
 * GrTkGetColorByName --
 *
 *	Returns a string appropriate for setting a color
 *	value in a Tk window (referenced to the Tk colormap)
 *	for the given color of the magic style referenced
 *	by the short name or long name (as given in the dstyle5
 *	file).
 *
 *	Note that the colormaps grXcmap and Tk_Colormap(tkwind)
 *	are the same unless we are in 8-bit PseudoColor mode,
 *	in which case this function maps between the two.
 *
 *	Note that when given an invalid short name, GrStyleNames[]
 *	always returns 0, which is the index for style
 *	"no_color_at_all".  On the other hand, long names are not
 *	uniquely defined between "normal" and "pale" styles.
 *
 * Results:
 *	A string allocated by Tcl_Alloc() which will need to
 *	be free'd by the calling function using Tcl_Free().
 *
 * Side Effects:
 *	None.
 *
 *---------------------------------------------------------
 */

char *
GrTkGetColorByName(name)
    char *name;
{
    Tk_Window tkwind = Tk_MainWindow(magicinterp);
    int style, red, green, blue;
    XColor falsecolor;
    char *colstring;

    if (strlen(name) == 1)
	style = GrStyleNames[name[0] & 0x7f];
    else if (DBWNumStyles == 0)
    {
	TxError("No style table exists.\n");
	return NULL;
    }
    else
    {
	for (style = 0; style < TECHBEGINSTYLES + DBWNumStyles; style++)
	    if (GrStyleTable[style].longname != NULL)
		if (!strcmp(name, GrStyleTable[style].longname))
		    break;
    }

    if (style >= TECHBEGINSTYLES + DBWNumStyles)
    {
	TxError("Style does not exist or style is not accessible\n");
	return NULL;
    }

    falsecolor.pixel = GrStyleTable[style].color;
    if (GrTkInstalledCMap)
    {
	XQueryColor(grXdpy, Tk_Colormap(tkwind), &falsecolor);
        colstring = Tcl_Alloc(14);
        sprintf(colstring, "#%04x%04x%04x", falsecolor.red,
			falsecolor.green, falsecolor.blue);
    }
    else
    {
	/* Note that XColor colors are unsigned short, but GrGetColor	*/
	/* expects an int *.						*/

	GrGetColor(falsecolor.pixel, &red, &green, &blue);
	falsecolor.red = red;
	falsecolor.green = green;
	falsecolor.blue = blue;
        colstring = Tcl_Alloc(8);
        sprintf(colstring, "#%02x%02x%02x", falsecolor.red,
			falsecolor.green, falsecolor.blue);
    }
    return colstring;
}

/*
 *---------------------------------------------------------
 * _magic_magiccolor --
 *	Tcl command callback for GrTkGetColorByName
 *
 * Results:
 *	TCL result type
 *
 * Side effects:
 *	None.
 *---------------------------------------------------------
 */

static int
_magic_magiccolor(ClientData clientData,
	Tcl_Interp *interp, int argc, char *argv[])
{
    char *result;
    char *name;

    if (argc != 2)
    {
	TxError("Usage: magiccolor name\n");
	return TCL_ERROR;
    }
    name = argv[1];

    result = GrTkGetColorByName(name);
    if (result)
    {
	Tcl_SetResult(interp, result, TCL_DYNAMIC);
 	return TCL_OK;
    }
    else {
	TxError("No such color name \"%s\" in style file.\n", name);
	return TCL_ERROR;
    }
}

/*
 * The following data structure represents the master for a layer
 * image:
 */

typedef struct LayerMaster {
    Tk_ImageMaster tkMaster;	/* Tk's token for image master.  NULL means
				 * the image is being deleted. */
    Tcl_Interp *interp;		/* Interpreter for application that is
				 * using image. */
    Tcl_Command imageCmd;	/* Token for image command (used to delete
				 * it when the image goes away).  NULL means
				 * the image command has already been
				 * deleted. */
    int width, height;		/* Dimensions of image. */
    int layerOff;		/* If TRUE layer is displayed in non-edit style */
    int layerLock;		/* Layer is displayed with a cursor icon */
    char *layerString;		/* Value of -layer option (malloc'ed). */
    struct LayerInstance *instancePtr;
				/* First in list of all instances associated
				 * with this master. */
} LayerMaster;

/*
 * The following data structure represents all of the instances of an
 * image that lie within a particular window:
 */

typedef struct LayerInstance {
    int refCount;		/* Number of instances that share this
				 * data structure. */
    LayerMaster *masterPtr;	/* Pointer to master for image. */
    Tk_Window tkwin;		/* Window in which the instances will be
				 * displayed. */
    Pixmap pixmap;		/* The bitmap to display. */
    GC gc;			/* Graphics context for displaying pixmap */
    struct LayerInstance *nextPtr;
				/* Next in list of all instance structures
				 * associated with masterPtr (NULL means
				 * end of list). */
} LayerInstance;

/*
 * The type record for bitmap images:
 */

static int		ImgLayerCreate _ANSI_ARGS_((Tcl_Interp *interp,
			    const char *name, int argc, Tcl_Obj *CONST objv[],
			    const Tk_ImageType *typePtr, Tk_ImageMaster master,
			    ClientData *clientDataPtr));
static ClientData	ImgLayerGet _ANSI_ARGS_((Tk_Window tkwin,
			    ClientData clientData));
static void		ImgLayerDisplay _ANSI_ARGS_((ClientData clientData,
			    Display *display, Drawable drawable, 
			    int imageX, int imageY, int width, int height,
			    int drawableX, int drawableY));
static void		ImgLayerFree _ANSI_ARGS_((ClientData clientData,
			    Display *display));
static void		ImgLayerDelete _ANSI_ARGS_((ClientData clientData));

Tk_ImageType tkLayerImageType = {
    "layer",			/* name */
    ImgLayerCreate,		/* createProc */
    ImgLayerGet,		/* getProc */
    ImgLayerDisplay,		/* displayProc */
    ImgLayerFree,		/* freeProc */
    ImgLayerDelete,		/* deleteProc */
    NULL,			/* postscriptProc */
    (Tk_ImageType *) NULL	/* nextPtr */
};

/*
 * Information used for parsing configuration specs:
 * Size defaults to a 16x16 area.
 */

static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_STRING, "-name", (char *) NULL, (char *) NULL,
	(char *) NULL, Tk_Offset(LayerMaster, layerString), TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-disabled", (char *) NULL, (char *) NULL,
	(char *) "0", Tk_Offset(LayerMaster, layerOff), 0},
    {TK_CONFIG_INT, "-icon", (char *) NULL, (char *) NULL,
	(char *) "-1", Tk_Offset(LayerMaster, layerLock), 0},
    {TK_CONFIG_INT, "-width", (char *) NULL, (char *) NULL,
	(char *) "16", Tk_Offset(LayerMaster, width), 0},
    {TK_CONFIG_INT, "-height", (char *) NULL, (char *) NULL,
	(char *) "16", Tk_Offset(LayerMaster, height), 0},
    {TK_CONFIG_END, (char *) NULL, (char *) NULL, (char *) NULL,
	(char *) NULL, 0, 0}
};

/*
 * Prototypes for procedures used only locally in this file:
 */

static int		ImgLayerCmd _ANSI_ARGS_((ClientData clientData,
			    Tcl_Interp *interp, int argc, Tcl_Obj *CONST objv[]));
static void		ImgLayerCmdDeletedProc _ANSI_ARGS_((
			    ClientData clientData));
static void		ImgLayerConfigureInstance _ANSI_ARGS_((
			    LayerInstance *instancePtr));
static int		ImgLayerConfigureMaster _ANSI_ARGS_((
			    LayerMaster *masterPtr, int argc, Tcl_Obj *CONST objv[],
			    int flags));

/*
 *----------------------------------------------------------------------
 *
 * ImgLayerCreate --
 *
 *	This procedure is called by the Tk image code to create "test"
 *	images.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	The data structure for a new image is allocated.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
static int
ImgLayerCreate(interp, name, argc, argv, typePtr, master, clientDataPtr)
    Tcl_Interp *interp;		/* Interpreter for application containing
				 * image. */
    const char *name;		/* Name to use for image. */
    int argc;			/* Number of arguments. */
    Tcl_Obj *CONST argv[];	/* Argument objects for options (doesn't
				 * include image name or type). */
    const Tk_ImageType *typePtr;/* Pointer to our type record (not used). */
    Tk_ImageMaster master;	/* Token for image, to be used by us in
				 * later callbacks. */
    ClientData *clientDataPtr;	/* Store manager's token for image here;
				 * it will be returned in later callbacks. */
{
    LayerMaster *masterPtr;

    masterPtr = (LayerMaster *) Tcl_Alloc(sizeof(LayerMaster));
    masterPtr->tkMaster = master;
    masterPtr->interp = interp;
    masterPtr->imageCmd = Tcl_CreateObjCommand(interp, name, ImgLayerCmd,
	    (ClientData) masterPtr, ImgLayerCmdDeletedProc);
    masterPtr->width = masterPtr->height = 0;
    masterPtr->layerOff = 0;
    masterPtr->layerLock = -1;
    masterPtr->layerString = NULL;
    masterPtr->instancePtr = NULL;
    if (ImgLayerConfigureMaster(masterPtr, argc, argv, 0) != TCL_OK) {
	ImgLayerDelete((ClientData) masterPtr);
	return TCL_ERROR;
    }
    *clientDataPtr = (ClientData) masterPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ImgLayerConfigureMaster --
 *
 *	This procedure is called when a bitmap image is created or
 *	reconfigured.  It process configuration options and resets
 *	any instances of the image.
 *
 * Results:
 *	A standard Tcl return value.  If TCL_ERROR is returned then
 *	an error message is left in the masterPtr->interp's result.
 *
 * Side effects:
 *	Existing instances of the image will be redisplayed to match
 *	the new configuration options.
 *
 *----------------------------------------------------------------------
 */

static int
ImgLayerConfigureMaster(masterPtr, objc, objv, flags)
    LayerMaster *masterPtr;	/* Pointer to data structure describing
				 * overall pixmap image to (reconfigure). */
    int objc;			/* Number of entries in objv. */
    Tcl_Obj *CONST objv[];	/* Pairs of configuration options for image. */
    int flags;			/* Flags to pass to Tk_ConfigureWidget,
				 * such as TK_CONFIG_ARGV_ONLY. */
{
    LayerInstance *instancePtr;
    int dummy1;

    char **argv = (char **) Tcl_Alloc((objc+1) * sizeof(char *));
    for (dummy1 = 0; dummy1 < objc; dummy1++) {
	argv[dummy1]=Tcl_GetString(objv[dummy1]);
    }
    argv[objc] = NULL;

    if (Tk_ConfigureWidget(masterPtr->interp, Tk_MainWindow(masterPtr->interp),
	    configSpecs, objc, (CONST84 char **)argv, (char *) masterPtr, flags)
	    != TCL_OK) {
	Tcl_Free((char *) argv);
	return TCL_ERROR;
    }
    Tcl_Free((char *) argv);

    /*
     * Cycle through all of the instances of this image, regenerating
     * the information for each instance.  Then force the image to be
     * redisplayed everywhere that it is used.
     */

    for (instancePtr = masterPtr->instancePtr; instancePtr != NULL;
	    instancePtr = instancePtr->nextPtr) {
	ImgLayerConfigureInstance(instancePtr);
    }
    Tk_ImageChanged(masterPtr->tkMaster, 0, 0, masterPtr->width,
	    masterPtr->height, masterPtr->width, masterPtr->height);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 * grDrawOffScreenBox --
 *	Draw a box on an off-screen drawable (convenience function for
 *	the following procedure ImgLayerConfigureInstance()
 * 
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws into an off-screen drawable destination.
 *
 *----------------------------------------------------------------------
 */

void
grDrawOffScreenBox(rect)
    Rect *rect;
{
    (*grDrawLinePtr)(rect->r_xbot, rect->r_ytop - 1, rect->r_xtop - 1,
			rect->r_ytop - 1);
    (*grDrawLinePtr)(rect->r_xbot, rect->r_ybot, rect->r_xtop - 1,
			rect->r_ybot);
    (*grDrawLinePtr)(rect->r_xbot, rect->r_ybot, rect->r_xbot,
			rect->r_ytop - 1);
    (*grDrawLinePtr)(rect->r_xtop - 1, rect->r_ybot, rect->r_xtop - 1,
			rect->r_ytop - 1);
}


/*
 *----------------------------------------------------------------------
 *
 * ImgLayerConfigureInstance --
 *
 *	This procedure is called to create displaying information for
 *	a layer image instance based on the configuration information
 *	in the master.  It is invoked both when new instances are
 *	created and when the master is reconfigured.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Generates errors via Tcl_BackgroundError if there are problems
 *	in setting up the instance.
 *
 *----------------------------------------------------------------------
 */

#define LAYER_NORMAL	0
#define LAYER_LABELS	1
#define LAYER_SUBCELL	2
#define LAYER_LAYOUT	3

static void
ImgLayerConfigureInstance(instancePtr)
    LayerInstance *instancePtr;	/* Instance to reconfigure. */
{
    LayerMaster *masterPtr = instancePtr->masterPtr;
    XGCValues gcValues;
    GC gc;
    unsigned int gcmask;
    TileType layer;
    TileTypeBitMask *mask;
    int i, special = LAYER_NORMAL;
    Rect r;
    Tk_Window tkwind = instancePtr->tkwin;
    MagWindow *mw, tmpmw;

    if (Tk_WindowId(tkwind) == 0)
	Tk_MakeWindowExist(tkwind);

    if (Tk_WindowId(tkwind) == 0)
    {
	Tcl_AddErrorInfo(masterPtr->interp, "No ID exists for window");
	goto error;
    }

    /*
     * For each of the options in masterPtr, translate the string
     * form into an internal form appropriate for instancePtr.
     */

    if (masterPtr->width <= 0 || masterPtr->height <= 0)
    {
	Tcl_AddErrorInfo(masterPtr->interp, "Image width or height is negative");
	goto error;
    }

    if (instancePtr->pixmap != None) {
	mw = WindSearchData((ClientData)instancePtr->pixmap);
	if (mw != NULL)
	{
	    windUnlink(mw);
	    windReClip();
	    windFree(mw);
	}
	Tk_FreePixmap(grXdpy, instancePtr->pixmap);
	instancePtr->pixmap = None;
    }

    if (masterPtr->layerString != NULL) {
	if (!strcmp(masterPtr->layerString, "none"))
	    layer = TT_SPACE;
	else if (!strcmp(masterPtr->layerString, "errors"))
	    layer = TT_ERROR_P;
	else if (!strcmp(masterPtr->layerString, "labels"))
	{
	    layer = TT_SPACE;
	    special = LAYER_LABELS;
	}
	else if (!strcmp(masterPtr->layerString, "subcell"))
	{
	    layer = TT_SPACE;
	    special = LAYER_SUBCELL;
	}
	else
	    layer = DBTechNameType(masterPtr->layerString);

	if (layer < 0)
	{
	    layer = (*GrWindowIdPtr)(masterPtr->layerString); 

	    if (layer >= 0)
		special = LAYER_LAYOUT;
	    else
	    {
		Tcl_AddErrorInfo(masterPtr->interp, "Unknown layer type");
		goto error;
	    }
	}

	r.r_xbot = r.r_ybot = 0;
	r.r_xtop = masterPtr->width;
	r.r_ytop = masterPtr->height;

	gcValues.graphics_exposures = FALSE;
	gcmask = GCGraphicsExposures;
	gc = Tk_GetGC(tkwind, gcmask, &gcValues);

	if (instancePtr->gc != None)
	    Tk_FreeGC(grXdpy, instancePtr->gc);
	instancePtr->gc = gc;

	if (special == LAYER_LAYOUT)	/* Off-Screen Rendering */
	{
	    Rect screenRect;
	    Tk_Window pixwind;

	    mw = WindSearchWid(layer);
	    if (mw == NULL)
	    {
		Tcl_AddErrorInfo(masterPtr->interp, "Unknown window ID\n");
		goto error;
	    }

	    pixwind = (Tk_Window)mw->w_grdata;

	    instancePtr->pixmap = Tk_GetPixmap(grXdpy,
			Tk_WindowId(pixwind),
			masterPtr->width, masterPtr->height,
			Tk_Depth(pixwind));

	    (*GrDeleteWindowPtr)(mw);
	    mw->w_flags |= WIND_OFFSCREEN;
	    mw->w_grdata = (ClientData)instancePtr->pixmap;

	    screenRect.r_xbot = 0;
	    screenRect.r_ybot = 0;
	    screenRect.r_xtop = masterPtr->width;
	    screenRect.r_ytop = masterPtr->height;

	    WindReframe(mw, &screenRect, FALSE, FALSE);
	    WindRedisplay(mw);

	    return;
	}

	instancePtr->pixmap = Tk_GetPixmap(grXdpy,
		Tk_WindowId(tkwind),
		masterPtr->width, masterPtr->height,
		Tk_Depth(tkwind));

	tmpmw.w_flags = WIND_OFFSCREEN;
	tmpmw.w_grdata = (ClientData)instancePtr->pixmap;
	tmpmw.w_allArea = r;
	tmpmw.w_clipAgainst = NULL;

	GrLock(&tmpmw, FALSE);

	/* First fill with background style */
	GrSetStuff(STYLE_ERASEALL);
	grInformDriver();
	(*grFillRectPtr)(&r);

	for (i = 0; i < DBWNumStyles; i++)
	{
	    mask = DBWStyleToTypes(i);
	    if (TTMaskHasType(mask, layer))
	    {
		GrSetStuff(i + TECHBEGINSTYLES + 
			((masterPtr->layerOff == 0) ? 0 : DBWNumStyles));
		grInformDriver();

		/* Solid areas */
		if ((grCurFill == GR_STSOLID) || (grCurFill == GR_STSTIPPLE))
		    (*grFillRectPtr)(&r);

		/* Outlines */
		if (grCurOutline != 0)
		    grDrawOffScreenBox(&r);

		/* Contact crosses */
		if (grCurFill == GR_STCROSS)
		{
		    (*grDrawLinePtr)(r.r_xbot, r.r_ybot, r.r_xtop - 1,
				r.r_ytop - 1);
		    (*grDrawLinePtr)(r.r_xbot, r.r_ytop - 1, r.r_xtop - 1,
				r.r_ybot);
		}
	    }
	}
	
	switch(special) {
	    case LAYER_LABELS:
		GrSetStuff(STYLE_LABEL);
		grInformDriver();
		grDrawOffScreenBox(&r);
		break;
	    case LAYER_SUBCELL:
		GrSetStuff(STYLE_BBOX);
		grInformDriver();
		grDrawOffScreenBox(&r);
		break;
	}
	if (masterPtr->layerLock >= 0) {
	    GrSetStuff(STYLE_BLACK);
	    grInformDriver();
	    GrDrawGlyphNum(masterPtr->layerLock, 0, 0);
	}
	GrUnlock(&tmpmw);
    }

    return;

error:

    /*
     * An error occurred: clear the graphics context in the instance to
     * make it clear that this instance cannot be displayed.  Then report
     * the error.
     */

    if (instancePtr->gc != None)
	Tk_FreeGC(grXdpy, instancePtr->gc);
    instancePtr->gc = None; 

    Tcl_AddErrorInfo(masterPtr->interp, "\n    (while configuring image \"");
    Tcl_AddErrorInfo(masterPtr->interp, Tk_NameOfImage(masterPtr->tkMaster));
    Tcl_AddErrorInfo(masterPtr->interp, "\")");
    Tcl_BackgroundError(masterPtr->interp);
}


/*
 *--------------------------------------------------------------
 *
 * ImgLayerCmd --
 *
 *	This procedure is invoked to process the Tcl command
 *	that corresponds to an image managed by this module.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

static int
ImgLayerCmd(clientData, interp, objc, objv)
    ClientData clientData;	/* Information about the image master. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    static char *layerOptions[] = {"cget", "configure", (char *) NULL};
    LayerMaster *masterPtr = (LayerMaster *) clientData;
    int code, index;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg arg ...?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)layerOptions,
		"option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    switch (index) {
      case 0: {
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    return TCL_ERROR;
	}
	return Tk_ConfigureValue(interp, Tk_MainWindow(interp), configSpecs,
		(char *) masterPtr, Tcl_GetString(objv[2]), 0);
      }
      case 1: {
	if (objc == 2) {
	    code = Tk_ConfigureInfo(interp, Tk_MainWindow(interp),
		    configSpecs, (char *) masterPtr, (char *) NULL, 0);
	} else if (objc == 3) {
	    code = Tk_ConfigureInfo(interp, Tk_MainWindow(interp),
		    configSpecs, (char *) masterPtr,
		    Tcl_GetString(objv[2]), 0);
	} else {
	    code = ImgLayerConfigureMaster(masterPtr, objc-2, objv+2,
		    TK_CONFIG_ARGV_ONLY);
	}
	return code;
      }
      default: {
	panic("bad const entries to layerOptions in ImgLayerCmd");
      }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ImgLayerGet --
 *
 *	This procedure is called for each use of a layer image in a
 *	widget.
 *
 * Results:
 *	The return value is a token for the instance, which is passed
 *	back to us in calls to ImgLayerDisplay and ImgLayerFree.
 *
 * Side effects:
 *	A data structure is set up for the instance (or, an existing
 *	instance is re-used for the new one).
 *
 *----------------------------------------------------------------------
 */

static ClientData
ImgLayerGet(tkwin, masterData)
    Tk_Window tkwin;		/* Window in which the instance will be
				 * used. */
    ClientData masterData;	/* Pointer to our master structure for the
				 * image. */
{
    LayerMaster *masterPtr = (LayerMaster *) masterData;
    LayerInstance *instancePtr;

    /*
     * See if there is already an instance for this window.  If so
     * then just re-use it.
     */

    for (instancePtr = masterPtr->instancePtr; instancePtr != NULL;
	    instancePtr = instancePtr->nextPtr) {
	if (instancePtr->tkwin == tkwin) {
	    instancePtr->refCount++;
	    return (ClientData) instancePtr;
	}
    }

    /*
     * The image isn't already in use in this window.  Make a new
     * instance of the image.
     */

    instancePtr = (LayerInstance *) Tcl_Alloc(sizeof(LayerInstance));
    instancePtr->refCount = 1;
    instancePtr->masterPtr = masterPtr;
    instancePtr->tkwin = tkwin;
    instancePtr->pixmap = None;
    instancePtr->gc = None;
    instancePtr->nextPtr = masterPtr->instancePtr;
    masterPtr->instancePtr = instancePtr;
    ImgLayerConfigureInstance(instancePtr);

    /*
     * If this is the first instance, must set the size of the image.
     */

    if (instancePtr->nextPtr == NULL) {
	Tk_ImageChanged(masterPtr->tkMaster, 0, 0, 0, 0, masterPtr->width,
		masterPtr->height);
    }

    return (ClientData) instancePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ImgLayerDisplay --
 *
 *	This procedure is invoked to draw a layer image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A portion of the image gets rendered in a pixmap or window.
 *
 *----------------------------------------------------------------------
 */

static void
ImgLayerDisplay(clientData, display, drawable, imageX, imageY, width,
	height, drawableX, drawableY)
    ClientData clientData;	/* Pointer to LayerInstance structure for
				 * for instance to be displayed. */
    Display *display;		/* Display on which to draw image. */
    Drawable drawable;		/* Pixmap or window in which to draw image. */
    int imageX, imageY;		/* Upper-left corner of region within image
				 * to draw. */
    int width, height;		/* Dimensions of region within image to draw. */
    int drawableX, drawableY;	/* Coordinates within drawable that
				 * correspond to imageX and imageY. */
{
    LayerInstance *instancePtr = (LayerInstance *) clientData;

    /*
     * If there's no GC, then an error occurred during image creation
     * and it should not be displayed.
     */

    if (instancePtr->gc == None) return;

    XCopyArea(display, instancePtr->pixmap, drawable, instancePtr->gc,
	    imageX, imageY, (unsigned) width, (unsigned) height,
	    drawableX, drawableY);
}

/*
 *----------------------------------------------------------------------
 *
 * ImgLayerFree --
 *
 *	This procedure is called when a widget ceases to use a
 *	particular instance of an image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Internal data structures get cleaned up.
 *
 *----------------------------------------------------------------------
 */

static void
ImgLayerFree(clientData, display)
    ClientData clientData;	/* Pointer to LayerInstance structure for
				 * for instance to be displayed. */
    Display *display;		/* Display containing window that used image. */
{
    LayerInstance *instancePtr = (LayerInstance *) clientData;
    LayerInstance *prevPtr;

    instancePtr->refCount--;
    if (instancePtr->refCount > 0) {
	return;
    }

    /*
     * There are no more uses of the image within this widget.  Free
     * the instance structure.
     */

    if (instancePtr->pixmap != None) {
	MagWindow *mw;
	mw = WindSearchData((ClientData)instancePtr->pixmap);
	if (mw != NULL)
	{
	    windUnlink(mw);
	    windReClip();
	    windFree(mw);
	}
	Tk_FreePixmap(display, instancePtr->pixmap);
    }
    if (instancePtr->masterPtr->instancePtr == instancePtr) {
	instancePtr->masterPtr->instancePtr = instancePtr->nextPtr;
    } else {
	for (prevPtr = instancePtr->masterPtr->instancePtr;
		prevPtr->nextPtr != instancePtr; prevPtr = prevPtr->nextPtr) {
	    /* Empty loop body */
	}
	prevPtr->nextPtr = instancePtr->nextPtr;
    }
    Tcl_Free((char *) instancePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * ImgLayerDelete --
 *
 *	This procedure is called by the image code to delete the
 *	master structure for an image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resources associated with the image get freed.
 *
 *----------------------------------------------------------------------
 */

static void
ImgLayerDelete(masterData)
    ClientData masterData;	/* Pointer to BitmapMaster structure for
				 * image.  Must not have any more instances. */
{
    LayerMaster *masterPtr = (LayerMaster *) masterData;

    if (masterPtr->instancePtr != NULL) {
	panic("tried to delete layer image when instances still exist");
    }
    masterPtr->tkMaster = NULL;
    if (masterPtr->imageCmd != NULL) {
	Tcl_DeleteCommandFromToken(masterPtr->interp, masterPtr->imageCmd);
    }
    Tk_FreeOptions(configSpecs, (char *) masterPtr, (Display *) NULL, 0);
    Tcl_Free((char *) masterPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * ImgLayerCmdDeletedProc --
 *
 *	This procedure is invoked when the image command for an image
 *	is deleted.  It deletes the image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The image is deleted.
 *
 *----------------------------------------------------------------------
 */

static void
ImgLayerCmdDeletedProc(clientData)
    ClientData clientData;	/* Pointer to BitmapMaster structure for
				 * image. */
{
    LayerMaster *masterPtr = (LayerMaster *) clientData;

    masterPtr->imageCmd = NULL;
    if (masterPtr->tkMaster != NULL) {
	Tk_DeleteImage(masterPtr->interp, Tk_NameOfImage(masterPtr->tkMaster));
    }
}

/*
 *---------------------------------------------------------
 * RegisterTkCommands --
 *	Register the "magiccolor" command with Tcl.
 *	Register the layer type with the Tk "image" command
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Command registration with the TCL interpreter.
 *---------------------------------------------------------
 */

void
RegisterTkCommands(Tcl_Interp *interp)
{
    Tcl_CreateCommand(interp, "magic::magiccolor",
		(Tcl_CmdProc *)_magic_magiccolor, (ClientData)NULL,
		(Tcl_CmdDeleteProc *)NULL);
    Tk_CreateImageType(&tkLayerImageType);
}

#endif   /* MAGIC_WRAPPER */
