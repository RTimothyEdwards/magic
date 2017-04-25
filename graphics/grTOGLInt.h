/*
 * grTOGLInt.h --
 *
 * Internal definitions for grTOGL[1..5].c.
 *
 * NOTE:  In order for the these defs to work correctly, this file
 * (grTOGLInt.h) must be included after all the Magic .h files and before
 * the X11, OpenGL, and Tk/Tcl .h files.
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 */

#include "utils/magic.h"

/* Constants
 */
#define M_WIDTH		1023
#define M_HEIGHT	750

#define TOGL_BATCH_SIZE	10000

/* Current settings for X function parameters */
typedef struct {
    Tk_Font		font;
    Tk_Cursor		cursor;
    int			fontSize;
    int			depth;
    int			maskmod;
    Tk_Window 		window;
    Window		windowid;
    MagWindow		*mw;
} TOGL_CURRENT;

#ifdef OGL_SERVER_SIDE_ONLY
typedef Rect TOGLRect;
#else

/* Used for vertex arrays */
typedef struct {
    Point r_ll, r_ul, r_ur, r_lr;
} TOGLRect;

#endif

extern TOGL_CURRENT toglCurrent;

extern bool grtoglGetCursorPos();
extern bool grtoglGetCursorRootPos();
extern bool grtoglDrawGrid();
extern void GrTOGLEnableTablet();
extern void GrTOGLDisableTablet();
extern void GrTOGLSetCMap();
extern void grtoglPutText();
#ifdef VECTOR_FONTS
extern void grtoglFontText();
#endif
extern void GrTOGLSetCursor();
extern void GrTOGLTextSize();
extern void GrTOGLDrawGlyph();
extern void GrTOGLBitBlt();
extern void NullBitBlt();
extern int GrTOGLReadPixel();
extern void grtoglSetLineStyle();
extern void grtoglSetCharSize();
extern void grtoglSetWMandC();
extern void grtoglRectConvert();
extern void grtoglFillPolygon();

extern void grtoglFreeBackingStore();
extern void grtoglCreateBackingStore();
extern bool grtoglGetBackingStore();
extern bool grtoglScrollBackingStore();
extern void grtoglPutBackingStore();

extern void grtoglDrawLine();
extern void grtoglDrawLines();
extern void grtoglFillRect();
extern void grtoglFillRects();

extern int grtoglNbLines;
extern int grtoglNbDiagonal;
extern int grtoglNbRects;

extern Rect grtoglLines[];
extern Rect grtoglDiagonal[];
extern TOGLRect grtoglRects[];

#define GR_TOGL_FLUSH_LINES() { \
    if (grtoglNbLines>0) { \
	grtoglDrawLines(grtoglLines, grtoglNbLines); \
	grtoglNbLines=0; \
    } \
}

#define GR_TOGL_FLUSH_DIAGONAL() { \
    if (grtoglNbDiagonal>0) { \
	glEnable(GL_LINE_SMOOTH); \
	grtoglDrawLines(grtoglDiagonal, grtoglNbDiagonal); \
	glDisable(GL_LINE_SMOOTH); \
	grtoglNbDiagonal=0; \
    } \
}

#define GR_TOGL_FLUSH_RECTS() { \
    if (grtoglNbRects>0) { \
	grtoglFillRects(grtoglRects, grtoglNbRects); \
	grtoglNbRects=0; \
    } \
}

#define	GR_TOGL_FLUSH_BATCH() {GR_TOGL_FLUSH_LINES(); \
		GR_TOGL_FLUSH_DIAGONAL(); \
		GR_TOGL_FLUSH_RECTS();}

/* Used by the wind3d window */
extern void TOGLEventProc();
