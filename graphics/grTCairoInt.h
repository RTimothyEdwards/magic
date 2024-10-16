/*
 * grTCairoInt.h --
 *
 * Internal definitions for grTCairo[1..5].c.
 *
 * NOTE:  In order for the these defs to work correctly, this file
 * (grTCairoInt.h) must be included after all the Magic .h files and before
 * the X11, OpenGL, and Tk/Tcl .h files.
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 */

#ifndef _MAGIC__GRAPHICS__GRTCAIROINT_H
#define _MAGIC__GRAPHICS__GRTCAIROINT_H

#include "utils/magic.h"

/* Constants
 */
#define M_WIDTH     1023
#define M_HEIGHT    750

#define TCAIRO_BATCH_SIZE   10000

/* Inverted Y axis definition */
#define grTransYs(n) (DisplayHeight(grXdpy, grXscrn)-(n))

/* Current settings for X function parameters */
typedef struct {
	Tk_Font     font;
	Tk_Cursor   cursor;
	int         fontSize;
	int         depth;
	int         maskmod;
	Tk_Window   window;
	Window      windowid;
	MagWindow   *mw;
} TCAIRO_CURRENT;

/* Per-window data held in MagWindow w->w_grdata2 */

typedef struct {
    /* Window surface and context */
    cairo_t *context;
    cairo_surface_t *surface;
    /* Backing store surface and context */
    cairo_t *backing_context;
    cairo_surface_t *backing_surface;
} TCairoData;

/* Used for vertex arrays */
typedef struct {
	Point r_ll, r_ul, r_ur, r_lr;
} TCairoRect;

extern TCAIRO_CURRENT tcairoCurrent;

extern bool grtcairoGetCursorPos();
extern bool grtcairoGetCursorRootPos();
extern bool grtcairoDrawGrid();
extern void GrTCairoEnableTablet();
extern void GrTCairoDisableTablet();
extern void GrTCairoSetCMap();
extern void grtcairoPutText();
#ifdef VECTOR_FONTS
extern void grtcairoFontText();
#endif
extern void GrTCairoSetCursor();
extern int GrTCairoTextSize();
extern void GrTCairoDrawGlyph();
extern void GrTCairoBitBlt();
extern void NullBitBlt();
extern int GrTCairoReadPixel();
extern void grtcairoSetLineStyle();
extern void grtcairoSetCharSize();
extern void grtcairoSetWMandC();
extern void grtcairoRectConvert();
extern void grtcairoFillPolygon();

extern void grtcairoFreeBackingStore();
extern void grtcairoCreateBackingStore();
extern bool grtcairoGetBackingStore();
extern bool grtcairoScrollBackingStore();
extern void grtcairoPutBackingStore();

extern void grtcairoDrawLine();
extern void grtcairoDrawLines();
extern void grtcairoFillRect();
extern void grtcairoFillRects();

extern int grtcairoNbLines;
extern int grtcairoNbDiagonal;
extern int grtcairoNbRects;

extern Rect grtcairoLines[];
extern Rect grtcairoDiagonal[];
extern TCairoRect grtcairoRects[];

#define GR_TCAIRO_FLUSH_LINES() { \
    if (grtcairoNbLines>0) { \
    grtcairoDrawLines(grtcairoLines, grtcairoNbLines); \
    grtcairoNbLines=0; \
    } \
}

#define GR_TCAIRO_FLUSH_DIAGONAL() { \
    if (grtcairoNbDiagonal>0) { \
	grtcairoDrawLines(grtcairoDiagonal, grtcairoNbDiagonal); \
	grtcairoNbDiagonal = 0; \
	} \
}

#define GR_TCAIRO_FLUSH_RECTS() { \
    if (grtcairoNbRects>0) { \
    grtcairoFillRects(grtcairoRects, grtcairoNbRects); \
    grtcairoNbRects=0; \
    } \
}

#define GR_TCAIRO_FLUSH_BATCH() { \
	GR_TCAIRO_FLUSH_LINES(); \
    GR_TCAIRO_FLUSH_DIAGONAL(); \
    GR_TCAIRO_FLUSH_RECTS(); \
}

/* Used by the wind3d window */
extern void TCairoEventProc();

#endif /* _MAGIC__GRAPHICS__GRTCAIROINT_H */
