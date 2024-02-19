// ************************************************************************
//
// Copyright (c) 1995-2002 Juniper Networks, Inc. All rights reserved.
//
// Permission is hereby granted, without written agreement and without
// license or royalty fees, to use, copy, modify, and distribute this
// software and its documentation for any purpose, provided that the
// above copyright notice and the following three paragraphs appear in
// all copies of this software.
//
// IN NO EVENT SHALL JUNIPER NETWORKS, INC. BE LIABLE TO ANY PARTY FOR
// DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
// ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
// JUNIPER NETWORKS, INC. HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
//
// JUNIPER NETWORKS, INC. SPECIFICALLY DISCLAIMS ANY WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
// NON-INFRINGEMENT.
//
// THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND JUNIPER
// NETWORKS, INC. HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT,
// UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
//
// ************************************************************************



/* bpMain.c
 *
 * Top-level routines for BPlanes
 * (interface to other modules)
 *
 * See bpEnum.c for enum routines.
 * See bpTcl.c for tcl-level interface.
 */

#include <stdio.h>
#include <stddef.h>
#include "utils/utils.h"
#include "utils/malloc.h"
#include "database/database.h"
#include "textio/textio.h"
#include "utils/geometry.h"
#include "bplane/bplaneInt.h"

/*
 * ----------------------------------------------------------------------------
 * BPNew --
 *
 * Return newly created BPlane.
 *
 * ----------------------------------------------------------------------------
 */
BPlane *BPNew(void)
{
  BPlane *new;

  new = (BPlane *)mallocMagic(sizeof(BPlane));

  new->bp_bbox = GeoNullRect;
  new->bp_bbox_exact = TRUE;

  new->bp_count = 0;

  /* ENUMS */
  new->bp_enums = NULL;

  /* HASH TABLE */
  new->bp_hashTable = IHashInit(4, /* initial buckets */
				offsetof(Element, e_rect), /* key */
				offsetof(Element, e_hashLink),
				IHash4WordKeyHash,
				IHash4WordKeyEq);

  /* IN BOX */
  new->bp_inBox = NULL;

  /* BINS */
  new->bp_binLife = 0;
  new->bp_inAdds = 0;
  new->bp_binArea = GeoNullRect;
  new->bp_rootNode = NULL;

  return new;
}

/*
 * ----------------------------------------------------------------------------
 * BPFree --
 *
 * free (empty) BPlane
 *
 * ----------------------------------------------------------------------------
 */
void BPFree(BPlane *bp)
{
  ASSERT(bp->bp_count == 0,"BPFree");
  IHashFree(bp->bp_hashTable);
  freeMagic((char *)bp);
}

/*
 * ----------------------------------------------------------------------------
 * BPAdd --
 *
 * Add element to the given bplane
 *
 * NOTE: e_rect better be canonical!
 *
 * ----------------------------------------------------------------------------
 */
void BPAdd(BPlane *bp, void *element)
{
  int size;
  int binDim;
  Element * e = element;
  Rect *r = &e->e_rect;

  /* Don't allow adds during active enumerations.
   * This is confusing, since newly added elements may or may not
   * be enumerated in on going enumerations, is not particularly
   * useful, since elements to add can just be stored up on a local
   * list and added after the enumeration completes.
   */
  ASSERT(!bp->bp_enums,
	 "BPAdd, attempted during active enumerations");

  /* element rect must be canonical! */
#ifdef BPARANOID
  ASSERT(GeoIsCanonicalRect(r),"BPAdd, rect must be canonical.");
#endif

  bp->bp_count++;

  /* update hash table */
  IHashAdd(bp->bp_hashTable, element);

  /* update bbox */
  if(bp->bp_count == 1)
  {
    bp->bp_bbox = *r;
  }
  else
  {
    GeoIncludeRectInBBox(r,&bp->bp_bbox);
  }

  /* no bins? */
  if(!bp->bp_rootNode) goto inBox;

  /* doesn't fit inside bins ? */
  if(!GEO_SURROUND(&bp->bp_binArea,r)) goto inBox;

  /* bin element */
  bpBinAdd(bp->bp_rootNode, e);
  return;

  /* add to in box */
 inBox:
  bp->bp_inAdds++;
  e->e_link = bp->bp_inBox;
  bp->bp_inBox = e;

  /* maintain back pointers */
  e->e_linkp = &bp->bp_inBox;
  if(e->e_link) e->e_link->e_linkp = &e->e_link;

}

/*
 * ----------------------------------------------------------------------------
 * BPDelete --
 *
 * remove element from bplane
 *
 * ----------------------------------------------------------------------------
 */
void BPDelete(BPlane *bp, void *element)
{
  Element *e = element;

  ASSERT(e,"BPDelete");
  if (bp->bp_count == 0)
  {
      TxError("Error:  Attempt to delete instance from empty cell!\n");
      return;
  }
  bp->bp_count--;

  /* if element was on edge of bbox, bbox may no longer
   * be exact.
   */
  if(bp->bp_bbox_exact &&
     (bp->bp_bbox.r_xbot == e->e_rect.r_xbot ||
      bp->bp_bbox.r_xtop == e->e_rect.r_xtop ||
      bp->bp_bbox.r_ybot == e->e_rect.r_ybot ||
      bp->bp_bbox.r_ytop == e->e_rect.r_ytop))
  {
    bp->bp_bbox_exact = FALSE;
  }

  /* advance any nextElement pointers at e */
  {
    BPEnum *bpe;

    for(bpe=bp->bp_enums; bpe; bpe=bpe->bpe_next)
    {
      if(bpe->bpe_nextElement != e) continue;

      if(bpe->bpe_match == BPE_EQUAL)
      {
	bpe->bpe_nextElement = IHashLookUpNext(bp->bp_hashTable, e);
      }
      else
      {
	bpe->bpe_nextElement = e->e_link;
      }
    }
  }

  IHashDelete(bp->bp_hashTable, e);

  /* next pointer of prev element */
  *e->e_linkp = e->e_link;

  /* back pointer of next element */
  if(e->e_link) e->e_link->e_linkp = e->e_linkp;
}

/*
 * ----------------------------------------------------------------------------
 * BPBBox --
 *
 * Get current bplane bbox.
 *
 * returns: current bplane bbox
 *          (returns an inverted rect, if bplane is empty)
 *
 * ----------------------------------------------------------------------------
 */
Rect BPBBox(BPlane *bp)
{

  if(bp->bp_count == 0) return GeoInvertedRect;

  /* if bbox is not up-to-date, recompute */
  if(!bp->bp_bbox_exact)
  {
    BPEnum bpe;
    Element *e;

    bp->bp_bbox_exact = TRUE;

    BPEnumInit(&bpe,
	       bp,
	       NULL,
	       BPE_ALL,
	       "BPBBox");

    e = BPEnumNext(&bpe);
    bp->bp_bbox = e->e_rect;

    while(e = BPEnumNext(&bpe))
    {
      GeoIncludeRectInBBox(&e->e_rect, &bp->bp_bbox);
    }
  }

  return bp->bp_bbox;
}








