/*
 * W3Dmain.c --
 *
 * Procedures to interface the 3D rendering window with the window package
 * for the purposes of window creation, deletion, and modification.
 *
 * Copyright 2003 Open Circuit Design, Inc., for MultiGiG Ltd.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This file is only applicable for OpenGL-enabled graphics;  THREE_D is */
/* defined by the "make config" process and includes all preconditions.	 */
#ifdef THREE_D

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <GL/gl.h>
#include <GL/glx.h>

#include "tcltk/tclmagic.h"
#include "utils/magic.h"
#include "utils/geometry.h"
#include "windows/windows.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "utils/undo.h"
#include "database/database.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "graphics/wind3d.h"
#include "graphics/graphicsInt.h"
#include "graphics/graphics.h"
#include "grTOGLInt.h"
#include "textio/textio.h"
#include "textio/txcommands.h"
#include "utils/utils.h"
#include "utils/styles.h"
#include "dbwind/dbwtech.h"
#include "dbwind/dbwind.h"
#include "extract/extract.h"
#include "graphics/glyphs.h"
#include "utils/malloc.h"
#include "windows/windInt.h"		/* for access to redisplay pointer */
#include "cif/cif.h"
#include "cif/CIFint.h"		/* access to CIFPlanes, CIFCurStyle, etc. */

extern Display     *grXdpy;		/* X11 display */
extern GLXContext  grXcontext;		/* OpenGL/X11 interface def. */
extern XVisualInfo *grVisualInfo;	/* OpenGL preferred visual */
extern char 	   *MainDisplayType;	/* make sure we're using OpenGL */
extern HashTable   grTOGLWindowTable;
extern int	   grXscrn;
extern int	   grCurFill;

static bool w3dNeedStyle;
static bool w3dIsLocked;
static int  w3dStyle;
static MagWindow *w3dWindow;
extern bool grDriverInformed;

extern void grInformDriver();

global WindClient W3DclientID;

#define glTransYs(n)   (DisplayHeight(grXdpy, grXscrn)-(n))

/* forward declarations */

void W3DCIFredisplay();
void W3Dredisplay();
void Set3DDefaults();
void w3drefreshFunc();
bool W3Ddelete();

/* ------------------------Low-Level Routines--------------------------------- */

void
w3dLock(w)
    MagWindow *w;
{
    grSimpleLock(w, TRUE);
    w3dSetProjection(w);
}

void
w3dUnlock(w)
    MagWindow *w;
{
    glFlush();
    glFinish();
    
    glDisable(GL_CULL_FACE);

    glDisable(GL_COLOR_MATERIAL);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_POLYGON_SMOOTH);

    grSimpleUnlock(w);
}

/* -------------------Low-Level Drawing Routines------------------------------ */

void
w3dFillEdge(bbox, r, ztop, zbot)
    Rect *bbox;				/* tile bounding box */
    Rect *r;
    float ztop;
    float zbot;
{
    float ztmp;
    float xbot = (float)r->r_xbot;
    float ybot = (float)r->r_ybot;
    float xtop = (float)r->r_xtop;
    float ytop = (float)r->r_ytop;

    if (ytop == bbox->r_ybot || xbot == bbox->r_xtop)
    {
	/* reverse z top and bottom so rectangle is drawn */
	/* counterclockwise as seen from the outside.	  */

	ztmp = zbot;
	zbot = ztop;
	ztop = ztmp;
    }

    glBegin(GL_POLYGON);
    glVertex3f(xbot, ybot, zbot);
    glVertex3f(xbot, ybot, ztop);
    glVertex3f(xtop, ytop, ztop);
    glVertex3f(xtop, ytop, zbot);
    glEnd();
}

void
w3dFillPolygon(p, np, zval, istop)
    Point *p;
    int np;
    float zval;
    bool istop;
{
    int i;

    glBegin(GL_POLYGON);
    if (istop)	/* counterclockwise */
	for (i = 0; i < np; i++)
	    glVertex3f((float)(p[i].p_x), (float)(p[i].p_y), zval);
    else	/* clockwise */
	for (i = np - 1; i >= 0; i--)
	    glVertex3f((float)(p[i].p_x), (float)(p[i].p_y), zval);

    glEnd();
}

void
w3dFillTile(r, zval, istop)
    Rect *r;
    float zval;
    bool istop;
{
    float xbot, ybot, xtop, ytop;

    ybot = (float)r->r_ybot;
    ytop = (float)r->r_ytop;

    if (istop)
    {
	xbot = (float)r->r_xbot;
	xtop = (float)r->r_xtop;
    }
    else	/* makes path counterclockwise as seen from the bottom */
    {
	xbot = (float)r->r_xtop;
	xtop = (float)r->r_xbot;
    }

    glBegin(GL_POLYGON);
    glVertex3f(xbot, ybot, zval);
    glVertex3f(xtop, ybot, zval);
    glVertex3f(xtop, ytop, zval);
    glVertex3f(xbot, ytop, zval);
    glEnd();
}

void
w3dFillXSide(xstart, xend, yval, ztop, zbot)
    float xstart, xend, yval, ztop, zbot;
{
    glBegin(GL_POLYGON);
    glVertex3f(xstart, yval, zbot);
    glVertex3f(xstart, yval, ztop);
    glVertex3f(xend, yval, ztop);
    glVertex3f(xend, yval, zbot);
    glEnd();
}

void
w3dFillYSide(xval, ystart, yend, ztop, zbot)
    float xval, ystart, yend, ztop, zbot;
{
    glBegin(GL_POLYGON);
    glVertex3f(xval, ystart, zbot);
    glVertex3f(xval, ystart, ztop);
    glVertex3f(xval, yend, ztop);
    glVertex3f(xval, yend, zbot);
    glEnd();
}

/* This routine assumes that vector (x1,y1)->(x2,y2) is in the   */
/* counterclockwise direction with respect to the tile interior. */

void
w3dFillDiagonal(x1, y1, x2, y2, ztop, zbot)
    float x1, y1, x2, y2, ztop, zbot;
{
    glBegin(GL_POLYGON);
    glVertex3f(x1, y1, zbot);
    glVertex3f(x2, y2, zbot);
    glVertex3f(x2, y2, ztop);
    glVertex3f(x1, y1, ztop);
    glEnd();
}

void
w3dFillOps(trans, tile, cliprect, ztop, zbot)
    Transform *trans;
    Tile *tile;
    Rect *cliprect;
    float ztop;
    float zbot;
{
    LinkedRect *tilesegs, *segptr;
    Rect r, r2;
    float xbot, ybot, xtop, ytop;
    Point p[5];
    int np;

    r2.r_xbot = LEFT(tile);
    r2.r_ybot = BOTTOM(tile);
    r2.r_xtop = RIGHT(tile);
    r2.r_ytop = TOP(tile);

    GeoTransRect(trans, &r2, &r);

    if (IsSplit(tile))
    {
	Rect fullr;
	TileType dinfo;

	dinfo = DBTransformDiagonal(TiGetTypeExact(tile), trans);

	fullr = r;
	if (cliprect != NULL)
	    GeoClip(&r, cliprect);

	GrClipTriangle(&fullr, &r, cliprect != NULL, dinfo, p, &np);
	
	if (np > 0)
	{
	    w3dFillPolygon(p, np, ztop, TRUE);
	    w3dFillPolygon(p, np, zbot, FALSE);
	}
    }
    else
    {
	/* Clip the tile area to the clipping area */
	if (cliprect != NULL)
	    GeoClip(&r, cliprect);

	/* draw tile top and bottom */
	if (!GEO_RECTNULL(&r))
	{
	    w3dFillTile(&r, ztop, TRUE);
	    w3dFillTile(&r, zbot, FALSE);
	}
    }

    /* If height is zero, don't bother to draw sides */
    if (ztop == zbot) return;

    /* Find tile outline and render sides */

    if (GrBoxOutline(tile, &tilesegs))
    {
	xbot = (float)r.r_xbot;
	ybot = (float)r.r_ybot;
	xtop = (float)r.r_xtop;
	ytop = (float)r.r_ytop;

	if (r.r_xtop != r.r_xbot)
	{
	    w3dFillXSide(xtop, xbot, ybot, ztop, zbot);
	    w3dFillXSide(xbot, xtop, ytop, ztop, zbot);
	}
	if (r.r_ytop != r.r_ybot)
	{
	    w3dFillYSide(xbot, ybot, ytop, ztop, zbot);
	    w3dFillYSide(xtop, ytop, ybot, ztop, zbot);
	}
    }
    else
    {
	for (segptr = tilesegs; segptr != NULL; segptr = segptr->r_next)
	{
	    GeoTransRect(trans, &segptr->r_r, &r2);
	    if (cliprect != NULL)
	    {
		if (GEO_OVERLAP(cliprect, &r2))
		{
		    GeoClip(&r2, cliprect);
		    w3dFillEdge(&r, &r2, ztop, zbot);
		}
	    }
	    else
		w3dFillEdge(&r, &r2, ztop, zbot);
	    freeMagic(segptr);
	}

	/* For non-manhattan tiles, GrBoxOutline only returns	*/
	/* the manhattan edges.  This leaves the (possibly	*/
	/* clipped) diagonal edge to render.			*/

	if (IsSplit(tile))
	{
	    int cp;
	    for (cp = 0; cp < np - 1; cp++)
	    {
		if ((p[cp].p_x != p[cp + 1].p_x) && (p[cp].p_y != p[cp + 1].p_y))
		{
		    w3dFillDiagonal((float)p[cp].p_x, (float)p[cp].p_y,
				(float)p[cp + 1].p_x, (float)p[cp + 1].p_y,
				ztop, zbot);
		    break;
		}
	    }
	    if (cp == (np - 1))
		if ((p[cp].p_x != p[0].p_x) && (p[cp].p_y != p[0].p_y))
		    w3dFillDiagonal((float)p[cp].p_x, (float)p[cp].p_y,
				(float)p[0].p_x, (float)p[0].p_y,
				ztop, zbot);
	}

	/* Render edges cut by the clipping area, if they're	*/
	/* inside the tile, so the tile doesn't look "hollow".	*/

	if (cliprect != NULL)
	{
	    xbot = (float)r.r_xbot;
	    ybot = (float)r.r_ybot;
	    xtop = (float)r.r_xtop;
	    ytop = (float)r.r_ytop;

	    if (r.r_ytop > r.r_ybot)
	    {
		if (r.r_xtop == cliprect->r_xtop)
		    w3dFillYSide(xtop, ytop, ybot, ztop, zbot);
		if (r.r_xbot == cliprect->r_xbot)
		    w3dFillYSide(xbot, ybot, ytop, ztop, zbot);
	    }
	    if (r.r_xtop > r.r_xbot)
	    {
		if (r.r_ytop == cliprect->r_ytop)
		    w3dFillXSide(xbot, xtop, ytop, ztop, zbot);
		if (r.r_ybot == cliprect->r_ybot)
		    w3dFillXSide(xtop, xbot, ybot, ztop, zbot);
	    }
	}
    }
}

void
w3dRenderVolume(tile, trans, cliprect)
    Tile *tile;
    Transform *trans;
    Rect *cliprect;
{
    float zbot, ztop;
    float ftop = 0.0, fthk = 0.0;
    W3DclientRec *crec;

    crec = (W3DclientRec *)(w3dWindow->w_clientData);

    ExtGetZAxis(tile, &ftop, &fthk);

    /* Negate z-axis values for OpenGL display */
    ztop = -ftop * crec->scale_z;
    zbot = ztop - (fthk * crec->scale_z);

    GR_CHECK_LOCK();
    if (!grDriverInformed)	
	grInformDriver();

    if ((grCurFill == GR_STSOLID) || (grCurFill == GR_STSTIPPLE))
    {
	w3dFillOps(trans, tile, cliprect, ztop, zbot);
    }

    /* To do:  Outlines and contact crosses */
    /* To do:  Labels			    */
}

void
w3dRenderCIF(tile, layer, trans)
    Tile *tile;
    CIFLayer *layer;
    Transform *trans;
{
    float zbot, ztop;
    float ftop, fthk;
    W3DclientRec *crec;

    crec = (W3DclientRec *)(w3dWindow->w_clientData);

    ftop = layer->cl_height;
    fthk = layer->cl_thick;

    /* Negate z-axis values for OpenGL display */
    ztop = -ftop * crec->scale_z;
    zbot = ztop - (fthk * crec->scale_z);

    GR_CHECK_LOCK();
    if (!grDriverInformed)	
	grInformDriver();

    if ((grCurFill == GR_STSOLID) || (grCurFill == GR_STSTIPPLE))
    {
	w3dFillOps(trans, tile, NULL, ztop, zbot);
    }

    /* To do:  Outlines */
    /* To do:  Labels	*/
}

void
w3dClear()
{
    float fr, fg, fb;
    int cidx, lr, lb, lg;

    /* STYLE_TRANSPARENT uses the background color definition,	*/
    /* so we get RGB values from there.				*/

    cidx = GrStyleTable[STYLE_TRANSPARENT].color;
    GrGetColor(cidx, &lr, &lg, &lb);

    fr = ((GLfloat)lr / 255);
    fg = ((GLfloat)lg / 255);
    fb = ((GLfloat)lb / 255);

    glClearColor(fr, fg, fb, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* -------------------High-Level Drawing Routines------------------------------ */

void
w3dSetProjection(w)
    MagWindow *w;
{
    W3DclientRec *crec;
    Window wind;
    GLfloat light0_pos[] = { 0.0, 0.0, 0.0, 0.0 };	/* light #0 position */
    GLfloat light0_amb[] = { 0.4, 0.4, 0.4, 1.0 };	/* light #0 ambient int. */
    GLfloat light0_dif[] = { 0.0, 0.0, 0.0, 1.0 };	/* light #0 diffuse int. */

    GLfloat light1_pos[] = { 50.0, 50.0, 50.0, 1.0 };	/* light #1 position */
    GLfloat light1_amb[] = { 0.0, 0.0, 0.0, 1.0 };	/* light #1 ambient int. */
    GLfloat light1_dif[] = { 1.0, 1.0, 1.0, 1.0 };	/* light #1 diffuse int. */

    crec = (W3DclientRec *) w->w_clientData;
    wind = Tk_WindowId((Tk_Window)w->w_grdata);
    if (wind == 0) return;	/* window was closed by window manager? */

    /* Should not mess with surfaceArea---we want the area to be displayed */
    /* in crec, in magic internal coordinates, set by the edit box (else-  */
    /* where).  Set the viewport to maintain 1:1 x:y aspect ratio on the   */
    /* rendered volume.							   */

    glXMakeCurrent(grXdpy, (GLXDrawable)wind, grXcontext);

    if (crec->level > 0)
    {
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_POLYGON_SMOOTH);
    }
    
    /* Need to look into dealing properly with double-buffered graphics */
    glDrawBuffer(GL_FRONT);
    /* glDrawBuffer(GL_BACK); */

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    /* Lighting */

    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);

    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);

    glLightfv(GL_LIGHT0, GL_POSITION, light0_pos);	/* positional */
    glLightfv(GL_LIGHT0, GL_AMBIENT, light0_amb);	/* ambient */
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_dif);	/* diffuse */

    glLightfv(GL_LIGHT1, GL_POSITION, light1_pos);	/* directional */
    glLightfv(GL_LIGHT1, GL_AMBIENT, light1_amb);	/* ambient */
    glLightfv(GL_LIGHT1, GL_DIFFUSE, light1_dif);	/* diffuse */

    /* Fill front-facing polygons only */

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    /* Preserve aspect ratio of 1:1 for internal units on the display */

    glScalef((GLfloat)crec->height / (GLfloat)crec->width, 1.0, 1.0);
    glViewport((GLsizei)0, (GLsizei)0, (GLsizei)crec->width, (GLsizei)crec->height);

    /* Projection matrix manipulations */

    glScalef(crec->scale_xy, crec->scale_xy, crec->prescale_z);

    glRotatef(crec->view_x, 1.0, 0.0, 0.0);
    glRotatef(crec->view_y, 0.0, 1.0, 0.0);
    glRotatef(crec->view_z, 0.0, 0.0, 1.0);

    glTranslatef(crec->trans_x, crec->trans_y, crec->trans_z);
}

/* Magic layer tile painting function */

int
w3dPaintFunc(tile, cxp)
    Tile *tile;			/* Tile to be displayed */
    TreeContext *cxp;		/* From DBTreeSrTiles */
{
    SearchContext *scx = cxp->tc_scx;

    /* Allow display interrupt a la dbwPaintFunc().	*/
    /* HOWEVER, note that GrEventPendingPtr looks at	*/
    /* events in the window defined by toglCurrent,	*/
    /* which is not the 3D window.  So rendering in the	*/
    /* 3D window can only be interrupted by events in	*/
    /* the layout window.				*/

    /* Honor display suspend status */
    if (GrDisplayStatus == DISPLAY_SUSPEND) return 0;

    if (GrDisplayStatus == DISPLAY_BREAK_PENDING)
    {
	GrDisplayStatus = DISPLAY_IN_PROGRESS;
	if (GrEventPendingPtr)
	{
	    if ((*GrEventPendingPtr)())
		sigOnInterrupt(0);
	    else
		SigSetTimer(0);
	}
    }

    if (!w3dIsLocked)
    {
	w3dLock(w3dWindow);
	w3dIsLocked = TRUE;
    }
    if (w3dNeedStyle)
    {
	GrSetStuff(w3dStyle);
	w3dNeedStyle = FALSE;
    }

    w3dRenderVolume(tile, &scx->scx_trans, &scx->scx_area);
    return 0;			/* keep the search going! */
}

/* CIF layer tile painting function */

int
w3dCIFPaintFunc(tile, arg)
    Tile *tile;			/* Tile to be displayed */
    ClientData *arg;		/* is NULL */
{
    CIFLayer *layer = (CIFLayer *)arg;

    /* Honor display suspend status */
    if (GrDisplayStatus == DISPLAY_SUSPEND) return 0;

    /* Allow display interrupt a la dbwPaintFunc() */
    if (GrDisplayStatus == DISPLAY_BREAK_PENDING)
    {
	GrDisplayStatus = DISPLAY_IN_PROGRESS;
	if (GrEventPendingPtr)
	{
	    if ((*GrEventPendingPtr)())
		sigOnInterrupt(0);
	    else
		SigSetTimer(0);
	}
    }

    if (!w3dIsLocked)
    {
	w3dLock(w3dWindow);
	w3dIsLocked = TRUE;
    }
    if (w3dNeedStyle)
    {
	/* TxError("CIF layer 0x%x (%s) render style %d\n",
		layer,
		layer->cl_name,
		layer->cl_renderStyle); */
	GrSetStuff(layer->cl_renderStyle + TECHBEGINSTYLES);
	w3dNeedStyle = FALSE;
    }

    w3dRenderCIF(tile, layer, &GeoIdentityTransform);
    return 0;			/* keep the search going! */
}

/* -----------------------Command Procedures------------------------------ */

/*
 * ----------------------------------------------------------------------------
 *
 * w3dHelp --
 *
 *	Print the list of commands available in this window.
 *
 * ----------------------------------------------------------------------------
 */

void
w3dHelp(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    char **msg;
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc == 1)
    {
	TxPrintf("\nWind3D command summary:\n");
	for (msg = WindGetCommandTable(W3DclientID); *msg != NULL; msg++)
	{
	    TxPrintf("    %s\n", *msg);
	}
	TxPrintf("\nType '?' in the window to get a key macro summary.\n");
    }
    else
	TxError("Usage: help\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dCutBox --
 *
 *	Set a clipping box for the 3D view
 *
 * ----------------------------------------------------------------------------
 */

void
w3dCutBox(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    bool hide = FALSE;
    int lidx = 1, num_layers;
    TileTypeBitMask mask;
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc == 1 || cmd->tx_argc == 2 || cmd->tx_argc == 5)
    {
	if (cmd->tx_argc == 1)
	{
	    if (crec->clipped)
	    {
		Tcl_Obj *rlist = Tcl_NewListObj(0, NULL);
		Tcl_ListObjAppendElement(magicinterp, rlist,
			Tcl_NewIntObj((int)(crec->cutbox.r_xbot)));
		Tcl_ListObjAppendElement(magicinterp, rlist,
			Tcl_NewIntObj((int)(crec->cutbox.r_ybot)));
		Tcl_ListObjAppendElement(magicinterp, rlist,
			Tcl_NewIntObj((int)(crec->cutbox.r_xtop)));
		Tcl_ListObjAppendElement(magicinterp, rlist,
			Tcl_NewIntObj((int)(crec->cutbox.r_ytop)));

		Tcl_SetObjResult(magicinterp, rlist);
	    }
	    else
		Tcl_SetResult(magicinterp, "none", NULL);
	}
	else if (cmd->tx_argc == 2)
	{
	    if (!strcmp(cmd->tx_argv[1], "none"))
		crec->clipped = FALSE;
	    if (!strcmp(cmd->tx_argv[1], "box"))
	    {
		Rect rootBox;
		CellDef *rootBoxDef;
		CellDef *cellDef = ((CellUse *)w->w_surfaceID)->cu_def;

		if (ToolGetBox(&rootBoxDef, &rootBox))
		{
		    if (rootBoxDef == cellDef)
		    {
			crec->clipped = TRUE;
			crec->cutbox = rootBox;
		    }
		}
	    }
	    w3drefreshFunc(w);
	}
	else
	{
	    if (StrIsInt(cmd->tx_argv[1]) &&	
			StrIsInt(cmd->tx_argv[2]) &&	
			StrIsInt(cmd->tx_argv[3]) &&	
			StrIsInt(cmd->tx_argv[4]))
	    {
		crec->clipped = TRUE;
		crec->cutbox.r_xbot = atoi(cmd->tx_argv[1]);
		crec->cutbox.r_ybot = atoi(cmd->tx_argv[2]);
		crec->cutbox.r_xtop = atoi(cmd->tx_argv[3]);
		crec->cutbox.r_ytop = atoi(cmd->tx_argv[4]);
		w3drefreshFunc(w);
	    }
	}
    }
    else
	TxError("Usage: cutbox [none|box|llx lly urx ur]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dSeeLayers --
 *
 *	See or hide layers from the 3D view
 *
 * ----------------------------------------------------------------------------
 */

void
w3dSeeLayers(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    bool hide = FALSE;
    int lidx = 1, num_layers;
    TileTypeBitMask mask;
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc == 2 || cmd->tx_argc == 3)
    {
	if (cmd->tx_argc == 3)
	{
	    lidx = 2;
	    if (!strcmp(cmd->tx_argv[1], "no")) hide = TRUE;
	}

	if (crec->cif)
	{
	    /* If CIF layers, match the layer name (1 only) */
	    if (!CIFNameToMask(cmd->tx_argv[lidx], &mask))
		return;
	}
	else
	{
	    /* If normal layers, pick up the name with TechType */
	    if (!CmdParseLayers(cmd->tx_argv[lidx], &mask))
		return;
	}
	if (hide)
	    TTMaskClearMask(&crec->visible, &mask);
	else
	    TTMaskSetMask(&crec->visible, &mask);

	w3drefreshFunc(w);
    }
    else
	TxError("Usage: see [no] layer\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dClose --
 *
 *	Close the 3D display.  This corresponds to the command-line command
 *	"closewindow" and overrides the default window client command of the
 *	same name.
 *
 * ----------------------------------------------------------------------------
 */

void
w3dClose(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc == 1)
	(void) WindDelete(w);
    else
	TxError("Usage: closewindow\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dRescale --
 *
 *	Change scale and translation by indicated scalefactor.
 *
 * ----------------------------------------------------------------------------
 */

void
w3dRescale(crec, scalefactor)
    W3DclientRec *crec;
    float scalefactor; 
{
    crec->scale_xy /= scalefactor;
    crec->prescale_z /= scalefactor;

    crec->scale_z *= scalefactor;
    crec->trans_y *= scalefactor;
    crec->trans_x *= scalefactor;
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dToggleCIF --
 *
 *	Change between CIF and magic layer views
 *
 * ----------------------------------------------------------------------------
 */

void
w3dToggleCIF(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc == 1)
    {

	if ((crec->cif == FALSE) && (CIFCurStyle != NULL))
	{
	    ((clientRec *)(W3DclientID))->w_redisplay = W3DCIFredisplay;
	    crec->cif = TRUE;
	    w3dRescale(crec, (float)CIFCurStyle->cs_scaleFactor);
	}
	else if (crec->cif == TRUE)
	{
	    ((clientRec *)(W3DclientID))->w_redisplay = W3Dredisplay;
	    crec->cif = FALSE;
	    w3dRescale(crec, 1.0 / (float)CIFCurStyle->cs_scaleFactor);
	}

	w3drefreshFunc(w);
    }
    else
	TxError("Usage: cif\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dLevel --
 *
 *	Change rendering level
 *
 * ----------------------------------------------------------------------------
 */

void
w3dLevel(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc == 2)
    {
	if (StrIsInt(cmd->tx_argv[1]))
	    crec->level = atoi(cmd->tx_argv[1]);
	else if (!strcmp(cmd->tx_argv[1], "up"))
	    crec->level++;
	else if (!strcmp(cmd->tx_argv[1], "down"))
	    crec->level--;
	else
	{
	    TxError("Usage: level [<n>|up|down]\n");
	    return;
	}

	if (crec->level < 0) crec->level = 0;
	w3drefreshFunc(w);
    }
    else if (cmd->tx_argc == 1)
    {
	Tcl_SetObjResult(magicinterp, Tcl_NewIntObj((int)crec->level));
    }
    else
	TxError("Usage: level [n]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * Function to perform refresh on the entire 3D window
 *
 * ----------------------------------------------------------------------------
 */

void
w3drefreshFunc(mw)
    MagWindow *mw;
{
    W3DclientRec *crec = (W3DclientRec *) mw->w_clientData;
    Rect screenRect;

    screenRect.r_xbot = 0;
    screenRect.r_ybot = 0;
    screenRect.r_xtop = crec->width;
    screenRect.r_ytop = crec->height;

    WindAreaChanged(mw, &screenRect);
    WindUpdate();
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dDefaults --
 *
 *	Revert to display defaults
 *
 * ----------------------------------------------------------------------------
 */

void
w3dDefaults(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc == 1)
    {
	Set3DDefaults(w, crec);
	w3drefreshFunc(w);
    }
    else
	TxError("Usage: defaults\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dRefresh --
 *
 *	Refresh the display
 *
 * ----------------------------------------------------------------------------
 */

void
w3dRefresh(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc == 1)
	w3drefreshFunc(w);
    else
	TxError("Usage: refresh\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dView --
 *
 *	Set the viewing angle into the 3D display.  Overrides the window
 *	client command of the same name.
 *
 * ----------------------------------------------------------------------------
 */

void
w3dView(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    bool relative = FALSE;
    int argc = cmd->tx_argc;
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (argc == 5)
    {
	argc--;
	if (!strncmp(cmd->tx_argv[argc], "rel", 3))
	    relative = TRUE;
	else if (strncmp(cmd->tx_argv[argc], "abs", 3))
	{
	    TxError("Usage: view angle_x angle_y angle_z absolute|relative\n");
	    return;
	}
    }

    if (argc == 4)
    {
	/* Pick up x, y, z angles */

	if (StrIsNumeric(cmd->tx_argv[1]) &&
	    StrIsNumeric(cmd->tx_argv[2]) &&
	    StrIsNumeric(cmd->tx_argv[3]))
	{
	    if (relative)
	    {
		crec->view_x += (float)atof(cmd->tx_argv[1]);
		crec->view_y += (float)atof(cmd->tx_argv[2]);
		crec->view_z += (float)atof(cmd->tx_argv[3]);
	    }
	    else
	    {
		crec->view_x = (float)atof(cmd->tx_argv[1]);
		crec->view_y = (float)atof(cmd->tx_argv[2]);
		crec->view_z = (float)atof(cmd->tx_argv[3]);
	    }

	    /* Call redisplay function */
	    w3drefreshFunc(w);
	}
    }
    else if (argc == 1)
    {
	Tcl_Obj *vlist = Tcl_NewListObj(0, NULL);
	Tcl_ListObjAppendElement(magicinterp, vlist,
			Tcl_NewDoubleObj((double)(crec->view_x)));
	Tcl_ListObjAppendElement(magicinterp, vlist,
			Tcl_NewDoubleObj((double)(crec->view_y)));
	Tcl_ListObjAppendElement(magicinterp, vlist,
			Tcl_NewDoubleObj((double)(crec->view_z)));

	Tcl_SetObjResult(magicinterp, vlist);
    }
    else
	TxError("Usage: view [angle_x angle_y angle_z [relative|absolute]]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dScroll --
 *
 *	Set the viewing position into the 3D display.
 *
 * ----------------------------------------------------------------------------
 */

void
w3dScroll(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    bool relative = FALSE;
    int argc = cmd->tx_argc;
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (argc == 5)
    {
	argc--;
	if (!strncmp(cmd->tx_argv[argc], "rel", 3))
	    relative = TRUE;
	else if (strncmp(cmd->tx_argv[argc], "abs", 3))
	{
	    TxError("Usage: scroll pos_x pos_y pos_z absolute|relative\n");
	    return;
	}
    }

    if (argc == 4)
    {
	/* Pick up x, y, z translations */

	if (StrIsNumeric(cmd->tx_argv[1]) &&
	    StrIsNumeric(cmd->tx_argv[2]) &&
	    StrIsNumeric(cmd->tx_argv[3]))
	{
	    if (relative)
	    {
		crec->trans_x += (float)atof(cmd->tx_argv[1]) / crec->scale_xy;
		crec->trans_y += (float)atof(cmd->tx_argv[2]) / crec->scale_xy;
		crec->trans_z += (float)atof(cmd->tx_argv[3]) / crec->scale_xy;
	    }
	    else
	    {
		crec->trans_x = (float)atof(cmd->tx_argv[1]);
		crec->trans_y = (float)atof(cmd->tx_argv[2]);
		crec->trans_z = (float)atof(cmd->tx_argv[3]);
	    }

	    /* Call redisplay function */
	    w3drefreshFunc(w);
	}
    }
    else if (argc == 1)
    {
	Tcl_Obj *vlist = Tcl_NewListObj(0, NULL);
	Tcl_ListObjAppendElement(magicinterp, vlist,
			Tcl_NewDoubleObj((double)(crec->trans_x)));
	Tcl_ListObjAppendElement(magicinterp, vlist,
			Tcl_NewDoubleObj((double)(crec->trans_y)));
	Tcl_ListObjAppendElement(magicinterp, vlist,
			Tcl_NewDoubleObj((double)(crec->trans_z)));

	Tcl_SetObjResult(magicinterp, vlist);
    }
    else
	TxError("Usage: scroll [pos_x pos_y pos_z [absolute|relative]]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dZoom --
 *
 *	Set the viewing scale of the 3D display.  Overrides the window
 *	client command of the same name.
 *
 * ----------------------------------------------------------------------------
 */

void
w3dZoom(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    bool relative = FALSE;
    int argc = cmd->tx_argc;
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (argc == 4)
    {
	argc--;
	if (!strncmp(cmd->tx_argv[argc], "rel", 3))
	    relative = TRUE;
	else if (strncmp(cmd->tx_argv[argc], "abs", 3))
	{
	    TxError("Usage: zoom scale_xy scale_z relative|absolute\n");
	    return;
	}
    }

    if (argc == 3)
    {
	/* Pick up xy and z scales */

	if (StrIsNumeric(cmd->tx_argv[1]) &&
	    StrIsNumeric(cmd->tx_argv[2]))
	{
	    float xy, z;
	    xy = (float)atof(cmd->tx_argv[1]);
	    z = (float)atof(cmd->tx_argv[2]);
	    if ((xy <= 0) || (z <= 0))
	    {
		TxError("Error: zoom values/factors must be positive and nonzero\n");
		return;
	    }
	    if (relative)
	    {
		crec->scale_xy *= xy;
		crec->scale_z *= z;
	    }
	    else
	    {
		crec->scale_xy = xy;
		crec->scale_z = z;
	    }

	    /* Call redisplay function */
	    w3drefreshFunc(w);
	}
    }
    else if (cmd->tx_argc == 1)
    {
	Tcl_Obj *vlist = Tcl_NewListObj(0, NULL);
	Tcl_ListObjAppendElement(magicinterp, vlist,
			Tcl_NewDoubleObj((double)(crec->scale_xy)));
	Tcl_ListObjAppendElement(magicinterp, vlist,
			Tcl_NewDoubleObj((double)(crec->scale_z)));

	Tcl_SetObjResult(magicinterp, vlist);
    }
    else
	TxError("Usage: zoom [scale_xy scale_z [relative|absolute]]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * w3dRenderValues --
 *
 *	Set values for 3D rendering.  These are independent of any
 *	extraction or other physical meaning, so we allow them to
 *	be independently controlled here.
 *
 * ----------------------------------------------------------------------------
 */

void
w3dRenderValues(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int lidx;
    CIFLayer *layer;
    W3DclientRec *crec = (W3DclientRec *) w->w_clientData;

    if (cmd->tx_argc > 1)
    {
	for (lidx = 0; lidx < CIFCurStyle->cs_nLayers; lidx++)
	{
	    layer = CIFCurStyle->cs_layers[lidx];
	    if (!strcmp(layer->cl_name, cmd->tx_argv[1]))
		break;
	}

	if (lidx == CIFCurStyle->cs_nLayers)
	{
	    TxError("Unknown CIF layer \"%s\"\n", cmd->tx_argv[1]);
	    return;
	}
    }

    if (cmd->tx_argc == 2)
    {
	Tcl_Obj *llist;
	llist = Tcl_NewListObj(0, NULL);
	Tcl_ListObjAppendElement(magicinterp, llist,
			Tcl_NewDoubleObj((double)(layer->cl_height)));
	Tcl_ListObjAppendElement(magicinterp, llist,
			Tcl_NewDoubleObj((double)(layer->cl_thick)));
	Tcl_ListObjAppendElement(magicinterp, llist,
			Tcl_NewIntObj((int)(layer->cl_renderStyle)));
	Tcl_SetObjResult(magicinterp, llist);
    }
    else if (cmd->tx_argc == 4 || cmd->tx_argc == 5)
    {
	int style;
	float height, thick;

	style = -1;
	if (cmd->tx_argc == 5 && StrIsInt(cmd->tx_argv[4]))
	    style = atoi(cmd->tx_argv[4]);
	if (!StrIsNumeric(cmd->tx_argv[3]) || !StrIsNumeric(cmd->tx_argv[2]))
	    goto badusage;
	height = (float)atof(cmd->tx_argv[2]);
	thick = (float)atof(cmd->tx_argv[3]);

	/* It is necessary to update ALL layers of the same name */
	for (lidx = 0; lidx < CIFCurStyle->cs_nLayers; lidx++)
	{
	    layer = CIFCurStyle->cs_layers[lidx];
	    if (!strcmp(layer->cl_name, cmd->tx_argv[1]))
	    {
		if (style >= 0) layer->cl_renderStyle = style;
		layer->cl_height = height;
		layer->cl_thick = thick;
	    }
	}

	/* Call redisplay function */
	w3drefreshFunc(w);
    }
    else

badusage:
	TxError("Usage: render name [height thick [style]]\n");
}

/*
 * ----------------------------------------------------------------------------
 *
 * Set3DDefaults --
 *
 *	Set default values for the viewing parameters (scale, angle,
 *	and translation in 3 dimensions)
 *
 *	Default view is top-down (layout) view scaled to fit the celldef's
 *	bounding box in the 3D window.
 *
 * ----------------------------------------------------------------------------
 */

void
Set3DDefaults(mw, crec)
    MagWindow *mw;
    W3DclientRec *crec;
{
    int height, width;
    int centerx, centery;
    float scalex, scaley;

    /* Get translation and scale from the cell bounding box	*/
    /* (= window's bounding box, set in loadWindow proc)	*/

    height = mw->w_bbox->r_ytop - mw->w_bbox->r_ybot;
    width = mw->w_bbox->r_xtop - mw->w_bbox->r_xbot;
    centerx = -(mw->w_bbox->r_xbot + (width >> 1));
    centery = -(mw->w_bbox->r_ybot + (height >> 1));

    scalex = 2.0 / ((float)width * 1.1);   /* Add 10% margins in x and y */
    scaley = 2.0 / ((float)height * 1.1);

    crec->trans_x = (float)centerx;
    crec->trans_y = (float)centery;
    crec->trans_z = 0.0;

    crec->scale_xy = (scalex > scaley) ? scaley : scalex;
    crec->scale_z = 25.0;	/* Height exaggerated by 25x */

    /* The small z-scale value is necessary to keep large layout distances	*/
    /* from exceeding the OpenGL (-1,1) limits of the z-axis.  Distances in Z,	*/
    /* like layer thickness, simply scale up to compensate.			*/

    crec->prescale_z = 0.0001;

    /* layout view (top down) */

    crec->view_x = 0.0;
    crec->view_y = 0.0;
    crec->view_z = 0.0;

    TTMaskZero(&crec->visible);
    TTMaskSetMask(&crec->visible, &DBAllTypeBits);

    /* Scale all factors appropriately for CIF display */

    if (crec->cif == TRUE)
	w3dRescale(crec, (float)CIFCurStyle->cs_scaleFactor);

    /* Default is no clipping */
    crec->clipped = FALSE;	/* no clipping by default */
}

/* -----------------------Client Procedures------------------------------ */

/*
 * ----------------------------------------------------------------------------
 *
 * W3DloadWindow --
 *
 *	Replace the root cell of a window by the specified cell.
 *
 * Results:
 *	TRUE if successful, FALSE if not.  The 3D rendering window is
 *	not allowed to create new cells, only to render existing ones.
 *
 * Side effects:
 *	None.
 *
 * ----------------------------------------------------------------------------
 */

bool
W3DloadWindow(window, name)
    MagWindow *window;
    char *name;
{
    CellDef *newEditDef;
    CellUse *newEditUse;
    Rect loadBox;

    newEditDef = DBCellLookDef(name);
    if (newEditDef == (CellDef *)NULL)
	return FALSE;

    if (!DBCellRead(newEditDef, (char *)NULL, TRUE, NULL))
	return FALSE;

    DBReComputeBbox(newEditDef);
    loadBox = newEditDef->cd_bbox;

    /* Attach cell to window */

    newEditUse = DBCellNewUse(newEditDef, (char *) NULL);
    (void) StrDup(&(newEditUse->cu_id), "3D rendered cell");

    window->w_bbox = &(newEditUse->cu_def->cd_bbox);
    return WindLoad(window, W3DclientID, (ClientData)newEditUse, &loadBox);
}

/*
 * ----------------------------------------------------------------------------
 *
 * W3Dcreate --
 *
 * A new window has been created.  Create and initialize the needed 
 * structures.
 *
 * Results:
 *	FALSE if we have too many windows, TRUE otherwise.
 *
 * Side effects:
 *	Initialize the window to be editing the background color.
 *
 * ----------------------------------------------------------------------------
 */

bool
W3Dcreate(window, argc, argv)
    MagWindow *window;
    int argc;
    char *argv[];
{
    W3DclientRec *crec;
    Tk_Window tkwind, tktop;
    Window wind;    
    Colormap colormap;
    HashEntry *entry;
    CellDef *boxDef;
    Rect box;
    bool result;
    char *name = NULL;

    /* At least for now, there's only one 3D window allowed.		*/

    if (w3dWindow != NULL)
    {
	TxError("Only one 3D window allowed.\n");
	return FALSE;
    }

    /* The 3D rendering, of course, *only* works with OpenGL, so in an	*/
    /* executable compiled for multiple graphics interfaces, we need	*/
    /* to make sure that our display type is OpenGL.			*/

    if (!GrIsDisplay(MainDisplayType, "OGL"))
    {
	TxError("Display type is \"%s\".  OpenGL is required for the 3D display.\n",	
		MainDisplayType);
	TxError("Please restart magic with option \"-d OGL\".\n");
	return FALSE;
    }
 
    crec = (W3DclientRec *) mallocMagic(sizeof(W3DclientRec));

    /* The MagWindow structure and frameArea indicates the cross-sectional */
    /* area of the layout to be rendered in the 3D display window.	   */

    /* Need to parse the argument list here. . .  At least one argument	*/
    /* should allow the Tk path name to be passed to the routine, as	*/
    /* it is for the standard layout window in the Tk interface.	*/ 

    /* Set surface area, etc. of the MagWindow. . . ? */

    /* Initial window height and width (should be passed as optional	*/
    /* arguments to the specialopen command)				*/

    crec->width = 500;
    crec->height = 500;

    /* Rendering level (0 = coarse & fast, 1 = finer but slower, etc.	*/
    /* Render cif by default.						*/

    crec->level = 1;
    crec->cif = TRUE;

    window->w_clientData = (ClientData) crec;
    window->w_flags &= ~(WIND_SCROLLABLE | WIND_SCROLLBARS | WIND_CAPTION |
		WIND_BORDER | WIND_COMMANDS);

    /* Load the current cellDef into the window */

    if ((argc > 0) && (strlen(argv[0]) > 0))
	result = W3DloadWindow(window, argv[0]);
    else if (ToolGetBox(&boxDef, &box))
	result = W3DloadWindow(window, boxDef->cd_name);
    else
    {
	MagWindow *mw = NULL;

	windCheckOnlyWindow(&mw, DBWclientID);
	if (mw != NULL)
	{
	    boxDef = ((CellUse *)mw->w_surfaceID)->cu_def;
	    result = W3DloadWindow(window, boxDef->cd_name);
	}
	else
	{
	    TxError("Ambiguous directive:  Put cursor box in one of the windows.\n");
	    return FALSE;
	}
    }

    if (result == FALSE)
    {
	TxError("Cells cannot be created in the 3D window.\n");
	return result;
    }

    /* Generate the window. */

    colormap = XCreateColormap(grXdpy, RootWindow(grXdpy, DefaultScreen(grXdpy)),
		grVisualInfo->visual, AllocNone);

    if (!(tktop = Tk_MainWindow(magicinterp))) return FALSE;
	
    /* Check for a Tk pathname for the window;  allows window to be	*/
    /* by a Tk GUI script.						*/
    if (argc > 1) name = argv[1];

    if (name == NULL)
       tkwind = Tk_CreateWindowFromPath(magicinterp, tktop, ".magic3d", "");
    else
       tkwind = Tk_CreateWindowFromPath(magicinterp, tktop, name, NULL);

    if (tkwind != 0)
    {
	window->w_grdata = (ClientData) tkwind;
	entry = HashFind(&grTOGLWindowTable, (char *)tkwind);
	HashSetValue(entry, window);

	if (name != NULL)
	{
	    /* Visual type must match what we chose in the graphics init proc */
	    Tk_SetWindowVisual(tkwind, grVisualInfo->visual, toglCurrent.depth,
			colormap);
	    Tk_MapWindow(tkwind);
	    Tk_GeometryRequest(tkwind, crec->width, crec->height);

	    wind = Tk_WindowId(tkwind);
	    if (wind == 0)
	    glXMakeCurrent(grXdpy, (GLXDrawable)wind, grXcontext);
	}

	/* execute any pending Tk events */

	while (Tcl_DoOneEvent(TCL_DONT_WAIT) != 0);

	/* use the OpenGL Tk event handler (see grTOGL1.c) for the 3d window */

	Tk_CreateEventHandler(tkwind, ExposureMask | StructureNotifyMask |
		ButtonPressMask | KeyPressMask,
		(Tk_EventProc *)TOGLEventProc, (ClientData)tkwind);

	w3dWindow = window;

	/* Use Tcl to pass commands to the window */
	MakeWindowCommand((name == NULL) ? ".magic3d" : name, window);
	
	/* Now that a cell is loaded, set default values for the	*/
	/* client record based on the cell bounding box.		*/

	Set3DDefaults(window, crec);

	return TRUE;
    }

    TxError("Could not create a new Tk window\n");
    return FALSE;
}

/*
 * ----------------------------------------------------------------------------
 *
 * W3Ddelete --
 *
 * Clean up the data structures before deleting a window.
 *
 * Results:
 *	TRUE if we really want to delete the window, FALSE otherwise.
 *
 * Side effects:
 *	A W3DclientRec is freed.
 *
 * ----------------------------------------------------------------------------
 */

bool
W3Ddelete(window)
    MagWindow *window;
{
    W3DclientRec *cr;
    Tk_Window xw;

    cr = (W3DclientRec *) window->w_clientData;
    xw = (Tk_Window)window->w_grdata;
    w3dWindow = NULL;

    freeMagic((char *)cr);
    window->w_clientData = (ClientData)NULL;
    xw = (Tk_Window)window->w_grdata;
    /* Tk_DestroyWindow(xw); */
    return (TRUE);
}

/*
 * ----------------------------------------------------------------------------
 *
 * W3Dredisplay --
 *
 * Redisplay a portion of the 3D rendered layout view
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Redisplay is done.
 * ----------------------------------------------------------------------------
 */

void
W3Dredisplay(w, rootArea, clipArea)
    MagWindow *w;	/* The window containing the area. */
    Rect *rootArea;	/* Ignore this---area defined by window with box */
    Rect *clipArea;	/* Ignore this, too */
{
    W3DclientRec *crec;
    CellDef *cellDef;
    SearchContext scontext;
    Rect largerArea, *clipRect = &largerArea;
    int i;
    TileTypeBitMask *mask, layers;

    w3dLock(w);

    crec = (W3DclientRec *) w->w_clientData;
    cellDef = ((CellUse *)w->w_surfaceID)->cu_def;

    if (crec->clipped)
	clipRect = &crec->cutbox;

    if (rootArea != NULL)
        largerArea = *rootArea;
    else
	largerArea = w->w_surfaceArea;

    largerArea.r_xbot--;
    largerArea.r_ybot--;
    largerArea.r_xtop++;
    largerArea.r_ytop++;

    scontext.scx_area = *clipRect;
    scontext.scx_use = ((CellUse *)w->w_surfaceID);
    scontext.scx_x = scontext.scx_y = -1;
    scontext.scx_trans = GeoIdentityTransform;

    w3dClear();

    /* follow the same locking procedure as DBWredisplay() */
    w3dUnlock(w);
    w3dIsLocked = FALSE;
    for (i = 0; i < DBWNumStyles; i++)
    {
	/* This should probably be redesigned. . . */
	mask = DBWStyleToTypes(i);
	TTMaskAndMask3(&layers, mask, &crec->visible);
	if (!TTMaskIsZero(&layers))
	{
	    w3dStyle = i + TECHBEGINSTYLES;
	    w3dNeedStyle = TRUE;
	    (void) DBTreeSrTiles(&scontext, &layers, 0,
			w3dPaintFunc, (ClientData) NULL);
	    if (w3dIsLocked)
	    {
		w3dUnlock(w);
		w3dIsLocked = FALSE;
	    }
	}
    }
}


/*
 * ----------------------------------------------------------------------------
 *
 * W3DCIFredisplay --
 *
 * Redisplay a portion of the 3D rendered layout, in CIF layers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Redisplay is done.
 * ----------------------------------------------------------------------------
 */

void
W3DCIFredisplay(w, rootArea, clipArea)
    MagWindow *w;	/* The window containing the area. */
    Rect *rootArea;	/* Ignore this---area defined by window with box */
    Rect *clipArea;	/* Ignore this, too */
{
    W3DclientRec *crec;
    SearchContext scx;
    CellDef *cellDef;
    Rect clipRect;
    int i;
    TileTypeBitMask *mask;

    w3dLock(w);

    crec = (W3DclientRec *) w->w_clientData;
    cellDef = ((CellUse *)w->w_surfaceID)->cu_def;

    clipRect = (crec->clipped) ? crec->cutbox : cellDef->cd_bbox;
    GEO_EXPAND(&clipRect, CIFCurStyle->cs_radius, &scx.scx_area);

    /* The following is basically a copy of CIFSeeLayer() */

    CIFErrorDef = cellDef;
    CIFInitCells();
    UndoDisable();
    CIFDummyUse->cu_def = cellDef;
    scx.scx_use = CIFDummyUse;
    scx.scx_trans = GeoIdentityTransform;
    (void) DBTreeSrTiles(&scx, &DBAllButSpaceAndDRCBits, 0,
	cifHierCopyFunc, (ClientData) CIFComponentDef);
    CIFGen(CIFComponentDef, &clipRect, CIFPlanes, &DBAllTypeBits, TRUE, TRUE);
    DBCellClearDef(CIFComponentDef);

    w3dClear();

    /* follow the same locking procedure as DBWredisplay() */
    w3dUnlock(w);
    w3dIsLocked = FALSE;

    for (i = 0; i < CIFCurStyle->cs_nLayers; i++)
    {
	if (TTMaskHasType(&crec->visible, i))
	{
	    w3dNeedStyle = TRUE;
	    DBSrPaintArea((Tile *) NULL, CIFPlanes[i], &TiPlaneRect,
			&CIFSolidBits, w3dCIFPaintFunc,
			(ClientData)(CIFCurStyle->cs_layers[i]));

	    if (w3dIsLocked)
	    {
		w3dUnlock(w);
		w3dIsLocked = FALSE;
	    }
	}
    }
    UndoEnable();
}

/*
 * ----------------------------------------------------------------------------
 *
 * W3Dcommand --
 *
 *	This procedure is invoked by the window package whenever a
 *	command is typed while the cursor is over a 3D window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	TBD
 *
 * ----------------------------------------------------------------------------
 */

void
W3Dcommand(w, cmd)
    MagWindow *w;
    TxCommand *cmd;
{
    int cmdNum;

    switch (cmd->tx_button)
    {
        case TX_NO_BUTTON:
	    WindExecute(w, W3DclientID, cmd);
	    break;
        case TX_LEFT_BUTTON:
        case TX_RIGHT_BUTTON:
        case TX_MIDDLE_BUTTON:
	    /* No action---we shouldn't be here anyway. */
	    break;
	default:
	    ASSERT(FALSE, "W3Dcommand");
    }
    UndoNext();
}

/*
 * ----------------------------------------------------------------------------
 *
 * W3Dinit --
 *
 *	Add the 3D rendering window client to the window module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Add ourselves as a client to the window package.
 *
 * ----------------------------------------------------------------------------
 */

void
W3Dinit()
{
    W3DclientID = WindAddClient("wind3d", W3Dcreate, W3Ddelete,
			W3DCIFredisplay, W3Dcommand,
			(void(*)())NULL, (bool(*)())NULL,
			(void(*)())NULL, (GrGlyph *)NULL);

    /* Register commands with the client */

    WindAddCommand(W3DclientID,
	"view [x y z]	specify viewpoint angle",
	w3dView, FALSE);
    WindAddCommand(W3DclientID,
	"scroll [x y z]	specify viewpoint position",
	w3dScroll, FALSE);
    WindAddCommand(W3DclientID,
	"zoom [xy z]		specify render volume scale",
	w3dZoom, FALSE);
    WindAddCommand(W3DclientID,
	"refresh		refresh 3D display",
	w3dRefresh, FALSE);
    WindAddCommand(W3DclientID,
	"cif			switch to/from CIF layers display",
	w3dToggleCIF, FALSE);
    WindAddCommand(W3DclientID,
	"level [<n>|up|down]	set rendering level",
	w3dLevel, FALSE);
    WindAddCommand(W3DclientID,
	"defaults		revert to defaults",
	w3dDefaults, FALSE);
    WindAddCommand(W3DclientID,
	"closewindow		close the 3D display",	
	w3dClose, FALSE);
    WindAddCommand(W3DclientID,
	"render name [height thick [style]]\n"
	"			properties of CIF layer rendering",
	w3dRenderValues, FALSE);
    WindAddCommand(W3DclientID,
	"see [no] layer	view or hide layers from the 3D view",
	w3dSeeLayers, FALSE);
    WindAddCommand(W3DclientID,
	"cutbox [none|box|llx lly urx ury]\n"
	"			set clipping rectangle for 3D view",
	w3dCutBox, FALSE);
    WindAddCommand(W3DclientID,
	"help		print this command list",
	w3dHelp, FALSE);
}

#endif  /* THREE_D */
