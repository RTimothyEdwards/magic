/*
 * grX11Int.h --
 *
 * Internal definitions for grX11su[1..5].c.
 *
 * NOTE:  In order for the these defs to work correctly, this file
 * (grXInt.h) must be included after all the Magic .h files and before
 * the X .h files.
 */

#include "utils/magic.h"

/* Constants
 */
#define M_WIDTH		1023
#define M_HEIGHT	750

#define MAX_CURSORS	32	/* Maximum number of programmable cursors */

#define GR_DEFAULT_FONT "9x15"
#define X11_BATCH_SIZE	10000

#define grMagicToX(y) ( grCurrent.mw->w_allArea.r_ytop - (y))
#define grXToMagic(y) ( grCurrent.mw->w_allArea.r_ytop - (y))

#ifdef	OLD_R2_FONTS
/*
 * Some machines still run release 2 of X.
 */
# define       X_FONT_SMALL    "vg-13"
# define       X_FONT_MEDIUM   "fg-18"
# define       X_FONT_LARGE    "vrb-25"
# define       X_FONT_XLARGE   "vrb-37"
#else
/*
 * Our default fonts for X11.  (Release 3 fonts.)
 */

# define	X_FONT_SMALL	"-*-helvetica-medium-r-normal--10-*-75-75-p-*-iso8859-*"
# define	X_FONT_MEDIUM	"-*-helvetica-medium-r-normal--14-*-75-75-p-*-iso8859-*"
# define	X_FONT_LARGE	"-*-helvetica-medium-r-normal--18-*-75-75-p-*-iso8859-*"
# define	X_FONT_XLARGE 	"-*-helvetica-medium-r-normal--24-*-75-75-p-*-iso8859-*"
#endif

/* Macro for conversion between X and Magic coordinates
 */

/* Current settings for X function parameters */
typedef struct {
    XFontStruct 	*font;
    Cursor		cursor;
    int			fontSize;
    int			maskmod;
    Window 		window;
    MagWindow		*mw;
} GR_CURRENT;

/* X11 Display settings determined by the visual */
typedef struct {
    unsigned long basepixel;
    unsigned long planes[32];
    int		depth;
    int		planeCount;
    int		colorCount;
    int		realColors;
    int		red_mask;
    int		green_mask;
    int		blue_mask;
} GR_DISPLAY;

extern Display *grXdpy;
extern Colormap grXcmap;
extern int	grXscrn;
extern unsigned int grClass;
extern unsigned long grPixels[];
extern unsigned long grPlanes[];
extern GR_CURRENT grCurrent;
extern GR_DISPLAY grDisplay;
extern GC grGCFill, grGCText, grGCDraw, grGCCopy, grGCGlyph;

extern bool grx11GetCursorRootPos();
extern bool grx11DrawGrid();
extern void GrX11EnableTablet();
extern void GrX11DisableTablet();
extern void GrX11SetCMap();
extern void grx11PutText();
extern void grx11FontText();
extern void grx11DefineCursor();
extern void GrX11SetCursor();
extern void GrX11TextSize();
extern void GrX11DrawGlyph();
extern void GrX11BitBlt();
extern void NullBitBlt();
extern int  GrX11ReadPixel();
extern void grx11DrawLine();
extern void grx11SetLineStyle();
extern void grx11SetCharSize();
extern void grx11SetWMandC();
extern void grx11FillRect();
extern void grx11RectConvert();
extern void grx11FillPolygon();

extern void grx11FreeBackingStore();
extern void grx11CreateBackingStore();
extern bool grx11GetBackingStore();
extern bool grx11ScrollBackingStore();
extern void grx11PutBackingStore();

extern void grx11DrawLines();
extern XSegment grx11Lines[];
extern int grx11NbLines;
extern void grx11FillRects();
extern XRectangle grx11Rects[];
extern int grx11NbRects;

#define GR_X_FLUSH_LINES() {if (grx11NbLines>0) {grx11DrawLines(grx11Lines, grx11NbLines); grx11NbLines=0;}}
#define GR_X_FLUSH_RECTS() {if (grx11NbRects>0) {grx11FillRects(grx11Rects, grx11NbRects); grx11NbRects=0;}}
#define	GR_X_FLUSH_BATCH() {GR_X_FLUSH_LINES(); GR_X_FLUSH_RECTS();}
