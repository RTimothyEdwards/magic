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

#ifndef _MAGIC__BPLANE__BPENUM_H
#define _MAGIC__BPLANE__BPENUM_H

/* bpEnum.h --
 *
 * inlined Search routines for bplanes (see also bpEnum.c)
 *
 */

#include <stdio.h>
#include "utils/utils.h"
#include "utils/geometry.h"
#include "utils/geofast.h"
#include "bplane/bplane.h"
#include "bplane/bplaneInt.h"

extern void DumpRect(char *msg, Rect *r);

/* state machine states */
#define BPS_BINS 0
#define BPS_BINS_INSIDE 1
#define BPS_INBOX 2
#define BPS_INBOX_INSIDE 3
#define BPS_HASH 4
#define BPS_DONE 5

/* range code */
#define R_LEFT 1
#define R_RIGHT 2
#define R_BOT 4
#define R_TOP 8

/*
 * ----------------------------------------------------------------------------
 *
 * bpBinArea -- compute area covered by given bin.
 *
 * Returns: bin area.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ Rect bpBinArea(BinArray *ba, int i)
{
  int dimX = ba->ba_dimX;
  int dx = ba->ba_dx;
  int dy = ba->ba_dy;
  int xi = i % dimX;
  int yi = i / dimX;
  Rect area;

  area.r_xbot = ba->ba_bbox.r_xbot + dx*xi;
  area.r_ybot = ba->ba_bbox.r_ybot + dy*yi;
  area.r_xtop = area.r_xbot + dx;
  area.r_ytop = area.r_ybot + dy;

  return area;
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumRange --
 *
 * Determine which edges of search area bin overlaps.
 *
 * (Used to make match checks as efficient as possible, for example if bin
 *  does not extend past srch area in any direction, no match checking is
 *  required)
 *
 * Returns:  int encoding 'range'.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ int
bpEnumRange(Rect *bin, Rect *srch)
{
  int range = 0;

  if (bin->r_xbot < srch->r_xbot) range |= R_LEFT;
  if (bin->r_xtop > srch->r_xtop) range |= R_RIGHT;
  if (bin->r_ybot < srch->r_ybot) range |= R_BOT;
  if (bin->r_ytop > srch->r_ytop) range |= R_TOP;

  return range;
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumMatchQ -
 *
 * Check if element intersects search area
 *
 * range specifies which search area boundaries the element
 * can potentially extend beyond.
 *
 * We rely on the optimizing compiler to remove unnecessary checks
 * based on compile time knowledge of range value.
 *
 * Returns:  TRUE on match, FALSE otherwise.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ bool
bpEnumMatchQ(BPEnum *bpe, Element *e)
{
  Rect *area = &bpe->bpe_srchArea;
  Rect *r = &e->e_rect;

  if(r->r_xtop < area->r_xbot) return FALSE;
  if(r->r_xbot > area->r_xtop) return FALSE;
  if(r->r_ytop < area->r_ybot) return FALSE;
  if(r->r_ybot > area->r_ytop) return FALSE;

  return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumPushInside --
 *
 * called by bpEnumPush when the binarray is entirely inside the search area
 *
 * push a bin array onto an enum stack.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ bool bpEnumPushInside(BPEnum *bpe,
					BinArray *ba)
{
  BPStack *bps;

  /* push stack */
  ++bpe->bpe_top;
  bps = bpe->bpe_top;
  bps->bps_node = ba;
  bps->bps_state = BPS_BINS_INSIDE;

  /* set up indices to scan entire bin array */
  bps->bps_i = -1;
  bps->bps_max = ba->ba_numBins;

  return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumPush --
 *
 * push a bin array onto an enum stack.
 *
 * normally returns TRUE, returns FALSE on (possible) state change.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ bool bpEnumPush(BPEnum *bpe,
				  BinArray *ba,
				  bool inside)
{
  Rect area;
  Rect *bbox;
  int dx;
  int dy;
  BPStack *bps;

  /*
  fprintf(stderr,"DEBUG bpEnumPush, inside=%d\n", inside);
  */

  /* special case inside */
  if(inside) return bpEnumPushInside(bpe,ba);

  bbox = &ba->ba_bbox;
  if(GEO_SURROUND(&bpe->bpe_srchArea,bbox))
  {
    bpEnumPushInside(bpe,ba);
    return FALSE;  /* state change */
  }

  /* push stack */
  ++bpe->bpe_top;
  bps = bpe->bpe_top;
  bps->bps_node = ba;
  bps->bps_state = BPS_BINS;
  bps->bps_subbin = FALSE;
  bps->bps_rejects = 0;

  /* compute search area for this bin array */
  dx = ba->ba_dx;
  dy = ba->ba_dy;
  area.r_xbot = bpe->bpe_srchArea.r_xbot - dx;
  area.r_xtop = bpe->bpe_srchArea.r_xtop + 1;
  area.r_ybot = bpe->bpe_srchArea.r_ybot - dy;
  area.r_ytop = bpe->bpe_srchArea.r_ytop + 1;
  GEOCLIP(&area,bbox);

  if(GEO_RECTNULL(&area))
  {
    /* only need to check oversized */
    bps->bps_i = 0;
    bps->bps_rowMax = 0;
    bps->bps_max = 0;
  }
  else
  {
    /* setup indices for this array and search area */
    int dimX = ba->ba_dimX;
    int i;

    /* make area relative to bin bbox */
    area.r_xbot -= bbox->r_xbot;
    area.r_xtop -= bbox->r_xbot;
    area.r_ybot -= bbox->r_ybot;
    area.r_ytop -= bbox->r_ybot;

    /* DumpRect("area relative to bin bbox = ",&area); */

    area.r_xbot /= ba->ba_dx;
    area.r_xtop /= ba->ba_dx;
    area.r_ybot /= ba->ba_dy;
    area.r_ytop /= ba->ba_dy;

    i = area.r_ybot*dimX + area.r_xbot;  /* next index */
    bps->bps_i = i-1;
    bps->bps_rowMax = i + area.r_xtop - area.r_xbot;
    bps->bps_max = area.r_ytop*dimX + area.r_xtop;
    bps->bps_rowDelta = dimX + area.r_xbot - area.r_xtop;
    bps->bps_dimX = dimX;

    /* consider subbinning? */
    if(dx >= bpe->bpe_subBinMinX || dy >= bpe->bpe_subBinMinY)
    {
      bps->bps_subbin = TRUE;
    }
  }

  return TRUE;
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumNextBin1 --
 *
 * called by bpEnumNextBin() after indexes for new bin are setup
 *
 * returns:  normally returns TRUE, returns FALSE on state change.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ bool
bpEnumNextBin1(BPEnum *bpe, BPStack *bps, bool inside)
{
  if(bpBinType(bps->bps_node,bps->bps_i) != BT_ARRAY)
  {
    bpe->bpe_nextElement = bpBinList(bps->bps_node,bps->bps_i);
    return TRUE;
  }

  /* array, push into it */
  return bpEnumPush(bpe, bpSubArray(bps->bps_node,bps->bps_i), inside);
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumNextBin --
 *
 * called by bpEnumNextBINS to advance to next bin (bucket).
 *
 * cycles through normal bins first, then oversized,
 * finally, for toplevel, sets INBOX state.
 *
 * sets bpe->bpe_nextElement to first element in next bin.
 *
 * returns:  normally returns TRUE, returns FALSE on state change.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ bool
bpEnumNextBin(BPEnum *bpe, bool inside)
{
  BPStack *bps = bpe->bpe_top;

#ifdef BPARANOID
  ASSERT(bps,"bpEnumNextBin");
  ASSERT(!bpe->bpe_nextElement,"bpEnumNextBin");
#endif

  /*
  fprintf(stderr,"DEBUG bpEnumNextBin TOP inside=%d nextElement=%x\n",
	  inside, bpe->bpe_nextElement);
  */

  /* consider subbining this bin before advancing to next */
  if(!inside)
  {
    if(bps->bps_rejects >= bpMinBAPop
       && (bps->bps_subbin || bps->bps_i == bps->bps_node->ba_numBins))
    {
      int i = bps->bps_i;
      BinArray *ba = bps->bps_node;
      BinArray *sub;

      /* fprintf(stderr,"DEBUG, subbining!\n"); */
      sub = bpBinArrayBuild(bpBinArea(ba,i),
			    bpBinList(ba,i),
			    FALSE);  /* don't recursively subbin! */

      if(sub)
      {
	ba->ba_bins[i] =
	  (void *) ((pointertype) sub | BT_ARRAY);
      }
    }
    bps->bps_rejects = 0;
  }

  /* handle inside case first */
  if(inside)
  {
    /* Inside case, cycle through all bins */

    /* next bin */
    if(bps->bps_i<bps->bps_max)
    {
      bps->bps_i += 1;
      return bpEnumNextBin1(bpe,bps,inside);
    }
  }
  else
  {
    /* cycle only through relevant bins */

    /* next in row */
    if(bps->bps_i<bps->bps_rowMax)
    {
      bps->bps_i += 1;
      goto bin;
    }

    /* next row */
    if(bps->bps_i<bps->bps_max)
    {
      bps->bps_i += bps->bps_rowDelta;
      bps->bps_rowMax += bps->bps_dimX;
      goto bin;
    }

    /* oversized */
    if(bps->bps_i == bps->bps_max)
    {
      bps->bps_i = bps->bps_node->ba_numBins;
      goto bin;
    }
  }

  /* pop stack */
  /* fprintf(stderr,"DEBUG BPEnumNextBin Pop.\n"); */
  bpe->bpe_top--;
  if(bpe->bpe_top>bpe->bpe_stack) return FALSE; /* state may have changed */

  /* inbox */
  /* fprintf(stderr,"DEBUG BPEnumNextBin INBOX.\n"); */
  bpe->bpe_nextElement = bpe->bpe_plane->bp_inBox;
  bpe->bpe_top->bps_state = BPS_INBOX | inside;
  return FALSE; /* state change */

  /* dive into indexed bin */
 bin:
  return bpEnumNextBin1(bpe,bps,inside);
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumNextBINS --
 *
 * Handle BINS state for BPEnumNext()
 *
 * (bin enumeration.)
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ Element* bpEnumNextBINS(BPEnum *bpe, bool inside)
{
  /* bin by bin */
  do
  {
    /* search this bin */
    Element *e = bpe->bpe_nextElement;

    while(e && !inside && !bpEnumMatchQ(bpe,e))
    {
      bpe->bpe_top->bps_rejects++;
      e = e->e_link;
    }

    if(e)
    {
      bpe->bpe_nextElement = e->e_link;
      /* DumpRect("DEBUG e_rect= ",&e->e_rect); */
      return e;
    }

    bpe->bpe_nextElement = NULL;
  }
  while(bpEnumNextBin(bpe,inside));

  /* next state */
  return NULL;
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumNextINBOX --
 *
 * Handle INBOX states for BPEnumNext()
 *
 * unbinned enumeration.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ Element *bpEnumNextINBOX(BPEnum *bpe,
					   bool inside)
{
  Element *e = bpe->bpe_nextElement;

  while(e && !inside && !bpEnumMatchQ(bpe,e)) e = e->e_link;
  if(e)
  {
    bpe->bpe_nextElement = e->e_link;
    return e;
  }

  /* done */
  bpe->bpe_top->bps_state = BPS_DONE;
  return NULL;
}

/*
 * ----------------------------------------------------------------------------
 * bpEnumNextHASH --
 *
 * Handle HASH state for BPEnumNext()
 *
 * (hash based (EQUALS) enumerations.)
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ Element *bpEnumNextHASH(BPEnum *bpe)
{
  Element *e = bpe->bpe_nextElement;

  if(e)
  {
    bpe->bpe_nextElement =
      IHashLookUpNext(bpe->bpe_plane->bp_hashTable, e);
  }
  else
  {
    bpe->bpe_top->bps_state = BPS_DONE;
  }
  return e;
}

/*
 * ----------------------------------------------------------------------------
 * BPEnumNext --
 *
 * get next element in enumeration.
 *
 * ----------------------------------------------------------------------------
 */
static __inline__ void *BPEnumNext(BPEnum *bpe)
{
  Element *e;

  while(TRUE)
  {
    /*
    fprintf(stderr,"DEBUG state=%d\n",bpe->bpe_top->bps_state);
    */
    switch (bpe->bpe_top->bps_state)
    {
    case BPS_BINS:
      if((e=bpEnumNextBINS(bpe, 0))) return e;
      break;

    case BPS_BINS_INSIDE:
      if((e=bpEnumNextBINS(bpe, 1))) return e;
      break;

    case BPS_INBOX:
      if((e=bpEnumNextINBOX(bpe, 0))) return e;
      break;

    case BPS_INBOX_INSIDE:
      if((e=bpEnumNextINBOX(bpe, 1))) return e;
      break;

    case BPS_HASH:
      if((e=bpEnumNextHASH(bpe))) return e;
      break;

    case BPS_DONE:
      return NULL;

    default:
      ASSERT(FALSE,"BPEnumNext, bad state");
    }
  }
}

#endif /* _MAGIC__BPLANE__BPENUM_H */
