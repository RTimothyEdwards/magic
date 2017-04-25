/*
 * grTkCommon.h ---
 *
 *	Header information common to the Tk and TOGL interfaces
 *
 */

#define MAX_CURSORS 32

#define grSmallFont     grTkFonts[0]
#define grMediumFont    grTkFonts[1]
#define grLargeFont     grTkFonts[2]
#define grXLargeFont    grTkFonts[3]

/* Macro for conversion between X and Magic coordinates
 * (X11/OpenGL independent)
 */

#define grXtransY(w, y) (w->w_allArea.r_ytop - (y))

/*
 * Our default fonts for Tk (X11-style naming conventions).
 */

#define TK_DEFAULT_FONT "9x15"

#define TK_FONT_SMALL   "-*-helvetica-medium-r-normal--10-*-75-75-p-*-iso8859-*"
#define TK_FONT_MEDIUM  "-*-helvetica-medium-r-normal--14-*-75-75-p-*-iso8859-*"
#define TK_FONT_LARGE   "-*-helvetica-medium-r-normal--18-*-75-75-p-*-iso8859-*"
#define TK_FONT_XLARGE  "-*-helvetica-medium-r-normal--24-*-75-75-p-*-iso8859-*"

extern void grTkDefineCursor();
extern void grTkFreeCursors();

extern void grtkFreeBackingStore();
extern void grtkCreateBackingStore();
extern bool grtkGetBackingStore();
extern bool grtkScrollBackingStore();
extern void grtkPutBackingStore();

extern Tk_Font grTkFonts[4];
extern Tk_Cursor grCursors[MAX_CURSORS];

extern Display *grXdpy;
extern int      grXscrn;

