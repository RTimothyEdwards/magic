/*
 * grOGLInt.h --
 *
 * Internal definitions for grOGL[1..].c.
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

#define MAX_CURSORS	32	/* Maximum number of cursors */

#define GR_DEFAULT_FONT "9x15"
#define OGL_BATCH_SIZE	10000

# define        X_FONT_SMALL    "-*-helvetica-medium-r-normal--10-*-75-75-p-*-iso8859-*"
# define        X_FONT_MEDIUM   "-*-helvetica-medium-r-normal--14-*-75-75-p-*-iso8859-*"
# define        X_FONT_LARGE    "-*-helvetica-medium-r-normal--18-*-75-75-p-*-iso8859-*"
# define        X_FONT_XLARGE   "-*-helvetica-medium-r-normal--24-*-75-75-p-*-iso8859-*"

/* Current settings for X function parameters */
typedef struct {
    XFontStruct		*font;
    int			cursor;
    int			fontSize;
    int			depth;
    int			maskmod;
    Window 		window;
    MagWindow		*mw;
} OGL_CURRENT;

#ifdef OGL_SERVER_SIDE_ONLY
typedef Rect OGLRect;
#else

/* Used for vertex arrays */
typedef struct {
    Point r_ll, r_ul, r_ur, r_lr;
} OGLRect;

#endif

/*------------------------------------------------------*/

extern OGL_CURRENT oglCurrent;

extern bool groglGetCursorPos();
extern bool groglGetCursorRootPos();
extern bool groglDrawGrid();
extern void GrOGLEnableTablet();
extern void GrOGLDisableTablet();
extern void GrOGLSetCMap();
extern void GrOGLInitWithArgs();
extern void groglPutText();
extern void groglFontText();
extern void groglDefineCursor();
extern void GrOGLSetCursor();
extern void GrOGLSetWindow();
extern void GrOGLTextSize();
extern void GrOGLDrawGlyph();
extern void GrOGLBitBlt();
extern void NullBitBlt();
extern int  GrOGLReadPixel();
extern void groglDrawLine();
extern void groglSetLineStyle();
extern void groglSetCharSize();
extern void groglSetWMandC();
extern void groglFillRect();
extern void groglFillPolygon();

extern void groglFreeBackingStore();
extern void groglCreateBackingStore();
extern bool groglGetBackingStore();
extern bool groglScrollBackingStore();
extern void groglPutBackingStore();

extern Rect groglLines[];
extern OGLRect groglRects[];
extern void groglDrawLines();
extern int groglNbLines;
extern void groglFillRects();
extern int groglNbRects;

#define glTransY(mw, n)	( mw->w_allArea.r_ytop - (n))

#define GR_X_FLUSH_LINES() {if (groglNbLines>0) {groglDrawLines(groglLines, groglNbLines); groglNbLines=0;}}
#define GR_X_FLUSH_RECTS() {if (groglNbRects>0) {groglFillRects(groglRects, groglNbRects); groglNbRects=0;}}
#define	GR_X_FLUSH_BATCH() {GR_X_FLUSH_LINES(); GR_X_FLUSH_RECTS();}
