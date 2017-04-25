/*************************************************************************
 *
 *  lispMagic.c --
 *
 *   This module contains the builtin mini-scheme functions that interact
 *   with magic's data structures.
 *
 *  (c) 1996 California Institute of Technology
 *  Department of Computer Science
 *  Pasadena, CA 91125.
 *
 *  Permission to use, copy, modify, and distribute this software
 *  and its documentation for any purpose and without fee is hereby
 *  granted, provided that the above copyright notice appear in all
 *  copies. The California Institute of Technology makes no representations
 *  about the suitability of this software for any purpose. It is
 *  provided "as is" without express or implied warranty. Export of this
 *  software outside of the United States of America may require an
 *  export license.
 *
 *  $Id: lispMagic.c,v 1.1.1.1 2008/02/03 20:43:50 tim Exp $
 *
 *************************************************************************/

#include <stdio.h>

#include "lisp/lisp.h"
#include "lispInt.h"
#include "lispargs.h"
#include "utils/geofast.h"
#include "textio/textio.h"
#include "utils/malloc.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "utils/main.h"
#include "commands/commands.h"
#include "utils/utils.h"
#include "select/select.h"


static LispObj *_internal_list;

/*------------------------------------------------------------------------
 *
 *  LispGetbox --
 *
 *      Returns a list with (llx,lly,urx,ury) corresponding to the current
 *      box, or #f if the command failed otherwise.
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *
 *------------------------------------------------------------------------
 */

LispObj *
LispGetbox (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  Rect editbox;
  Rect rootBox;
  CellDef *rootBoxDef;

  if (ARG1P(s)) {
    TxPrintf ("Usage: (%s)\n", name);
    RETURN;
  }
  /*
     This code adapted from the commands/CmdAB.c code for
     "box"
     */
  if (!ToolGetBox (&rootBoxDef, &rootBox)) {
    TxPrintf ("%s: Box tool must be present\n", name);
    RETURN;
  }
  if (EditRootDef == rootBoxDef)
    {
      char buf[128];

      (void) ToolGetEditBox(&editbox);
      sprintf (buf, "(%d %d %d %d)", editbox.r_xbot, editbox.r_ybot,
	       editbox.r_xtop, editbox.r_ytop);
      return LispParseString (buf);
    }
  else
    {
      TxPrintf ("%s: box not in cell being edited.\n",name);
      RETURN;
    }
}


/*------------------------------------------------------------------------
 *
 *  LispGetPoint --
 *
 *      Returns a list with (x,y) corresponding to the current point
 *
 *  Results:
 *      Returns result of evaluation.
 *
 *  Side effects:
 *      None.
 *
 *
 *------------------------------------------------------------------------
 */

LispObj *
LispGetPoint (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  MagWindow *w;
  char buf[128];
  Rect editRect, rootRect;
  CellDef *rootDef;
  extern TxCommand *WindCurrentCmd;

  if (ARG1P(s)) {
    TxPrintf ("Usage: (%s)\n", name);
    RETURN;
  }

  if ((w = ToolGetPoint((Point *) NULL, &rootRect)) != (MagWindow *) NULL)
    {
	rootDef = ((CellUse *)w->w_surfaceID)->cu_def;
	if (EditRootDef == rootDef)
	{
	    GeoTransRect(&RootToEditTransform, &rootRect, &editRect);
	    sprintf (buf, "(%d %d %d %d)", editRect.r_xbot, editRect.r_ybot,
		     WindCurrentCmd->tx_p.p_x, WindCurrentCmd->tx_p.p_y);
	}
	else {
	    sprintf (buf, "(%d %d %d %d)", rootRect.r_xbot, rootRect.r_ybot,
		     WindCurrentCmd->tx_p.p_x, WindCurrentCmd->tx_p.p_y);
	}
    }
  else {
    TxPrintf ("%s: point is not in a window\n", name);
    RETURN;
  }
  return LispParseString (buf);
}


/*-----------------------------------------------------------------------------
 *
 *  LispGetPaint --
 *
 *      Get paint under the current box for a particular layer.
 *
 *  Results:
 *      List containing paint description.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
static int
lispprinttile (tile,cxp)
     Tile *tile;
     TreeContext *cxp;
{
  TileType type;
  Transform tt;
  Rect sourceRect, targetRect;
  Rect *arg;
  SearchContext *scx = cxp->tc_scx;
  LispObj *l;
  static  char getpaint_buf[128];

  if ((type = TiGetType(tile)) == TT_SPACE)
	return 0;

  TITORECT(tile, &targetRect);
  
  /* Transform to target coordinates */
  GEOTRANSRECT(&scx->scx_trans, &targetRect, &sourceRect);
  
  /* Clip against the target area */
  arg = (Rect *) cxp->tc_filter->tf_arg;

  GEOCLIP(&sourceRect, arg);

  /* go to edit coordinates from root coordinates */
  GEOTRANSRECT (&RootToEditTransform, &sourceRect, &targetRect);

  sprintf (getpaint_buf, "((\"%s\" %d %d %d %d))", 
	   DBTypeShortName (type),
	   targetRect.r_xbot, targetRect.r_ybot,
	   targetRect.r_xtop, targetRect.r_ytop);
  l = LispParseString (getpaint_buf);
  CDR(LLIST(l)) = _internal_list;
  _internal_list = l;
  return 0;
}
     

LispObj *
LispGetPaint (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  SearchContext scx;
  TileTypeBitMask mask;
  int windowMask, xMask;
  DBWclientRec *crec;
  MagWindow *window;
  CellUse *PaintUse, *u;
  CellDef *def;

  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s str)\n", name);
    RETURN;
  }
  bzero (&scx, sizeof(SearchContext));
  window = ToolGetBoxWindow (&scx.scx_area, &windowMask);
  if (window == NULL) {
    TxPrintf ("%s: Box tool must be present.\n", name);
    RETURN;
  }
  xMask = ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
  if ((windowMask & ~xMask) != 0)
    {
      window = CmdGetRootPoint((Point *) NULL, (Rect *) NULL);
      xMask = ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
      if ((windowMask & xMask) == 0)
	{
	  TxPrintf("The box is in more than one window;  use the cursor\n");
	  TxPrintf("to select the one you want to select from.\n");
	  RETURN;
	}
    }
  if (CmdParseLayers (LSTR(ARG1(s)), &mask)) {
    if (TTMaskEqual (&mask, &DBSpaceBits)) 
      CmdParseLayers ("*,label", &mask);
    TTMaskClearType(&mask,TT_SPACE);
  }
  else {
    TxPrintf ("%s: invalid layer specification\n", name);
    RETURN;
  }
  scx.scx_use = (CellUse *) window->w_surfaceID;
  scx.scx_trans = GeoIdentityTransform;
  crec = (DBWclientRec *) window->w_clientData;

  _internal_list = LispNewObj ();
  LTYPE(_internal_list) = S_LIST;
  LLIST(_internal_list) = NULL;

  (void) DBTreeSrTiles(&scx, &mask, xMask, lispprinttile, 
		       (ClientData) &scx.scx_area);
		       
  return _internal_list;
}


/*-----------------------------------------------------------------------------
 *
 *  LispGetSelPaint --
 *
 *      Get paint in the current selection
 *
 *  Results:
 *      List containing paint description.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispGetSelPaint (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  SearchContext scx;
  TileTypeBitMask mask;

  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s str)\n", name);
    RETURN;
  }
  bzero (&scx, sizeof(SearchContext));
  if (CmdParseLayers (LSTR(ARG1(s)), &mask)) {
    if (TTMaskEqual (&mask, &DBSpaceBits)) 
      CmdParseLayers ("*,label", &mask);
    TTMaskClearType(&mask,TT_SPACE);
  }
  else {
    TxPrintf ("%s: invalid layer specification\n", name);
    RETURN;
  }
  if (SelectUse)
    scx.scx_use = SelectUse;
  else {
    TxPrintf ("%s: no selection present", name);
    RETURN;
  }
  scx.scx_trans = GeoIdentityTransform;
  scx.scx_area = SelectUse->cu_bbox;
  _internal_list = LispNewObj ();
  LTYPE(_internal_list) = S_LIST;
  LLIST(_internal_list) = NULL;
  (void) DBTreeSrTiles(&scx, &mask, 0, lispprinttile, 
		       (ClientData) &scx.scx_area);
  return _internal_list;
}




/*-----------------------------------------------------------------------------
 *
 *  LispGetLabel --
 *
 *      Return a list of labels
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
static int
lispprintlabel (scx, label, tpath, cdarg)
     SearchContext *scx;
     Label *label;
     TerminalPath *tpath;
     ClientData cdarg;
{
  LispObj *l;
  Rect sourceRect, targetRect;
  static  char buf[128];
  char *nm, *p;
  int bad;

  /* Transform to target coordinates */
  GEOTRANSRECT(&scx->scx_trans, &label->lab_rect, &sourceRect);

  bad = 0;
  for (nm = label->lab_text; *nm; nm++)
    if (*nm == '\"')
      bad++;
  if (bad) {
    int i;
    nm = (char *) mallocMagic((unsigned) (strlen(label->lab_text)+1+bad));
    p = nm;
    for (i=0; label->lab_text[i]; i++) {
      if (label->lab_text[i] == '\"')
	*p++ = '\\';
      *p++ = label->lab_text[i];
    }
    *p = '\0';
  }
  else
    nm = label->lab_text;

  /* go to edit coords from root coords */
  GEOTRANSRECT (&RootToEditTransform, &sourceRect, &targetRect);
  
  sprintf (buf, "((\"%s\" \"%s\" %d %d %d %d))",
	   nm,
	   DBTypeShortName (label->lab_type),
	   targetRect.r_xbot, targetRect.r_ybot,
	   targetRect.r_xtop, targetRect.r_ytop);
  if (bad)
    freeMagic(nm);
  l = LispParseString (buf);
  CDR(LLIST(l)) = _internal_list;
  _internal_list = l;
  return 0;
}
     
LispObj *
LispGetLabel (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  SearchContext scx;
  TileTypeBitMask mask;
  int windowMask, xMask;
  DBWclientRec *crec;
  MagWindow *window;
  CellUse *PaintUse, *u;
  CellDef *def;

  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s str)\n", name);
    RETURN;
  }
  bzero (&scx, sizeof(SearchContext));
  window = ToolGetBoxWindow (&scx.scx_area, &windowMask);
  if (window == NULL) {
    TxPrintf ("%s: Box tool must be present.\n", name);
    RETURN;
  }
  xMask = ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
  if ((windowMask & ~xMask) != 0)
    {
      window = CmdGetRootPoint((Point *) NULL, (Rect *) NULL);
      xMask = ((DBWclientRec *) window->w_clientData)->dbw_bitmask;
      if ((windowMask & xMask) == 0)
	{
	  TxPrintf("The box is in more than one window;  use the cursor\n");
	  TxPrintf("to select the one you want to select from.\n");
	  RETURN;
	}
    }
  CmdParseLayers ("*,space,label", &mask);
  scx.scx_use = (CellUse *) window->w_surfaceID;
  scx.scx_trans = GeoIdentityTransform;
  crec = (DBWclientRec *) window->w_clientData;

  _internal_list = LispNewObj ();
  LTYPE(_internal_list) = S_LIST;
  LLIST(_internal_list) = NULL;

  (void) DBSearchLabel (&scx, &mask, xMask, LSTR(ARG1(s)), 
			lispprintlabel, (ClientData) &scx.scx_area);

  return _internal_list;
}


/*-----------------------------------------------------------------------------
 *
 *  LispGetSelLabel --
 *
 *      Return a list of labels in current selection
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispGetSelLabel (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  SearchContext scx;
  TileTypeBitMask mask;

  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_STRING || ARG2P(s)) {
    TxPrintf ("Usage: (%s str)\n", name);
    RETURN;
  }
  bzero (&scx, sizeof(SearchContext));
  CmdParseLayers ("*,label", &mask);
  TTMaskClearType(&mask,TT_SPACE);
  if (SelectUse)
    scx.scx_use = SelectUse;
  else {
    TxPrintf ("%s: no selection present", name);
    RETURN;
  }
  scx.scx_trans = GeoIdentityTransform;
  scx.scx_area = SelectUse->cu_bbox;

  _internal_list = LispNewObj ();
  LTYPE(_internal_list) = S_LIST;
  LLIST(_internal_list) = NULL;

  (void) DBSearchLabel (&scx, &mask, 0, LSTR(ARG1(s)), 
			lispprintlabel, (ClientData) &scx.scx_area);

  return _internal_list;
}


/*-----------------------------------------------------------------------------
 *
 *  LispGetCellNames --
 *
 *      Get cell names and their locations in the current edit cell
 *
 *  Results:
 *      List containing cell description
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
static
int lispprintcell (use,cdarg)
     CellUse *use;
     ClientData cdarg;
{
  static char cellbuffer[1024];
  LispObj *l;

  sprintf (cellbuffer, "((\"%s\" %d %d %d %d %d %d %d %d %d %d))",
	   use->cu_id,
	   use->cu_bbox.r_xbot, use->cu_bbox.r_ybot,
	   use->cu_bbox.r_xtop, use->cu_bbox.r_ytop,
	   use->cu_xlo, use->cu_xhi,
	   use->cu_ylo, use->cu_yhi,
	   use->cu_xsep, use->cu_ysep);
  
  l = LispParseString (cellbuffer);
  
  CDR(LLIST(l)) = _internal_list;
  _internal_list = l;
  return 0;
}


LispObj *
LispGetCellNames (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  static char cellbuffer[1024];
  LispObj *l;
  
  if (ARG1P(s)) {
    TxPrintf ("Usage: (%s)\n", name);
    RETURN;
  }
  
  _internal_list = LispNewObj ();
  LTYPE(_internal_list) = S_LIST;
  LLIST(_internal_list) = NULL;

  (void) DBCellEnum (EditCellUse ?  EditCellUse->cu_def :  EditRootDef,
		     lispprintcell, (ClientData) NULL);
  
  return _internal_list;
}



/*-----------------------------------------------------------------------------
 *
 *  LispEvalMagic --
 *
 *      Force its argument to be a magic function
 *
 *  Results:
 *      None.
 *
 *  Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

LispObj *
LispEvalMagic (name,s,f)
     char *name;
     Sexp *s;
     Sexp *f;
{
  LispObj *l;
  if (!ARG1P(s) || LTYPE(ARG1(s)) != S_SYM || ARG2P(s)) {
    TxPrintf ("Usage: (%s symbol)\n",name);
    RETURN;
  }
  l = LispCopyObj(ARG1(s));
  LTYPE(l) = S_MAGIC_BUILTIN;
  return l;
}

