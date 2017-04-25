/*
 * grTkInt.h --
 *
 * Internal definitions for grTk[1..5].c.
 *
 * NOTE:  In order for the these defs to work correctly, this file
 * (grXInt.h) must be included after all the Magic .h files and before
 * the X .h files.
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 */

#include "utils/magic.h"

/* Constants
 */
#define M_WIDTH		1023
#define M_HEIGHT	750

#define TK_BATCH_SIZE	10000

/*
 * Macros for conversion between X and Magic coordinates
 */
#define grMagicToX(y) ( grCurrent.mw->w_allArea.r_ytop - (y))
#define grXToMagic(y) ( grCurrent.mw->w_allArea.r_ytop - (y))

/* Current settings for X function parameters */
typedef struct {
    Tk_Font 		font;
    Tk_Cursor		cursor;
    int			fontSize;
    int			maskmod;
    Tk_Window 		window;
    Window		windowid;
    MagWindow		*mw;
} GR_CURRENT;

typedef struct {
    unsigned long	basepixel;
    unsigned long	planes[32];
    int			depth;
    int			planeCount;
    int			colorCount;
    int			realColors;
    int			red_mask;
    int			green_mask;
    int			blue_mask;
} GR_DISPLAY;

extern Display *grXdpy;
extern Colormap grXcmap;
extern int	grXscrn;
extern unsigned long grPixels[];
extern unsigned long grPlanes[];
extern GR_CURRENT grCurrent;
extern GR_DISPLAY grDisplay;
extern GC grGCFill, grGCText, grGCDraw, grGCCopy, grGCGlyph;

extern bool grtkGetCursorPos();
extern bool grtkGetCursorRootPos();
extern bool grtkDrawGrid();
extern void GrTkEnableTablet();
extern void GrTkDisableTablet();
extern void GrTkSetCMap();
extern void grtkPutText();
extern void grtkFontText();
extern void GrTkSetCursor();
extern void GrTkTextSize();
extern void GrTkDrawGlyph();
extern void GrTkBitBlt();
extern void NullBitBlt();
extern int  GrTkReadPixel();
extern void grtkDrawLine();
extern void grtkSetLineStyle();
extern void grtkSetCharSize();
extern void grtkSetWMandC();
extern void grtkFillRect();
extern void grtkRectConvert();
extern void grtkFillPolygon();

extern void grtkDrawLines();
extern XSegment grtkLines[];
extern int grtkNbLines;
extern void grtkFillRects();
extern XRectangle grtkRects[];
extern int grtkNbRects;

#define GR_TK_FLUSH_LINES() {if (grtkNbLines>0) {grtkDrawLines(grtkLines, grtkNbLines); grtkNbLines=0;}}
#define GR_TK_FLUSH_RECTS() {if (grtkNbRects>0) {grtkFillRects(grtkRects, grtkNbRects); grtkNbRects=0;}}
#define	GR_TK_FLUSH_BATCH() {GR_TK_FLUSH_LINES(); GR_TK_FLUSH_RECTS();}

