/*
 * selUnselect.c --
 *
 * the program you can't see in your mirror.  And why
 * does it only come around after dark?
 *
 */

#include <stdio.h>

#include "utils/magic.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "tiles/tile.h"
#include "utils/hash.h"
#include "database/database.h"
#include "windows/windows.h"
#include "dbwind/dbwind.h"
#include "commands/commands.h"
#include "utils/main.h"
#include "select/select.h"
#include "select/selInt.h"
#include "utils/malloc.h"
#include "textio/textio.h"


/*
 * ----------------------------------------------------------------------------
 *
 * selUnselFunc --
 *
 *	This function is used by SelRemoveSel2; it is passed to DBSrPaintArea as
 *	the search function for a search of the select2 cell, and erases
 *	equivalent areas of paint in the select cell (in effect, it deletes
 *	the contents of select2 from select).
 *
 * Results:
 *	Always returns zero (so the search will continue).
 *
 * Side effects:
 *	Paint is removed from the select cell.
 *
 * ----------------------------------------------------------------------------
 */

int
selUnselFunc(tile, arg)
     Tile *tile;
     ClientData *arg;
{
  TileType type;
  Rect rect;
  
  if ((type = TiGetType(tile)) >= DBNumUserLayers) return 0;
  TiToRect(tile, &rect);
  DBErase(SelectDef, &rect, type);
  return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * selRemoveCellFunc --
 *
 *	This procedure hides some (limited number of) cell uses for later
 *	munging; it is a search procedure for DBCellSrArea.
 *
 * Results:
 *	Always aborts array enumeration (we only need one placement in order
 *	to rip out the whole array).  Aborts the search once MAXUNSELUSES
 *	cell uses have been hidden away.
 *
 * Side effects:
 *	Cell uses are entered in the array selRemoveUses; the variable
 *	selNRemove is incremented.
 *
 * ----------------------------------------------------------------------------
 */

#define MAXUNSELUSES 3
static CellUse *(selRemoveUses[MAXUNSELUSES]);
static int selNRemove;

int
selRemoveCellFunc(scx, cdarg)
     SearchContext *scx;
     Rect *cdarg;

{
  ASSERT((selNRemove < MAXUNSELUSES) && (selNRemove >= 0),
	 "selRemoveCellFunc(selNRemove)");
  selRemoveUses[selNRemove] = scx->scx_use;
  GeoIncludeAll(&scx->scx_use->cu_bbox, cdarg);
  if (++selNRemove >= MAXUNSELUSES) return 1;
  else return 2;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelRemoveArea --
 *
 *	Remove a rectangular chunk of the select cell, possibly masked
 *	by a user-specified mask (which may include the pseudo-levels
 *	L_CELL and L_LABEL).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paint, labels, and/or cell uses may be removed from the select cell.
 *	The selection highlights are redrawn, and undo checkpoints are saved,
 *	so this thrilling process may be undone or redone.  The select cell's
 *	bounding box is updated.
 *
 * ----------------------------------------------------------------------------
 */

void
SelRemoveArea(area, mask)
     Rect *area;
     TileTypeBitMask *mask;
{
  SearchContext scx;
  Rect bbox, areaReturn;

  /* get ready; save checkpoint for undo. */

  SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);

  /* defenestrate a few labels.  Do this before the paint, or	*/
  /* we will not have a proper calculation of the bounding box	*/
  /* of the erasure for rendered text.				*/

  areaReturn = *area;
  if (TTMaskHasType(mask, L_LABEL))
	DBEraseLabel(SelectDef, area, &DBAllTypeBits, &areaReturn);
  else
	DBEraseLabel(SelectDef, area, mask, &areaReturn);

  /* erase ordinary paint in area */

  DBEraseMask(SelectDef, area, mask);

  /* now blast away at cells; we do this a tiny bit at a time, as in
     selectClear.  We search, and remember up to MAXUNSELUSES cell
     placements in each search; then we rip out those placements, and
     search further.  As always, thou shalt not rip out what thou
     searchest, or thine database shall be corrupt, and thy name
     anathema in the ears of thy mask designers.  */

  bbox = *area;
  if (TTMaskHasType(mask, L_CELL))
    {
      scx.scx_use = SelectUse;
      scx.scx_trans = GeoIdentityTransform;
      scx.scx_area = *area;
      while (TRUE)
	{
	  int i;
	  
	  selNRemove = 0;
	  (void) DBCellSrArea(&scx, selRemoveCellFunc, (ClientData) &bbox);
	  for (i = 0; i < selNRemove; i++)
	    {
	      if (selectLastUse == selRemoveUses[i])
		selectLastUse = (CellUse *) NULL;
	      DBUnLinkCell(selRemoveUses[i], SelectDef);
	      DBDeleteCell(selRemoveUses[i]);
	      (void) DBCellDeleteUse(selRemoveUses[i]);
	    }
	  if (selNRemove < MAXUNSELUSES) break;
	}
    }

  /* now remember stuff for redo (and fill in info for undo), redraw highlights,
     recompute the bounding box on the off chance it has changed, tell the
     database we've mucked around with the select cell, then go home. */

  SelRememberForUndo(FALSE, SelectRootDef, &bbox);
  GeoInclude(&areaReturn, &bbox);
  DBWHLRedraw(SelectRootDef, &bbox, TRUE);
  DBReComputeBbox(SelectDef);
  DBWAreaChanged(SelectDef, &bbox, DBW_ALLWINDOWS, (TileTypeBitMask *) NULL);
}

/*
 * ----------------------------------------------------------------------------
 *
 * selRemoveLabelPaintFunc --
 *
 *	Put labels in the select2 cell.  The SelRemoveSel2 function runs through
 *	all the labels in the select cell, and calls DBSrPaintArea on select2 for
 *	each one.  If there is (suitable) paint in select2 under the label, this
 *	function gets called, and places the label in select2.
 *
 * Results:
 *	Always returns one (and thus aborts the search).
 *
 * Side effects:
 *	Labels may appear in select2.
 *
 * ----------------------------------------------------------------------------
 */

int
selRemoveLabelPaintFunc(tile, label)
     Tile *tile;
     Label *label;
{
  (void) DBPutFontLabel(Select2Def, &label->lab_rect, label->lab_font,
	label->lab_size, label->lab_rotate, &label->lab_offset,
	label->lab_just, label->lab_text, label->lab_type,
	label->lab_flags);

  return 1;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelRemoveSel2 --
 *	Run through the select2 cell, removing corresponding paint and labels
 *	from the select cell.
 *
 * Results:
 *	Should always return zero; returns 1 if there is a problem traversing
 *	select2.
 *
 * Side effects:
 *	Paint and labels (but not cell uses) may be deleted from the select
 *	cell.  The calling procedure is responsible for updating highlighting
 *	and undo information.
 *
 *	Labels may be placed in the select2 cell; SelRemoveSel2 assumes that
 *	there are no labels in select2 when it is called.
 *
 * ----------------------------------------------------------------------------
 */

int
SelRemoveSel2()

{
  int plane;
  Label *label;

  /* Enumerate paint tiles in select2; selUnselFunc will erase corresponding
     pieces in the select cell. */

  for (plane = PL_TECHDEPBASE; plane < DBNumPlanes; plane++)
    {
      if (DBSrPaintArea((Tile *) NULL, Select2Def->cd_planes[plane],
			&TiPlaneRect, &DBAllButSpaceAndDRCBits, selUnselFunc,
			(ClientData) NULL) != 0)
	return 1;
    }

  /* Enumerate labels in select; selRemoveLabelPaintFunc will place a copy of
     the label in select2 if it finds appropriate paint underneath the label. */

  ASSERT(Select2Def->cd_labels == (Label *) NULL, "SelRemoveSel2 labels");

  for (label = SelectDef->cd_labels;
       label != (Label *) NULL;
       label = label->lab_next)
    {
      Rect area, searchArea;

      if (label->lab_type == TT_SPACE) continue;
      area = label->lab_rect;
      GEO_EXPAND(&area, 1, &searchArea);
      (void) DBSrPaintArea((Tile *) NULL,
			   Select2Def->cd_planes[DBPlane(label->lab_type)],
			   &searchArea, &DBConnectTbl[label->lab_type],
			   selRemoveLabelPaintFunc,
			   (ClientData) label);
    }

  /* Now run through the labels we just copied, and delete them from
     the select cell.  */

  for (label = Select2Def->cd_labels;
       label != (Label *) NULL;
       label = label->lab_next)
    DBEraseLabelsByContent(SelectDef, &label->lab_rect, -1, label->lab_text);
  return 0;
}

typedef struct
{
  CellUse *ed_use, *sel_use;
  Transform *orient;
} SelRemoveCellArgs;

/*
 * ----------------------------------------------------------------------------
 *
 * SelRemoveCellSearchFunc --
 *	find the cell use in the select cell which matches a given
 *	cell use in the root def.
 *
 * Results:
 *	Returns 1 to abort the search if it finds a match.  Otherwise
 *	returns zero.
 *
 * Side effects:
 *	fills in the sel_use field of its client argument if it finds a match.
 *
 * ----------------------------------------------------------------------------
 */

int
SelRemoveCellSearchFunc(scx, cdarg)
     SearchContext *scx;
     SelRemoveCellArgs *cdarg;
{
  Transform *et, *st;

  /* To match, cell uses must point to the same cell def. */

  if (scx->scx_use->cu_def != cdarg->ed_use->cu_def)
    return 0;

  /* If these usages are in the same orientation, at the same
     location, they match.  To check this, we compare the
     search context transformation with the transformation
     computed earlier for the usage in the edit cell. */

  st = &scx->scx_trans;
  et = cdarg->orient;
  if ((st->t_a == et->t_a) &&
      (st->t_b == et->t_b) &&
      (st->t_c == et->t_c) &&
      (st->t_d == et->t_d) &&
      (st->t_e == et->t_e) &&
      (st->t_f == et->t_f))
    {
      cdarg->sel_use = scx->scx_use;
      return 1;
    }
  return 0;
}

/*
 * ----------------------------------------------------------------------------
 *
 * SelectRemoveCellUse --
 *	remove the cell use in the select cell which matches the given
 *	use from the root def.
 *
 * Results:
 *	Returns 1 if no such use was found; returns zero otherwise.
 *
 * Side effects:
 *	If SelectRemoveCellUse returns 1, there are no side effects.
 *	Otherwise:  undo/redo markers will be created, one cell use
 *	will be deleted from the select cell, the select cell's bounding
 *	box will be recomputed, the highlights redrawn, and the area
 *	which had been covered by the cell use will be marked as
 *	changed.  If selectLastUse was pointing to the use, it will
 *	be set to NULL, so the select cycling code will not try
 *	to deselect this (now trashed) cell use.
 *
 * ----------------------------------------------------------------------------
 */

int
SelectRemoveCellUse(use, trans)
     CellUse *use;
     Transform *trans;

{
  SearchContext scx;
  CellUse selectedUse;
  SelRemoveCellArgs args;

  /* The search context is the area covered by the cell's bounding box in
     the select cell. */

  scx.scx_use = SelectUse;
  GEOTRANSRECT(trans, &use->cu_def->cd_bbox, &scx.scx_area);
  scx.scx_trans = GeoIdentityTransform;
  args.ed_use = use;
  args.orient = trans;

  /* DBCellSrArea will return all of the cells overlapping this box;
     often, this is just one cell.  If the search runs to completion
     (is not aborted), we did not find a match, so we quit, returning
     1 as a failure indication.  */

  if (DBCellSrArea(&scx, SelRemoveCellSearchFunc, (ClientData) &args) == 0)
    return 1;

  /* remunge the selectLastUse Horrid Side Effect Pointer */

  if (selectLastUse == args.sel_use)
    selectLastUse = (CellUse *) NULL;

  /* Now remove the cell use (with appropriate undo and database
     incantations). */

  SelRememberForUndo(TRUE, (CellDef *) NULL, (Rect *) NULL);
  DBUnLinkCell(args.sel_use, SelectDef);
  DBDeleteCell(args.sel_use);
  (void) DBCellDeleteUse(args.sel_use);
  SelRememberForUndo(FALSE, SelectRootDef, &scx.scx_area);
  DBWHLRedraw(SelectRootDef, &scx.scx_area, TRUE);
  DBReComputeBbox(SelectDef);
  DBWAreaChanged(SelectDef, &scx.scx_area,
		 DBW_ALLWINDOWS, (TileTypeBitMask *) NULL);
  return 0;
}

